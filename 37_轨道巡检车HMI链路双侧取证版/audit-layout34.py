"""Audit and redraw native VP states with embedded resources and 4-bit PixelFonts.
Images are deterministic reconstructions, not physical screen screenshots.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import json,base64,csv,hashlib,copy,math
R=Path(__file__).resolve().parent;E=R/'运行核验/34号实施_20261103';B=R/'运行核验/31号实施_20261101';O=E/'日夜状态图';O.mkdir(exist_ok=True)
def read(n):return json.loads((E/n).read_text(encoding='utf-8-sig'))
m=read('34号实际工程读回.json');states=read('34号VP原生显示状态.json')
fonts={f['Id']:f for f in read('34号实际字模.json')};glyphs={k:{g['Char']:g for g in v['Glyphs']} for k,v in fonts.items()}
images={i['Id']:next((B/'资源读回').glob(i['Name']+'.*'),None) for i in m['Images']}
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
 im=Image.open(images[s['ImageId']]).convert('RGB');pg=pages[s['Page']];cs={c['Name']:c for c in pg['Controls']};tag=f"{s['Theme']}/{s['Page']}/{s.get('Scenario','normal')}"
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

for s in states:render(s).save(O/f"{s['Page']}_{s['Theme']}_normal.png")
overview=Image.new('RGB',(1536,3*636),'#18283b');d=ImageDraw.Draw(overview)
for i,p in enumerate(m['Pages']):
 for t in [0,1]:overview.paste(Image.open(O/f"{p['Name']}_{t}_normal.png").resize((512,300)),((i%3)*512,(i//3)*636+36+t*300))
 d.text(((i%3)*512+8,(i//3)*636+7),p['Name']+' 上日间/下夜间',font=caption,fill='white')
overview.save(E/'34号九页日夜总览.png')
result=dict(Evidence='Saved PIX native event states + embedded bitmap resources; not physical screenshots',States=len(states),TextChecks=len(measurements),MinContrast=min(x['Ratio'] for x in contrasts),Issues=issues,Measurements=measurements,Contrasts=contrasts)
(E/'34号布局字模核验.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k not in ['Measurements','Contrasts']},ensure_ascii=True,indent=2))
