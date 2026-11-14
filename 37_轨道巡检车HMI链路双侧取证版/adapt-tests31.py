from pathlib import Path
R=Path(__file__).resolve().parent
for n in ['hmi13.test.cjs','hmi13-failures.test.cjs']:
 p=R/n;s=p.read_text(encoding='utf-8-sig')
 s=s.replace('vm.runInContext(source,run.context)',"require('./ui31-vm.cjs').compile(source).runInContext(run.context)")
 s=s.replace("vm.runInContext(fs.readFileSync(path.join(__dirname,'事件','滑块_滑动.txt'),'utf8'),run.context)","run.event('事件/滑块_滑动.txt')")
 p.write_text(s,encoding='utf8')
# The old artifact suite remains a check of the protected historical delivery.
p=R/'hmi30-artifact.test.cjs';s=p.read_text(encoding='utf8').replace('const root=__dirname,',"const root=path.join(__dirname,'..','30_轨道巡检车全界面科技风优化版'),");p.write_text(s,encoding='utf8')
p=R/'hmi30-display.test.cjs';s=p.read_text(encoding='utf8')
s=s.replace("evidence=path.join(root,'运行核验/30号实施_20261101')","evidence=path.join(root,'..','30_轨道巡检车全界面科技风优化版','运行核验/30号实施_20261101')")
s=s.replace("base=path.join(root,'..','29_轨道巡检车跨页运行与伺服快捷导航版')","base=path.join(root,'..','30_轨道巡检车全界面科技风优化版')")
s=s.replace('return vm.createContext(c);',"require('./ui31-vm.cjs').initialize(c);return vm.createContext(c);")
s=s.replace('const script=new vm.Script(',"const script=require('./ui31-vm.cjs').compile(").replace('const display=new vm.Script(',"const display=require('./ui31-vm.cjs').compile(")
s=s.replace("+' rpm');", "+'rpm|'+m+'m/s|'+km+'km/h');")
s=s.replace("  assert.equal(c.P8_ServoControl[side+'ActualUnits'].txt,m+' m/s | '+km+' km/h');", "  assert.equal(c.P8_ServoControl[side+'ActualUnits'].txt,'');")
start=s.index("  for(const f of ['Target','Output'])")
end=s.index('\n }',start)
s=s[:start]+"  for(const f of ['Target','Output'])assert.ok(c.P8_ServoControl[side+f].txt.endsWith(rpm+'rpm|'+m+'m/s|'+km+'km/h'));"+s[end:]
s=s.replace("rpm+' rpm\\n'+m+' m/s | '+km+' km/h'","rpm+'rpm|'+m+'m/s|'+km+'km/h'")
s=s.replace("assert.equal(c.P8_ServoControl.leftActualUnits.txt,'-- m/s | -- km/h');", "assert.equal(c.P8_ServoControl.leftActualUnits.txt,'');assert.doesNotMatch(c.P8_ServoControl.leftActual.txt,/\\d+rpm/);")
s=s.replace("+' m/s | '+", "+'m/s|'+").replace("+' km/h';", "+'km/h';").replace("+' rpm'", "+'rpm'")
# Historical structural preservation test still targets the historical 29/30 readbacks;
# its refresh source must likewise come from that delivery.
s=s.replace("fs.readFileSync(path.join(root,'全局计龄刷新.txt'),'utf8').replace", "fs.readFileSync(path.join(base,'全局计龄刷新.txt'),'utf8').replace")
s=s.replace('30 相同模拟工况下29/30','31 相同模拟工况下30/31')
p.write_text(s,encoding='utf8')
p=R/'hmi23-snapshot.test.cjs';s=p.read_text(encoding='utf8')
s=s.replace(r'100 rpm\n0\.363 m\/s \| 1\.308 km\/h',r'100rpm\|0\.363m\/s\|1\.308km\/h')
s=s.replace(r'27\.5 rpm\n0\.100 m\/s \| 0\.360 km\/h',r'27\.5rpm\|0\.100m\/s\|0\.360km\/h')
s=s.replace(r'24\.8 rpm\n0\.090 m\/s \| 0\.324 km\/h',r'24\.8rpm\|0\.090m\/s\|0\.324km\/h')
s=s.replace(r'-- rpm\n-- m\/s \| -- km\/h',r'--rpm\|--m\/s\|--km\/h')
s=s.replace('30号实施_20261101/30号实际工程读回.json','31号实施_20261101/31号实际工程读回.json').replace("f.Name==='UI30_微软雅黑20'","f.Name==='UI31_20'")
p.write_text(s,encoding='utf8')
p=R/'全模式只读显示回归测试.cjs';s=p.read_text(encoding='utf8')
s=s.replace('const script = new vm.Script(',"const script = require('./ui31-vm.cjs').compile(").replace('return vm.createContext(c);',"require('./ui31-vm.cjs').initialize(c);return vm.createContext(c);")
p.write_text(s,encoding='utf8')
