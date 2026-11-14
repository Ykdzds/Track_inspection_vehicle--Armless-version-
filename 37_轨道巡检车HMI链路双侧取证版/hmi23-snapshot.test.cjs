const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {fixture} = require('./hmi13.test.cjs');
const read = name => fs.readFileSync(path.join(__dirname, name), 'utf8').replace(/^\uFEFF/, '');
const historical = path.join(__dirname, '..', '23_轨道巡检车UART5统一状态快照版');
const historicalRead = name => fs.readFileSync(path.join(historical, name), 'utf8').replace(/^\uFEFF/, '');
const hash = name => crypto.createHash('sha256').update(fs.readFileSync(path.join(historical, name))).digest('hex').toUpperCase();
for (const owner of [1, 2, 3]) test('23号三源只读监测 owner=' + owner, () => {
    const run = fixture({globalDisplay: true});
    run.tick(100);
    run.sim.owner = owner;
    run.sim.target = 100;
    run.context.sys_pid = 8;
    run.tick(100);
    const state = run.context.h13;
    const status = run.context.h13Status;
    assert.equal(status.vals.length, 96);
    status.vals[59] = 1;
    status.vals[58] = 100;
    status.vals[60] = 90;
    status.vals[61] = 91;
    state.vals[86] = 1;
    state.vals[21] = state.vals[73] = state.vals[1];
    state.vals[1]+=50;status.vals[5]++;state.vals[21]=state.vals[73]=state.vals[1];run.renderGlobal();
    assert.match(run.context.P8_ServoControl.acceptedTarget.txt, owner===1 ? /100rpm\|0\.363m\/s\|1\.308km\/h/ : /27\.5rpm\|0\.100m\/s\|0\.360km\/h/);
    assert.match(run.context.P8_ServoControl.linkStatus.txt, /正常/);
    if (owner !== 1) assert.match(run.context.P8_ServoControl.leftOutput.txt, /24\.8rpm\|0\.090m\/s\|0\.324km\/h/);
    const retained=run.context.P8_ServoControl.acceptedTarget.txt;run.setOffline(true);
    run.tick(100);
    assert.equal(run.context.P8_ServoControl.acceptedTarget.txt,retained);assert.match(run.context.P8_ServoControl.linkStatus.txt,/超时/);
});
test('23号显示脚本字模覆盖，不借历史证据声明新字存在', () => {
    const glyphs = JSON.parse(read('运行核验/31号实施_20261101/31号实际工程读回.json')).Fonts.find(f=>f.Name==='UI31_20');
    for (const name of ['全局计龄刷新.txt', 'HMI13界面刷新.txt'])
        for (const literal of read(name).matchAll(/"([^"\n]*)"/g))
            for (const character of literal[1])
                if (/[\u4e00-\u9fff]/.test(character)) assert.ok(glyphs.Characters.includes(character), name + ':' + character);
});
test('23号本轮PIX/BIN及事件读回证据独立且匹配', () => {
    const delivery = JSON.parse(historicalRead('运行核验/23号交付索引_20261023.json'));
    assert.equal(delivery.PIX_SHA256, hash(delivery.PIX));
    assert.equal(delivery.BIN_SHA256, hash(delivery.BIN));
    assert.equal(delivery.BIN_Length, fs.statSync(path.join(historical, delivery.BIN)).size);
    assert.equal(delivery.PhysicalCommunicationVerified, false);
    assert.equal(delivery.MotionVerified, false);
    for (const record of JSON.parse(historicalRead('运行核验/23号重开事件绑定_20261023.json'))) {
        assert.equal(record.Matches, true);
        assert.equal(record.SHA256, hash(record.Source));
    }
    const config = JSON.parse(historicalRead('运行核验/23号配置重开核验_20261023.json'));
    assert.equal(config.Master, 'On');
    assert.equal(config.Mode, 'Default');
    assert.equal(config.RxBuffer, '255');
    assert.match(historicalRead('运行核验/23号VisualPIX编译_20261023.txt'), /编译成功/);
});
