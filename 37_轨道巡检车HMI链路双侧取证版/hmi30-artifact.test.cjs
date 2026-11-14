'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const root=path.join(__dirname,'..','30_轨道巡检车全界面科技风优化版'),E=path.join(root,'运行核验/30号实施_20261101');
const read=f=>fs.readFileSync(path.join(root,f),'utf8').replace(/^\uFEFF/,'');
const json=f=>JSON.parse(fs.readFileSync(path.join(E,f),'utf8').replace(/^\uFEFF/,''));
const hash=f=>crypto.createHash('sha256').update(fs.readFileSync(path.join(root,f))).digest('hex').toUpperCase();
test('30 新PIX/BIN与本次VP实际编译证据一致',()=>{
 const r=json('30号编译产物.json');assert.equal(r.NewBinary,true);assert.equal(hash(r.PIX),r.PIX_SHA256);assert.equal(hash(r.BIN),r.BIN_SHA256);assert.equal(fs.statSync(path.join(root,r.BIN)).size,r.BIN_Length);
 assert.match(fs.readFileSync(path.join(E,'30号VisualPIX编译.log'),'utf8'),/编译成功/);
 assert.ok(r.BIN_Length<4*1024*1024*.8,'conservative nominal 4 MiB budget retains 20%');
 assert.equal(r.PhysicalCommunicationVerified,false);assert.equal(r.MotionVerified,false);
});
test('30 实际字模覆盖全部控件文本及全部事件字面量',()=>{
 const m=json('30号实际工程读回.json'),sets=new Map(m.Fonts.map(f=>[f.Id,new Set(f.Characters)])),ui=m.Fonts.filter(f=>/^UI30/.test(f.Name));
 assert.deepEqual(ui.map(f=>f.Height),[20,24,32,36]);
 for(const p of m.Pages)for(const c of p.Controls){
  if(c.Properties.Text)for(const char of c.Properties.Text)if(!/[\r\n\t]/.test(char))assert.ok(sets.get(c.Properties.FontId)?.has(char),p.Name+'.'+c.Name+':'+char);
  for(const e of c.Events)for(const s of (e.Code||'').matchAll(/"([^"\n]*)"/g))for(const char of s[1])if(/[\u4e00-\u9fff]/.test(char))for(const f of ui)assert.ok(f.Characters.includes(char),e.Name+':'+char);
 }
 for(const f of ['全局计龄刷新.txt','HMI13界面刷新.txt'])assert.doesNotMatch(read(f),/mm\/s|modbusBufferSend|moubusBufferSend/);
});
test('30 九页触摸范围均在画布内且互不重叠',()=>{
 const m=json('30号实际工程读回.json');
 for(const p of m.Pages){const controls=p.Controls.filter(c=>/\.(ButtonCtrl|ToggleButtonCtrl|SliderCtrl)$/.test(c.Type));
  for(const c of controls){const a=c.Properties;assert.ok(a.X>=0&&a.Y>=0&&a.X+a.Width<=1024&&a.Y+a.Height<=600,p.Name+c.Name);if(c.Type.endsWith('SliderCtrl')){assert.ok(a.Height>=48);assert.equal(a.ThumbWidth,32);}
   for(const d of controls){if(c.Id>=d.Id)continue;const b=d.Properties;assert.ok(!(a.X<b.X+b.Width&&b.X<a.X+a.Width&&a.Y<b.Y+b.Height&&b.Y<a.Y+a.Height),p.Name+c.Name+'/'+d.Name);}
  }
 }
});
test('30 36组字体宽高核验通过、保护基线未变',()=>{
 const r=json('布局与保护核验.json');assert.ok(r.LayoutChecks>=750);assert.deepEqual(r.Issues,[]);assert.equal(r.ProtectedFiles,2339);assert.deepEqual(r.ProtectedChanged,[]);
 for(const f of json('保护基线SHA256.json'))assert.equal(crypto.createHash('sha256').update(fs.readFileSync(f.File)).digest('hex').toUpperCase(),f.SHA256,f.File);
});
