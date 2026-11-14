"""Verify delivery copies/baselines and refresh immutable SHA256 manifests."""
from pathlib import Path
import hashlib,json,subprocess
R=Path(__file__).resolve().parent;ROOT=R.parents[3]
E=R/'运行核验/33号实施_20261102';M=ROOT/'5.工程代码/RailInspectionCar_Inno_V1';D=M/'Docs/HMI33_20261102'
P=R/'33号配套调试交付'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
def obj(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def put(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2),encoding='utf-8-sig')
protected=obj(E/'31_32保护基线.json')
changed=[x['Path'] for x in protected if not Path(x['Path']).is_file() or sha(Path(x['Path']))!=x['SHA256']]
assert not changed,changed
put(E/'31_32最终保护复核.json',{'CheckedFiles':len(protected),'Changed':changed,'Unchanged':not changed})
for x in obj(P/'配套交付SHA256.json'):assert sha(P/x['File'])==x['SHA256'],x['File']
compiled=obj(E/'33号编译产物.json')
assert sha(R/compiled['PIX'])==compiled['PIX_SHA256']
assert sha(R/compiled['BIN'])==compiled['BIN_SHA256']
assert sha(P/'33号屏端.pix')==compiled['PIX_SHA256']
assert sha(P/'33号屏端.pix.bin')==compiled['BIN_SHA256']
readback=obj(E/'33号实际工程读回.json');actual={}
for pg in readback['Pages']:
 for c in [pg]+pg['Controls']:
  for event in c['Events']:
   actual[f"{pg['Name']}.{c['Name']}/{event['Id']}"]={'Bytes':len((event['Code'] or '').encode('utf8')),'SHA256':hashlib.sha256((event['Code'] or '').encode('utf8')).hexdigest().upper(),'Enable':event['Enable']}
put(E/'33号事件绑定SHA256.json',actual)
put(E/'33号保护与配套复核.json',{'ProtectedFiles':len(protected),'ProtectedChanges':0,'PackageHashesVerified':True,'SavedPIXMatchesBINCompilation':True,'HardwareAcceptance':'pending'})
validation=D/'screen-validation'
for name in ['31_32最终保护复核.json','33号事件绑定SHA256.json','33号保护与配套复核.json']:(validation/name).write_bytes((E/name).read_bytes())
selected=[]
selected.extend(p for p in R.iterdir() if p.is_file() and (p.name.startswith('HMI33') or p.name.startswith('33号') or p.name in ['17_轨道巡检车.pix','17_轨道巡检车.pix.bin','全局计龄刷新.txt','build33.py','apply33.ps1','audit33.py','hmi33.test.cjs','native33.ps1','prepare-native33.cjs','audit-layout33.py','verify-saved33.ps1','编译33号工程.ps1','write-delivery33.py','finalize33.py']))
selected.extend(p for p in (R/'事件33').iterdir() if p.is_file())
selected.extend(R/'事件'/name for name in ['授权_按下.txt','授权_弹起.txt','急停_按下.txt','滑块_滑动.txt','故障恢复按钮_按下.txt'])
selected.extend(p for p in E.iterdir() if p.is_file() and p.name not in ['33号最终SHA256.json','desktop33.png'])
put(E/'33号最终SHA256.json',[{'File':p.relative_to(R).as_posix(),'Bytes':p.stat().st_size,'SHA256':sha(p)} for p in sorted(set(selected))])
put(D/'源码与证据SHA256.json',[{'File':p.relative_to(D).as_posix(),'Bytes':p.stat().st_size,'SHA256':sha(p)} for p in sorted(D.rglob('*')) if p.is_file() and p.name!='源码与证据SHA256.json' and 'firmware' not in p.relative_to(D).parts])
print(json.dumps({'ProtectedFiles':len(protected),'Changes':changed,'PackageVerified':True,'ManifestEntries':len(selected)},ensure_ascii=False))
