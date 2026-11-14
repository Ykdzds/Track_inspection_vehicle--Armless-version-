'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const protocol=require('./r2-protocol.cjs');
const {audit}=require('./现场抓帧核验.cjs');
const record=(time,dir,bytes)=>JSON.stringify({t_ms:time,dir,hex:bytes.toString('hex')});

test('被动工具支持分片133字节回复和启动字节，不关联等长回复地址',()=>{
    const reply=protocol.frame(Buffer.concat([Buffer.from([1,3,128]),Buffer.alloc(128)]));
    const text=[record(0,'TX',Buffer.concat([Buffer.from([5,254]),protocol.readFrame(1120,64)])),record(5,'RX',reply.subarray(0,60)),record(10,'RX',reply.subarray(60))].join('\n');
    const report=audit(text);
    assert.equal(report.tx.validFrames,1);assert.equal(report.rx.validFrames,1);
    assert.equal(report.tx.discardedBytes,2);assert.equal(report.rx.unparsedTailBytes,0);
    assert.equal(report.readRegions['0460/64'].samples,1);
    assert.equal(report.physicalCommunicationVerified,false);
    assert.equal(report.motionVerified,false);
});

test('坏CRC及截断不计有效响应，后续完整帧可恢复',()=>{
    const good=protocol.frame([1,3,2,0,0]);const bad=Buffer.from(good);bad[bad.length-1]^=1;
    const report=audit([record(0,'RX',bad),record(1,'RX',good.subarray(0,4)),record(2,'RX',good)].join('\n'));
    assert.equal(report.rx.validFrames,1);assert.ok(report.rx.crcCandidateChecksFailed>0);
    assert.equal(report.rx.discardedBytes,bad.length+4);
});

test('全部只读区覆盖与10分钟窗口只描述采集，不自动实屏放行',()=>{
    const rows=protocol.REGIONS.filter((region,index)=>index!==6).map(([start,count],index)=>record(index*100,'TX',protocol.readFrame(start,count)));
    rows.push(record(600000,'TX',protocol.readFrame(0,64)));
    const report=audit(rows.join('\n'));
    assert.deepEqual(report.requiredRegionsMissing,[]);assert.equal(report.tenMinuteCapture,true);
    assert.equal(report.physicalCommunicationVerified,false);
});

test('目标负数范围、57字节邮箱和采集间隔正确识别',()=>{
    const rows=[0,100,300].map((time,index)=>record(time,'TX',protocol.writeFrame(protocol.mailbox({boot:409,session:2,sequence:index+1,command:33,rpm:-100}))));
    const report=audit(rows.join('\n'));
    assert.equal(report.targetTiming.max_ms,200);assert.equal(report.writes[0].rpm,-100);
    assert.deepEqual(report.mailboxViolations,[]);
    const invalid=protocol.mailbox({boot:409,session:2,sequence:5,command:33,rpm:1601});invalid[9]=1;
    assert.equal(audit(record(0,'TX',protocol.writeFrame(invalid))).mailboxViolations.length,2);
});

test('同记录多帧明确标记采集时间精度不足，异常回复单独列出',()=>{
    const report=audit([record(0,'TX',Buffer.concat([protocol.readFrame(0,64),protocol.readFrame(0,64)])),record(1,'RX',protocol.frame([1,0x83,2]))].join('\n'));
    assert.equal(report.readRegions['0000/64'].coarseOrSharedTimestamp,true);
    assert.equal(report.rx.exceptions[0].code,2);
});

test('拒绝缺时间、非法HEX及倒退时钟，不从无时间抓包推导实时预算',()=>{
    for(const input of ['',JSON.stringify({dir:'TX',hex:'0103'}),JSON.stringify({t_ms:0,dir:'TX',hex:'1Z'}),[record(1,'TX',protocol.readFrame(0,64)),record(0,'RX',protocol.frame([1,0x83,2]))].join('\n')])assert.throws(()=>audit(input));
});

test('最新实物原始帧可被扫描，但人工合成时间轴不作为实物时序证据',()=>{
    const capture=JSON.parse(fs.readFileSync(path.join(__dirname,'运行核验/现场R3失配抓包_20261017.json'),'utf8'));
    const rows=[...capture.Requests.map((hex,index)=>({t_ms:index,dir:'TX',hex})),...capture.Responses.map((hex,index)=>({t_ms:index,dir:'RX',hex}))].sort((left,right)=>left.t_ms-right.t_ms);
    const report=audit(rows.map(row=>JSON.stringify(row)).join('\n'));
    assert.equal(report.tx.validFrames,178);assert.equal(report.rx.validFrames,64);
    assert.ok(report.requiredRegionsMissing.includes('0200/24'));
    assert.equal(report.physicalCommunicationVerified,false);
});
