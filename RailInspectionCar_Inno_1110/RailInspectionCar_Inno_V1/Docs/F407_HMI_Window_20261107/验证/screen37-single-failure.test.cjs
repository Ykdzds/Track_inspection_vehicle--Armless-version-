'use strict';
const {test}=require('node:test'), assert=require('node:assert/strict');
const {fixture}=require("D:\\wtzn_sx_\\同济大学-轨道巡检车\\同济大学轨道巡检车协作资料\\2.硬件资料\\7.三易串口触摸屏10.1寸\\用户代码\\37_轨道巡检车HMI链路双侧取证版\\hmi37-fixture.cjs");
const {frame}=require("D:\\wtzn_sx_\\同济大学-轨道巡检车\\同济大学轨道巡检车协作资料\\2.硬件资料\\7.三易串口触摸屏10.1寸\\用户代码\\37_轨道巡检车HMI链路双侧取证版\\r2-protocol.cjs");
function singleFailure(mode) {
    const r=fixture();
    r.authorize(); r.slide(2351); r.tick(20);
    assert.equal(r.sim.target,751); assert.equal(r.h[2],3);
    const original=r.sim.handle.bind(r.sim);
    let injected=0, captureAt=null;
    r.sim.handle=request=>{
        if(!injected && request[1]===3 && request.readUInt16BE(2)===1120) {
            injected++; captureAt=r.now();
            return mode==='FC83/02' ? frame([1,0x83,2]) : null;
        }
        return original(request);
    };
    const from=r.calls.length;
    for(let i=0;i<8 && r.c.P0_Boot.d37c.vals[0]!==1;i++) r.tick();
    const d=r.c.P0_Boot.d37.vals;
    assert.equal(injected,1); assert.equal(r.h[2],6); assert.equal(r.h[10],0);
    assert.equal(d[4],2); assert.equal(d[11],1120); assert.equal(d[12],64);
    assert.ok(d[6]&16); assert.equal(d[6]&2,0);
    if(mode==='FC83/02') {
        assert.equal(d[7],0); assert.equal(d[8],1); assert.equal(d[9],0x83); assert.equal(d[10],1);
    } else {
        assert.equal(d[7],65535); assert.equal(d[8],0); assert.equal(d[9],0); assert.equal(d[10],0);
    }
    const frozen=Array.from(d);
    r.tick(80);
    assert.equal(r.h[2],5); assert.equal(r.sim.owner,0); assert.equal(r.sim.session,0);
    const afterwards=r.calls.slice(from), stops=afterwards.filter(x=>x.kind==='write'&&x.request[24]===16);
    assert.ok(stops.length>0);
    assert.ok(!afterwards.some(x=>x.kind==='write'&&x.request[24]===33&&x.request.readInt32BE(27)!==0));
    assert.deepEqual(Array.from(d),frozen);
    console.log('SINGLE_FAILURE '+JSON.stringify({mode,injected,captureAt,stopAt:stops[0].at,stopDelay:stops[0].at-captureAt,firstPage:frozen.slice(0,24),finalState:r.h[2],stopCount:stops.length}));
}
test('one valid FC83/02 critical reply triggers frozen first cause then protective STOP without target replay',()=>singleFailure('FC83/02'));
test('one missing critical reply triggers frozen raw FFFF then protective STOP without target replay',()=>singleFailure('timeout'));
