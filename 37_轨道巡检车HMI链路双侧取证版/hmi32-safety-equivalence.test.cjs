'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict'),path=require('node:path');
const {fixture}=require('./hmi13.test.cjs');
const {fixture:display}=require('./hmi32-fixture.cjs');
const base=path.join(__dirname,'..','31_轨道巡检车日夜双主题切换版');
test('31与32在相同50ms刷新下的请求字节、发送时间和安全数组完全一致',()=>{
 const a=fixture({presentationRoot:base,globalDisplayEvery:1}),b=fixture({globalDisplayEvery:1});
 for(const r of [a,b]){
  r.authorize();r.context.h13.vals[10]=300;r.tick(40);
  for(let p=1;p<=8;p++){r.context.sys_pid=p;r.tick(10);}
  r.event('事件/急停_按下.txt');r.tick(60);r.intent(1);r.tick(40);
  r.context.h13.vals[10]=-100;r.tick(30);r.setOffline(true);r.tick(30);
  r.setOffline(false);r.tick(100);
 }
 const frames=r=>r.calls.map(c=>({at:c.at,kind:c.kind,hex:c.request.toString('hex')}));
 assert.deepEqual(frames(b),frames(a));assert.deepEqual(b.context.h13.vals,a.context.h13.vals);
 assert.deepEqual(b.context.h13Status.vals,a.context.h13Status.vals);
 assert.equal(b.maximumInflight(),1);assert.notEqual(b.context.h13.vals[2],3);
});
test('错误站号、区域、地址、长度和中断类型均不能覆盖显示缓存',()=>{
 const r=fixture();r.authorize();const before=[...r.context.h13Status.vals];
 const shown=r.context.P8_ServoControl.leftActual.txt;
 for(const wrong of [{bcaddr:2},{bdindex:6},{bdaddr:1121},{bdcount:63},{interruptCode:3}]){
  Object.assign(r.context.mod,{bcaddr:1,bdindex:7,bdaddr:1120,bdcount:64,interruptCode:1,bdbuf:Array(128).fill(255)},wrong);
  Object.assign(r.context.h13.vals,{13:1,14:1,15:7,16:1120,17:64});
  r.deliver();r.renderGlobal();assert.deepEqual(r.context.h13Status.vals,before);
  assert.equal(r.context.P8_ServoControl.leftActual.txt,shown);
 }
});
test('字段年龄、质量滞回和正常零故障状态具有独立语义',()=>{
 const x=display(3),f=x.field('P3_Position.startupTravel'),n=x.spec.count;
 x.tick(1000);assert.equal(x.c.P0_Boot.ui32Field.vals[n*2+f.index],1000);
 x.r[124]=7;x.tick();x.receive([2],false);x.r[124]=3;x.tick();x.receive([2],false);
 x.r[124]=7;x.tick();x.receive([2],false);x.tick();x.receive([2],false);
 assert.equal(x.value('P3_Position.positionIncomplete'),'数据不完整：否');
 x.tick();x.receive([2],false);assert.equal(x.value('P3_Position.positionIncomplete'),'数据不完整：是');
});
