const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const readJson = filename => JSON.parse(fs.readFileSync(path.join(__dirname, filename), 'utf8').replace(/^\uFEFF/, ''));
const sha256 = filename => crypto.createHash('sha256').update(fs.readFileSync(path.join(__dirname, filename))).digest('hex').toUpperCase();
test('真实重开记录确认主站角色，并绑定当前PIX哈希', () => {
    const role = readJson('运行核验/全量修复角色重开核验_20261016.json');
    assert.equal(role.Project13Master, 'On');
    assert.equal(role.Baseline08Master, 'On');
    assert.equal(role.Reopened, true);
    assert.equal(role.PIX, sha256('13_轨道巡检车.pix'));
});

test('当前可见中文提示全部在真实字体编辑器字模清单中',()=>{const glyphs=readJson('运行核验/全量修复字体读回_20261016.json');assert.equal(glyphs.Status,'SOURCE_GLYPHS_VERIFIED');assert.equal(glyphs.SHA256,sha256(glyphs.Font));assert.deepEqual(glyphs.RequiredChineseMissing,[]);for(const filename of glyphs.VerifiedSources){const source=fs.readFileSync(path.join(__dirname,filename),'utf8');for(const match of source.matchAll(/"([^"\n]*)"/g))for(const character of match[1])if(/[\u4e00-\u9fff]/.test(character))assert.ok(glyphs.Characters.includes(character),filename+':'+character);}});

test('本轮核心事件、解析副本和全页面生命周期证据完整',()=>{const records=readJson('运行核验/全量修复重开事件绑定核验_20261016.json');assert.equal(records.length,11);for(const item of records){assert.equal(item.Matches,true);assert.equal(item.SHA256,sha256(item.Source));}assert.equal(fs.readFileSync(path.join(__dirname,'mod.txt'),'utf8').replace(/\r/g,''),fs.readFileSync(path.join(__dirname,'Modbus中断只读解析.txt'),'utf8').replace(/\r/g,''));const pages=readJson('运行核验/全页面加载离开事件读回_20261016.json');assert.equal(pages.length,18);assert.equal(new Set(pages.map(item=>item.Page)).size,9);});
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
        assert.equal(row['读取超时时间（ms）'], '50');
    });
});
test('本次编译产物与交付哈希一致，不冒充实物通过', () => {
    const delivery = readJson('运行核验/全量修复交付哈希_20261016.json');
    for (const file of delivery.Files) {
        assert.equal(sha256(file.Name), file.SHA256);
        assert.equal(fs.statSync(path.join(__dirname, file.Name)).size, file.Length);
    }
    assert.equal(delivery.Baseline08Unchanged, true);
    assert.equal(delivery.PhysicalCommunicationVerified, false);
    assert.equal(delivery.MotionVerified, false);
    const compile = fs.readFileSync(path.join(__dirname, '运行核验/全量修复VisualPIX编译日志_20261016.txt'), 'utf8');
    assert.match(compile, /编译成功/);
    assert.doesNotMatch(compile, /编译失败/);
});

test('真实PIX操作控件与页面事件读回核验完整', () => {
    const controls = readJson('运行核验/全量修复操作控件事件核验_20261016.json');
    assert.deepEqual(controls.map(record => record.Control).sort(), ['authorize', 'backHome', 'estop', 'rpmSlider', 'servoEntry']);
    for (const record of controls) {
        assert.equal(record.Matches, true);
        assert.equal(record.SHA256, sha256(record.Source));
    }
    const pages = readJson('运行核验/全量修复P8页面事件核验_20261016.json');
    assert.deepEqual(pages.map(record => record.Event).sort(), ['加载页面', '离开页面']);
    assert.ok(pages.every(record => record.Matches === true));
});
