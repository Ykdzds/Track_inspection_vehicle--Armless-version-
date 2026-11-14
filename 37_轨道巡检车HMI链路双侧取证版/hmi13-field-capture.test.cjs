'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const protocol=require('./r2-protocol.cjs');
const {fixture}=require('./hmi13.test.cjs');
const capture=JSON.parse(fs.readFileSync(path.join(__dirname,'运行核验/现场基础区饥饿抓包_20261016.json'),'utf8'));
const responses=capture.responses.map(value=>Buffer.from(value,'hex'));
function deliver(run,region,frame){
    const [start,count]=protocol.REGIONS[region];
    Object.assign(run.context.h13.vals,{13:1,14:1,15:region,16:start,17:count});
    Object.assign(run.context.mod,{bcaddr:1,bdindex:region,bdaddr:start,bdcount:count,interruptCode:1,bdbuf:[...frame.subarray(3,-2)]});
    run.deliver();
}

test('本次现场210请求和68完整回复CRC正确，但元数据和辅助区完全缺失',()=>{
    assert.equal(capture.requests.length,210);assert.equal(responses.length,68);
    const counts=new Map();
    for(const value of capture.requests){const frame=Buffer.from(value,'hex');assert.ok(protocol.validFrame(frame));const address=frame.readUInt16BE(2);counts.set(address,(counts.get(address)||0)+1);}
    assert.deepEqual([...counts].sort((left,right)=>left[0]-right[0]),[[0,53],[1120,157]]);
    for(const frame of responses){assert.ok(protocol.validFrame(frame));assert.equal(frame.length,133);}
    assert.equal(responses.filter(frame=>frame.readUInt16BE(3)===0x534d).length,51);
});

test('真实B0回复经匹配回调提交全部64字及区域有效标志',()=>{
    const run=fixture();const frame=responses.find(value=>value.readUInt16BE(3)!==0x534d);
    deliver(run,0,frame);
    assert.equal(run.context.h13.vals[124],0);assert.equal(run.context.h13.vals[109],1);
    assert.equal(run.context.regionSeen.vals[0],1);assert.equal(run.context.regionValid.vals[0],1);
    assert.deepEqual([...run.context.rawRegs.vals].slice(0,64),protocol.bytesToWords(frame.subarray(3,-2)));
    assert.equal(protocol.get32(run.context.rawRegs.vals,24),405);
});

test('B0与B7等长迟到回复不能污染对方缓存或刷新有效年龄',()=>{
    for(const region of [0,7]){
        const run=fixture();const frame=responses.find(value=>(value.readUInt16BE(3)===0x534d)===(region===0));
        const before=JSON.stringify([run.context.rawRegs.vals,run.context.h13Status.vals]);
        deliver(run,region,frame);
        assert.equal(run.context.h13.vals[124],7);assert.equal(run.context.h13.vals[147],1);
        assert.equal(run.context.h13.vals[104],0);assert.equal(run.context.h13.vals[13],1);
        assert.equal(JSON.stringify([run.context.rawRegs.vals,run.context.h13Status.vals]),before);
    }
});

test('错误站号地址数量仍拒收，不以修复通信为由放松关联检查',()=>{
    for(const [field,value] of [['bcaddr',2],['bdaddr',1120],['bdcount',32],['bdindex',7]]){
        const run=fixture();const frame=responses.find(value=>value.readUInt16BE(3)!==0x534d);
        deliver(run,0,frame);const count=run.context.h13.vals[104];
        Object.assign(run.context.h13.vals,{13:1,14:1,15:0,16:0,17:64});run.context.mod[field]=value;run.deliver();
        assert.equal(run.context.h13.vals[104],count);assert.ok(run.context.h13.vals[124]>0);
    }
});

test('调度采用非负空闲哨兵和固定区域时间戳，空闲时所有只读区有界推进',()=>{
    const source=fs.readFileSync(path.join(__dirname,'HMI13事务服务.txt'),'utf8');
    assert.doesNotMatch(source,/hSelected\s*(?:<0|=0-1|>=0)|vals\[48\+hSelected\]/);
    const run=fixture({globalDisplay:true});run.context.sys_pid=1;run.tick(400);
    for(const [address] of protocol.REGIONS.filter((words,region)=>region!==6)){
        assert.ok(run.calls.filter(call=>call.kind===1&&call.request.readUInt16BE(2)===address).length>=5,'区域'+address+'未推进');
    }
    assert.equal(run.context.h13.vals[3],1);assert.equal(run.context.regionValid.vals[0],1);
    assert.ok(run.context.h13.vals[109]>0);assert.ok(run.context.h13.vals[114]>0);
    assert.equal(run.calls.some(call=>call.kind===2),false);
});
