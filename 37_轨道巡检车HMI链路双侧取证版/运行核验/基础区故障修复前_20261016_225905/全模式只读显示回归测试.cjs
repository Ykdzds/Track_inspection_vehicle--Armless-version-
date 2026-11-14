const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '全局计龄刷新.txt'), 'utf8');
// 仅执行显示脚本等价分支，不操作串口、实屏或STM32。
const script = new vm.Script(source.replace(/^int (\w+);$/gm, 'var $1;'));
const names = ['P0_Boot', 'P1_Main', 'P2_Diag', 'P3_Position', 'P4_Info', 'P5_Alarm', 'P6_Sync', 'P7_System_Ext'];
function context(page) {
    const c = {sys_pid: page, intToString: String, floatToString: (v, n) => v.toFixed(n), showPage: p => { c.sys_pid = p; }};
    c.P8_ServoControl = new Proxy({id:8}, {get: (obj,key) => key in obj ? obj[key] : (obj[key]={})});
    for (const name of ['h13','h13Status','h13Result']) c[name]={vals:Array(160).fill(0)};
    for (const name of ['h13','h13Status','h13Result']) c.P8_ServoControl[name]=c[name];
    for (const [i, name] of names.entries()) c[name] = new Proxy({id: i}, {get: (obj, key) => key in obj ? obj[key] : (obj[key] = {})});
    for (const name of ['rawRegs','metaRegs','regionSeen','regionAge','regionValid','regionGoodRun','regionBadRun','regionFails','metaSeen','metaAge','metaValid','metaBadRun','bootState']) c[name] = {vals: Array(1024).fill(0)};
    for (const name of ['mainState','ctrlStatus','stageText','progressText','bootProgress','leftStatus','rightStatus','safeStatus','resetReason','swVer','protoVer','faultText']) c[name] = {};
    for (let i=0;i<5;i++) {c.regionSeen.vals[i]=1;c.regionValid.vals[i]=1;c.regionGoodRun.vals[i]=3;}
    c.rawRegs.vals[192]=0x5249;c.rawRegs.vals[193]=1;c.rawRegs.vals[194]=0x1807;c.rawRegs.vals[28]=1;
    c.rawRegs.vals[142]=1;c.rawRegs.vals[157]=1;c.rawRegs.vals[207]=1;
    return vm.createContext(c);
}
function run(c) {script.runInContext(c);return c;}
let passed=0;
function test(name, action) {action();passed++;console.log('PASS '+name);}
test('单轴有效不被整车位屏蔽',()=>{const c=context(3);c.rawRegs.vals[124]=0;c.rawRegs.vals[140]=1;c.rawRegs.vals[141]=0;c.rawRegs.vals[113]=12345;run(c);assert.equal(c.P3_Position.axis1Position.txt,'轴1原始位置：12345 count');assert.match(c.P3_Position.axis2Position.txt,/无效/);assert.match(c.P3_Position.positionBaseline.txt,/未建立/);});
test('整车有效不能覆盖单轴无效',()=>{const c=context(3);c.rawRegs.vals[124]=3;c.rawRegs.vals[140]=0;c.rawRegs.vals[141]=1;run(c);assert.match(c.P3_Position.axis1Position.txt,/无效/);});
test('原始位置独立于未知里程标志',()=>{const c=context(3);c.rawRegs.vals[124]=65535;c.rawRegs.vals[140]=1;c.rawRegs.vals[112]=65535;c.rawRegs.vals[113]=65535;run(c);assert.equal(c.P3_Position.axis1Position.txt,'轴1原始位置：-1 count');});
test('S32完整范围不套用基础区哨兵',()=>{const c=context(3);c.rawRegs.vals[140]=1;c.rawRegs.vals[112]=32767;c.rawRegs.vals[113]=65535;run(c);assert.match(c.P3_Position.axis1Position.txt,/2147483647/);c.rawRegs.vals[112]=32768;c.rawRegs.vals[113]=0;run(c);assert.match(c.P3_Position.axis1Position.txt,/-2147483648/);});
test('数据不完整不显示旧里程',()=>{const c=context(3);c.rawRegs.vals[124]=7;c.rawRegs.vals[127]=1;run(c);assert.match(c.P3_Position.startupTravel.txt,/不完整/);assert.match(c.P3_Position.mileageValid.txt,/不完整/);});
test('累计行程越界不遮蔽合法净位移',()=>{const c=context(3);c.rawRegs.vals[124]=3;c.rawRegs.vals[117]=123;c.rawRegs.vals[118]=32768;run(c);assert.match(c.P3_Position.startupDisplacement.txt,/123 mm/);assert.match(c.P3_Position.startupTravel.txt,/越界/);});
for (const owner of [0,1,2,3,4]) test('模式'+owner+'只读诊断持续显示',()=>{const c=context(6);c.rawRegs.vals[203]=owner;c.rawRegs.vals[128]=0;c.rawRegs.vals[130]=1;c.rawRegs.vals[132]=65534;c.rawRegs.vals[136]=25;run(c);assert.equal(c.P6_Sync.syncActualDiff.txt,'实际速度差：-2 rpm');assert.equal(c.P6_Sync.syncAxis1Age.txt,'轴1样本年龄：25 ms');assert.match(c.P6_Sync.syncCompensation.txt,/未执行/);});
test('遥控激活显示真实补偿',()=>{const c=context(6);c.rawRegs.vals[203]=2;c.rawRegs.vals[128]=3;c.rawRegs.vals[130]=1;c.rawRegs.vals[134]=65535;run(c);assert.equal(c.P6_Sync.syncCompensation.txt,'当前整数补偿：-1 rpm');});
test('非遥控不沿用补偿',()=>{const c=context(6);c.rawRegs.vals[203]=4;c.rawRegs.vals[128]=3;c.rawRegs.vals[130]=1;c.rawRegs.vals[134]=5;run(c);assert.match(c.P6_Sync.syncCompensation.txt,/未执行/);});
test('没有配对也能显示未执行',()=>{const c=context(6);c.rawRegs.vals[128]=1;c.rawRegs.vals[130]=0;run(c);assert.match(c.P6_Sync.syncCompensation.txt,/未执行/);assert.match(c.P6_Sync.syncActualDiff.txt,/无效/);});
test('同步无效快照不展示数值',()=>{const c=context(6);c.rawRegs.vals[142]=0;c.rawRegs.vals[130]=1;c.rawRegs.vals[132]=99;run(c);assert.match(c.P6_Sync.syncState.txt,/无效/);assert.doesNotMatch(c.P6_Sync.syncActualDiff.txt,/99/);});
test('告警无效不能显示无告警',()=>{const c=context(5);c.rawRegs.vals[157]=0;c.rawRegs.vals[145]=0;run(c);assert.equal(c.P5_Alarm.alarmLevel.txt,'告警快照无效');});
test('有效无告警快照',()=>{const c=context(5);c.rawRegs.vals[157]=1;run(c);assert.equal(c.P5_Alarm.alarmLevel.txt,'告警等级：无');});
for (const [page,field] of [[3,'axis1Position'],[6,'syncActualDiff'],[5,'alarmCode']]) test('区域过期页'+page,()=>{const c=context(page);const region=page===5?3:2;c.regionAge.vals[region]=9;run(c);assert.match(c[names[page]][field].txt,/已过期/);assert.equal(c.regionValid.vals[0],1);});
for (const page of [3,5,6]) test('能力未配置页'+page,()=>{const c=context(page);c.rawRegs.vals[194]=0;run(c);const labels=Object.values(c[names[page]]).filter(v=>v && typeof v==='object' && 'txt' in v);assert(labels.length>0);if(page===5){assert(labels.filter(v=>v.txt.includes('告警记录')).every(v=>v.txt.includes('历史未接入')));assert(labels.filter(v=>!v.txt.includes('告警记录')).every(v=>v.txt.includes('未配置')));}else{assert(labels.every(v=>v.txt.includes('未配置')));}});
test('未接入扩展功能不是故障',()=>{const c=context(7);c.rawRegs.vals[194]=0;run(c);assert.match(c.P7_System_Ext.batteryState.txt,/未配置/);assert.match(c.P7_System_Ext.cameraState.txt,/未配置/);});
test('CLI源和屏幕只读锁定相互独立',()=>{const c=context(1);c.rawRegs.vals[0]=3;c.rawRegs.vals[203]=4;c.rawRegs.vals[21]=0;c.rawRegs.vals[23]=1;run(c);assert.equal(c.P1_Main.controlSource.txt,'控制源：CLI维护');assert.equal(c.P1_Main.state.txt,'RUNNING');assert.match(c.P1_Main.controlAuthority.txt,/只读/);});
test('初始化完成与运行状态分开',()=>{const c=context(0);c.rawRegs.vals[0]=3;c.rawRegs.vals[1]=6;c.rawRegs.vals[2]=100;c.rawRegs.vals[15]=1;run(c);assert.equal(c.mainState.txt,'RUNNING');assert.equal(c.stageText.txt,'初始化完成');});
test('初始化无效不能冒充完成',()=>{const c=context(0);c.rawRegs.vals[28]=0;c.rawRegs.vals[1]=6;c.rawRegs.vals[2]=100;c.rawRegs.vals[15]=1;run(c);assert.equal(c.stageText.txt,'系统快照无效');});
test('协议不匹配拒绝基础数据',()=>{const c=context(1);c.rawRegs.vals[192]=0x5349;run(c);assert.notEqual(c.P1_Main.ctrlComm.txt,'控制器通信：正常');});
test('元数据暂时过期不遮蔽已确认基础监控',()=>{const c=context(1);c.metaSeen.vals[0]=1;c.metaRegs.vals[0]=0x5249;c.metaRegs.vals[1]=1;c.metaAge.vals[0]=10;c.regionValid.vals[4]=0;c.regionAge.vals[4]=10;run(c);assert.equal(c.P1_Main.ctrlComm.txt,'控制器通信：正常');});
test('明确不兼容元数据否决旧身份',()=>{const c=context(1);c.metaSeen.vals[0]=1;c.metaRegs.vals[0]=0x5349;run(c);assert.notEqual(c.P1_Main.ctrlComm.txt,'控制器通信：正常');});
test('无伺服反馈时通信与伺服状态分离',()=>{const c=context(1);c.rawRegs.vals[28]=0;run(c);assert.equal(c.P1_Main.ctrlComm.txt,'控制器通信：主控已回复，伺服状态无效');});
test('重试与最小栈未知哨兵',()=>{const c=context(2);c.rawRegs.vals[82]=65535;run(c);assert.match(c.P2_Diag.retryCount.txt,/未知/);c.sys_pid=4;c.rawRegs.vals[95]=65535;run(c);assert.match(c.P4_Info.minStack.txt,/未知/);});
test('正式源没有测试注入和写请求',()=>{assert.doesNotMatch(source,/modbusBufferSend|moubusBufferSend|writeRegister|sys_run_time|stageText\.txt="T="/i);});
console.log('PASS '+passed+' 项等价脚本回归；不是Visual-PIX运行或实物联调证据');
