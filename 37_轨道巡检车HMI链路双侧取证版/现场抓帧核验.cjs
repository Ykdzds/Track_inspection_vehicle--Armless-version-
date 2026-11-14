'use strict';
const fs=require('node:fs');
const path=require('node:path');
const protocol=require('./r2-protocol.cjs');

function loadRecords(text){
    const records=[];
    for(const [lineIndex,line] of text.replace(/^\uFEFF/,'').split(/\r?\n/).entries()){
        if(!line.trim())continue;
        const record=JSON.parse(line);
        if(!Number.isFinite(record.t_ms)||record.t_ms<0||!['TX','RX'].includes(record.dir)||typeof record.hex!=='string')throw Error('第'+(lineIndex+1)+'行：要求t_ms非负毫秒、dir=TX/RX及hex字符串');
        const hex=record.hex.replace(/\s/g,'');
        if(!hex.length||hex.length%2||!/^[0-9a-f]+$/i.test(hex))throw Error('第'+(lineIndex+1)+'行：hex不是完整十六进制字节');
        if(records.length&&record.t_ms<records[records.length-1].t_ms)throw Error('第'+(lineIndex+1)+'行：时间倒退；两方向必须使用同一时钟');
        records.push({...record,bytes:Buffer.from(hex,'hex'),line:lineIndex+1});
    }
    if(!records.length)throw Error('输入没有采集记录');
    return records;
}

function extract(records,direction){
    let pending=[];let discarded=0;let crcCandidates=0;
    const frames=[];
    for(const record of records.filter(item=>item.dir===direction)){
        pending.push(...Array.from(record.bytes,byte=>({byte,time:record.t_ms,line:record.line})));
        while(pending.length>=2){
            let selected=null;
            for(let offset=0;offset<pending.length-1;offset++){
                if(pending[offset].byte!==1)continue;
                const code=pending[offset+1].byte;
                let length=0;
                if(direction==='TX'&&code===3)length=8;
                if(direction==='TX'&&code===16&&pending.length-offset>=7)length=9+pending[offset+6].byte;
                if(direction==='RX'&&code===3&&pending.length-offset>=3)length=5+pending[offset+2].byte;
                if(direction==='RX'&&code===16)length=8;
                if(direction==='RX'&&(code===0x83||code===0x90))length=5;
                if(!length||length>256||pending.length-offset<length)continue;
                const bytes=Buffer.from(pending.slice(offset,offset+length).map(item=>item.byte));
                if(!protocol.validFrame(bytes)){crcCandidates++;continue;}
                selected={offset,length,bytes};break;
            }
            if(!selected){
                if(pending.length>256){discarded+=pending.length-256;pending=pending.slice(-256);}
                break;
            }
            discarded+=selected.offset;
            const start=pending[selected.offset];
            const finish=pending[selected.offset+selected.length-1];
            frames.push({bytes:selected.bytes,start_ms:start.time,end_ms:finish.time,start_line:start.line,end_line:finish.line});
            pending=pending.slice(selected.offset+selected.length);
        }
    }
    return {frames,discardedBytes:discarded,unparsedTailBytes:pending.length,crcCandidateChecksFailed:crcCandidates};
}

function gaps(frames){
    const values=[];
    for(let index=1;index<frames.length;index++)values.push(frames[index].start_ms-frames[index-1].start_ms);
    return {samples:frames.length,intervals:values.length,max_ms:values.length?Math.max(...values):null,coarseOrSharedTimestamp:frames.some((frame,index)=>index>0&&frame.start_ms===frames[index-1].start_ms)};
}

function audit(text){
    const records=loadRecords(text);
    const tx=extract(records,'TX');const rx=extract(records,'RX');
    const reads=new Map();const targets=[];const writes=[];const violations=[];
    for(const frame of tx.frames){
        const bytes=frame.bytes;
        if(bytes[1]===3){
            const start=bytes.readUInt16BE(2);const count=bytes.readUInt16BE(4);
            const key=start.toString(16).toUpperCase().padStart(4,'0')+'/'+count;
            if(!reads.has(key))reads.set(key,[]);
            reads.get(key).push(frame);
        }else{
            if(bytes.readUInt16BE(2)!==256||bytes.readUInt16BE(4)!==24||bytes[6]!==48||bytes.length!==57){violations.push({at_ms:frame.start_ms,reason:'非完整0100H/24字邮箱写'});continue;}
            const words=protocol.bytesToWords(bytes.subarray(7,-2));
            const command=words[8];const rpm=(protocol.get32(words,10)|0);
            const entry={at_ms:frame.start_ms,command,boot:protocol.get32(words,2),session:protocol.get32(words,4),sequence:protocol.get32(words,6),axis:words[9],rpm};
            writes.push(entry);
            if(words[0]!==0x484d||words[1]!==1||words[22]!==2||words[23]!==0xa55a||words[9]!==0)violations.push({at_ms:frame.start_ms,reason:'邮箱身份、修订、提交标记或axis无效'});
            if(!Object.values(protocol.COMMAND).includes(command))violations.push({at_ms:frame.start_ms,reason:'未知运动命令'});
            if(command===protocol.COMMAND.RPM){
                targets.push(frame);
                if(rpm < -1600||rpm>1600||words[17]!==500)violations.push({at_ms:frame.start_ms,reason:'目标rpm越界或有效期不是500ms'});
            }
        }
    }
    const required=protocol.REGIONS.filter((region,index)=>index!==6).map(([start,count])=>start.toString(16).toUpperCase().padStart(4,'0')+'/'+count);
    const duration=records[records.length-1].t_ms-records[0].t_ms;
    return {
        inputRecords:records.length,observedDuration_ms:duration,
        tx:{validFrames:tx.frames.length,discardedBytes:tx.discardedBytes,unparsedTailBytes:tx.unparsedTailBytes,crcCandidateChecksFailed:tx.crcCandidateChecksFailed},
        rx:{validFrames:rx.frames.length,discardedBytes:rx.discardedBytes,unparsedTailBytes:rx.unparsedTailBytes,crcCandidateChecksFailed:rx.crcCandidateChecksFailed,exceptions:rx.frames.filter(frame=>frame.bytes[1]&0x80).map(frame=>({at_ms:frame.start_ms,function:frame.bytes[1],code:frame.bytes[2]}))},
        readRegions:Object.fromEntries(Array.from(reads,([key,frames])=>[key,gaps(frames)])),
        requiredRegionsMissing:required.filter(key=>!reads.has(key)),
        targetTiming:gaps(targets),writes,mailboxViolations:violations,
        tenMinuteCapture:duration>=600000,
        physicalCommunicationVerified:false,motionVerified:false,
        limitations:[
            '时间是输入记录的采集时间，不是工具测得的线上首字节时间；USB缓存、整段导出或同时间戳多帧不能证明200ms实时预算。',
            'CRC失败候选检查数可能重复，不能当作真实CRC坏帧数；丢弃及尾部可能含启动字节、截断或未支持帧。',
            '03H不含请求地址，未将回复强行关联至区域；没有计算丢包率或用ACK认定授权、停车完成。',
            '跨会话、停车和故意断线会增大目标全窗口间隔，须按实际授权区间单独裁剪采集文件。',
            '只证明采集内容特征，不证明屏缓存已接收、滑块可见、驱动已停或零速使能；需同步P2视频、F4诊断和机械证据。'
        ]
    };
}

if(require.main===module){
    try{
        const args=process.argv.slice(2);
        if(args.length!==1&&!(args.length===3&&args[1]==='--out'))throw Error('用法：node 现场抓帧核验.cjs <输入.jsonl> [--out <报告.json>]');
        const input=path.resolve(args[0]);
        const report=JSON.stringify(audit(fs.readFileSync(input,'utf8')),null,2)+'\n';
        if(args.length===3){
            const output=path.resolve(args[2]);
            if(output===input)throw Error('报告不能覆盖原始输入');
            fs.writeFileSync(output,report,{flag:'wx'});
        }else process.stdout.write(report);
    }catch(error){console.error(error.message);process.exitCode=1;}
}
module.exports={loadRecords,extract,audit};
