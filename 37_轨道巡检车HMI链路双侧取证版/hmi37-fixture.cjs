'use strict';
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const protocol=require('./r2-protocol.cjs'),{Simulator}=require('./r2-simulator.cjs');
const root=process.env.HMI34_BASELINE||__dirname;
const version='37';
const evidence='运行核验/37号实施_20261107';
const model=JSON.parse(fs.readFileSync(path.join(root,evidence,version+'号实际工程读回.json'),'utf8').replace(/^\uFEFF/,''));
const resources=require('./ui31-vm.cjs').resources;
function bounded(n,fill=0){return new Proxy(Array(n).fill(fill),{get(a,k){if(/^\d+$/.test(String(k)))assert.ok(+k<n,`read ${k}/${n}`);return a[k];},set(a,k,v){if(/^\d+$/.test(String(k)))assert.ok(+k<n,`write ${k}/${n}`);a[k]=v;return true;}});}
function compile(s){return new vm.Script(s.replace(/^\uFEFF/,'').replace(/\bint (\w+);/g,'var $1;').replace(/\(byte\)/g,'').replace('hv34*944/3200','Math.trunc(hv34*944/3200)').replace('(hm34*768794+5000000)/10000000','Math.trunc((hm34*768794+5000000)/10000000)').replace('hw34=hm34/1000','hw34=Math.trunc(hm34/1000)').replace(/user\.(image|font)\.[A-Za-z0-9_]+/g,k=>resources[k]));}
function fixture(options={}){
 const sim=new Simulator(options),calls=[],c={sys_pid:8,sys_run_time:0,intToString:v=>String(Math.trunc(v)),floatToString:(v,n)=>v.toFixed(n),bytesToShort:(b,i)=>{assert.ok(i>=0&&i+1<b.length);return b[i]*256+b[i+1];}};
 let now=0,lastStart=0,offline=false,drop=0,diagDrop=0,maxInflight=0,inflight=0,hook=options.hook,crcBad=false,freeze=false,frozen=null;
 const advance=ms=>{now+=ms;sim.tick(ms);c.sys_run_time=Math.floor(now/1000);};
 for(const p of model.Pages){c[p.Name]={id:p.Id};for(const ctl of p.Controls){const a=ctl.Properties;c[p.Name][ctl.Name]={txt:a.Text||'',val:a.Int||a.Value||0,x:a.X,y:a.Y,font:model.Fonts.findIndex(f=>f.Id===a.FontId)+1,fontColor:a.FontColor,enable:a.Enable};if(ctl.Type.endsWith('ArrayCtrl')){c[p.Name][ctl.Name]={vals:bounded(a.Length,ctl.Name==='dRaw'||ctl.Name==='dStatus'?65535:0),valbs:bounded(a.Length)};if(ctl.Name==='dStatus')for(let n=18;n<=24;n++)c[p.Name][ctl.Name].vals[n]=32767;}}}
 for(const [k,v]of Object.entries(c.P0_Boot))c[k]=v;
 const h=c.P8_ServoControl.h13.vals,q=c.P0_Boot.s33.vals,u=c.P0_Boot.u34?.vals;
 const run=s=>compile(s||'').runInContext(c);
 function controlEvent(name,id){const ctl=model.Pages[c.sys_pid].Controls.find(x=>x.Name===name);assert.ok(ctl,`${name} page ${c.sys_pid}`);run(ctl.Events.find(x=>x.Id===id)?.Code);advance(options.touchMs??1);}
 c.showPage=id=>{run(model.Pages[c.sys_pid].Events.find(e=>e.Id===22)?.Code);c.sys_pid=id;advance(options.pageMs??2);if(options.resetSliderOnEntry&&id===8)c.P8_ServoControl.rpmSlider.val=1600;run(model.Pages[id].Events.find(e=>e.Id===21)?.Code);};
 function transport(request,kind){assert.equal(++inflight,1);maxInflight=Math.max(maxInflight,inflight);calls.push({at:now,kind,request:Buffer.from(request)});advance(offline?50:options.responseMs??10);let reply=offline?null:request.readUInt16BE(2)===1792?protocol.frame([1,16,7,0,0,24]):sim.handle(request);if(hook)hook({kind,request,c,h,q,u,sim,advance});if(kind==='diag'&&diagDrop>0){diagDrop--;reply=null;}if(kind==='write'&&request[24]===16&&drop>0){drop--;reply=null;}if(reply&&crcBad){reply=Buffer.from(reply);reply[reply.length-1]^=1;}const m=c.P0_Boot.mod;Object.assign(m,{addr:0,fcode:0,rxLen:0,rxBuf:[]});if(reply&&protocol.validFrame(reply)){Object.assign(m,{addr:reply[0],fcode:reply[1],rxLen:reply.length-4,rxBuf:[...reply.subarray(2,-2)]});if(freeze&&kind==='read'&&request.readUInt16BE(2)===1120){if(!frozen)frozen=[...m.rxBuf.slice(1,129)];for(let i=0;i<128;i++)m.rxBuf[i+1]=frozen[i];}if(options.transform)options.transform(m,request,c);}inflight--;return reply&&protocol.validFrame(reply)?0:-1;}
 c.modbusRead=(a,f,start,count,timeout)=>{assert.deepEqual([a,f,timeout],[1,3,50]);return transport(protocol.readFrame(start,count),'read');};
 c.modbusWrites=(a,f,start,count,b,len,timeout)=>{assert.deepEqual([a,f,count,len,timeout],[1,16,24,48,50]);assert.ok(start===256||start===1792);const words=protocol.bytesToWords(Buffer.from(b).subarray(0,len));const frame=start===256?protocol.writeFrame(words):protocol.frame(Buffer.concat([Buffer.from([1,16,7,0,0,24,48]),protocol.wordsToBytes(words)]));return transport(frame,start===256?'write':'diag');};
 vm.createContext(c);run(fs.readFileSync(path.join(root,`HMI${version}自定义函数.txt`),'utf8').replace(/void (HMI\w+)\(\)/g,'function $1()'));
 let slider=1600;Object.defineProperty(c.P8_ServoControl.rpmSlider,'val',{get:()=>slider,set:v=>{if(v===slider)return;slider=v;if(options.programEvents){const e=model.Pages[8].Controls.find(x=>x.Name==='rpmSlider').Events.find(x=>x.Id===5);run(e.Code);}}});
 const service=compile(fs.readFileSync(path.join(root,`HMI${version}事务服务.txt`),'utf8')),display=compile(fs.readFileSync(path.join(root,`HMI${version}显示刷新.txt`),'utf8'));
 run(model.Pages[8].Events.find(e=>e.Id===21)?.Code);
 function input(){if(c.HMI34Input)c.HMI34Input();}
 function tick(n=1){for(let i=0;i<n;i++){const at=Math.max(lastStart+50,now);advance(at-now);lastStart=at;input();service.runInContext(c);input();if(options.render!==false){display.runInContext(c);advance(options.renderMs??4);}}}
 const event=name=>run(fs.readFileSync(path.join(root,'事件',name+'.txt'),'utf8'));
 function authorize(){tick(15);assert.ok(h[2]===1||h[2]===5,'ready '+h[2]);event('授权_按下');tick(10);assert.equal(h[2],3,'authorized '+h[2]);}
 function slide(v,callback=true){slider=v;if(callback)event('滑块_滑动');}
 return{c,h,q,u,sim,calls,tick,input,slide,event,controlEvent,advance,authorize,now:()=>now,dropStopAck:n=>drop=n,dropDiagAck:n=>diagDrop=n,setOffline:v=>offline=v,setCrcBad:v=>crcBad=v,setFreeze:v=>freeze=v,setHook:fn=>hook=fn,maxInflight:()=>maxInflight};
}
module.exports={fixture,model,compile};
