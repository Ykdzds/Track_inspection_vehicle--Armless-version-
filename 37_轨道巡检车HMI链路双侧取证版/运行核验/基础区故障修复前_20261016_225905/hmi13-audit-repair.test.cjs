'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const {fixture}=require('./hmi13.test.cjs');
const protocol=require('./r2-protocol.cjs');
const index=require('./HMI13状态索引.json');
const reads=(run,address)=>run.calls.filter(call=>call.kind===1&&call.request.readUInt16BE(2)===address);
const gap=calls=>Math.max(0,...calls.slice(1).map((call,offset)=>call.at-calls[offset].at));

test('元数据持续丢失也轮询全部旧区，失败重试不超过每500ms一次',()=>{
    const run=fixture({nativeTimeout:true});const original=run.sim.handle.bind(run.sim);
    run.sim.handle=request=>request[1]===3&&request.readUInt16BE(2)===512?null:original(request);
    run.tick(400);
    for(const address of [0,80,112,144,176])assert.ok(reads(run,address).length>=5,'区域'+address+'被饿死');
    const metadata=reads(run,512);assert.ok(metadata.length>5);
    for(let offset=1;offset<metadata.length;offset++)assert.ok(metadata[offset].at-metadata[offset-1].at>=500);
    assert.equal(run.context.h13.vals[index.ready],0);assert.equal(run.calls.some(call=>call.kind===2),false);
    assert.ok(run.context.h13.vals[index.nativeTimeouts]>0);
});

test('50ms应答和全局显示联合负载下目标与状态均不超过200ms',()=>{
    const run=fixture({responseDelayMs:50,globalDisplay:true});run.authorize();const start=run.calls.length;run.tick(2400);
    const calls=run.calls.slice(start);const targets=calls.filter(call=>call.kind===2&&call.request[24]===33);
    assert.ok(gap(targets)<=200);assert.ok(gap(calls.filter(call=>call.kind===1&&call.request.readUInt16BE(2)===1120))<=200);
    for(const address of [0,80,112,144,176,512])assert.ok(reads(run,address).length>10);
    assert.equal(run.maximumInflight(),1);assert.equal(run.context.h13.vals[index.state],3);
});

test('R2已确认但基础区未读不再误报主控没有回复',()=>{
    const run=fixture();run.tick(40);run.context.sys_pid=1;run.context.regionSeen.vals[0]=0;run.context.regionValid.vals[0]=0;
    run.renderGlobal();assert.match(run.context.P1_Main.ctrlComm.txt,/R2正常，基础区未更新/);
});

test('独立急停不会被随后返回意图覆盖',()=>{
    const run=fixture();run.authorize();run.event('事件/急停_按下.txt');run.context.h13.vals[index.intent]=4;run.tick(15);
    assert.ok(run.calls.some(call=>call.kind===2&&call.request[24]===16));assert.equal(run.context.h13.vals[index.target],0);
});

test('只读诊断模式只读B5或B5加B7，不授权',()=>{
    for(const mode of [1,2]){const run=fixture();run.context.h13.vals[index.diagnosticMode]=mode;run.tick(100);run.intent(1);run.tick(20);
        assert.ok(run.calls.every(call=>call.kind===1&&([512,...mode===2?[1120]:[]].includes(call.request.readUInt16BE(2)))));
        assert.notEqual(run.context.h13.vals[index.state],3);
    }
});

test('回调拒因记录含实际站号与在途上下文，失败不覆盖缓存',()=>{
    const run=fixture();run.tick(40);const before=[...run.context.h13Status.vals];
    Object.assign(run.context.mod,{bcaddr:2,bdindex:7,bdaddr:1120,bdcount:64,interruptCode:1});run.deliver();
    assert.equal(run.context.h13.vals[index.lastIrqStation],2);assert.ok(run.context.h13.vals[index.mismatchReason]>0);
    assert.deepEqual([...run.context.h13Status.vals],before);
});

test('授权交叉确认后的第一条目标必须为零',()=>{
    const run=fixture();run.tick(40);run.intent(1);let first;
    for(let attempt=0;attempt<40&&!first;attempt++){run.tick();first=run.calls.find(call=>call.kind===2&&call.request[24]===33);}
    assert.ok(first);assert.equal(protocol.get32(protocol.bytesToWords(first.request.subarray(7,-2)),10),0);
});

test('50ms原生应答超时和无回调看门狗分别统计',()=>{
    for(const nativeTimeout of [false,true]){const run=fixture({nativeTimeout});run.setOffline(false);run.sim.handle=()=>null;run.tick(40);
        assert.ok(run.context.h13.vals[nativeTimeout?index.nativeTimeouts:index.watchdogTimeouts]>0);
        assert.equal(run.context.h13.vals[index.ready],0);
    }
});

test('离页与急停同拍仍优先急停，不被自动释放降级',()=>{const run=fixture();run.authorize();run.context.h13.vals[index.target]=100;run.tick(20);const before=run.calls.length;run.context.sys_pid=1;run.event('事件/急停_按下.txt');run.tick(5);const commands=run.calls.slice(before).filter(call=>call.kind===2).map(call=>protocol.bytesToWords(call.request.subarray(7,-2))[8]);assert.equal(commands[0],16);});
test('运行中进入只读诊断先撤权，不能保留后台运动',()=>{const run=fixture();run.authorize();run.context.h13.vals[index.diagnosticMode]=1;run.tick(40);assert.notEqual(run.context.h13.vals[index.state],3);assert.equal(run.sim.owner,0);assert.equal(run.context.h13.vals[index.target],0);});

test('全局P8显示段与独立显示源保持一致',()=>{const fs=require('node:fs'),path=require('node:path');const global=fs.readFileSync(path.join(__dirname,'全局计龄刷新.txt'),'utf8').replace(/\r/g,'');const view=fs.readFileSync(path.join(__dirname,'HMI13界面刷新.txt'),'utf8').replace(/\r/g,'');const start='P8_ServoControl.linkStatus.txt=';assert.equal(global.slice(global.indexOf(start)).trimEnd(),view.slice(view.indexOf(start)).trimEnd());});

test('正常授权全局显示负载下辅助区不超过原2秒失效门槛',()=>{const run=fixture({responseDelayMs:50,globalDisplay:true});run.authorize();run.tick(2400);for(const address of [80,112,144,176])assert.ok(gap(reads(run,address))<2000,'辅助区'+address+'最大间隔超时');});
