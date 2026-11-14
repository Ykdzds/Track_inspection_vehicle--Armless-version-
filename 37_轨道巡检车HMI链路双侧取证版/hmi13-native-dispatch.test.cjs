'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {fixture}=require('./hmi13.test.cjs');

test('Default不进入Buffer后台派发，不产生R1/R3在途竞争',()=>{
    const run=fixture({globalDisplay:true});
    run.tick(400);
    assert.equal(run.maximumInflight(),1);
    assert.equal(run.context.h13.vals[130],0);
    assert.equal(run.context.h13.vals[140],0);
    assert.equal(run.context.h13.vals[142],0);
    assert.ok(run.context.h13.vals[109]>0);
    assert.equal(run.readOperations.length,0);
});

test('同步超时立即结束事务，不遗留后台读取次数',()=>{
    const run=fixture();run.setOffline(true);
    run.tick(3);
    assert.ok(run.context.h13.vals[139]>0);
    assert.ok(run.readCounts.every(count=>count===0));
    assert.equal(run.context.h13.vals[13],0);
    run.tick(80);
    assert.equal(run.maximumInflight(),1);
    assert.equal(run.context.h13.vals[104],0);
});

test('同步单事务模型保持目标状态及全部旧区刷新预算',()=>{
    const run=fixture({globalDisplay:true});
    run.authorize();const start=run.calls.length;run.tick(1200);
    const calls=run.calls.slice(start);
    for(const filtered of [calls.filter(call=>call.kind===2&&call.request[24]===33),calls.filter(call=>call.kind===1&&call.request.readUInt16BE(2)===1120)]){
        assert.ok(filtered.length>30);
        for(let offset=1;offset<filtered.length;offset++)assert.ok(filtered[offset].at-filtered[offset-1].at<=200);
    }
    for(const address of [0,80,112,144,176,512])assert.ok(calls.some(call=>call.kind===1&&call.request.readUInt16BE(2)===address));
    assert.equal(run.context.h13.vals[3],1);
    assert.equal(run.maximumInflight(),1);
});

test('Default服务源码不再触发Buffer后台读或异步写',()=>{
    const run=fixture();run.tick(80);
    assert.equal(run.readOperations.length,0);
    const source=fs.readFileSync(path.join(__dirname,'HMI13事务服务.txt'),'utf8');
    assert.doesNotMatch(source,/modbusBufferSetReadCount|modbusBufferSends|modbusBufferCheckStatus/);
    assert.match(source,/modbusRead\(1,3,/);assert.match(source,/modbusWrites\(1,16,256,24,/);
});

test('滑块未授权也可见，但拖动不能写入运动目标',()=>{
    const run=fixture({globalDisplay:true});run.tick(40);
    assert.equal(run.context.P8_ServoControl.rpmSlider.visible,1);
    run.context.P8_ServoControl.rpmSlider.val=3200;
    run.event('事件/滑块_滑动.txt');
    assert.equal(run.context.h13.vals[10],0);
    assert.equal(run.calls.some(call=>call.kind===2),false);
    run.authorize();
    assert.equal(run.context.P8_ServoControl.rpmSlider.val,1600);
    run.intent(3);run.tick(2);
    assert.equal(run.context.P8_ServoControl.rpmSlider.visible,1);
    run.context.P8_ServoControl.rpmSlider.val=0;run.event('事件/滑块_滑动.txt');
    assert.equal(run.context.h13.vals[10],0);
});

test('已否决的1ms配置不进入交付，Buffer保持停用默认值',()=>{
    const source=fs.readFileSync(path.join(__dirname,'配置13号Buffer.ps1'),'utf8');
    assert.match(source,/'循环读取间隔时间（ms）'=100;/);
    assert.match(source,/'读取超时时间（ms）'=50;/);
});

for(const [name,change] of [
    ['站号',mod=>{mod.addr=2;}],
    ['功能码',mod=>{mod.fcode=4;}],
    ['短载荷',mod=>{mod.rxLen--;}],
    ['字节数',mod=>{mod.rxBuf[0]--;}],
])test('同步读取'+name+'错误不提交任何缓存',()=>{
    const run=fixture({transformResponse:change});run.tick(40);
    assert.equal(run.context.h13.vals[104],0);
    assert.equal(run.context.h13.vals[3],0);
    assert.ok(run.context.h13.vals[139]>0);
    assert.ok(run.context.rawRegs.vals.every(value=>value===0));
});

test('错误10H回执地址或数量不能标记ACK成功，结果查询仍能恢复',()=>{
    for(const offset of [0,3]){
        const run=fixture({transformResponse:(mod,request)=>{if(request[1]===16&&request[24]===1)mod.rxBuf[offset]^=1;}});
        run.tick(40);run.intent(1);
        assert.equal(run.context.h13.vals[90],0);
        run.tick(30);
        assert.equal(run.context.h13.vals[2],3);
        assert.equal(run.calls.filter(call=>call.kind===2&&call.request[24]===1).length,1);
    }
});

test('Default中断只能计数，不读取旧Buffer字段或改变会话',()=>{
    const run=fixture();run.authorize();const before=JSON.stringify(run.context.h13.vals.slice(0,155));
    run.event('Default模式中断.txt');
    assert.equal(JSON.stringify(run.context.h13.vals.slice(0,155)),before);
    assert.equal(run.context.h13.vals[155],1);
});

test('最新178请求和64完整响应均CRC正确，仍缺元数据和辅助请求',()=>{
    const protocol=require('./r2-protocol.cjs');
    const capture=JSON.parse(fs.readFileSync(path.join(__dirname,'运行核验/现场R3失配抓包_20261017.json'),'utf8'));
    assert.equal(capture.Requests.length,178);assert.equal(capture.Responses.length,64);
    const counts=new Map();
    for(const value of capture.Requests){const frame=Buffer.from(value,'hex');assert.ok(protocol.validFrame(frame));const address=frame.readUInt16BE(2);counts.set(address,(counts.get(address)||0)+1);}
    assert.deepEqual([...counts].sort((left,right)=>left[0]-right[0]),[[0,46],[1120,132]]);
    for(const value of capture.Responses){const frame=Buffer.from(value,'hex');assert.ok(protocol.validFrame(frame));assert.equal(frame.length,133);}
    assert.equal(capture.Responses.filter(value=>value.slice(6,10)==='534D').length,47);
});

test('真实新B0与B7载荷按Default前缀偏移进入缓存而不串区',()=>{
    const capture=JSON.parse(fs.readFileSync(path.join(__dirname,'运行核验/现场R3失配抓包_20261017.json'),'utf8'));
    const protocol=require('./r2-protocol.cjs');const run=fixture();
    run.sim.handle=request=>{
        if(request[1]!==3)return null;
        const address=request.readUInt16BE(2);
        if(address===512)return protocol.frame(Buffer.concat([Buffer.from([1,3,48]),protocol.wordsToBytes([0x5249,1,0xf0,2,0,409,...Array(18).fill(0)])]));
        if(address===0||address===1120){
            const recorded=Buffer.from(capture.Responses.find(value=>(value.slice(6,10)==='534D')===(address===1120)),'hex');
            if(address===1120 && request.readUInt16BE(4)===96)
                return protocol.frame(Buffer.concat([Buffer.from([1,3,192]),recorded.subarray(3,-2),Buffer.alloc(64)]));
            return recorded;
        }
        return null;
    };
    run.tick(80);
    assert.ok(run.context.h13.vals[109]>0);assert.ok(run.context.h13.vals[114]>0);
    assert.equal(run.context.regionSeen.vals[0],1);
    assert.equal(run.context.rawRegs.vals[25],409);
    assert.equal(run.context.h13Status.vals[0],0x534d);
    assert.equal(run.context.h13Status.vals[3],409);
});

test('同步服务生成源完整一致，避免模板与实际绑定漂移',()=>{
    const crypto=require('node:crypto');const file=path.join(__dirname,'HMI13事务服务.txt');
    const before=crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
    const {generate}=require('./生成同步事务服务.cjs');
    assert.equal(crypto.createHash('sha256').update(generate()).digest('hex'),before);
    assert.equal(crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex'),before);
});

test('32号P2字段语义固定，原同步诊断值仍保留内部索引',()=>{
    const run=fixture({globalDisplay:true});run.tick(80);
    run.context.sys_pid=run.context.P2_Diag.id;
    run.context.h13.vals[1]=2000;run.context.h13.vals[156]=-1;
    run.context.h13.vals[157]=129;run.context.h13.vals[158]=1;run.context.h13.vals[159]=3;
    run.renderGlobal();
    assert.match(run.context.P2_Diag.timeoutCount.txt,/轴2I\/V/);assert.equal(run.context.h13.vals[156],-1);assert.equal(run.context.h13.vals[157],129);
    assert.doesNotMatch(run.context.P2_Diag.crcCount.txt,/HMI ADDR/);assert.equal(run.context.h13.vals[158],1);assert.equal(run.context.h13.vals[159],3);
});
