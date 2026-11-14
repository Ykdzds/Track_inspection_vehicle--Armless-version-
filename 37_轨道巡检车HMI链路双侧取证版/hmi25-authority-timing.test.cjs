const {test}=require('node:test');
const assert=require('node:assert/strict');
const {fixture}=require('./hmi13.test.cjs');
const protocol=require('./r2-protocol.cjs');
const words=c=>protocol.bytesToWords(c.request.subarray(7,-2));
const isStatus=req=>req[1]===3&&req.readUInt16BE(2)===1120;
function faulty(mode,continuous=false){
 let run,armed=false,remaining=continuous?1000:1;
 run=fixture({transformResponse(mod,req){
  if(!armed||remaining<=0||!isStatus(req))return;remaining--;
  if(mode==='exception'){mod.fcode=0x83;mod.rxLen=1;mod.rxBuf=[2];}
  if(mode==='timeout'){mod.rxLen=0;mod.addr=0;mod.fcode=0;}
  if(mode==='frozen'){
   const h=run.context.h13.vals;
   for(const [offset,value] of [[9,h[70]],[11,h[71]]]){mod.rxBuf[offset]=value>>8;mod.rxBuf[offset+1]=value&255;}
  }
 }});
 return {run,arm(){armed=true;}};
}
for(const mode of ['exception','timeout'])test('25 single status '+mode+' recovers before expiry without release',()=>{
 const f=faulty(mode);const run=f.run;run.authorize();f.arm();const before=run.calls.length;
 for(let i=0;i<80;i++){run.tick();assert.equal(run.context.h13.vals[2],3);}
 assert.equal(run.context.h13.vals[161],0);assert.equal(run.sim.owner,1);
 assert.ok(!run.calls.slice(before).some(c=>c.kind===2&&[3,16].includes(words(c)[8])));
 const targets=run.calls.slice(before).filter(c=>c.kind===2&&words(c)[8]===33);
 assert.ok(Math.max(...targets.slice(1).map((c,i)=>c.at-targets[i].at))<500);
 if(mode==='exception'){assert.equal(run.context.h13.vals[165],131);assert.equal(run.context.h13.vals[166],2);}
});
for(const [mode,cause] of [['exception',1],['timeout',1],['frozen',2]])test('25 continuous '+mode+' revokes at unchanged 300ms gate and retains evidence',()=>{
 const f=faulty(mode,true);const run=f.run;run.authorize();f.arm();let revoked=false;
 for(let i=0;i<50;i++){run.tick();const h=run.context.h13.vals;if(h[161]){assert.equal(h[161],cause);assert.equal(cause===1?h[162]:h[163],300);revoked=true;break;}}
 assert.ok(revoked);const captured=run.context.h13.vals.slice(161,170);run.tick(60);
 assert.deepEqual(run.context.h13.vals.slice(161,170),captured);assert.notEqual(run.context.h13.vals[2],3);
 if(mode==='exception'){assert.equal(captured[6],131);assert.equal(captured[7],2);}
 assert.match(run.context.P8_ServoControl.errorStatus.txt,/D[12]/);
});
test('25 ten minute simulated idle authorization keeps first target zero and supports new movement',()=>{
 const run=fixture({globalDisplay:true});run.authorize();run.tick(12000);assert.equal(run.context.h13.vals[2],3);assert.equal(run.sim.target,0);
 const targets=run.calls.filter(c=>c.kind===2&&words(c)[8]===33);assert.ok(targets.length>2000);
 assert.ok(targets.every(c=>protocol.get32(words(c),10)===0));
 assert.ok(Math.max(...targets.slice(1).map((c,i)=>c.at-targets[i].at))<=200);
 run.context.P8_ServoControl.rpmSlider.val=1700;run.event('事件/滑块_滑动.txt');run.tick(15);assert.equal(run.sim.target,100);
});
test('25 metadata expiry and master revoke are independently diagnosed',()=>{
 for(const cause of [3,4,5]){const run=fixture();run.authorize();
  if(cause===3)run.context.h13.vals[22]=run.context.h13.vals[1]-3000;
  if(cause===4)run.sim.session=0;
  if(cause===5){run.context.metaRegs.vals[5]++;}
  for(let i=0;i<12&&!run.context.h13.vals[161];i++)run.tick();
  assert.equal(run.context.h13.vals[161],cause);
 }
});
