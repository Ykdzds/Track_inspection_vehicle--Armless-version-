from pathlib import Path
import re,json,hashlib
r=Path(r'D:\wtzn_sx_\同济大学-轨道巡检车\同济大学轨道巡检车协作资料\2.硬件资料\7.三易串口触摸屏10.1寸\用户代码\37_轨道巡检车HMI链路双侧取证版');e=r/'运行核验/37号实施_20261107'
a=(r/'HMI34事务服务.txt').read_text('utf-8-sig');b=(r/'HMI37事务服务.txt').read_text('utf-8-sig');base=a.splitlines();new=b.splitlines()
# Strong equivalence check: each original line remains a subsequence of the instrumented line in order.
pos=0;fail=[]
for n,line in enumerate(base,1):
 while pos<len(new) and ('HMI37Upload();'==new[pos]):pos+=1
 if pos>=len(new):fail.append((n,'missing'));break
 # Remove only inserted instrumentation snippets generated before an existing token.
 s=new[pos];pos+=1
 s=re.sub(r'P0_Boot\.d37c\.vals\[7\]=\d+;P0_Boot\.d37c\.vals\[8\]=\d+;P0_Boot\.d37c\.vals\[28\]=\d+;','',s)
 s=s.replace('P0_Boot.d37c.vals[9]=r33;HMI37Reply();','')
 s=s.replace('P0_Boot.d37c.vals[24]=gap33;','')
 s=re.sub(r'P0_Boot\.d37c\.vals\[17\]=\d+;P0_Boot\.d37c\.vals\[18\]=\d+;.*?HMI37Capture\(\);','',s)
 if s!=line:fail.append((n,s[:100]))
assert not fail,fail
capture_sites=0
for n,line in enumerate(new,1):
 if 'HMI37Capture();' in line:
  capture_sites+=1
  prefix=line.split('HMI37Capture();')[0]
  assert not re.search(r'P8_ServoControl\.h13\.vals\[(?:6|7|10|26)\]=0;',prefix),('clear before capture',n)
assert capture_sites==59,capture_sites
model=json.loads((e/'37号实际工程读回.json').read_text('utf-8-sig'));caps={p['Name']+'.'+c['Name']:c['Properties']['Length'] for p in model['Pages'] for c in p['Controls'] if c['Type'].endswith('ArrayCtrl')}
refs=0
for name in ['HMI37事务服务.txt','HMI37自定义函数.txt','HMI37全局刷新.txt']:
 s=(r/name).read_text('utf-8-sig')
 for m in re.finditer(r'(P\d+_\w+)\.(\w+)\.val(?:s|bs)\[(\d+)\]',s):
  k=m[1]+'.'+m[2];assert int(m[3])<caps[k],(name,k,m[3],caps[k]);refs+=1
src=r.parent/'34_轨道巡检车跨页运行与触控响应修复版';manifest=json.loads((e/'34号原始SHA256.json').read_text('utf-8-sig'))
changed=[]
for x in manifest:
 p=src/Path(x['Path']);h=hashlib.sha256(p.read_bytes()).hexdigest().upper()
 if h!=x['Hash']:changed.append(x['Path'])
assert not changed,changed
result={'ControlServiceOriginalLinesPreserved':True,'OriginalLineCount':len(base),'CaptureSitesBeforeTargetSessionCommandClear':capture_sites,'ConstantArrayReferencesChecked':refs,'AddedArrays':{k:v for k,v in caps.items() if k.startswith('P0_Boot.d37')},'Baseline34FilesUnchanged':len(manifest),'HardwareVerified':False}
(e/'37号静态与基线核验.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8-sig');print(json.dumps(result,ensure_ascii=False))
