'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const {fixture}=require('./hmi13.test.cjs');
const protocol=require('./r2-protocol.cjs');
function stopWrites(run){return run.calls.filter(call=>call.kind===2&&protocol.bytesToWords(call.request.subarray(7,-2))[8]===16);}
for(const owner of [1,2,3])for(const previous of [false,true])test('22号无本地令牌全局停车 owner='+owner+' 历史成功='+previous,()=>{
    const run=fixture({globalDisplay:true});run.tick(40);
    if(previous){run.authorize();run.event('事件/急停_按下.txt');run.tick(180);assert.equal(run.context.h13.vals[2],5);}
    run.sim.owner=owner;run.sim.session=900;run.sim.actual=100;run.sim.target=100;
    run.sim.lastTarget=run.sim.lastLease=run.sim.now;
    run.event('事件/急停_按下.txt');run.tick(4);
    assert.ok(run.sim.stop);const event=run.sim.stop.event;const started=run.sim.stop.started;
    for(let index=0;index<20;index++){run.event('事件/急停_按下.txt');run.tick();if(run.sim.stop){assert.equal(run.sim.stop.event,event);assert.equal(run.sim.stop.started,started);}}
    run.tick(180);assert.equal(run.sim.owner,0);assert.equal(run.sim.actual,0);
    assert.equal(run.sim.stopRecord.event,event);assert.equal(run.context.h13.vals[2],5);
    assert.ok(stopWrites(run).every(call=>protocol.get32(protocol.bytesToWords(call.request.subarray(7,-2)),4)===0));
    run.event('事件/授权_按下.txt');run.tick(40);assert.equal(run.context.h13.vals[2],3);assert.equal(run.sim.target,0);
    run.event('事件/返回_弹起.txt');run.tick(180);assert.equal(run.context.sys_pid,1);
});
for(const invalid of ['enable','lock','speed'])test('22号空闲停车反馈不安全不能伪造成功 '+invalid,()=>{
    const run=fixture();run.tick(40);const original=run.sim.registers.bind(run.sim);
    run.sim.registers=()=>{const words=original();if(invalid==='enable')words[1120+27]=0;if(invalid==='lock')words[1120+37]=4;if(invalid==='speed')words[1120+23]=1;return words;};
    run.event('事件/急停_按下.txt');run.tick(150);
    assert.notEqual(run.context.h13.vals[2],5);assert.notEqual(run.context.h13.vals[2],3);
    run.event('事件/授权_按下.txt');run.tick(10);assert.notEqual(run.context.h13.vals[2],3);
});
