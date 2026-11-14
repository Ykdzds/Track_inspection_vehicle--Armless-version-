'use strict';
// Execute read-back event scripts. Native VP verification is recorded separately.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const {initialize,compile,model,resources,E}=require('./ui31-vm.cjs');
const read=f=>fs.readFileSync(path.join(__dirname,f),'utf8').replace(/^\uFEFF/,'');
const base=JSON.parse(read('运行核验/31号实施_20261101/30号完整模型基线.json'));
const normalize=s=>(s||'').replace(/\r/g,'').trim();
const scripts={};for(const p of model.Pages){scripts[p.Name]=compile(p.Events[0].Code);for(const c of p.Controls)for(const ev of c.Events)if(ev.Code)scripts[p.Name+'.'+c.Name+'.'+ev.Id]=compile(ev.Code);}
const checks=[];function check(name,fn){fn();checks.push(name);}
function context(){
 const c={sys_pid:8,intToString:String,floatToString:(v,n)=>v.toFixed(n),showPage:p=>c.sys_pid=p};
 for(const p of model.Pages)c[p.Name]={id:p.Id};
 for(const n of ['h13','h13Status','h13Result','h13Tx','rawRegs','metaRegs','regionSeen','regionAge','regionValid','regionGoodRun','regionBadRun','regionFails','metaSeen','metaAge','metaValid','metaBadRun','bootState']){const o={vals:Array(1024).fill(0),valbs:Array(1024).fill(0)};c[n]=o;c.P0_Boot[n]=o;c.P8_ServoControl[n]=o;}
 for(const n of ['mainState','ctrlStatus','stageText','progressText','bootProgress','leftStatus','rightStatus','safeStatus','resetReason','swVer','protoVer','faultText'])c[n]={};
 initialize(c);return vm.createContext(c);
}
function seed(c,scenario='normal',rpm=540){
 const h=c.h13.vals,s=c.h13Status.vals,r=c.rawRegs.vals;
 if(scenario==='unknown')return;
 h[86]=h[47]=h[74]=1;h[1]=h[21]=h[73]=1000;h[10]=rpm;h[2]=3;h[3]=1;
 s[9]=s[25]=s[26]=s[27]=s[57]=3;s[8]=1;
 for(const i of [18,19,20,21,22,23,24])s[i]=rpm&65535;
 s[24]=538;s[46]=s[47]=0;s[59]=s[88]=s[91]=1;s[58]=s[60]=s[61]=363;
 c.metaSeen.vals[0]=1;Object.assign(c.metaRegs.vals,{0:21065,1:1,2:255,3:2});
 for(let i=0;i<5;i++){c.regionSeen.vals[i]=c.regionValid.vals[i]=1;c.regionGoodRun.vals[i]=3;}
 Object.assign(r,{192:21065,193:1,194:8191,28:1,142:1,157:1,207:1,0:3,1:6,2:100,15:1,8:1,9:1,34:363,35:363,36:363,37:363,23:1,203:1,124:7,140:1,141:1,130:1,128:3,132:2,134:1,136:25,137:25});
 if(scenario==='stale'){h[21]=h[73]=0;c.regionAge.vals.fill(10000);s[46]=s[47]=1000;}
 if(scenario==='disabled'){h[2]=0;h[3]=0;h[116]=1;}
 if(scenario==='fault'||scenario==='long'){h[2]=7;h[30]=14;s[11]=96;s[30]=5;s[37]=1;s[89]=7;s[93]=14;r[0]=7;Object.assign(r,{145:3,146:15,147:2,148:1,149:1,150:5});}
 if(scenario==='long'){
  const keep=[0,1,2,8,9,15,21,23,28,34,35,36,37,124,128,130,140,141,142,157,192,193,194,203,207];
  for(let i=0;i<208;i++)if(!keep.includes(i))r[i]=65534;
  Object.assign(r,{112:32768,113:0,114:32767,115:65535,116:32768,117:0,124:7,127:0});
  h[30]=12;for(const i of [161,162,163,164,167,168])h[i]=65535;
  for(const i of [37,11,93,94,95])s[i]=65535;for(const i of [18,19,20,21,22,23,24])s[i]=32768;
 }
}
const view=compile(read('HMI13界面刷新.txt')),hint=scripts['P8_ServoControl.rpmSlider.5'];
check('31 actual binding and synchronized display sources',()=>{
 assert.equal(normalize(model.Pages[0].Controls.find(x=>x.Name==='refreshTick').Events[0].Code),normalize(read('全局计龄刷新.txt').replace(/user\.(?:font|image)\.[A-Za-z0-9_]+/g,k=>resources[k])));
 assert.equal(normalize(model.Pages[8].Controls.find(x=>x.Name==='rpmSlider').Events[0].Code),normalize(read('事件/滑块_滑动.txt')));
 const g=read('全局计龄刷新.txt'),v=read('HMI13界面刷新.txt');assert.equal(g.slice(g.indexOf('P8_ServoControl.leftTarget.font=')),v.slice(v.indexOf('P8_ServoControl.leftTarget.font=')));
});
check('31 original controls, protocol, safety events and 50ms timers preserved',()=>{
 assert.deepEqual(base.Config,model.Config);
 const visual=new Set(['FontId','FontIdDisplay','Text','TextDisplay','FontColor','BackgroundColor','BackgroundType','ImageId','ImageId1','ImageId2','DisableBackgroundImageId','ForegroundColor','ThumbColor','X','Y','Width','Height','WordWrap','Visbile']);
 for(const [i,p] of base.Pages.entries()){
  const q=model.Pages[i];assert.equal(q.Id,p.Id);assert.equal(q.Name,p.Name);
  for(const c of p.Controls){const d=q.Controls.find(x=>x.Name===c.Name);assert.equal(d.Type,c.Type);assert.equal(d.Id,c.Name==='rpmSlider'?36:c.Id);
   for(const [k,v] of Object.entries(c.Properties))if(!visual.has(k)&&!(c.Name==='rpmSlider'&&k==='Id'))assert.deepEqual(d.Properties[k],v,p.Name+'.'+c.Name+'.'+k);
   if(!['refreshTick','rpmSlider'].includes(c.Name))assert.deepEqual(d.Events,c.Events,p.Name+'.'+c.Name);
  }
  for(const [j,ev] of p.Events.entries())assert.ok(normalize(q.Events[j].Code).includes(normalize(ev.Code)),p.Name+' original page event');
 }
 assert.equal(model.Pages[0].Controls.find(c=>c.Name==='refreshTick').Properties.Interval,5);
 assert.equal(model.Pages[8].Controls.find(c=>c.Name==='h13Service').Properties.Interval,5);
 const old=normalize(base.Pages[8].Controls.find(c=>c.Name==='rpmSlider').Events[0].Code);assert.ok(normalize(model.Pages[8].Controls.find(c=>c.Name==='rpmSlider').Events[0].Code).includes(old));
});
check('31 toggle only on release, preserves theme over all nine pages and reboot resets day',()=>{
 const c=context(),toggle=model.Pages[1].Controls.find(x=>x.Name==='themeToggle');assert.equal(toggle.Events[0].Code||'','');
 for(let i=0;i<20;i++){scripts['P1_Main.themeToggle.2'].runInContext(c);assert.equal(c.P0_Boot.themeMode.val,(i+1)%2);for(let p=0;p<9;p++){c.sys_pid=p;scripts[model.Pages[p].Name].runInContext(c);assert.equal(c.P0_Boot.themeMode.val,(i+1)%2);assert.equal(c[model.Pages[p].Name].bgImg,resources['user.image.'+((i+1)%2?'Night':'Day')+'_'+model.Pages[p].Name]);}}
 assert.equal(context().P0_Boot.themeMode.val,0);
});
let values=0;check('31 all 3201 values in allowed and protected conditions, direction, 12px spacing, bounds and read-only state',()=>{
 for(const allowed of [false,true]){const c=context();seed(c);c.h13.vals[2]=allowed?3:0;
  for(let val=0;val<=3200;val++){const before=JSON.stringify(c.h13.vals);c.P8_ServoControl.rpmSlider.val=val;hint.runInContext(c);const z=c.P8_ServoControl.sliderSpeedHint,rpm=val-1600,thumb=24+Math.floor(val*944/3200);assert.equal(z.x,rpm>0?thumb-204:thumb+44);assert.equal(z.txtHorAgn,rpm>0?2:0);assert.equal(z.txt,(allowed?'设定：':'预览：')+(rpm*Math.PI*.185/(60*(8/3))*3.6).toFixed(3)+'km/h');assert.ok(z.x>=24&&z.x+192<=1000);if(allowed)assert.equal(c.h13.vals[10],rpm);else assert.equal(JSON.stringify(c.h13.vals),before);values++;}
 }
});
check('31 every original guard blocks motion and reports preview',()=>{for(const [index,value] of [[2,0],[3,0],[32,1],[138,1],[116,1]]){const c=context();seed(c);c.h13.vals[index]=value;c.h13.vals[10]=123;c.P8_ServoControl.rpmSlider.val=2140;hint.runInContext(c);assert.equal(c.h13.vals[10],123);assert.match(c.P8_ServoControl.sliderSpeedHint.txt,/^预览：/);}});
const speedRecords=[];check('31 signed extrema, sentinels, separate left/right values and source-dependent fonts',()=>{
 for(const rpm of [0,1,-1,100,-100,540,-540,1600,-1600,32766,-32768]){const c=context();seed(c,'normal',rpm);view.runInContext(c);const left=c.P8_ServoControl.leftActual.txt;assert.equal(left,'左轴实际：'+rpm+'rpm|'+(rpm*.003632466505713198).toFixed(3)+'m/s|'+(rpm*.013076879420567515).toFixed(3)+'km/h');assert.match(c.P8_ServoControl.rightActual.txt,/538rpm/);for(const name of ['leftActual','rightActual','leftTarget','rightTarget','leftOutput','rightOutput','commonTarget','acceptedTarget']){assert.doesNotMatch(c.P8_ServoControl[name].txt,/[\r\n]|\.\.\.|…/);speedRecords.push({rpm,name,...c.P8_ServoControl[name]});}}
 for(const bad of ['sentinel','age','axis','valid']){const c=context();seed(c);if(bad==='sentinel')c.h13Status.vals[23]=32767;if(bad==='age')c.h13Status.vals[46]=500;if(bad==='axis')c.h13Status.vals[9]=2;if(bad==='valid')c.h13Status.vals[25]=2;view.runInContext(c);assert.doesNotMatch(c.P8_ServoControl.leftActual.txt,/\d+rpm/);assert.match(c.P8_ServoControl.rightActual.txt,/538rpm/);}
 for(const value of [0,1,-1,100,-100,32766,-32768]){const c=context();seed(c);c.h13Status.vals[8]=3;for(const i of [58,60,61])c.h13Status.vals[i]=value&65535;view.runInContext(c);for(const name of ['leftTarget','rightTarget','leftOutput','rightOutput','acceptedTarget']){assert.equal(c.P8_ServoControl[name].font,resources['user.font.UI31_20']);speedRecords.push({linear:value,name,...c.P8_ServoControl[name]});}}
});
const cases=[];for(const theme of [0,1])for(const scenario of ['normal','unknown','stale','fault','disabled','long'])for(let page=0;page<9;page++){
 const c=context();seed(c,scenario);c.sys_pid=page;c.P0_Boot.themeMode.val=theme;
 const arrays={};for(const n of ['h13','h13Status','h13Result','h13Tx','rawRegs','metaRegs','regionSeen','regionAge','regionValid','regionGoodRun','regionBadRun','regionFails','metaSeen','metaAge','metaValid','metaBadRun','bootState'])arrays[n]=Array.from(c[n].vals);
 cases.push({theme,scenario,page,arrays});
}
fs.writeFileSync(path.join(E,'31号显示工况输入.json'),JSON.stringify(cases));fs.writeFileSync(path.join(E,'31号速度边界.json'),JSON.stringify(speedRecords,null,2));
fs.writeFileSync(path.join(E,'31号独立回归.json'),JSON.stringify({pass:checks.length,fail:0,sliderEvaluations:values,checks,native:false},null,2));
console.log(JSON.stringify({pass:checks.length,fail:0,sliderEvaluations:values,cases:cases.length}));
