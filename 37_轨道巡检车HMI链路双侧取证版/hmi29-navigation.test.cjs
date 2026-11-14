'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const {fixture}=require('./hmi13.test.cjs');
const protocol=require('./r2-protocol.cjs');
const words=call=>protocol.bytesToWords(call.request.subarray(7,-2));
const command=call=>words(call)[8];
const maxGap=calls=>Math.max(0,...calls.slice(1).map((call,i)=>call.at-calls[i].at));
function running(rpm=100,extra={}){
 const r=fixture({globalDisplay:true,navigationLifecycle:true,fireProgramEvents:true,...extra});
 r.authorize();r.context.P8_ServoControl.rpmSlider.val=1600+rpm;r.tick(60);return r;
}
function home(r){r.event('事件/返回_弹起.txt');assert.equal(r.context.sys_pid,1);}
function control(r){r.event('事件/主页入口_弹起.txt');assert.equal(r.context.sys_pid,8);}

for(const rpm of [-100,0,100])test(`29 cross-page 120s/page target=${rpm}`,()=>{
 const r=running(rpm),h=r.context.h13.vals,session=r.sim.session;
 home(r);
 for(let page=1;page<=7;page++){
  r.navigate(page);const offset=r.calls.length,previousSequence=r.sim.lastSequence,previousStatus=h[21];
  r.tick(2400);const calls=r.calls.slice(offset),writes=calls.filter(c=>c.kind===2);
  assert.equal(r.context.sys_pid,page);assert.equal(r.sim.owner,1);assert.equal(r.sim.session,session);
  assert.equal(h[2],3);assert.equal(h[10],rpm);assert.equal(r.sim.target,rpm);
  assert.ok(r.sim.lastSequence>previousSequence);assert.ok(h[21]>previousStatus);
  assert.ok(writes.length>500);assert.ok(writes.every(c=>command(c)===33));
  assert.ok(writes.every(c=>(protocol.get32(words(c),10)|0)===rpm));
  const targetGap=maxGap(writes),statusGap=maxGap(calls.filter(c=>c.kind===1&&c.request.readUInt16BE(2)===1120));
  assert.ok(targetGap<=200,`page=${page} targetGap=${targetGap}`);
  assert.ok(statusGap<=150,`page=${page} statusGap=${statusGap}`);
  assert.equal(r.maximumInflight(),1);
  console.log(JSON.stringify({scenario:'29 navigation simulated',rpm,page,durationMs:120000,targetGapMs:targetGap,statusGapMs:statusGap}));
  control(r);assert.equal(h[10],rpm);assert.equal(r.context.P8_ServoControl.rpmSlider.val,1600+rpm);home(r);
 }
});

for(const axis of [1,2,3])test(`29 repeated navigation and view reload axis=${axis}`,()=>{
 const r=running(-100,{axis}),h=r.context.h13.vals,session=r.sim.session;
 for(let i=0;i<20;i++){
  home(r);r.navigate(2+i%6);r.tick(2);control(r);
  // Force an editor-like view reset without a user gesture; the load event must restore it safely.
  h[32]=1;r.context.P8_ServoControl.rpmSlider.val=1600;h[32]=0;
  r.event('事件/P8_加载.txt');r.tick(2);
  assert.equal(h[10],-100);assert.equal(r.context.P8_ServoControl.rpmSlider.val,1500);
  assert.equal(r.context.P8_ServoControl.authorize.press,1);assert.equal(r.sim.session,session);
 }
 assert.ok(r.calls.filter(c=>c.kind===2).every(c=>![3,16].includes(command(c))));
});

test('29 authorization pending navigates immediately and completes at zero',()=>{
 const r=fixture({globalDisplay:true,navigationLifecycle:true,fireProgramEvents:true});r.tick(40);
 r.event('事件/授权_按下.txt');r.tick();assert.equal(r.context.h13.vals[2],2);home(r);r.navigate(4);r.tick(50);
 assert.equal(r.context.sys_pid,4);assert.equal(r.context.h13.vals[2],3);assert.equal(r.sim.target,0);
 assert.equal(r.calls.filter(c=>c.kind===2&&command(c)===1).length,1);
 assert.ok(r.calls.filter(c=>c.kind===2).every(c=>![3,16].includes(command(c))));
});

for(const stop of ['cancel','estop'])test(`29 ${stop} survives navigation and does not force delayed page jump`,()=>{
 const r=running(),h=r.context.h13.vals;
 r.event(stop==='cancel'?'事件/授权_弹起.txt':'事件/急停_按下.txt');
 home(r);r.navigate(3);r.tick(150);
 assert.equal(r.context.sys_pid,3);assert.equal(r.sim.owner,0);assert.equal(h[10],0);assert.equal(h[2],5);
 assert.ok(r.calls.some(c=>c.kind===2&&command(c)===(stop==='cancel'?3:16)));
 control(r);assert.equal(r.context.P8_ServoControl.rpmSlider.val,1600);
 r.event('事件/授权_按下.txt');r.tick(40);assert.equal(h[2],3);assert.equal(r.sim.target,0);
});

for(const source of ['K5','PB10'])test(`29 background ${source} stop and fresh reauthorization`,()=>{
 const r=running(),oldSession=r.sim.session;home(r);r.navigate(6);
 r.sim.beginStop(source==='K5'?16:128,0);r.sim.stop.reason=5;r.tick(160);
 assert.equal(r.context.sys_pid,6);assert.equal(r.sim.owner,0);assert.equal(r.context.h13.vals[10],0);
 control(r);r.event('事件/授权_按下.txt');r.tick(40);
 assert.equal(r.context.h13.vals[2],3);assert.notEqual(r.sim.session,oldSession);assert.equal(r.sim.target,0);
});

for(const fault of ['offline','frozen','boot'])test(`29 background ${fault} invalidates old motion`,()=>{
 const r=running();home(r);r.navigate(5);
 if(fault==='offline')r.setOffline(true);
 if(fault==='frozen'){
  const original=r.sim.registers.bind(r.sim),frozen=original().slice(1120,1216);
  r.sim.registers=()=>{const regs=original();regs.splice(1120,96,...frozen);return regs;};
 }
 if(fault==='boot'){r.sim.boot++;r.sim.owner=0;r.sim.session=0;r.sim.target=0;r.sim.lastSequence=0;}
 r.tick(160);assert.equal(r.context.sys_pid,5);assert.notEqual(r.context.h13.vals[2],3);assert.equal(r.context.h13.vals[10],0);
 if(fault==='offline'){r.setOffline(false);r.tick(80);assert.equal(r.sim.owner,0);assert.notEqual(r.context.h13.vals[2],3);}
 control(r);assert.equal(r.context.P8_ServoControl.rpmSlider.val,1600);
});

for(const owner of [2,3])test(`29 read-only navigation never acquires other owner=${owner}`,()=>{
 const r=fixture({globalDisplay:true,navigationLifecycle:true,fireProgramEvents:true});r.sim.owner=owner;r.tick(40);
 for(let page=1;page<=7;page++){r.navigate(page);control(r);home(r);}
 assert.equal(r.sim.owner,owner);assert.equal(r.calls.filter(c=>c.kind===2).length,0);
 control(r);r.event('事件/急停_按下.txt');home(r);r.tick(40);
 assert.ok(r.calls.some(c=>c.kind===2&&command(c)===16));assert.equal(r.sim.owner,0);
});

test('29 all fault states allow navigation without changing control state',()=>{
 const r=fixture({navigationLifecycle:true,fireProgramEvents:true});r.tick(40);
 for(const state of [0,1,2,3,4,5,6,7]){
  r.context.h13.vals[2]=state;r.context.h13.vals[10]=state===3?100:0;
  r.context.h13.vals[11]=16;r.context.h13.vals[116]=1;
  r.navigate(8);home(r);r.navigate(7);control(r);
  assert.equal(r.context.h13.vals[2],state);assert.equal(r.context.h13.vals[11],16);assert.equal(r.context.h13.vals[116],1);
 }
});
