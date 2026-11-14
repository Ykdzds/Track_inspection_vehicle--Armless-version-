'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const {fixture}=require('./hmi13.test.cjs');
for(const axis of [1,2,3])for(const rpm of [-449,449])test('27 CAN PB10 0x080, transient reply loss, reauthorize axis='+axis+' rpm='+rpm,()=>{
 const r=fixture({axis,globalDisplay:true});r.authorize();r.context.h13.vals[10]=rpm;r.tick(60);
 assert.equal(r.sim.owner,1);r.sim.beginStop(128,1);r.sim.stop.reason=5;r.sim.stop.decel=600;
 const event=r.sim.stop.event,started=r.sim.stop.started;
 r.tick(4);r.setOffline(true);r.tick(8);r.setOffline(false);r.tick(180);
 assert.equal(r.sim.stopRecord.event,event);assert.equal(r.sim.stopRecord.started,started);
 assert.equal(r.sim.owner,0);assert.equal(r.context.h13.vals[2],5);assert.equal(r.context.h13.vals[10],0);
 r.event('事件/授权_按下.txt');r.tick(40);assert.equal(r.context.h13.vals[2],3);assert.equal(r.sim.target,0);
});
test('27 first communication cause survives subsequent errors and stale speeds remain unknown',()=>{
 const r=fixture({globalDisplay:true});r.authorize();r.setOffline(true);r.tick(80);
 const h=r.context.h13.vals,first=[h[161],h[162],h[163],h[164],h[167],h[168]];
 assert.ok(first[0]);assert.match(r.context.P8_ServoControl.leftActual.txt,/--/);
 r.tick(160);assert.deepEqual([h[161],h[162],h[163],h[164],h[167],h[168]],first);
 assert.notEqual(h[2],3);assert.equal(h[10],0);
});
