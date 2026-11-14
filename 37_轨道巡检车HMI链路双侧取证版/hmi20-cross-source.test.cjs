'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const {fixture}=require('./hmi13.test.cjs');
const protocol=require('./r2-protocol.cjs');
const command=call=>protocol.bytesToWords(call.request.subarray(7,-2))[8];
function running(options={}){const run=fixture({globalDisplay:true,...options});run.authorize();run.context.h13.vals[10]=options.rpm||100;run.tick(60);return run;}
function k5(run){if(!run.sim.owner)return run.sim.stopRecord.event;run.sim.beginStop(16,0);run.sim.stop.reason=5;return run.sim.stop.event;}
function stopped(run){run.tick(160);assert.equal(run.sim.owner,0);assert.equal(run.context.h13.vals[2],5);assert.equal(run.context.h13.vals[83],0);assert.equal(run.context.h13.vals[10],0);assert.equal(run.context.P8_ServoControl.rpmSlider.val,1600);}
function mutateStatus(run,mutate){const original=run.sim.registers.bind(run.sim);run.sim.registers=()=>{const regs=original();mutate(regs);return regs;};}
for(const axis of [1,2,3])for(const rpm of [100,-100])test('K5跨源停车并一次授权恢复 axis='+axis+' rpm='+rpm,()=>{
 const run=running({axis,rpm});const before=run.calls.length;const event=k5(run);run.tick(6);assert.equal(run.context.h13.vals[2],4);assert.equal(run.context.h13.vals[56],1);assert.match(run.context.P8_ServoControl.authorityStatus.txt,/全局停车/);
 run.context.P8_ServoControl.rpmSlider.val=3000;run.event('事件/滑块_滑动.txt');assert.equal(run.context.h13.vals[10],0);stopped(run);
 assert.equal(run.sim.stopRecord.event,event);assert.equal(run.calls.slice(before).filter(call=>call.kind===2).length,0);
 run.event('事件/授权_按下.txt');run.tick(40);assert.equal(run.context.h13.vals[2],3);assert.equal(run.sim.target,0);assert.equal(run.sim.owner,1);
});
for(const presses of [1,5,20])test('减速中重复K5与屏端急停和返回 '+presses+'次',()=>{
 const run=running();const event=k5(run);const started=run.sim.stop.started;run.tick(5);run.event('事件/返回_弹起.txt');run.tick();assert.equal(run.context.sys_pid,1);
 for(let index=0;index<presses;index++){k5(run);assert.equal((run.sim.stop||run.sim.stopRecord).event,event);assert.equal((run.sim.stop||run.sim.stopRecord).started,started);run.event('事件/急停_按下.txt');run.tick();}
 stopped(run);assert.equal(run.context.sys_pid,1);assert.equal(run.sim.stopRecord.event,event);
});
test('首次收到的K5事件已完成仍正确收尾',()=>{const run=running();k5(run);run.sim.actual=0;run.sim.tick(250);assert.equal(run.sim.stop,null);run.tick(12);assert.equal(run.context.h13.vals[2],5);});
test('授权时的历史成功不能当成本次停车',()=>{const run=fixture();run.sim.stopRecord={event:7,sequence:0,command:16,reason:5,result:3,owner:1,started:0,completed:1,decel:500};run.sim.stopEvent=7;run.authorize();assert.equal(run.context.h13.vals[60],7);run.sim.session=0;run.sim.owner=0;run.sim.target=run.sim.actual=0;run.tick(10);assert.equal(run.context.h13.vals[2],6);assert.equal(run.context.h13.vals[56],0);});
for(const scenario of ['错误owner','错误boot','反馈无效','使能关闭','实际非零','安全锁','失败事件'])test('不得用异常事件解除锁存 '+scenario,()=>{
 const run=running();k5(run);
 mutateStatus(run,regs=>{if(scenario==='错误owner')regs[1120+56]=2;if(scenario==='错误boot')regs[1120+3]^=1;if(scenario==='反馈无效')regs[1120+25]=0;if(scenario==='使能关闭')regs[1120+27]=0;if(scenario==='实际非零'){regs[1120+23]=50;regs[1120+24]=50;}if(scenario==='安全锁')regs[1120+37]=4;if(scenario==='失败事件'){regs[1120+53]=5;regs[1120+30]=5;}});
 run.tick(180);assert.notEqual(run.context.h13.vals[2],5);assert.notEqual(run.context.h13.vals[2],3);run.event('事件/授权_按下.txt');run.event('事件/返回_弹起.txt');run.tick(8);assert.equal(run.context.sys_pid,1);assert.notEqual(run.context.h13.vals[2],3);
});
test('快照冻结不能确认新出现的完成事件',()=>{const run=running();k5(run);run.tick(5);const high=run.context.h13.vals[70],low=run.context.h13.vals[71];mutateStatus(run,regs=>{regs[1120+4]=high;regs[1120+5]=low;});run.tick(180);assert.notEqual(run.context.h13.vals[2],5);assert.equal(run.context.h13.vals[2],6);});
test('本次成功后遥控占用允许只读返回且不能抢占',()=>{const run=running();k5(run);stopped(run);run.sim.owner=2;run.sim.session=0;run.sim.target=50;run.tick(10);run.event('事件/授权_按下.txt');run.tick(20);assert.equal(run.sim.owner,2);assert.notEqual(run.context.h13.vals[2],3);assert.equal(run.sim.result[8],5);run.event('事件/返回_弹起.txt');run.tick(4);assert.equal(run.context.sys_pid,1);run.sim.owner=0;run.sim.target=run.sim.actual=0;run.context.sys_pid=8;run.tick(10);run.event('事件/授权_按下.txt');run.tick(30);assert.equal(run.context.h13.vals[2],3);assert.equal(run.sim.target,0);});
test('关联事件被覆盖后不推断成功，用户急停采用session0重新确认',()=>{const run=running();k5(run);run.tick(5);run.sim.actual=0;run.sim.tick(250);run.sim.stopRecord={...run.sim.stopRecord,event:run.sim.stopEvent+1,owner:2};run.sim.stopEvent++;run.sim.owner=2;run.sim.session=0;run.sim.target=50;run.tick(10);assert.equal(run.context.h13.vals[2],6);const before=run.calls.length;run.event('事件/急停_按下.txt');run.tick(12);const stop=run.calls.slice(before).find(call=>call.kind===2&&command(call)===16);assert.ok(stop);assert.equal(protocol.get32(protocol.bytesToWords(stop.request.subarray(7,-2)),4),0);});
test('19号短时显示缓存不参与跨源停车完成判定',()=>{const run=running();k5(run);run.tick(5);run.setOffline(true);run.context.regionSeen.vals[0]=1;run.context.regionAge.vals[0]=5;run.context.regionBadRun.vals[0]=1;run.renderGlobal();run.tick(50);assert.notEqual(run.context.h13.vals[2],5);assert.notEqual(run.context.h13.vals[2],3);assert.equal(run.context.h13.vals[10],0);});

test('屏端先释放后K5覆盖请求序号仍按同事件完成',()=>{const run=running();run.intent(3);run.tick(4);const event=run.sim.stop.event;k5(run);run.tick(5);assert.equal(run.context.h13.vals[56],1);stopped(run);assert.equal(run.sim.stopRecord.event,event);});
test('屏端先急停后K5到达不依赖本地请求序号',()=>{const run=running();run.event('事件/急停_按下.txt');run.tick(4);const event=run.sim.stop.event;k5(run);run.tick(5);stopped(run);assert.equal(run.sim.stopRecord.event,event);});
