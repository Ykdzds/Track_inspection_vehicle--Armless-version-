const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const baselineRoot=path.join(__dirname,'..','22_轨道巡检车UART5三源全局快速停车版');
const crypto = require('node:crypto');
const readJson = filename => JSON.parse(fs.readFileSync(path.join(baselineRoot, filename), 'utf8').replace(/^\uFEFF/, ''));
const sha256 = filename => crypto.createHash('sha256').update(fs.readFileSync(path.join(baselineRoot, filename))).digest('hex').toUpperCase();
test('真实重开记录确认主站角色，并绑定当前PIX哈希', () => {
    const role = readJson('运行核验/22号配置重开核验_20261022.json');
    assert.equal(role.Master, 'On');
    assert.equal(readJson('运行核验/全量修复角色重开核验_20261016.json').Baseline08Master, 'On');
    assert.equal(role.Reopened, true);
    assert.equal(role.PIX, sha256('17_轨道巡检车.pix'));
    const mode=readJson('运行核验/22号配置重开核验_20261022.json');
    assert.equal(mode.Mode,'Default');assert.equal(mode.RxBuffer,'255');assert.equal(mode.BufferPollEnabled,false);
    assert.equal(mode.PIX,sha256('17_轨道巡检车.pix'));
});

test('当前可见中文提示全部在真实字体编辑器字模清单中',()=>{const glyphs=readJson('运行核验/全量修复字体读回_20261016.json');assert.equal(glyphs.Status,'SOURCE_GLYPHS_VERIFIED');assert.equal(glyphs.SHA256,sha256(glyphs.Font));assert.deepEqual(glyphs.RequiredChineseMissing,[]);for(const filename of glyphs.VerifiedSources){const source=fs.readFileSync(path.join(baselineRoot,filename),'utf8');for(const match of source.matchAll(/"([^"\n]*)"/g))for(const character of match[1])if(/[\u4e00-\u9fff]/.test(character))assert.ok(glyphs.Characters.includes(character),filename+':'+character);}});

test('本轮核心事件、解析副本和全页面生命周期证据完整',()=>{const records=readJson('运行核验/22号重开事件绑定_20261022.json');assert.equal(records.length,12);for(const item of records){assert.equal(item.Matches,true);assert.equal(item.SHA256,sha256(item.Source));}assert.equal(fs.readFileSync(path.join(baselineRoot,'mod.txt'),'utf8').replace(/\r/g,''),fs.readFileSync(path.join(baselineRoot,'Modbus中断只读解析.txt'),'utf8').replace(/\r/g,''));const pages=readJson('运行核验/22号全页生命周期_20261022.json');assert.equal(pages.length,18);assert.equal(new Set(pages.map(item=>item.Page)).size,9);});
test('真实八区读回保持单次调度前提，不依赖可见列序号', () => {
    const audit = readJson('运行核验/Buffer重开核验_20261016.json');
    assert.equal(audit.AuditOnly, true);
    assert.equal(audit.Rows.length, 8);
    const expected = [[0,64],[80,32],[112,32],[144,32],[176,32],[512,24],[288,20],[1120,64]];
    audit.Rows.forEach((row, index) => {
        assert.equal(row.Buffer, index);
        assert.equal(row.Type, '保持寄存器');
        assert.equal(row['从机地址'], '1');
        assert.equal(Number(row['起始地址']), expected[index][0]);
        assert.equal(Number(row['数量']), expected[index][1]);
        assert.equal(row['读取次数'], '0');
        assert.equal(row['循环读取间隔时间（ms）'], '100');
        assert.equal(row['读取超时时间（ms）'], '50');
    });
});
test('本次编译产物与交付哈希一致，不冒充实物通过', () => {
    const current=readJson('运行核验/22号交付索引_20261022.json');
    assert.equal(current.Sources.length,12);
    for(const source of current.Sources)assert.equal(source.SHA256,sha256(source.Source));
    assert.equal(current.Mode,'Default');
    assert.equal(current.PIX_SHA256,sha256(current.PIX));
    assert.equal(current.BIN_SHA256,sha256(current.BIN));
    assert.equal(current.BIN_Length,fs.statSync(path.join(baselineRoot,current.BIN)).size);
    assert.equal(current.PhysicalCommunicationVerified,false);
    assert.equal(current.MotionVerified,false);
    assert.equal(current.PhysicalCommunicationVerified, false);
    assert.equal(current.MotionVerified, false);
    const compile = fs.readFileSync(path.join(baselineRoot, '运行核验/22号VisualPIX编译_20261022.txt'), 'utf8');
    assert.match(compile, /编译成功/);
    assert.doesNotMatch(compile, /编译失败/);
});

test('真实PIX操作控件与页面事件读回核验完整', () => {
    const controls = readJson('运行核验/22号重开事件绑定_20261022.json').filter(record=>['authorize','backHome','estop','rpmSlider','servoEntry'].includes(record.Control));
    assert.deepEqual([...new Set(controls.map(record => record.Control))].sort(), ['authorize', 'backHome', 'estop', 'rpmSlider', 'servoEntry']);
    assert.deepEqual(controls.filter(record=>record.Control==='authorize').map(record=>record.Event).sort(),['按下事件','弹起事件'].sort());
    for (const record of controls) {
        assert.equal(record.Matches, true);
        assert.equal(record.SHA256, sha256(record.Source));
    }
    const pages = readJson('运行核验/22号重开事件绑定_20261022.json').filter(record=>record.Page==='P8_ServoControl'&&!record.Control);
    assert.deepEqual(pages.map(record => record.Event).sort(), ['加载页面', '离开页面']);
    assert.ok(pages.every(record => record.Matches === true));
});

test('17号使用扩容后的授权刷新保护位，13号通信基线逐项未变',()=>{
    const config=readJson('运行核验/22号配置重开核验_20261022.json');
    assert.equal(config.PIX,sha256('17_轨道巡检车.pix'));
    assert.equal(config.StateArrayLength,161);assert.equal(config.AuthorizeControlType,'TOGGLE_BUTTON');
    const baseline=readJson('运行核验/17号复制基线.json');
    for(const entry of readJson('运行核验/通信正常13号原工程保护哈希.json')){
        const actual=crypto.createHash('sha256').update(fs.readFileSync(path.join(baseline.Source,entry.Relative))).digest('hex').toUpperCase();
        assert.equal(actual,entry.SHA256,entry.Relative);
    }
});

test('19号回退基线逐文件哈希未变且20号未扩展线协议',()=>{const source=path.join(baselineRoot,'..','19_轨道巡检车通信状态稳定修复版');const baseline=readJson('19号基线SHA256_20261019.json');for(const item of baseline){const current=fs.readFileSync(path.join(source,item.Path));assert.equal(current.length,item.Length,item.Path);assert.equal(crypto.createHash('sha256').update(current).digest('hex').toUpperCase(),item.SHA256,item.Path);}assert.equal(readJson('HMI13状态索引.json').externalStopTrack,56);assert.equal(readJson('HMI13状态索引.json').grantTracked,69);});

test('20号实物回退基线304文件逐项未变',()=>{const root=path.join(baselineRoot,'..','20_轨道巡检车UART5跨源停车恢复修复版');for(const item of readJson('20号基线SHA256_20261019.json')){const data=fs.readFileSync(path.join(root,item.Path));assert.equal(data.length,item.Length,item.Path);assert.equal(crypto.createHash('sha256').update(data).digest('hex').toUpperCase(),item.SHA256,item.Path);}});

