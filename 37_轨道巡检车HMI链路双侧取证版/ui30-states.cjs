// Equivalent script execution only; never writes samples into the formal PIX.
'use strict';
const fs=require('node:fs'),path=require('node:path');
const {context,script,fresh}=require('./hmi30-display.test.cjs');
const E=path.join(__dirname,'运行核验/30号实施_20261101'),states=[];
for(const scenario of ['normal','unknown','fault','long'])for(let page=0;page<9;page++){
 const c=context(page),r=c.rawRegs.vals,h=c.h13.vals,s=c.h13Status.vals;
 if(scenario!=='unknown'){
  fresh(c,scenario==='long'?-1600:100);h[47]=h[74]=1;
  c.metaSeen.vals[0]=1;c.metaRegs.vals[0]=21065;c.metaRegs.vals[1]=1;c.metaRegs.vals[2]=255;c.metaRegs.vals[3]=2;
  for(let i=0;i<5;i++){c.regionSeen.vals[i]=c.regionValid.vals[i]=1;c.regionGoodRun.vals[i]=3;}
  r[192]=21065;r[193]=1;r[194]=0x1fff;r[28]=r[142]=r[157]=r[207]=1;r[0]=3;r[1]=6;r[2]=100;r[15]=1;
  r[8]=r[9]=1;r[34]=363;r[35]=363;r[36]=363;r[37]=363;r[21]=0;r[23]=1;r[203]=1;
  r[124]=7;r[140]=r[141]=1;r[130]=1;r[128]=3;r[132]=2;r[134]=1;r[136]=r[137]=25;
  s[59]=1;s[58]=363;s[60]=s[61]=363;s[89]=0;s[88]=1;s[91]=1;
  h[2]=3;
 }
 if(scenario==='fault'){
  h[2]=7;h[30]=14;s[11]=96;s[30]=5;s[37]=1;s[89]=7;s[93]=14;r[0]=7;r[145]=3;r[146]=15;r[147]=2;r[148]=1;r[149]=1;r[150]=5;
 }
 if(scenario==='long'){
  // Exercise signed extrema, sentinel ages, longest counters and diagnostic append.
  for(let i=0;i<r.length;i++)if(![0,1,2,8,9,15,21,23,28,34,35,36,37,124,128,130,140,141,142,157,192,193,194,203,207].includes(i))r[i]=65534;
  r[112]=32768;r[113]=0;r[114]=32767;r[115]=65535;r[116]=32768;r[117]=0;r[124]=7;r[127]=0;
  h[2]=7;h[30]=12;h[161]=65535;h[162]=h[163]=h[164]=h[167]=h[168]=65535;
  s[37]=65535;s[11]=65535;s[30]=5;s[89]=7;s[93]=s[94]=s[95]=65535;
  for(const i of [18,19,20,21,22,23,24])s[i]=32768;
 }
 script.runInContext(c);
 const pages={};
 for(const name of ['P0_Boot','P1_Main','P2_Diag','P3_Position','P4_Info','P5_Alarm','P6_Sync','P7_System_Ext','P8_ServoControl'])pages[name]=Object.fromEntries(Object.entries(c[name]).filter(([k,v])=>v&&typeof v==='object'&&('txt'in v||'fontColor'in v||'enable'in v||'press'in v)).map(([k,v])=>[k,Object.fromEntries(Object.entries(v).filter(([p])=>['txt','fontColor','enable','press','val'].includes(p)))]));
 // P0 local references are unqualified in original script.
 for(const name of ['mainState','ctrlStatus','stageText','progressText','bootProgress','leftStatus','rightStatus','safeStatus','resetReason','swVer','protoVer','faultText'])pages.P0_Boot[name]=c[name];
 states.push({scenario,page,pages});
}
fs.writeFileSync(path.join(E,'等价显示状态.json'),JSON.stringify(states,null,2));
console.log('wrote 36 equivalent script display states; not physical captures');
