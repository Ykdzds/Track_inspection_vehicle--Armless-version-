"""Build HMI31 presentation from the protected, tested HMI30. No control code generation."""
from pathlib import Path
from PIL import Image, ImageDraw
import json,re,math
ROOT=Path(__file__).resolve().parent;BASE=ROOT.parent/'30_轨道巡检车全界面科技风优化版'
OUT=ROOT/'ui31';OUT.mkdir(exist_ok=True);EVENTS=ROOT/'事件31';EVENTS.mkdir(exist_ok=True)
model=json.loads((BASE/'运行核验/30号实施_20261101/30号实际工程读回.json').read_text(encoding='utf-8-sig'))
palettes={
 'Day':dict(bg='#FFFFFF',card='#F1F6FC',edge='#9AAFC4',ink='#10243A',muted='#42566D',accent='#0068A6',good='#146C43',warn='#8A4B00',fault='#B42318',track='#DCE8F3',thumb='#146C43',decor='#E9F0F8',nav='#DCE9F5',navPress='#BCD4E9',primary='#0068A6',primaryPress='#005385',danger='#B42318',dangerPress='#8E1B13',disabled='#D0D9E2'),
 'Night':dict(bg='#0B1426',card='#14243B',edge='#263E59',ink='#EDF5FF',muted='#A9BBD0',accent='#22D3EE',good='#5DE0AE',warn='#FFC766',fault='#FF657A',track='#253D56',thumb='#5DE0AE',decor='#102138',nav='#1C3552',navPress='#285476',primary='#17648D',primaryPress='#1A7195',danger='#AF2940',dangerPress='#D43C52',disabled='#303E51')
}
layout={p['Name']:{c['Name']:{k:c['Properties'][k] for k in ['X','Y','Width','Height'] if k in c['Properties']} for c in p['Controls']} for p in model['Pages']}
updates={}
def patch(pg,n,**kw):
 updates.setdefault(pg,{}).setdefault(n,{}).update(kw)
def rect(pg,n,x,y,w,h,font=None):
 kw=dict(X=x,Y=y,Width=w,Height=h)
 if font:kw['FontSize31']=font
 patch(pg,n,**kw);layout[pg][n]=kw
rect('P1_Main','P1_Title',24,12,344,40)
pg='P8_ServoControl'
for side,x in [('left',32),('right',536)]:
 for field,y,h in [('Actual',112,32),('Target',146,30),('Output',180,30),('Enable',214,28)]:
  rect(pg,side+field,x,y,456,h,24);patch(pg,side+field,WordWrap=False)
 patch(pg,side+'ActualUnits',Visbile=False,Text='')
for n,x in [('commonTarget',32),('acceptedTarget',536)]:
 rect(pg,n,x,252,456,32,24);patch(pg,n,WordWrap=False)
rect(pg,'rpmSlider',24,296,976,48)
for n,x,w in [('sliderScale',32,280),('sliderZero',506,32),('sliderPositive',812,188)]:rect(pg,n,x,344,w,22,20)
for n,x,y,w,h,f in [('authorityStatus',24,370,976,30,24),('stopStatus',24,404,476,30,24),('recoveryStatus',536,404,464,30,24),('recoveryDetail',24,438,976,30,24),('errorStatus',24,472,976,48,24)]:rect(pg,n,x,y,w,h,f)
patch(pg,'controlTitle',Text='伺服控制 / 31')
new=[
 dict(Page='P0_Boot',Name='themeMode',Type='VarCtrl',IsGlobal=True,Int=0),
 *[dict(Page='P0_Boot',Name='theme'+role.title(),Type='VarCtrl',IsGlobal=True,Int=0) for role in ['ink','muted','good','warn','fault']],
 dict(Page='P1_Main',Name='themeToggle',Type='ButtonCtrl',IsGlobal=False,X=384,Y=4,Width=176,Height=52,FontSize31=24,Text='日间 / 切夜间'),
 dict(Page=pg,Name='sliderSpeedHint',Type='TextCtrl',IsGlobal=True,X=540,Y=308,Width=192,Height=24,FontSize31=24,Text='预览：0.000km/h',Enable=False,WordWrap=False)
]
boxes={
 'P0_Boot':[(24,140,1000,312),(24,322,1000,484),(24,496,1000,564)],
 'P1_Main':[(16,104,504,216),(520,104,1008,216),(16,228,504,316),(520,228,1008,316),(16,328,504,508),(520,328,1008,508)],
 'P2_Diag':[(16,64,504,524),(520,64,1008,524)],
 'P3_Position':[(16,80,504,138),(520,80,1008,138),(16,146,1008,298),(16,306,504,460),(520,306,1008,460),(16,466,1008,524)],
 'P4_Info':[(16,74,1008,438),(16,446,1008,524)],
 'P5_Alarm':[(16,80,1008,246),(16,258,1008,398),(16,402,1008,454),(16,466,1008,516)],
 'P6_Sync':[(16,80,1008,136),(16,146,1008,304),(16,320,1008,464),(16,474,1008,520)],
 'P7_System_Ext':[(16,78,1008,132),(16,142,1008,248),(16,258,1008,422),(16,432,1008,496)],
 pg:[(16,110,504,244),(520,110,1008,244),(16,248,504,288),(520,248,1008,288)]
}
sizes={(c['Properties']['Width'],c['Properties']['Height']) for p in model['Pages'] for c in p['Controls'] if c['Type'].endswith(('ButtonCtrl','ToggleButtonCtrl'))}
sizes.add((176,52))
for theme,pal in palettes.items():
 for page in model['Pages']:
  name=page['Name'];im=Image.new('RGB',(1024,600),pal['bg']);d=ImageDraw.Draw(im)
  for x in range(720,1050,38):d.line([(x,0),(x-160,110)],fill=pal['decor'])
  y=128 if name=='P0_Boot' else 58
  d.line((24,y,1000,y),fill=pal['edge']);d.line((24,y,184,y),fill=pal['accent'],width=2)
  for i,box in enumerate(boxes[name]):
   d.rounded_rectangle(box,radius=10,fill=pal['card'],outline=pal['edge'],width=1)
   if i<2:d.rounded_rectangle((box[0]+1,box[1]+14,box[0]+4,box[3]-14),radius=1,fill=pal['accent'])
  
  if name==pg:d.rectangle((24,296,999,343),fill=pal['track'])
  im.save(OUT/f'{theme}_{name}.jpg',quality=94,subsampling=0)
 for kind in ['nav','primary','danger']:
  for state in ['normal','pressed','disabled']:
   col=('#52677C' if theme=='Day' and kind in ['primary','danger'] else pal['disabled']) if state=='disabled' else pal[kind+'Press'] if state=='pressed' else pal[kind]
   for w,h in sorted(sizes):
    im=Image.new('RGB',(w,h),pal['bg']);d=ImageDraw.Draw(im)
    d.rounded_rectangle((0,0,w-1,h-1),radius=8,fill=col,outline=pal['edge'],width=1)
    d.line((12,1,w-13,1),fill=pal['accent'] if state=='pressed' else pal['edge'])
    im.save(OUT/f'{theme}_{kind}_{state}_{w}x{h}.png')
Image.new('RGBA',(976,48),(0,0,0,0)).save(OUT/'sliderTouchTransparent.png')
def argb(c):
 n=int(c[1:],16)|0xff000000
 return n-(1<<32)
def assignment(ctrl,prop,value):return f'{ctrl}.{prop}={value};'
def value_set(theme):
 return '\n'.join(f'P0_Boot.theme{role.title()}.val={argb(palettes[theme][role])};' for role in ['ink','muted','good','warn','fault'])
def theme_code(page):
 name=page['Name'];code=['// UI31_THEME: presentation properties only.']
 for theme,cond in [('Day','P0_Boot.themeMode.val==0'),('Night','P0_Boot.themeMode.val!=0')]:
  pal=palettes[theme];code+=['if('+cond+'){',value_set(theme),f'{name}.bgImg=user.image.{theme}_{name};']
  controls=[dict(c['Properties'],Name=c['Name'],Type=c['Type'].split('.')[-1]) for c in page['Controls']]
  controls += [c for c in new if c['Page']==name and c['Type']!='VarCtrl']
  for c in controls:
   n=c['Name'];ctrl=name+'.'+n;t=c['Type']
   if t=='TextCtrl':
    background=pal['track'] if n=='sliderSpeedHint' else pal['bg'] if c.get('BackgroundColor')==argb(palettes['Night']['bg']) else pal['card']
    code.append(assignment(ctrl,'bgColor',argb(background)))
    # Keep semantic state across theme changes, including untouched cached fields.
    for i,role in enumerate(['good','warn','fault','muted','accent']):
     a=argb(palettes['Day'][role]);b=argb(palettes['Night'][role])
     code.append(('if' if i==0 else 'else if')+f'({ctrl}.fontColor=={a} || {ctrl}.fontColor=={b})'+'{'+assignment(ctrl,'fontColor',argb(pal[role]))+'}')
    code.append('else{'+assignment(ctrl,'fontColor',argb(pal['ink']))+'}')
   elif t in ['ButtonCtrl','ToggleButtonCtrl']:
    kind='danger' if n=='estop' else 'primary' if n in ['servoEntry','authorize','themeToggle'] else 'nav'
    w=c['Width'];h=c['Height']
    for prop,state in [('bgImg','normal'),('pbgImg','pressed'),('dbgImg','disabled')]:code.append(f'{ctrl}.{prop}=user.image.{theme}_{kind}_{state}_{w}x{h};')
    code.append(assignment(ctrl,'fontColor',argb('#FFFFFF' if kind in ['primary','danger'] else pal['ink'])))
   elif t=='SliderCtrl':
    code += [assignment(ctrl,'tbColor',argb(pal['thumb']))]
   elif t=='ProgressBarCtrl':
    code += [assignment(ctrl,'bgColor',argb(pal['track'])),assignment(ctrl,'fgColor',argb(pal['accent']))]
  if name=='P1_Main':code.append(f'P1_Main.themeToggle.txt="'+('日间 / 切夜间' if theme=='Day' else '夜间 / 切日间')+'";')
  code.append('}')
 return '\n'.join(code)+'\n'
for page in model['Pages']:(EVENTS/(page['Name']+'_主题.txt')).write_text(theme_code(page),encoding='utf-8')
toggle='if(P0_Boot.themeMode.val==0){P0_Boot.themeMode.val=1;}else{P0_Boot.themeMode.val=0;}\n'+theme_code(model['Pages'][1])
(EVENTS/'主题切换_弹起.txt').write_text(toggle,encoding='utf-8')
allowed='P8_ServoControl.h13.vals[2]==3 && P8_ServoControl.h13.vals[3]==1 && P8_ServoControl.h13.vals[32]==0 && P8_ServoControl.h13.vals[138]==0 && P8_ServoControl.h13.vals[116]==0'
hint='''// UI31_SLIDER_HINT: never writes motion state.
ui31Slider=P8_ServoControl.rpmSlider.val;
ui31Rpm=ui31Slider-1600;
ui31Thumb=24+ui31Slider*944/3200;
if(ui31Rpm>0){P8_ServoControl.sliderSpeedHint.x=ui31Thumb-204;P8_ServoControl.sliderSpeedHint.txtHorAgn=2;}
else{P8_ServoControl.sliderSpeedHint.x=ui31Thumb+44;P8_ServoControl.sliderSpeedHint.txtHorAgn=0;}
ui31Abs=ui31Rpm;
if(ui31Abs<0){ui31Abs=0-ui31Abs;}
ui31Milli=ui31Abs*13+(ui31Abs*768794+5000000)/10000000;
ui31Whole=ui31Milli/1000;
ui31Frac=ui31Milli-ui31Whole*1000;
P8_ServoControl.sliderSpeedHint.txt="预览：";
if('''+allowed+'''){P8_ServoControl.sliderSpeedHint.txt="设定：";}
if(ui31Rpm<0){P8_ServoControl.sliderSpeedHint.txt=P8_ServoControl.sliderSpeedHint.txt+"-";}
P8_ServoControl.sliderSpeedHint.txt=P8_ServoControl.sliderSpeedHint.txt+intToString(ui31Whole)+".";
if(ui31Frac<100){P8_ServoControl.sliderSpeedHint.txt=P8_ServoControl.sliderSpeedHint.txt+"0";}
if(ui31Frac<10){P8_ServoControl.sliderSpeedHint.txt=P8_ServoControl.sliderSpeedHint.txt+"0";}
P8_ServoControl.sliderSpeedHint.txt=P8_ServoControl.sliderSpeedHint.txt+intToString(ui31Frac)+"km/h";
P8_ServoControl.sliderSpeedHint.fontColor=P0_Boot.themeInk.val;
'''
decl='int ui31Slider;\nint ui31Rpm;\nint ui31Thumb;\nint ui31Abs;\nint ui31Milli;\nint ui31Whole;\nint ui31Frac;\n'
(EVENTS/'滑块提示.txt').write_text(decl+hint,encoding='utf-8')
original=(BASE/'事件/滑块_滑动.txt').read_text(encoding='utf-8-sig')
(ROOT/'事件/滑块_滑动.txt').write_text(decl+original+'\n'+hint,encoding='utf-8')
original=(BASE/'事件/P8_加载.txt').read_text(encoding='utf-8-sig')
(ROOT/'事件/P8_加载.txt').write_text(decl+original+'\n'+theme_code(model['Pages'][8])+hint,encoding='utf-8')
k=math.pi*.185/(60*(8/3))
linear=lambda v:f'floatToString(({v})*{k:.14f},3)+"m/s|"+floatToString(({v})*{k*3.6:.14f},3)+"km/h"'
view=(BASE/'HMI13界面刷新.txt').read_text(encoding='utf-8-sig')
# Drop only the duplicated auxiliary-actual presentation; keep accepted/local target block.
tail=view[view.index('P8_ServoControl.commonTarget.fontColor=',view.index('// UI30_PRESENTATION_BEGIN')):]
view=view[:view.index('// UI30_PRESENTATION_BEGIN')]+tail
for side,label in [('left','左'),('right','右')]:
 old=f'P8_ServoControl.{side}Actual.txt="{label}轴实际："+intToString(uiValue)+" rpm";'
 view=view.replace(old,f'P8_ServoControl.{side}Actual.txt="{label}轴实际："+intToString(uiValue)+"rpm|"+'+linear('uiValue')+';')
 view=view.replace(f'"{label}轴实际：-- rpm"',f'"{label}轴实际：--rpm|--m/s|--km/h"')
 for field,label2 in [('Target','最终目标'),('Output','执行输出')]:
  view=view.replace(f'"{label}轴'+('目标' if field=='Target' else '输出')+'：--"',f'"{label2}：--rpm|--m/s|--km/h"')
def compact(m):
 s=m[0].replace(' rpm\\n','rpm|').replace(' m/s | ','m/s|').replace(' km/h','km/h').replace('等效 ','等效转速')
 return s
view=re.sub(r'P8_ServoControl\.(?:leftActual|rightActual|leftTarget|rightTarget|leftOutput|rightOutput|commonTarget|acceptedTarget)\.txt=[^;]+;',compact,view)
# These old target strings are overwritten by the final local-rpm presentation.
# Avoid transient text/overflow during one refresh; keep all control expressions.
split=view.index('P8_ServoControl.authorityStatus.fontColor=')
view=re.sub(r'^.*P8_ServoControl\.commonTarget\..*$', '', view[:split],flags=re.M)+view[split:]
view=re.sub(r'P8_ServoControl\.commonTarget\.txt="整车目标[^;]+;', '', view)
# Reset the font on every render; linear-source overrides choose the smaller font.
font_reset='\n'.join(f'P8_ServoControl.{c}.font=user.font.UI31_24;' for c in ['leftTarget','rightTarget','leftOutput','rightOutput','acceptedTarget'])+'\n'
start=view.index('P8_ServoControl.linkStatus.fontColor=')
view=view[:start]+font_reset+view[start:]
view=re.sub(r'(P8_ServoControl\.(?:leftTarget|rightTarget|leftOutput|rightOutput|acceptedTarget))\.txt=([^;]*0\.275295[^;]*);',lambda m:m[1]+'.font=user.font.UI31_20;'+m[0],view)
view=decl+view+'\nif(sys_pid==P8_ServoControl.id){\n'+hint+'}\n'
global_source=(BASE/'全局计龄刷新.txt').read_text(encoding='utf-8-sig')
cut=global_source.index('P8_ServoControl.linkStatus.fontColor=')
prefix=global_source[:cut].replace('屏幕工程：30','屏幕工程：31')
view_start=view.index(font_reset)
global_source=decl+prefix+view[view_start:]
roles={argb(palettes['Night'][role]):role for role in ['ink','muted','good','warn','fault']}
def themed(src):
 return re.sub(r'\.fontColor=(-?\d+);',lambda m:'.fontColor=P0_Boot.theme'+roles[int(m[1])].title()+'.val;' if int(m[1]) in roles else m[0],src)
for name,src in [('HMI13界面刷新.txt',view),('全局计龄刷新.txt',global_source)]:
 (ROOT/name).write_text(themed(src).replace('UI30_PRESENTATION_END','UI31_PRESENTATION_END'),encoding='utf-8')
# Static default strings are single line too; preserve names and IDs.
for side,label in [('left','左'),('right','右')]:
 patch(pg,side+'Actual',Text=label+'轴实际：--rpm|--m/s|--km/h')
 for field,label2 in [('Target','最终目标'),('Output','执行输出')]:patch(pg,side+field,Text=label2+'：--rpm|--m/s|--km/h')
patch(pg,'commonTarget',Text='设定：0rpm|0.000m/s|0.000km/h')
patch(pg,'acceptedTarget',Text='主控接受：--rpm|--m/s|--km/h')
manifest=dict(palettes=palettes,updates=updates,add=new,boxes=boxes,mechanics=dict(diameter_m=.185,gear_ratio=8/3,rpm_to_mps=k,rpm_to_kmh=k*3.6))
(OUT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
chars=set((BASE/'ui30/characters.txt').read_text(encoding='utf-8'));chars.update('日夜间切换预览设定')
for file in [ROOT/'HMI13界面刷新.txt',ROOT/'全局计龄刷新.txt',*EVENTS.glob('*.txt')]:
 for s in re.findall(r'"([^"\n]*)"',file.read_text(encoding='utf-8')):chars.update(s.replace('\\n',''))
chars={c for c in chars if c>=' ' and c not in '\r\n'}
(OUT/'characters.txt').write_text(''.join(sorted(chars)),encoding='utf-8')
print('Generated dual themes, nine page events, compact speed displays and slider hint;',len(chars),'characters')
