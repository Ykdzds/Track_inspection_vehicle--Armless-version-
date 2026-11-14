'use strict';
const {test}=require('node:test');const assert=require('node:assert/strict');const fs=require('node:fs'),path=require('node:path');
const {fixture,spec}=require('./hmi32-fixture.cjs');
const local=k=>path.join(__dirname,k);
test('九页保存最近有效数据，10秒无回复只改变通信质量',()=>{
 const x=fixture();const keys=['P1_Main.targetSpeed','P1_Main.leftSpeed','P1_Main.rightSpeed','P1_Main.leftFault','P1_Main.controlSource','P2_Diag.leftState','P3_Position.startupTravel','P3_Position.axis1Position','P4_Info.ctrlVersion','P5_Alarm.alarmLevel','P6_Sync.syncActualDiff','P7_System_Ext.batteryState','P8_ServoControl.leftActual','P8_ServoControl.leftTarget','P8_ServoControl.rightOutput'];
 const old=Object.fromEntries(keys.map(k=>[k,x.value(k)]));for(const k of keys)assert.doesNotMatch(old[k],/未知|--/,k);
 for(let i=0;i<200;i++){x.c.sys_pid=i%9;x.tick();for(const k of keys)assert.equal(x.value(k),old[k],k);}
 assert.match(x.value('P8_ServoControl.linkStatus'),/超时/);assert.match(x.value('P1_Main.ctrlComm'),/超时/);
});
test('短时延迟与单次无回调不切换正常状态',()=>{const x=fixture();for(let i=0;i<59;i++){x.tick();assert.match(x.value('P8_ServoControl.linkStatus'),/正常/);}x.tick();assert.match(x.value('P8_ServoControl.linkStatus'),/超时/);x.tick();x.receive();x.tick();assert.match(x.value('P8_ServoControl.linkStatus'),/正常/);});
test('全字段历史值不被未知、哨兵及无效快照覆盖',()=>{
 const x=fixture();const keys=['P8_ServoControl.leftActual','P8_ServoControl.rightTarget','P1_Main.targetSpeed','P1_Main.leftFault','P3_Position.startupTravel','P5_Alarm.alarmLevel'];const old=keys.map(k=>x.value(k));
 x.s[23]=x.s[20]=32767;x.r[202]=32767;x.r[40]=65535;x.r[157]=0;x.r[124]=7;x.tick();x.receive();
 keys.forEach((k,i)=>assert.equal(x.value(k),old[i],k));
});
test('合法零、负数、16位边界与轴未参与',()=>{
 const x=fixture();for(const [word,text] of [[0,'0rpm'],[65535,'-1rpm'],[32768,'-32768rpm'],[32766,'32766rpm']]){x.tick();x.s[23]=word;x.receive();assert.match(x.value('P8_ServoControl.leftActual'),new RegExp(text));}
 x.tick();x.s[9]=1;x.receive();assert.match(x.value('P8_ServoControl.rightActual'),/未参与/);
});
test('无效位置样本保留数字，质量状态确认3个独立样本后才变化',()=>{
 const x=fixture(3);const old=x.value('P3_Position.startupTravel');x.r[124]=7;x.tick();x.receive([2],false);assert.equal(x.value('P3_Position.startupTravel'),old);assert.equal(x.value('P3_Position.positionIncomplete'),'数据不完整：否');
 for(let i=0;i<20;i++)x.tick();assert.equal(x.value('P3_Position.positionIncomplete'),'数据不完整：否');
 x.tick();x.receive([2],false);assert.equal(x.value('P3_Position.positionIncomplete'),'数据不完整：否');
 x.tick();x.receive([2],false);assert.equal(x.value('P3_Position.positionIncomplete'),'数据不完整：是');assert.equal(x.value('P3_Position.startupTravel'),old);
});
test('位置基准改变只清里程；更换控制源不清物理里程',()=>{
 const x=fixture(3);x.r[124]=7;x.r[127]=2;x.tick();x.receive([2],false);assert.match(x.value('P3_Position.startupTravel'),/未知/);assert.match(x.value('P3_Position.axis1Position'),/12345/);
 x.r[124]=3;x.tick();x.receive([2]);const before=x.value('P3_Position.startupTravel');x.tick();x.s[8]=2;x.s[12]=x.s[13]=0;x.receive([],true);assert.equal(x.value('P3_Position.startupTravel'),before);assert.match(x.value('P1_Main.targetSpeed'),/未知/);assert.match(x.value('P8_ServoControl.ownerStatus'),/遥控/);
});
test('主控重启清全部旧代次显示且不会改变控制数组',()=>{
 const x=fixture();x.h[4]=111;x.h[5]=222;x.c.metaRegs.vals[4]=111;x.c.metaRegs.vals[5]=222;x.tick();assert.match(x.value('P8_ServoControl.leftActual'),/--/);assert.match(x.value('P3_Position.startupTravel'),/未知/);assert.match(x.value('P1_Main.leftSpeed'),/未知/);
});
test('序号倒退不提交新数值，冻结快照不刷新年龄',()=>{
 const x=fixture();const old=x.value('P8_ServoControl.leftActual');const stamp=x.c.P0_Boot.ui32SourceAt.vals[7];x.s[5]=0;x.s[23]=777;x.tick();x.h[21]=x.h[73]=x.h[1];x.render();assert.equal(x.value('P8_ServoControl.leftActual'),old);assert.equal(x.c.P0_Boot.ui32SourceAt.vals[7],stamp);
});
test('刷新文本写入是最终提交，不出现占位符瞬态',()=>{
 const x=fixture();x.writes.length=0;for(let i=0;i<30;i++){x.tick();if(i%3===0){x.s[23]++;x.r[202]++;x.receive();}}
 assert.ok(x.writes.length>0);assert.ok(x.writes.every(w=>!w.text.includes('统一快照 S')));assert.ok(x.writes.filter(w=>/leftActual|targetSpeed/.test(w.key)).every(w=>!/(未知|--)/.test(w.text)));
});
test('显示代码不得写控制数据；31号安全与协议源逐字节保持',()=>{
 const src=fs.readFileSync(local('全局计龄刷新.txt'),'utf8');assert.doesNotMatch(src,/modbusRead\(|modbusWrites\(/);assert.doesNotMatch(src,/P8_ServoControl\.h13Status\.vals\[[^\]]+\]\s*=(?!=)/);
 const base=path.join(__dirname,'..','31_轨道巡检车日夜双主题切换版');for(const name of ['HMI13事务服务.txt','Modbus中断只读解析.txt','事件/授权_按下.txt','事件/授权_弹起.txt','事件/安全急停_按下.txt','事件/故障恢复按钮_按下.txt','事件/滑块_滑动.txt','事件/返回_弹起.txt']){if(fs.existsSync(local(name)))assert.deepEqual(fs.readFileSync(local(name)),fs.readFileSync(path.join(base,name)),name);}
 const assign=[...src.matchAll(/P8_ServoControl\.linkStatus\.txt\s*=/g)];assert.equal(assign.length,1);assert.equal(spec.count,123);
});

test('线速度哨兵不切换到其它源转速覆盖历史值',()=>{const x=fixture();x.s[8]=2;x.tick();x.receive();const keys=['P8_ServoControl.acceptedTarget','P8_ServoControl.leftTarget','P8_ServoControl.leftOutput'];const before=keys.map(k=>x.value(k));x.s[58]=x.s[60]=32767;x.tick();x.receive();keys.forEach((k,i)=>assert.equal(x.value(k),before[i]));});
