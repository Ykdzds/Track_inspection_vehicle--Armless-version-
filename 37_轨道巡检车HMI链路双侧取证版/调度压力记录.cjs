'use strict';
const {fixture}=require('./hmi13.test.cjs');const {bytesToWords}=require('./r2-protocol.cjs');const fs=require('node:fs');const path=require('node:path');
const run=fixture();run.authorize();const start=run.calls.length;run.tick(2400);
const calls=run.calls.slice(start);const targets=calls.filter(call=>call.kind===2&&bytesToWords(call.request.subarray(7,-2))[8]===33);
const states=calls.filter(call=>call.kind===1&&call.request.readUInt16BE(2)===1120);
const maxGap=items=>Math.max(...items.slice(1).map((item,index)=>item.at-items[index].at));
const report={evidence:'50ms理想事件等价执行；每事务50ms内模拟应答；不是Visual-PIX实屏或UART抓帧',durationMs:120000,targetFrames:targets.length,targetMaxGapMs:maxGap(targets),statusReads:states.length,statusMaxGapMs:maxGap(states),maximumInflight:run.maximumInflight(),state:run.context.h13.vals[2],physicalValidated:false};
fs.writeFileSync(path.join(__dirname,'运行核验','调度压力记录.json'),JSON.stringify(report,null,2));console.log(report);
if(report.targetMaxGapMs>200||report.statusMaxGapMs>150||report.maximumInflight!==1||report.state!==3)throw Error('调度不合格');
