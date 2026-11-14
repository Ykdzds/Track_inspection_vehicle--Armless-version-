'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const {fixture} = require('./hmi13.test.cjs');
const index = require('./HMI13状态索引.json');
const readsAt = (run, address) => run.calls.filter(call => call.kind === 1 && call.request.readUInt16BE(2) === address);
const largestGap = calls => Math.max(0, ...calls.slice(1).map((call, offset) => call.at - calls[offset].at));

test('控制期间状态读取最大150ms，目标刷新最大200ms', () => {
    const run = fixture();
    run.authorize();
    const start = run.calls.length;
    run.tick(240);
    const calls = run.calls.slice(start);
    const status = calls.filter(call => call.kind === 1 && call.request.readUInt16BE(2) === 1120);
    const targets = calls.filter(call => call.kind === 2 && call.request[24] === 33);
    assert.ok(status.length > 20);
    assert.ok(targets.length > 20);
    assert.ok(largestGap(status) <= 150);
    assert.ok(largestGap(targets) <= 200);
    assert.equal(run.maximumInflight(), 1);
});

test('元数据丢失恢复含100ms排空且0460仍有界轮询，不能开放控制', () => {
    const run = fixture();
    const original = run.sim.handle.bind(run.sim);
    run.sim.handle = request => request[1] === 3 && request.readUInt16BE(2) === 512 ? null : original(request);
    run.tick(160);
    const statusReads = readsAt(run, 1120);
    assert.ok(statusReads.length >= 30);
    assert.ok(largestGap(statusReads) <= 300);
    assert.ok(readsAt(run, 512).length > 10);
    assert.equal(run.context.h13.vals[index.ready], 0);
    assert.equal(run.calls.filter(call => call.kind === 2).length, 0);
    assert.equal(run.maximumInflight(), 1);
});

test('元数据丢失恢复后重新确认身份，不自动申请授权', () => {
    const run = fixture();
    const original = run.sim.handle.bind(run.sim);
    let dropMetadata = true;
    run.sim.handle = request => dropMetadata && request[1] === 3 && request.readUInt16BE(2) === 512 ? null : original(request);
    run.tick(80);
    assert.equal(run.context.h13.vals[index.ready], 0);
    dropMetadata = false;
    run.tick(80);
    assert.equal(run.context.h13.vals[index.ready], 1);
    assert.equal(run.sim.owner, 0);
    assert.equal(run.calls.filter(call => call.kind === 2).length, 0);
    assert.ok(largestGap(readsAt(run, 1120)) <= 300);
});

for (const [field, value] of [['bcaddr', 2], ['bdindex', 6], ['bdaddr', 1119], ['bdcount', 63]]) {
    test('回调' + field + '失配不释放事务、不刷新状态缓存', () => {
        const run = fixture();
        for (let attempt = 0; attempt < 40; attempt++) {
            run.tick();
            if (run.context.h13.vals[index.busy] === 1 && run.context.h13.vals[index.kind] === 1 && run.context.h13.vals[index.readIndex] === 7) break;
        }
        assert.equal(run.context.h13.vals[index.readIndex], 7);
        assert.equal(run.context.h13.vals[index.busy], 1);
        const before = [...run.context.h13Status.vals];
        const statusAt = run.context.h13.vals[index.statusAt];
        const validCount = run.context.h13.vals[104];
        const oldMismatch = run.context.h13.vals[76];
        Object.assign(run.context.mod, {interruptCode: 1, bcaddr: 1, bdindex: 7, bdaddr: 1120, bdcount: 64, bdbuf: Array(128).fill(123), [field]: value});
        run.deliver();
        assert.deepEqual([...run.context.h13Status.vals], before);
        assert.equal(run.context.h13.vals[index.statusAt], statusAt);
        assert.equal(run.context.h13.vals[104], validCount);
        assert.equal(run.context.h13.vals[76], oldMismatch + 1);
        assert.equal(run.context.h13.vals[index.busy], 1);
        run.tick(2);
        assert.ok(run.context.h13.vals[104] > validCount);
    });
}

test('控制状态读取超时后清目标，恢复响应不自动重新授权', () => {
    const run = fixture();
    run.authorize();
    run.context.h13.vals[index.target] = 100;
    run.tick(20);
    const original = run.sim.handle.bind(run.sim);
    let dropStatus = true;
    run.sim.handle = request => dropStatus && request[1] === 3 && request.readUInt16BE(2) === 1120 ? null : original(request);
    run.tick(40);
    assert.notEqual(run.context.h13.vals[index.state], 3);
    assert.equal(run.context.h13.vals[index.target], 0);
    dropStatus = false;
    run.tick(100);
    assert.notEqual(run.context.h13.vals[index.state], 3);
    assert.equal(run.context.h13.vals[index.target], 0);
    assert.equal(run.sim.owner, 0);
});

test('只读主页与控制页往返不停止全局状态查询', () => {
    const run = fixture();
    run.context.sys_pid = 1;
    run.tick(80);
    const before = readsAt(run, 1120).length;
    run.context.sys_pid = 8;
    run.tick(80);
    run.context.sys_pid = 1;
    run.tick(80);
    assert.ok(readsAt(run, 1120).length > before + 20);
    assert.ok(largestGap(readsAt(run, 1120)) <= 200);
    assert.equal(run.sim.owner, 0);
    assert.equal(run.calls.filter(call => call.kind === 2).length, 0);
});
