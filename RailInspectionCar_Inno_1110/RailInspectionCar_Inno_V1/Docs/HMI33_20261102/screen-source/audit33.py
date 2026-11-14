from pathlib import Path
import json,re,hashlib
R=Path(__file__).resolve().parent;E=R/'运行核验/33号实施_20261102'
m=json.loads((E/'33号实际工程读回.json').read_text(encoding='utf-8-sig'))
arrays={p['Name']+'.'+c['Name']:c['Properties']['Length'] for p in m['Pages'] for c in p['Controls'] if c['Type'].endswith('ArrayCtrl')}
sources=[(str(p.name),p.read_text(encoding='utf-8-sig')) for p in [R/'HMI33事务服务.txt',R/'HMI33显示刷新.txt',R/'HMI33自定义函数.txt']]
errors=[];refs=0
for name,s in sources:
 for key,idx in re.findall(r'(P\d_\w+\.\w+)\.val(?:s|bs)\[(\d+)\]',s):
  refs+=1
  if key in arrays and int(idx)>=arrays[key]:errors.append(f'{name}: {key}[{idx}]/{arrays[key]}')
 for p in m['Pages']:
  for c in p['Controls']:
   if c['Type'].endswith('ArrayCtrl') and p['Id']==0:
    if re.search(r'(?<![.\w])'+c['Name']+r'\.vals',s):errors.append('unqualified '+c['Name'])
serial=[]
for p in m['Pages']:
 for c in [p]+p['Controls']:
  for ev in c['Events']:
   if re.search(r'modbus(Read|Writes)\(',ev['Code'] or ''):serial.append(p['Name']+'.'+c['Name'])
assert serial==['P0_Boot.refreshTick'],serial
f=(R/'HMI33自定义函数.txt').read_text(encoding='utf-8-sig')
assert not re.search(r'\.pressed\s*=(?!=)|\.val\s*=(?!=)',f), 'display mutates button/slider values'
assert f.count('P8_ServoControl.linkStatus.txt=')==2
assert '统一快照' not in next(x for x in re.split(r'void HMI33Field',f) if 'P8_ServoControl.linkStatus.txt=' in x)
assert not errors,errors
out={'StaticArrayReferences':refs,'Arrays':arrays,'SingleSerialWriter':serial,'Errors':errors,'HardwareVerified':False}
(E/'33号边界绑定核验.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(out,ensure_ascii=True))
