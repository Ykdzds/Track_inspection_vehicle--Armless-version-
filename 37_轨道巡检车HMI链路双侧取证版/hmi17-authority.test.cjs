'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
const {fixture}=require('./hmi13.test.cjs');
const protocol=require('./r2-protocol.cjs');
const command=call=>protocol.bytesToWords(call.request.subarray(7,-2))[8];

function toggleModel(run,fireOnAssignment){
    let pressed=0;let changes=0;
    Object.defineProperty(run.context.P8_ServoControl.authorize,'press',{
        configurable:true,get:()=>pressed,set:value=>{
            if(value===pressed)return;
            pressed=value;changes++;
            if(fireOnAssignment)run.event(value?'事件/授权_按下.txt':'事件/授权_弹起.txt');
        }
    });
    return {click(){pressed=pressed?0:1;run.event(pressed?'事件/授权_按下.txt':'事件/授权_弹起.txt');},changes:()=>changes};
}

for(const fireOnAssignment of [false,true])test('双态首次按下授权、连续刷新不自行撤销、再次弹起取消；程序事件='+fireOnAssignment,()=>{
    const run=fixture({globalDisplay:true});const button=toggleModel(run,fireOnAssignment);run.tick(40);
    assert.equal(run.calls.filter(call=>call.kind===2).length,0);
    button.click();run.tick(60);
    assert.equal(run.context.h13.vals[2],3);assert.equal(run.sim.owner,1);
    assert.equal(run.context.P8_ServoControl.authorize.press,1);
    assert.equal(run.calls.filter(call=>call.kind===2&&command(call)===1).length,1);
    assert.equal(run.calls.filter(call=>call.kind===2&&[3,16].includes(command(call))).length,0);
    const before=button.changes();run.tick(40);assert.equal(button.changes(),before);
    button.click();run.tick(50);
    assert.equal(run.sim.owner,0);assert.equal(run.context.h13.vals[2],5);
    assert.equal(run.context.P8_ServoControl.authorize.press,0);
});

test('新授权确认后不先等旧页，第一目标必须0且确认后100ms内提交',()=>{
    const run=fixture({globalDisplay:true,responseDelayMs:50});run.tick(40);
    const button=toggleModel(run,true);button.click();
    run.context.P8_ServoControl.rpmSlider.val=3200;run.event('事件/滑块_滑动.txt');
    for(let step=0;step<20&&!run.calls.some(call=>call.kind===2&&command(call)===33);step++)run.tick();
    const writes=run.calls.filter(call=>call.kind===2);
    assert.equal(command(writes[0]),1);assert.equal(command(writes[1]),33);
    assert.equal(protocol.get32(protocol.bytesToWords(writes[1].request.subarray(7,-2)),10),0);
    assert.ok(writes[1].at-writes[0].at<=200);
    assert.equal(run.context.P8_ServoControl.rpmSlider.val,1600);
    assert.equal(run.sim.stop,null);
});

test('授权准备延迟时结果与状态交替，不被辅助区抢占或重复申请',()=>{
    const run=fixture({globalDisplay:true});run.tick(40);
    const original=run.sim.handle.bind(run.sim);let requestedAt=null;let allocatedSession=0;
    run.sim.handle=request=>{
        if(request[1]===16&&command({request})===1){
            const response=original(request);requestedAt=run.sim.now;allocatedSession=run.sim.session;
            run.sim.session=0;run.sim.publish(1,run.sim.lastSequence,2,0);return response;
        }
        if(requestedAt!==null&&!run.sim.session&&run.sim.now-requestedAt>=600){
            run.sim.session=allocatedSession;run.sim.lastTarget=run.sim.lastLease=run.sim.now;
            run.sim.publish(1,run.sim.lastSequence,3,0);requestedAt=null;
        }
        return original(request);
    };
    const start=run.calls.length;const button=toggleModel(run,true);button.click();run.tick(35);
    assert.equal(run.context.h13.vals[2],3);assert.equal(run.sim.owner,1);
    assert.equal(run.calls.filter(call=>call.kind===2&&command(call)===1).length,1);
    const prefix=run.calls.slice(start).slice(1,9);
    assert.ok(prefix.every(call=>call.kind===1&&[288,1120].includes(call.request.readUInt16BE(2))));
});

test('准备未决时再次点击走独立急停，不能迟到自动授权',()=>{
    const run=fixture();run.tick(40);const button=toggleModel(run,true);button.click();run.tick();button.click();run.tick(40);
    assert.equal(run.sim.owner,0);assert.notEqual(run.context.h13.vals[2],3);
    assert.equal(run.calls.filter(call=>call.kind===2&&command(call)===1).length,1);
    assert.ok(run.calls.some(call=>call.kind===2&&command(call)===16));
});

test('停车成功后重复急停保持幂等，一次授权恢复并可返回主页',()=>{
    const run=fixture();run.authorize();
    run.context.h13.vals[10]=100;run.tick(80);
    run.event('事件/急停_按下.txt');run.tick(150);
    assert.equal(run.context.h13.vals[2],5);assert.equal(run.sim.owner,0);
    const before=run.calls.filter(call=>call.kind===2).length;
    const event=run.context.h13Status.vals[48];
    for(let index=0;index<20;index++){
        run.event('事件/急停_按下.txt');run.tick();
    }
    run.tick(40);
    assert.equal(run.context.h13.vals[2],5);assert.equal(run.sim.owner,0);
    assert.equal(run.context.h13Status.vals[48],event);
    const repeated=run.calls.filter(call=>call.kind===2).slice(before);
    assert.ok(repeated.length>0);
    assert.ok(repeated.every(call=>command(call)===16));
    assert.ok(repeated.every(call=>protocol.get32(protocol.bytesToWords(call.request.subarray(7,-2)),4)===0));
    run.event('事件/授权_按下.txt');run.tick(40);
    assert.equal(run.context.h13.vals[2],3);assert.equal(run.sim.owner,1);
    assert.equal(run.sim.target,0);
    run.event('事件/返回_弹起.txt');run.tick(30);
    assert.equal(run.context.sys_pid,1);assert.equal(run.context.h13.vals[2],3);assert.equal(run.sim.owner,1);
});

test('控件刷新保护位不处理程序按下弹起，真实滑动只在授权后有效',()=>{
    const run=fixture();run.tick(40);run.context.h13.vals[160]=1;
    run.event('事件/授权_按下.txt');assert.equal(run.context.h13.vals[11],0);
    run.context.h13.vals[160]=0;run.event('事件/授权_按下.txt');run.tick(30);
    run.context.P8_ServoControl.rpmSlider.val=1650;run.event('事件/滑块_滑动.txt');run.tick(10);
    assert.equal(run.sim.target,50);
    run.context.h13.vals[160]=1;run.event('事件/授权_弹起.txt');
    assert.equal(run.context.h13.vals[11],0);run.context.h13.vals[160]=0;
});

test('按F4实际状态字典，租期与年龄不冒充最后序号',()=>{
    const run=fixture();run.sim.lastSequence=65534;run.tick(40);
    const status=run.sim.registers().slice(1120,1184);
    assert.equal(status[14],0);assert.equal(status[15],65535);
    assert.equal(protocol.get32(status,16),65534);
    const button=toggleModel(run,true);button.click();run.tick(30);
    const acquire=run.calls.find(call=>call.kind===2&&command(call)===1);
    assert.equal(protocol.get32(protocol.bytesToWords(acquire.request.subarray(7,-2)),6),65535);
    assert.equal(run.context.h13.vals[2],3);
});

test('旧显示脚本的0到1反复切换可触发取消，新显示不重复切换',()=>{
    const baseline=JSON.parse(fs.readFileSync(path.join(__dirname,'运行核验/17号复制基线.json'),'utf8').replace(/^\uFEFF/,''));
    const run=fixture();run.authorize();toggleModel(run,true);run.context.P8_ServoControl.authorize.press=1;
    run.context.h13.vals[11]=0;
    const source=fs.readFileSync(path.join(baseline.Source,'HMI13界面刷新.txt'),'utf8').replace(/^int (\w+);$/gm,'var $1;');
    new vm.Script(source).runInContext(run.context);
    assert.equal(run.context.h13.vals[11],3);
    run.context.h13.vals[11]=0;run.tick(20);
    assert.equal(run.context.h13.vals[2],3);assert.equal(run.context.h13.vals[11],0);
});
