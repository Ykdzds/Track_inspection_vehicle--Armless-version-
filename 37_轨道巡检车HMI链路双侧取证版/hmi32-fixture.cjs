'use strict';
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const {initialize,compile,model}=require('./ui31-vm.cjs');
const spec=JSON.parse(fs.readFileSync(path.join(__dirname,'运行核验/32号实施_20261102/32号显示字段索引.json'),'utf8'));
const script=compile(fs.readFileSync(path.join(__dirname,'全局计龄刷新.txt'),'utf8').replace(/^\uFEFF/,''));
const array=n=>({vals:Array(n).fill(0),valbs:Array(n).fill(0)});
function attach(c){
 for(const p of ['P2_Diag','P3_Position','P4_Info','P5_Alarm','P6_Sync','P7_System_Ext'])c[p].dataQuality={};
 for(const [name,n] of Object.entries({ui32State:96,ui32Source:9,ui32SourceAt:9,ui32Field:spec.count*5,ui32Candidate:spec.count,ui32CandidateSource:spec.count,ui32Color:spec.count,ui32Font:spec.count,ui32WorkColor:spec.count,ui32WorkFont:spec.count,ui32Debounce:spec.count,ui32DebounceAt:spec.count}))c.P0_Boot[name]=array(n);
 const texts=Array(spec.count*3).fill('');c.P0_Boot.ui32Text={get:i=>texts[i],set:(i,v)=>{texts[i]=String(v);}};
 return texts;
}
function fixture(page=8){
 const c={sys_pid:page,intToString:String,floatToString:(v,n)=>v.toFixed(n),showPage:p=>{c.sys_pid=p;}};
 initialize(c);
 for(const p of model.Pages)for(const o of p.Controls)if(o.Type.endsWith('TextCtrl')){c[p.Name][o.Name].font=spec.fields.find(f=>f.key===p.Name+'.'+o.Name)?.font||1;}
 for(const [name,n] of Object.entries({h13:180,h13Status:96,h13Result:20,h13Tx:48}))c[name]=c.P8_ServoControl[name]=array(n);
 for(const [name,n] of Object.entries({rawRegs:208,metaRegs:24,regionSeen:5,regionAge:5,regionValid:5,regionGoodRun:5,regionBadRun:5,regionFails:5,metaSeen:1,metaAge:1,metaValid:1,metaBadRun:1,bootState:5}))c[name]=c.P0_Boot[name]=array(n);
 const texts=attach(c);
 for(const n of ['mainState','ctrlStatus','stageText','progressText','bootProgress','leftStatus','rightStatus','safeStatus','resetReason','swVer','protoVer','faultText'])c[n]=c.P0_Boot[n];
 vm.createContext(c);
 const h=c.h13.vals,s=c.h13Status.vals,r=c.rawRegs.vals;
 h[0]=1;h[1]=1000;h[89]=50;h[74]=h[47]=1;h[4]=0x1234;h[5]=0x5678;
 c.metaSeen.vals[0]=1;c.metaRegs.vals.splice(0,6,21065,1,255,2,h[4],h[5]);
 s[0]=21325;s[1]=2;s[2]=h[4];s[3]=h[5];s[5]=1;
 h[86]=1;h[21]=h[73]=h[22]=h[107]=h[1];h[104]=1;h[3]=1;
 for(let i=0;i<5;i++){c.regionSeen.vals[i]=c.regionValid.vals[i]=1;c.regionGoodRun.vals[i]=2;}
 r[0]=3;r[1]=6;r[2]=100;r[15]=1;r[8]=r[9]=1;r[3]=512;r[28]=1;r[34]=1525;r[44]=1529;r[52]=1527;r[40]=r[50]=0;r[41]=r[51]=1;
 r[192]=21065;r[193]=1;r[194]=0x1fff;r[142]=r[157]=r[172]=r[179]=r[206]=r[207]=1;r[202]=1529;r[203]=1;
 r[124]=3;r[127]=1;r[126]=10;r[140]=r[141]=1;r[113]=12345;r[115]=23456;r[117]=4000;r[119]=5000;r[121]=6000;r[123]=10;r[125]=20;
 r[128]=3;r[130]=1;r[132]=2;r[133]=1;r[134]=1;r[136]=r[137]=20;
 s[8]=1;s[9]=3;s[12]=1;s[13]=2;s[18]=421;s[19]=s[20]=421;s[21]=s[22]=421;s[23]=419;s[24]=420;s[25]=s[26]=s[27]=s[28]=s[57]=3;s[59]=1;s[58]=1529;s[60]=s[61]=1529;
 s[64]=s[76]=15;s[66]=139;s[67]=129;s[68]=64;s[69]=490;s[78]=117;s[79]=117;s[80]=58;s[81]=485;
 const writes=[];
 for(const f of spec.fields){const [pn,cn]=f.key.split('.');const ob=c[pn][cn];let txt=ob.txt;Object.defineProperty(ob,'txt',{get:()=>txt,set:v=>{writes.push({at:h[1],key:f.key,text:v});txt=v;},configurable:true});}
 function render(){script.runInContext(c);}
 function tick(ms=50){h[1]+=ms;render();}
 function receive(regions=[0,1,2,3,4],status=true){
  for(const i of regions){h[109+i]++;c.regionAge.vals[i]=0;c.regionValid.vals[i]=c.regionSeen.vals[i]=1;c.regionGoodRun.vals[i]=3;c.regionBadRun.vals[i]=0;}
  if(status){s[5]++;h[21]=h[73]=h[1];h[86]=h[3]=1;}h[104]++;h[107]=h[1];h[22]=h[1];render();
 }
 render();tick();receive();
 return {c,h,s,r,render,tick,receive,writes,spec,texts,field:key=>spec.fields.find(x=>x.key===key),value:key=>{const [p,n]=key.split('.');return c[p][n].txt;}};
}
module.exports={fixture,spec,script,attach};
