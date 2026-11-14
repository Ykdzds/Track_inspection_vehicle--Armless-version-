'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
// Historical 29 artifacts stay anchored to protected 29; 30 has separate artifact checks.
const root=path.join(__dirname,'..','29_轨道巡检车跨页运行与伺服快捷导航版'),evidence=path.join(root,'运行核验/29号实施_20261101');
const json=name=>JSON.parse(fs.readFileSync(path.join(evidence,name),'utf8').replace(/^\uFEFF/,''));
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex').toUpperCase();
test('29 actual reopened PIX event scripts match current sources',()=>{
 const records=json('29号重开事件绑定.json');assert.equal(records.length,27);
 for(const r of records){assert.equal(r.Matches,true);assert.equal(r.SHA256,hash(path.join(root,r.Source)));assert.equal(r.Script.replace(/\r/g,'').trimEnd(),fs.readFileSync(path.join(root,r.Source),'utf8').replace(/\r/g,'').trimEnd());}
 const service=records.find(r=>r.Control==='h13Service').Script;
 assert.doesNotMatch(service,/sys_pid!=P8_ServoControl.id|showPage\(/);
 const shortcuts=records.filter(r=>/^P[2-7]_/.test(r.Page)&&r.Control==='servoEntry');assert.equal(shortcuts.length,12);
 for(const r of shortcuts)assert.equal(r.Script.trim(),r.Event==='弹起事件'?'showPage(P8_ServoControl.id);':'');
});
test('29 reopened properties retain timer, global slider and navigation geometry',()=>{
 const records=json('29号重开控件属性.json'),props=name=>records.find(r=>r.Name===name).Properties;
 assert.equal(props('rpmSlider')[1],'True');assert.equal(props('h13')[5],'179');assert.equal(props('h13Status')[5],'95');
 assert.equal(props('h13Service')[1],'True');assert.equal(props('h13Service')[5],'True');assert.equal(props('h13Service')[6],'5');
 for(const r of records.filter(r=>/^P[2-7]_/.test(r.Page))){assert.equal(r.Name,'servoEntry');assert.deepEqual(r.Properties.slice(4,8),['832','554','150','40']);assert.equal(r.Properties[11],'True');assert.equal(r.Properties[14],'伺服控制');}
 assert.ok(json('29号查询页生命周期.json').every(r=>!r.Script.trim()));
 assert.equal(json('29号主机模式核验.json').Master,'On');
});
test('29 compiled binary and project match actual VP compile evidence',()=>{
 const r=json('29号编译产物.json');assert.equal(r.NewBinary,true);
 assert.equal(hash(path.join(root,r.PIX)),r.PIX_SHA256);assert.equal(hash(path.join(root,r.BIN)),r.BIN_SHA256);
 assert.equal(fs.statSync(path.join(root,r.BIN)).size,r.BIN_Length);
 assert.match(fs.readFileSync(path.join(evidence,'29号VisualPIX编译.log'),'utf8'),/编译成功/);
 assert.equal(r.PhysicalCommunicationVerified,false);assert.equal(r.MotionVerified,false);
});
test('29 new visible text uses existing Chinese glyphs',()=>{
 const glyphs=JSON.parse(fs.readFileSync(path.join(root,'运行核验/全量修复字体读回_20261016.json'),'utf8').replace(/^\uFEFF/,''));
 for(const text of ['伺服控制','屏幕工程：29'])for(const c of text)if(/[\u4e00-\u9fff]/.test(c))assert.ok(glyphs.Characters.includes(c),c);
});
