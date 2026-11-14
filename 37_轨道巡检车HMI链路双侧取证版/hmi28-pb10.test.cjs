'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const {fixture}=require('./hmi13.test.cjs');
for(const axis of [1,2,3]) for(const source of [1,2,4]) test(`28 PB10 missed association source=${source} axis=${axis}`,()=>{
 const r=fixture({axis,globalDisplay:true});r.authorize();const h=r.context.h13.vals;
 const oldSession=r.sim.session,sequence=r.sim.lastSequence;
 r.sim.owner=source;r.sim.beginStop(128,1);r.sim.stop.reason=5;r.sim.stop.decel=600;
 // Reproduce lost external-event association while the link is unconfirmed.
 h[69]=0;h[56]=0;h[2]=6;h[11]=h[26]=h[39]=h[68]=0;h[10]=0;
 r.tick(50);
 assert.equal(h[2],5);assert.equal(h[6],0);assert.equal(h[7],0);assert.equal(h[10],0);
 assert.match(r.context.P8_ServoControl.authorityStatus.txt,/停车完成，可重新授权/);
 r.event('事件/授权_按下.txt');r.tick(40);
 assert.equal(h[2],3);assert.notEqual(r.sim.session,oldSession);assert.ok(r.sim.lastSequence>sequence);assert.equal(r.sim.target,0);
});
for(const fault of ['lock','recovery','error','speed','stale','disabled','active','failed','frozen','boot'])test('28 PB10 release denies '+fault,()=>{
 const r=fixture({globalDisplay:true});r.authorize();const h=r.context.h13.vals;
 r.sim.beginStop(128,1);r.sim.stop.reason=5;r.tick(10);
 h[69]=0;h[56]=0;h[2]=6;h[11]=h[26]=h[39]=h[68]=0;
 const original=r.sim.registers.bind(r.sim);let frozen;
 r.sim.registers=()=>{
   const w=original(),o=1120;
   if(fault==='lock')w[o+37]=1;
   if(fault==='recovery')w[o+89]=2;
   if(fault==='error')w[o+36]=7;
   if(fault==='speed')w[o+23]=1;
   if(fault==='stale')w[o+46]=500;
   if(fault==='disabled')w[o+27]=2;
   if(fault==='active')w[o+11]|=4;
   if(fault==='failed')w[o+53]=5;
   if(fault==='boot')w[o+2]^=1;
   if(fault==='frozen'){if(!frozen)frozen=w.slice(o,o+96);w.splice(o,96,...frozen);}
   return w;
 };
 r.tick(50);assert.notEqual(h[2],5);r.event('事件/授权_按下.txt');r.tick(10);assert.notEqual(h[2],3);assert.equal(r.sim.owner,0);
});
