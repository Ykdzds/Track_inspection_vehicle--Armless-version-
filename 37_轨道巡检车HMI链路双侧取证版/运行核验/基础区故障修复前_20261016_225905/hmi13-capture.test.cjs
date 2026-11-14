'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const protocol=require('./r2-protocol.cjs');
const {fixture}=require('./hmi13.test.cjs');
const index=require('./HMI13状态索引.json');
const capture=JSON.parse(fs.readFileSync(path.join(__dirname,'运行核验/真实F4回复样本_20261016.json'),'utf8'));
const frames=capture.responses.map(hex=>Buffer.from(hex,'hex'));
function referenceCollector(){
    let pending=Buffer.alloc(0);const accepted=[];
    return {accepted,push(bytes){pending=Buffer.concat([pending,bytes]);while(pending.length>=3){if(pending[0]!==1||pending[1]!==3||![48,128].includes(pending[2])){pending=pending.subarray(1);continue;}const length=pending[2]+5;if(pending.length<length)break;const candidate=pending.subarray(0,length);if(protocol.validFrame(candidate)){accepted.push(Buffer.from(candidate));pending=pending.subarray(length);}else pending=pending.subarray(1);}},expire(){pending=Buffer.alloc(0);}};
}
test('真实抓包97帧CRC、53/133长度、身份与快照推进',()=>{
    assert.equal(frames.length,97);let metadata=0,status=0,lastSnapshot=0;
    for(const frame of frames){assert.ok(protocol.validFrame(frame));const words=protocol.bytesToWords(frame.subarray(3,-2));assert.equal(protocol.get32(words,2),frame[2]===48?0x00ff0002:399);if(frame[2]===48){metadata++;assert.equal(frame.length,53);assert.equal(words[0],0x5249);assert.equal(protocol.get32(words,4),399);}else{status++;assert.equal(frame.length,133);assert.equal(words[0],0x534d);const sequence=protocol.get32(words,4);assert.ok(sequence>lastSnapshot);lastSnapshot=sequence;}}
    assert.equal(metadata,49);assert.equal(status,48);
});
test('参考收集器分片、循环边界等价切片及粘连不损坏真实帧',()=>{
    const stream=Buffer.concat(frames);for(const size of [1,7,53,128,133,255,256,512,stream.length]){const receiver=referenceCollector();for(let offset=0;offset<stream.length;offset+=size)receiver.push(stream.subarray(offset,offset+size));assert.deepEqual(receiver.accepted.map(frame=>frame.toString('hex')),capture.responses);}
});
test('参考收集器错CRC和短帧作废，排空后不更新残帧',()=>{
    const receiver=referenceCollector(),bad=Buffer.from(frames[0]);bad[10]^=1;receiver.push(bad);assert.equal(receiver.accepted.length,0);receiver.expire();receiver.push(frames[1].subarray(0,90));assert.equal(receiver.accepted.length,0);receiver.expire();receiver.push(frames[0]);assert.equal(receiver.accepted.length,1);
});
test('真实载荷经严格回调脚本进入元数据及64字状态缓存',()=>{
    const run=fixture();run.tick(40);for(const frame of frames){const selected=frame[2]===48?5:7;const start=selected===5?512:1120;Object.assign(run.context.mod,{bcaddr:1,bdindex:selected,bdaddr:start,bdcount:frame[2]/2,interruptCode:1,bdbuf:[...frame.subarray(3,-2)]});Object.assign(run.context.h13.vals,{[index.busy]:1,[index.kind]:1,[index.readIndex]:selected,[index.start]:start,[index.count]:frame[2]/2});run.context.h13.vals[16]=start;run.context.h13.vals[17]=frame[2]/2;run.deliver();assert.equal(run.context.h13.vals[index.mismatchReason],0);}
    assert.equal(run.context.metaRegs.vals[0],0x5249);assert.equal(run.context.h13Status.vals[0],0x534d);assert.equal(run.context.h13Status.vals.length,64);
});
test('诊断页交替显示明确HMI本地与主控统计，不混淆CRC来源',()=>{
    const run=fixture({globalDisplay:true});run.tick(100);run.context.sys_pid=2;run.context.h13.vals[index.now]=4000;run.renderGlobal();assert.doesNotMatch(run.context.P2_Diag.reqCount.txt,/HMI/);run.context.h13.vals[index.now]=6200;run.renderGlobal();assert.match(run.context.P2_Diag.reqCount.txt,/HMI/);
});
