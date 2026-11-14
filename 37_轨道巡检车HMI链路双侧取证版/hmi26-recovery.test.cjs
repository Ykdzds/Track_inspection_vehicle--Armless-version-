'use strict';
const {test}=require('node:test');const assert=require('node:assert/strict');
const {fixture}=require('./hmi13.test.cjs');const protocol=require('./r2-protocol.cjs');
const writes=r=>r.calls.filter(c=>c.kind===2).map(c=>protocol.bytesToWords(c.request.subarray(7,-2)));
function ready(){const r=fixture({globalDisplay:true});r.sim.recoveryState=2;r.sim.recoveryEligible=1;r.sim.recoveryGeneration=0x11223344;r.tick(50);return r;}
test('26 实际按钮与事务服务生成恢复帧，幂等，恢复完成保持未授权',()=>{
 const r=ready(),h=r.context.h13.vals;h[10]=-403;h[11]=1;h[39]=1;r.context.P8_ServoControl.rpmSlider.val=1197;
 r.recovery();r.recovery();r.tick(70);
 const a=writes(r);assert.equal(a.length,1);assert.equal(a[0][8],17);assert.equal(protocol.get32(a[0],4),0);assert.equal(protocol.get32(a[0],2),r.sim.boot);assert.equal(protocol.get32(a[0],12),0x11223344);assert.equal(protocol.get32(a[0],6),h[128]*65536+h[129]);
 assert.equal(h[133],6);assert.equal(h[2],1);assert.equal(h[10],0);assert.equal(h[6]+h[7],0);assert.equal(r.context.P8_ServoControl.rpmSlider.val,1600);assert.match(r.context.P8_ServoControl.recoveryStatus.txt,/恢复完成/);
 r.recovery();r.tick(10);assert.equal(writes(r).length,1);
 r.intent(1);r.tick(30);assert.equal(h[2],3);const motion=writes(r).filter(x=>x[8]===33);assert.ok(motion.length);assert.equal(protocol.get32(motion[0],10),0);
});
test('26 等待健康、资格不足、冻结快照、版本及零generation均拒绝且不排队',()=>{
 const cases=[r=>r.context.h13Status.vals[89]=1,r=>r.context.h13Status.vals[90]=0,r=>r.context.h13.vals[73]-=300,r=>r.context.h13Status.vals[88]=0,r=>{r.context.h13Status.vals[91]=0;r.context.h13Status.vals[92]=0;}];
 for(const change of cases){const r=ready();change(r);r.recovery();assert.equal(r.context.h13.vals[127],0);r.tick(30);assert.equal(writes(r).length,0);}
});
test('26 排队后generation变化或状态过期，发送前再次拒绝',()=>{
 for(const mutate of [r=>{r.sim.recoveryGeneration++;r.context.h13Status.vals[92]++;},r=>{r.context.h13.vals[73]-=300;r.context.h13.vals[24]=0;}]){const r=ready();r.recovery();mutate(r);r.tick(20);assert.equal(writes(r).length,0);assert.equal(r.context.h13.vals[133],7);}
});
test('26 恢复期间不发送授权速度释放，身份和结果轮询持续',()=>{
 const r=ready();r.recovery();r.tick(1);r.sim.recoveryRequestedAt=1e9;
 for(let i=0;i<60;i++){r.context.h13.vals[11]=i%2?1:3;r.tick();}
 assert.ok(writes(r).every(w=>w[8]===17));const reads=r.calls.filter(c=>c.kind===1).map(c=>c.request.readUInt16BE(2));assert.ok(reads.filter(x=>x===512).length>2);assert.ok(reads.includes(288));assert.ok(reads.includes(1120));
 r.sim.boot++;r.sim.recoveryGeneration++;r.tick(40);assert.equal(r.context.h13.vals[127],0);assert.equal(r.context.h13.vals[10],0);assert.notEqual(r.context.h13.vals[2],3);
});
test('26 普通加速不显示换向，reverse边沿后加速并退出',()=>{
 let reverse=false;const r=fixture({transformResponse(mod,request){if(request.readUInt16BE(2)===1120 && mod.fcode===3 && reverse){const n=1+11*2;const flags=mod.rxBuf[n]*256+mod.rxBuf[n+1];mod.rxBuf[n]=(flags|16)>>8;mod.rxBuf[n+1]=(flags|16)&255;}}});
 r.authorize();r.context.h13.vals[10]=278;r.tick(25);assert.doesNotMatch(r.context.P8_ServoControl.stopStatus.txt,/换向/);reverse=true;r.tick(12);assert.match(r.context.P8_ServoControl.stopStatus.txt,/减速过零/);reverse=false;r.tick(8);assert.match(r.context.P8_ServoControl.stopStatus.txt,/反向加速/);r.tick(180);assert.doesNotMatch(r.context.P8_ServoControl.stopStatus.txt,/换向/);
});
test('26 停车成功提示覆盖左轴右轴双轴，陈旧速度保持最后零速，严格控制已撤权',()=>{
 for(const axis of [1,2,3]){const r=fixture({axis,globalDisplay:true});r.authorize();r.context.h13.vals[10]=-403;r.tick(25);r.event('事件/急停_按下.txt');r.tick(100);assert.match(r.context.P8_ServoControl.stopStatus.txt,/已停车，未授权/);assert.equal(r.context.h13.vals[10],0);const last=r.context.P8_ServoControl[axis===2?'rightActual':'leftActual'].txt;r.setOffline(true);r.tick(15);assert.equal(r.context.P8_ServoControl[axis===2?'rightActual':'leftActual'].txt,last);assert.notEqual(r.context.h13.vals[2],3);}
});
test('26 主控恢复失败显示真实恢复原因，不能用恢复动作授权',()=>{
 const r=ready();r.recovery();r.tick(1);r.sim.recoveryState=7;r.sim.recoveryFailure=3;r.tick(20);assert.match(r.context.P8_ServoControl.recoveryStatus.txt,/恢复失败/);assert.match(r.context.P8_ServoControl.recoveryDetail.txt,/原因 3/);assert.equal(r.context.P8_ServoControl.recoveryButton.enable,0);assert.ok(writes(r).every(w=>w[8]===17));
});
test('26 主控恢复初始化清除结果槽，仍按本次generation的新鲜恢复状态完成',()=>{
 const r=ready();r.recovery();r.tick(1);r.sim.result=Array(20).fill(0);r.tick(50);assert.equal(r.context.h13.vals[133],6);assert.equal(r.context.h13.vals[2],1);assert.ok(writes(r).every(w=>w[8]===17));
});
test('26 已失败恢复拒绝新的普通授权',()=>{const r=ready();r.sim.recoveryState=7;r.sim.recoveryFailure=3;r.tick(15);r.intent(1);r.tick(15);assert.equal(writes(r).length,0);});
test('26 实际PIX中文资源读回覆盖所有当前提示',()=>{
 const fs=require('node:fs'),path=require('node:path');const glyphs=JSON.parse(fs.readFileSync(path.join(__dirname,'运行核验/26号实施_20261028/26号字体字符集读回.json'),'utf8').replace(/^\uFEFF/,'')).Characters;
 for(const name of ['HMI13界面刷新.txt','全局计龄刷新.txt'])for(const m of fs.readFileSync(path.join(__dirname,name),'utf8').matchAll(/"([^"\n]*)"/g))for(const c of m[1])if(/[\u4e00-\u9fff]/.test(c))assert.ok(glyphs.includes(c),name+':'+c);
});
