"""Audit and redraw native VP states with embedded resources and 4-bit PixelFonts.
Images are deterministic reconstructions, not physical screen screenshots.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import json,base64,csv,hashlib,copy,math
R=Path(__file__).resolve().parent;E=R/'运行核验/31号实施_20261101';O=E/'日夜状态图';O.mkdir(exist_ok=True)
def read(n):return json.loads((E/n).read_text(encoding='utf-8-sig'))
m=read('31号实际工程读回.json');base=read('30号完整模型基线.json');states=read('31号VP原生显示状态.json')
fonts={f['Id']:f for f in read('实际字模.json')};glyphs={k:{g['Char']:g for g in v['Glyphs']} for k,v in fonts.items()}
images={i['Id']:next((E/'资源读回').glob(i['Name']+'.*'),None) for i in m['Images']}
pages={p['Name']:p for p in m['Pages']};issues=[];measurements=[];contrasts=[]
caption=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',18)
def rgb(n):return tuple((int(n)&0xffffff)>>s&255 for s in (16,8,0))
def lum(c):
 a=[v/255 for v in c];a=[v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in a];return .2126*a[0]+.7152*a[1]+.0722*a[2]
def contrast(a,b):
 x,y=sorted((lum(a),lum(b)));return (y+.05)/(x+.05)
def lines(text,f,w,wrap):
 gs=glyphs[f];out=[''];missing=set()
 for c in (text or '').replace('\r',''):
  if c=='\n':out.append('');continue
  if c not in gs:missing.add(c);continue
  if wrap and sum(gs[x]['Width'] for x in out[-1])+gs[c]['Width']>w and out[-1]:out.append('')
  out[-1]+=c
 return out,missing
mask_cache={}
def textdraw(im,c,p,audit,tag):
 f=c['FontId'];x,y,w,h=(c[k] for k in ['X','Y','Width','Height']);font=fonts[f];gs=glyphs[f];ls,missing=lines(c['Text'],f,w,p.get('WordWrap',False));width=max([sum(gs[z]['Width'] for z in line) for line in ls] or [0]);height=len(ls)*font['Height']
 if audit:
  measurements.append(dict(State=tag,Control=c['Name'],Text=c['Text'],Width=width,Available=w,Height=height,AvailableHeight=h,FontHeight=font['Height']))
  if missing:issues.append([tag,c['Name'],'missing glyphs',''.join(missing)])
  if width>w or height>h:issues.append([tag,c['Name'],'overflow',width,w,height,h,c['Text']])
  if x<0 or y<0 or x+w>1024 or y+h>600:issues.append([tag,c['Name'],'outside canvas'])
 yy=y+(max(0,(h-height)//2) if c['VerticalAlignment']=='CENTER' else 0)
 for line in ls:
  length=sum(gs[z]['Width'] for z in line);xx=x+(w-length if c['HorizontalAlignment']=='RIGHT' else (w-length)//2 if c['HorizontalAlignment']=='CENTER' else 0)
  for z in line:
   g=gs[z];gw=g['Width'];key=(f,z)
   if key not in mask_cache:
    data=base64.b64decode(g['Data']);stride=(gw+1)//2;mask_cache[key]=Image.frombytes('L',(gw,font['Height']),bytes(((data[j*stride+i//2]>>(4 if i%2==0 else 0))&15)*17 for j in range(font['Height']) for i in range(gw)))
   im.paste(rgb(c['FontColor']),(xx,yy,xx+gw,yy+font['Height']),mask_cache[key]);xx+=gw
  yy+=font['Height']
def render(s,audit=True):
 im=Image.open(images[s['ImageId']]).convert('RGB');pg=pages[s['Page']];cs={c['Name']:c for c in pg['Controls']};tag=f"{s['Theme']}/{s['Page']}/{s['Scenario']}"
 for c in sorted(s['Controls'],key=lambda c:cs[c['Name']]['Id']):
  if not c['Visible']:continue
  p=cs[c['Name']]['Properties'];typ=cs[c['Name']]['Type'].split('.')[-1];x,y,w,h=(c[k] for k in ['X','Y','Width','Height']);d=ImageDraw.Draw(im)
  if typ in ['ButtonCtrl','ToggleButtonCtrl']:
   skin=images[c['ImageId1'] if c['Enable'] else c['DisableBackgroundImageId']];im.paste(Image.open(skin),(x,y));bg=im.getpixel((x+w//2,y+h//2))
  elif typ=='TextCtrl':bg=rgb(c['BackgroundColor']);d.rectangle((x,y,x+w-1,y+h-1),fill=bg)
  elif typ=='SliderCtrl':
   # Transparent background preserves the lower text. The original thumb is above it.
   tx=x+math.floor(c['Value']*(w-p['ThumbWidth'])/(p['MaxValue']-p['MinValue']));d.rectangle((tx,y,tx+p['ThumbWidth']-1,y+h-1),fill=rgb(c['ThumbColor']));continue
  elif typ=='ProgressBarCtrl':
   d.rectangle((x,y,x+w-1,y+h-1),fill=rgb(c['BackgroundColor']));continue
  else:continue
  if c['FontId'] not in fonts:issues.append([tag,c['Name'],'font missing']);continue
  if audit:
   ratio=contrast(rgb(c['FontColor']),bg);contrasts.append(dict(State=tag,Control=c['Name'],Ratio=ratio))
   if ratio<4.5:issues.append([tag,c['Name'],'contrast',round(ratio,3),rgb(c['FontColor']),bg])
  textdraw(im,c,p,audit,tag)
 return im
for s in states:render(s).save(O/f"{s['Page']}_{s['Theme']}_{s['Scenario']}.png")
# Check all normal/pressed/disabled skins, including states not active in a snapshot.
for s in states:
 for c in s['Controls']:
  if not c.get('ImageId1'):continue
  for k in ['ImageId1','ImageId2','DisableBackgroundImageId']:
   skin=Image.open(images[c[k]]).convert('RGB');ratio=contrast(rgb(c['FontColor']),skin.getpixel((skin.width//2,skin.height//2)))
   contrasts.append(dict(State=f"{s['Theme']}/{s['Page']}/all-skins",Control=c['Name']+'/'+k,Ratio=ratio))
   if ratio<4.5:issues.append(['all button skins',s['Theme'],s['Page'],c['Name'],k,ratio])
for p in m['Pages']:
 im=Image.new('RGB',(2048,636),'#18283b');d=ImageDraw.Draw(im)
 for t in [0,1]:
  im.paste(Image.open(O/f"{p['Name']}_{t}_normal.png"),(1024*t,36));d.text((1024*t+16,7),p['Name']+(' / 日间' if t==0 else ' / 夜间'),font=caption,fill='white')
 im.save(E/(p['Name']+'_日夜对照.png'))
overview=Image.new('RGB',(1536,3*636),'#18283b');d=ImageDraw.Draw(overview)
for i,p in enumerate(m['Pages']):
 for t in [0,1]:overview.paste(Image.open(O/f"{p['Name']}_{t}_normal.png").resize((512,300)),((i%3)*512,(i//3)*636+36+t*300))
 d.text(((i%3)*512+8,(i//3)*636+7),p['Name']+'  上：日间 下：夜间',font=caption,fill='white')
overview.save(E/'九页日夜总览.png')
# Speed boundaries from equivalent execution, measured with the same embedded fonts.
records=read('31号速度边界.json');resource=read('资源索引绑定.json');by_index={resource['user.font.'+f['Name']]:f['Id'] for f in fonts.values()}
for z in records:
 fid=by_index.get(z.get('font'),next(f['Id'] for f in fonts.values() if f['Height']==24));ls,missing=lines(z['txt'],fid,456,False);width=max(sum(glyphs[fid][c]['Width'] for c in l) for l in ls)
 if width>456 or len(ls)>1 or missing:issues.append(['speed boundary',z,width,sorted(missing)])
 measurements.append(dict(State='speed-boundary',Control=z['name'],Text=z['txt'],Width=width,Available=456,FontHeight=fonts[fid]['Height']))
speed_im=Image.new('RGB',(1024,15*70+50),'#FFFFFF');sd=ImageDraw.Draw(speed_im)
for i,rpm in enumerate([0,1,-1,100,-100,540,-540,1600,-1600,32766,-32768]):
 sd.text((24,i*70+5),f'rpm={rpm} / 24px',font=caption,fill='#42566D')
 for side,x in [('left',32),('right',536)]:
  z=next(z for z in records if z.get('rpm')==rpm and z['name']==side+'Actual');fid=next(f['Id'] for f in fonts.values() if f['Height']==24)
  c=dict(Name=side+'Actual',FontId=fid,X=x,Y=i*70+32,Width=456,Height=32,Text=z['txt'],FontColor=0x10243A,HorizontalAlignment='LEFT',VerticalAlignment='TOP');textdraw(speed_im,c,{'WordWrap':False},False,'speed-figure')
for i,v in enumerate([32766,-32768],11):
 z=next(z for z in records if z.get('linear')==v and z['name']=='acceptedTarget');fid=by_index[z['font']];sd.text((24,i*70+5),f'线速度源 {v} mm/s / 20px',font=caption,fill='#42566D');c=dict(Name='acceptedTarget',FontId=fid,X=32,Y=i*70+32,Width=456,Height=32,Text=z['txt'],FontColor=0x10243A,HorizontalAlignment='LEFT',VerticalAlignment='TOP');textdraw(speed_im,c,{'WordWrap':False},False,'speed-figure')
sd.text((24,14*70+6),'等价脚本边界值 / 正式字模重绘；非实屏截图。右轴独立输入保持538rpm。',font=caption,fill='#42566D');speed_im.save(E/'P8典型速度与极值.png')
# Five thumb positions and the -1/0/+1 side transition, using verified arithmetic.
s=copy.deepcopy(next(s for s in states if s['Theme']==0 and s['Scenario']=='normal' and s['Page']=='P8_ServoControl'))
strips=Image.new('RGB',(1024,8*112),'white');d=ImageDraw.Draw(strips)
for i,val in enumerate([0,800,1599,1600,1601,2400,3200]):
 z=copy.deepcopy(s);rpm=val-1600;th=24+val*944//3200
 for c in z['Controls']:
  if c['Name']=='rpmSlider':c['Value']=val
  if c['Name']=='sliderSpeedHint':c.update(X=th-204 if rpm>0 else th+44,HorizontalAlignment='RIGHT' if rpm>0 else 'LEFT',Text='设定：'+f'{rpm*math.pi*.185/(60*(8/3))*3.6:.3f}'+'km/h')
 strips.paste(render(z,False).crop((0,290,1024,366)),(0,i*112+30));d.text((24,i*112+4),f'val={val}  rpm={rpm}  thumbLeft={th}  间距=12px',font=caption,fill='#10243A')
d.text((24,7*112+12),'VP原生事件状态 / 正式字模重绘；非实屏截图。',font=caption,fill='#10243A');strips.save(E/'P8滑块五点与跨零.png')
# Property/event diffs, including page events and the explicit slider z-order migration.
diffs=[];events=[]
for p in base['Pages']:
 q=pages[p['Name']];lookup={c['Name']:c for c in q['Controls']}
 for c in p['Controls']:
  z=lookup[c['Name']]
  for k,v in c['Properties'].items():
   if v!=z['Properties'].get(k):diffs.append([p['Name'],c['Name'],c['Id'],z['Id'],k,str(v),str(z['Properties'].get(k))])
 for c in q['Controls']:
  if c['Name'] not in {c['Name'] for c in p['Controls']}:
   diffs.append([p['Name'],c['Name'],'',c['Id'],'APPENDED','',''])
   for b in c['Events']:
    if b['Code']:events.append(dict(Page=p['Name'],Control=c['Name'],EventId=b['Id'],Added=True,AfterSHA256=hashlib.sha256(b['Code'].encode()).hexdigest(),After=b['Code']))
 for old,new in [(p,q)]+[(c,lookup[c['Name']]) for c in p['Controls']]:
  for a,b in zip(old['Events'],new['Events']):
   if (a['Code'] or '')!=(b['Code'] or ''):events.append(dict(Page=p['Name'],Control=old['Name'],EventId=a['Id'],BeforeSHA256=hashlib.sha256((a['Code'] or '').encode()).hexdigest(),AfterSHA256=hashlib.sha256((b['Code'] or '').encode()).hexdigest(),Before=a['Code'],After=b['Code']))
with (E/'30到31控件差异.csv').open('w',encoding='utf-8-sig',newline='') as f:
 w=csv.writer(f);w.writerow(['Page','Control','OldId','NewId','Property','30','31']);w.writerows(diffs)
(E/'30到31事件差异.json').write_text(json.dumps(events,ensure_ascii=False,indent=2),encoding='utf-8')
protected=read('保护基线SHA256.json');changed=[x['File'] for x in protected if not Path(x['File']).exists() or hashlib.sha256(Path(x['File']).read_bytes()).hexdigest().upper()!=x['SHA256']]
result=dict(Evidence='Native VP event states + embedded bitmap resources and PixelFonts; not physical screenshots',States=len(states),TextChecks=len(measurements),MinContrast=min(x['Ratio'] for x in contrasts),Issues=issues,ProtectedFiles=len(protected),ProtectedChanged=changed,Measurements=measurements,Contrasts=contrasts)
(E/'31号布局字模与保护核验.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k not in ['Measurements','Contrasts']},ensure_ascii=True,indent=2))
