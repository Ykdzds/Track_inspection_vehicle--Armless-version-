'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const {fixture}=require('./hmi13.test.cjs');
const {bytesToWords}=require('./r2-protocol.cjs');
const root=__dirname,evidence=path.join(root,'..','30_轨道巡检车全界面科技风优化版','运行核验/30号实施_20261101'),base=path.join(root,'..','30_轨道巡检车全界面科技风优化版');
const json=f=>JSON.parse(fs.readFileSync(path.join(evidence,f),'utf8').replace(/^\uFEFF/,''));
const k=Math.PI*.185/(60*(8/3));
function context(page=8){
 const c={sys_pid:page,intToString:String,floatToString:(v,n)=>v.toFixed(n),showPage:p=>{c.sys_pid=p;}};
 for(const [i,name] of ['P0_Boot','P1_Main','P2_Diag','P3_Position','P4_Info','P5_Alarm','P6_Sync','P7_System_Ext','P8_ServoControl'].entries())c[name]=new Proxy({id:i},{get:(o,k)=>o[k]||(o[k]={})});
 for(const n of ['h13','h13Status','h13Result','h13Tx','metaRegs','rawRegs','regionSeen','regionAge','regionValid','regionGoodRun','regionBadRun','regionFails','metaSeen','metaAge','metaValid','metaBadRun','bootState']){c[n]={vals:Array(1024).fill(0)};c.P0_Boot[n]=c[n];c.P8_ServoControl[n]=c[n];}
 for(const name of ['mainState','ctrlStatus','stageText','progressText','bootProgress','leftStatus','rightStatus','safeStatus','resetReason','swVer','protoVer','faultText'])c[name]={};
 require('./ui31-vm.cjs').initialize(c);return vm.createContext(c);
}
const script=require('./ui31-vm.cjs').compile(fs.readFileSync(path.join(base,'全局计龄刷新.txt'),'utf8').replace(/^int (\w+);$/gm,'var $1;'));
const display=require('./ui31-vm.cjs').compile(fs.readFileSync(path.join(root,'HMI13界面刷新.txt'),'utf8').replace(/^int (\w+);$/gm,'var $1;'));
function fresh(c,rpm=100){
 const h=c.h13.vals,s=c.h13Status.vals;h[86]=1;h[1]=1000;h[21]=h[73]=1000;h[10]=rpm;
 s[9]=3;s[25]=s[26]=s[27]=s[57]=3;s[8]=1;
 for(const i of [18,19,20,21,22,23,24])s[i]=rpm<0?65536+rpm:rpm;
 s[46]=s[47]=0;
}
for(const rpm of [0,1,-1,100,-100,1600,-1600])test('30 rpm三单位同源 '+rpm,()=>{
 const c=context();fresh(c,rpm);display.runInContext(c);
 const m=(rpm*k).toFixed(3),km=(rpm*k*3.6).toFixed(3);
 for(const side of ['left','right']){
  assert.equal(c.P8_ServoControl[side+'Actual'].txt,(side==='left'?'左':'右')+'轴实际：'+rpm+'rpm|'+m+'m/s|'+km+'km/h');
  assert.equal(c.P8_ServoControl[side+'ActualUnits'].txt,'');
  for(const f of ['Target','Output'])assert.ok(c.P8_ServoControl[side+f].txt.endsWith(rpm+'rpm|'+m+'m/s|'+km+'km/h'));
 }
 assert.equal(c.P8_ServoControl.commonTarget.txt,'设定：'+rpm+'rpm|'+m+'m/s|'+km+'km/h');
 assert.equal(c.P8_ServoControl.acceptedTarget.txt,'主控接受：'+rpm+'rpm|'+m+'m/s|'+km+'km/h');
 assert.doesNotMatch(JSON.stringify(c.P8_ServoControl),/-0\.000|mm\/s/);
});
for(const mode of ['snapshot','heartbeat','age','sentinel','bit','axis'])test('30 实际三单位同步失效 '+mode,()=>{
 const c=context();fresh(c);display.runInContext(c);const h=c.h13.vals,s=c.h13Status.vals;
 if(mode==='snapshot')h[86]=0;if(mode==='heartbeat')h[21]=0;if(mode==='age')s[46]=500;if(mode==='sentinel')s[23]=32767;if(mode==='bit')s[25]=2;if(mode==='axis')s[9]=2;
 display.runInContext(c);assert.equal(c.P8_ServoControl.leftActualUnits.txt,'');assert.doesNotMatch(c.P8_ServoControl.leftActual.txt,/\d+rpm/);assert.doesNotMatch(c.P8_ServoControl.leftActual.txt,/100 rpm/);
});
for(const owner of [0,2,3,4])for(const value of [0,1,-1,100,-100,32766,-32768])test('30 线速度源三单位 '+owner+'/'+value,()=>{
 const c=context();fresh(c);const s=c.h13Status.vals;s[8]=owner;s[59]=1;s[58]=s[60]=s[61]=value<0?65536+value:value;
 display.runInContext(c);const linear=(value*.001).toFixed(3)+'m/s|'+(value*.0036).toFixed(3)+'km/h';
 for(const f of ['acceptedTarget','leftTarget','rightTarget','leftOutput','rightOutput']){assert.ok(c.P8_ServoControl[f].txt.endsWith(linear),f);assert.ok(c.P8_ServoControl[f].txt.includes((value*.001/k).toFixed(1)+'rpm'));assert.doesNotMatch(c.P8_ServoControl[f].txt,/-0\.000|mm\/s/);}
});
test('30 原控件和非展示属性、原事件及页面跳转完全保留',()=>{
 const a=json('29号完整模型基线.json'),b=json('30号实际工程读回.json');
 assert.deepEqual(a.Config,b.Config);assert.equal(b.Pages.length,9);
 const allowed=new Set(['MaxLength','MaxTextLength','Text','TextDisplay','FontId','FontIdDisplay','FontColor','BackgroundColor','BackgroundType','ImageId1','ImageId2','DisableBackgroundImageId','WordWrap','X','Y','Width','Height','ForegroundColor','ThumbColor','ThumbWidth']);
 for(const [i,p] of a.Pages.entries()){const q=b.Pages[i];assert.equal(q.Id,p.Id);assert.equal(q.Name,p.Name);assert.deepEqual(q.Events,p.Events);
  for(const c of p.Controls){const d=q.Controls.find(t=>t.Id===c.Id);assert.equal(d.Name,c.Name);assert.equal(d.Type,c.Type);
   for(const [k,v] of Object.entries(c.Properties))if(!allowed.has(k))assert.deepEqual(d.Properties[k],v,p.Name+'.'+c.Name+'.'+k);
   if(c.Name==='refreshTick')assert.equal(d.Events[0].Code.replace(/\r/g,''),fs.readFileSync(path.join(base,'全局计龄刷新.txt'),'utf8').replace(/\r/g,''));
   else assert.deepEqual(d.Events,c.Events,p.Name+'.'+c.Name);
  }
  for(const c of q.Controls.slice(p.Controls.length))assert.ok(c.Events.every(e=>!e.Code));
 }
});
test('31 相同模拟工况下30/31控制数组和逐字节发送帧相同',()=>{
 const runs=[fixture({globalDisplay:true,fireProgramEvents:true,presentationRoot:base}),fixture({globalDisplay:true,fireProgramEvents:true})];
 for(const r of runs){r.authorize();for(const rpm of [100,-100,0,1600,-1600]){r.context.h13.vals[10]=rpm;r.tick(40);for(let p=0;p<9;p++){r.navigate(p);r.tick(10);}}r.intent(3);r.tick(100);r.setOffline(true);r.tick(50);r.setOffline(false);r.tick(80);}
 assert.deepEqual(runs[0].calls.map(c=>[c.at,c.kind,c.request.toString('hex')]),runs[1].calls.map(c=>[c.at,c.kind,c.request.toString('hex')]));
 for(const n of ['h13','h13Status','h13Tx','h13Result'])assert.deepEqual(runs[0].context[n].vals,runs[1].context[n].vals,n);
 assert.equal(runs[1].maximumInflight(),1);
});
module.exports={context,script,display,fresh};
