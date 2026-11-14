"""Deterministic presentation-only assets and VP layout manifest for HMI30."""
from pathlib import Path
from PIL import Image, ImageDraw
import json, math, re

ROOT=Path(__file__).resolve().parent
BASE=ROOT.parent/'29_轨道巡检车跨页运行与伺服快捷导航版'
OUT=ROOT/'ui30'; OUT.mkdir(exist_ok=True)
EVID=ROOT/'运行核验/30号实施_20261101'
BG='#0B1426'; CARD='#14243B'; EDGE='#263E59'; INK='#EDF5FF'; MUTED='#A9BBD0'; CYAN='#22D3EE'
baseline=json.loads((EVID/'baseline-objects.json').read_text(encoding='utf-8-sig'))
manifest={'pages':{},'palette':dict(background=BG,card=CARD,text=INK,muted=MUTED,accent=CYAN),'mechanics':{'diameter_m':.185,'gear_num':8,'gear_den':3,'sign':[1,1]}}

def panel(d, box, accent=False):
    d.rounded_rectangle(box, radius=10, fill=CARD, outline=EDGE, width=1)
    if accent: d.rounded_rectangle((box[0]+1,box[1]+14,box[0]+4,box[3]-14),radius=1,fill=CYAN)

for p in baseline:
    name=p['Page']; controls={}
    manifest['pages'][name]={'id':p['Id'],'controls':controls,'add':[]}
    for c in p['Elements']:
        if c['X'] is not None:
            controls[c['Name']]={'x':c['X'],'y':c['Y'],'w':c['Width'],'h':c['Height'],'font':24}

def place(pg,n,x,y,w,h,font=24,**kw):
    manifest['pages'][pg]['controls'][n]=dict(x=x,y=y,w=w,h=h,font=font,**kw)

def add(pg,n,text,x,y,w,h,font=20,**kw):
    manifest['pages'][pg]['add'].append(dict(name=n,text=text,x=x,y=y,w=w,h=h,font=font,**kw))

titles={'P0_Boot':'1435 轨道巡检车','P1_Main':'车辆运行总览','P2_Diag':'伺服与通信诊断','P3_Position':'位置与里程','P4_Info':'系统信息','P5_Alarm':'告警与维护','P6_Sync':'双轴同步诊断','P7_System_Ext':'整机扩展状态','P8_ServoControl':'伺服控制 / 30'}
title_names=['text0','P1_Title','diagTitle','positionTitle','infoTitle','alarmTitle','syncTitle','extensionTitle','controlTitle']
for i,(pg,title) in enumerate(titles.items()):
    place(pg,title_names[i],24,12,680,40,32,text=title,background=BG)
    if i not in [0,1,8]:
        nav=[c for c in baseline[i]['Elements'] if c['Type'].endswith('ButtonCtrl') and c['Name']!='servoEntry']
        for j,c in enumerate(nav): place(pg,c['Name'],16+j*168,532,152,52)
        place(pg,'servoEntry',856,532,152,52)

# P0: the existing progress data and boot navigation remain authoritative.
pg='P0_Boot'
place(pg,'text0',40,40,660,48,36,text=titles[pg],background=BG)
place(pg,'text1',42,94,600,30,24,background=BG)
place(pg,'swVer',734,52,250,32,24,background=BG)
for n,y,f in [('mainState',156,32),('stageText',204,24),('progressText',242,20)]: place(pg,n,48,y,928,40 if f==32 else 30,f)
place(pg,'bootProgress',48,282,928,16,20)
for n,x,y in [('ctrlStatus',48,336),('leftStatus',536,336),('rightStatus',48,388),('safeStatus',536,388),('resetReason',48,440),('protoVer',536,440)]: place(pg,n,x,y,440,36,24)
place(pg,'faultText',48,512,928,36,24)

# P1: large vehicle metrics, paired axis cards, then grouped status rows.
pg='P1_Main';place(pg,'P1_Title',24,12,480,40,32,background=BG)
place(pg,'servoEntry',576,12,176,44,24)
place(pg,'state',776,16,224,36,24,background=BG)
place(pg,'ctrlComm',24,64,652,30,20,background=BG)
place(pg,'safe',696,64,304,30,20,background=BG)
for n,x,y,f in [('vehicleSpeed',32,118,32),('targetSpeed',536,118,32),('leftSpeed',32,240,24),('rightSpeed',536,240,24)]:
    place(pg,n,x,y,456,40,f)
    add(pg,n+'Units','-- km/h | 等效转速 -- rpm',x,y+44,456,30,20,global_=True)
for n,x,y in [('leftOnline',32,336),('rightOnline',536,336),('leftFault',32,368),('rightFault',536,368),('currentMode',32,400),('controlSource',536,400),('controlAuthority',32,432),('stopState',536,432),('axisEnableSummary',32,464),('alarmSummary',536,464)]: place(pg,n,x,y,456,30,20)
for j,n in enumerate(['openDiag','openPosition','openInfo','openAlarm','openSync','openExtension']):place(pg,n,16+j*168,532,152,52)

# P2: all 26 diagnostic values, including the original alternating detail rows.
pg='P2_Diag';place(pg,'diagReadonly',792,22,208,28,20,background=BG)
add(pg,'leftHeading','左轴 / AXIS 01',32,70,440,28,24,color=CYAN)
add(pg,'rightHeading','右轴 / AXIS 02',536,70,440,28,24,color=CYAN)
rows=[('leftState','rightState'),('diagLeftFault','diagRightFault'),('diagAxis1Online','diagAxis2Online'),('diagAxis1Enable','diagAxis2Enable'),('diagAxis1SpeedValid','diagAxis2SpeedValid'),('diagAxis1SpeedAge','diagAxis2SpeedAge'),('diagAxis1FaultValid','diagAxis2FaultValid'),('diagAxis1Zero','diagAxis2Zero'),('diagModeVerified','diagSafetyLocked'),('diagAxis1CommFail','diagAxis2CommFail'),('reqCount','respCount'),('retryCount','timeoutCount'),('crcCount','frameCount')]
for j,pair in enumerate(rows):
    for side,n in enumerate(pair):place(pg,n,32+504*side,104+j*32,456,28,20)

pg='P3_Position'
for n,x,y,w in [('axis1Position',32,94,456),('axis2Position',536,94,456),('startupDisplacement',32,154,960),('startupTravel',32,204,960),('segmentTravel',32,254,960),('axisPositionDiff',32,314,456),('positionValid',536,314,456),('mileageValid',32,350,456),('positionIncomplete',536,350,456),('positionBaseline',32,386,456),('positionSample',536,386,456),('positionAge',32,422,456),('lostInterval',536,422,456)]:place(pg,n,x,y,w,34,24 if y<300 else 20)
place(pg,'mileageNote',32,472,960,24,20)
place(pg,'mileageNoteTail',32,496,960,24,20)

pg='P4_Info'
for j,n in enumerate(['ctrlVersion','screenVersion','protocolVersion','resetCode','diagLow','diagHigh','heartbeat','minStack']):place(pg,n,32,86+j*44,960,36,24)
add(pg,'mechanicalNote','速度换算：轮径 0.185 m | 总传动比 8/3 | 方向 +1 / +1\n线速度由轮速换算，非独立地面测速',32,454,960,64,20)

pg='P5_Alarm'
for n,x,y,w,f in [('alarmLevel',32,94,456,32),('alarmCode',536,98,456,24),('alarmSource',32,154,456,24),('alarmActive',536,154,456,24),('alarmEmergency',32,204,456,24),('alarmStopResult',536,204,456,24),('alarmSequence',32,268,960,24),('alarmEventTime',32,312,960,24),('alarmExtra',32,356,960,24),('alarmFaultSummary',32,408,960,24),('alarmHistory',32,476,960,20)]:place(pg,n,x,y,w,40 if f==32 else 34,f)

pg='P6_Sync'
for n,x,y,w,f in [('syncMacro',32,92,456,24),('syncState',536,92,456,24),('syncActualDiff',32,156,960,32),('syncEffectiveError',32,212,960,24),('syncCompensation',32,260,960,24),('syncPairValid',32,330,456,24),('syncRejectReason',536,330,456,24),('syncSkew',32,376,960,24),('syncAxis1Age',32,422,456,24),('syncAxis2Age',536,422,456,24),('syncReadonly',32,480,960,20)]:place(pg,n,x,y,w,40 if f==32 else 34,f)

pg='P7_System_Ext'
rows=[('batteryState','batteryPercent'),('positioningState','positioningAccuracy'),('positioningX','positioningY'),('armState','lightingState'),('cameraState','acquisitionState'),('storageState','extensionComm'),('extensionFault','extensionReadonly')]
for j,pair in enumerate(rows):
    for side,n in enumerate(pair):place(pg,n,32+504*side,92+j*58,456,40,24 if j<6 else 20)

pg='P8_ServoControl';place(pg,'controlTitle',24,8,976,40,32,background=BG)
place(pg,'linkStatus',24,50,976,28,20,background=BG)
place(pg,'ownerStatus',24,82,460,28,20,background=BG)
place(pg,'axisMode',536,82,464,28,20,background=BG)
for side,prefix in enumerate(['left','right']):
    x=32+504*side
    place(pg,prefix+'Actual',x,120,456,40,32)
    add(pg,prefix+'ActualUnits','-- m/s | -- km/h',x,160,456,26,20,global_=True)
    place(pg,prefix+'Target',x,190,456,44,20)
    place(pg,prefix+'Output',x,234,456,44,20)
    place(pg,prefix+'Enable',x,278,456,26,20)
place(pg,'commonTarget',32,316,456,48,20)
add(pg,'acceptedTarget','主控接受：-- rpm\n-- m/s | -- km/h',536,316,456,48,20,global_=True)
place(pg,'rpmSlider',24,368,976,48)
add(pg,'sliderScale','负向 -1600 rpm                         0                         正向 +1600 rpm',32,418,960,22,20,background=BG,color=MUTED)
# Dense but complete feedback area, with full-width multiline error details.
place(pg,'authorityStatus',24,444,976,24,20,background=BG)
place(pg,'stopStatus',24,470,476,24,20,background=BG)
place(pg,'recoveryStatus',536,470,464,24,20,background=BG)
place(pg,'recoveryDetail',24,496,976,24,20,background=BG)
# Keep the diagnostic text complete on a separate two-line region above slider.
# At 1024x600 the status and interaction areas need a tighter axis section.
for prefix in ['left','right']:
    for n,dy in [('Actual',0),('Target',-4),('Output',-12),('Enable',-20)]:manifest['pages'][pg]['controls'][prefix+n]['y']+=dy
place(pg,'commonTarget',32,290,456,48,20)
manifest['pages'][pg]['add'][-2]['y']=290  # acceptedTarget
for side,prefix in enumerate(['left','right']):
    x=32+504*side
    place(pg,prefix+'Actual',x,112,456,40,32)
    next(c for c in manifest['pages'][pg]['add'] if c['name']==prefix+'ActualUnits').update(y=152,h=22)
    place(pg,prefix+'Target',x,174,456,44,20)
    place(pg,prefix+'Output',x,218,456,44,20)
    place(pg,prefix+'Enable',x,262,456,24,20)
place(pg,'commonTarget',32,286,456,44,20)
next(c for c in manifest['pages'][pg]['add'] if c['name']=='acceptedTarget').update(y=286,h=44)
place(pg,'rpmSlider',24,332,976,48)
next(c for c in manifest['pages'][pg]['add'] if c['name']=='sliderScale').update(y=380)
next(c for c in manifest['pages'][pg]['add'] if c['name']=='sliderScale').update(text='负向 -1600 rpm',w=280)
add(pg,'sliderZero','0',506,380,32,22,20,background=BG,color=MUTED)
add(pg,'sliderPositive','正向 +1600 rpm',812,380,188,22,20,background=BG,color=MUTED)
place(pg,'authorityStatus',24,404,976,22,20,background=BG)
place(pg,'stopStatus',24,428,476,22,20,background=BG)
place(pg,'recoveryStatus',536,428,464,22,20,background=BG)
place(pg,'recoveryDetail',24,452,976,22,20,background=BG)
place(pg,'errorStatus',24,476,976,44,20,background=BG)
place(pg,'safetyNote',24,520,976,22,20,background=BG,color=MUTED)
for j,n in enumerate(['authorize','estop','recoveryButton','backHome']):place(pg,n,16+j*252,544,236,52,24)

for i,(pg,title) in enumerate(titles.items()):
    manifest['pages'][pg]['controls'][title_names[i]]['text']=title
for prefix in ['left','right']:
    for field,label in [('Target','最终目标：'),('Output','执行输出：')]:
        manifest['pages']['P8_ServoControl']['controls'][prefix+field]['text']=label+'-- rpm\n-- m/s | -- km/h'
manifest['pages']['P8_ServoControl']['controls']['commonTarget']['text']='设定：0 rpm\n0.000 m/s | 0.000 km/h'

# Static raster assets are generated from geometry, with all display text native.
for pg,data in manifest['pages'].items():
    im=Image.new('RGB',(1024,600),BG);d=ImageDraw.Draw(im)
    for x in range(720,1050,38):d.line([(x,0),(x-160,110)],fill='#102138',width=1)
    header_y=128 if pg=='P0_Boot' else 58
    d.line((24,header_y,1000,header_y), fill=EDGE,width=1)
    d.line((24,header_y,184,header_y),fill=CYAN,width=2)
    boxes={
      'P0_Boot':[(24,140,1000,312),(24,322,1000,484),(24,496,1000,564)],
      'P1_Main':[(16,104,504,216),(520,104,1008,216),(16,228,504,316),(520,228,1008,316),(16,328,504,508),(520,328,1008,508)],
      'P2_Diag':[(16,64,504,524),(520,64,1008,524)],
      'P3_Position':[(16,80,504,138),(520,80,1008,138),(16,146,1008,298),(16,306,504,460),(520,306,1008,460),(16,466,1008,524)],
      'P4_Info':[(16,74,1008,438),(16,446,1008,524)],
      'P5_Alarm':[(16,80,1008,246),(16,258,1008,398),(16,402,1008,454),(16,466,1008,516)],
      'P6_Sync':[(16,80,1008,136),(16,146,1008,304),(16,320,1008,464),(16,474,1008,520)],
      'P7_System_Ext':[(16,78,1008,132),(16,142,1008,248),(16,258,1008,422),(16,432,1008,496)],
      'P8_ServoControl':[(16,110,504,286),(520,110,1008,286),(16,286,504,330),(520,286,1008,330)]
    }[pg]
    for k,b in enumerate(boxes):panel(d,b,k<2)
    for c in data['controls'].values():
        if c.get('background')==BG:continue
    im.save(OUT/(pg+'.jpg'),quality=94,subsampling=0)

# Reusable button skins preserve native button event handling and text.
for kind,normal,pressed in [('nav','#1C3552','#285476'),('primary','#17648D','#2188B5'),('danger','#AF2940','#D43C52')]:
    for state,col in [('normal',normal),('pressed',pressed),('disabled','#303E51')]:
        for width,height in [(152,52),(176,44),(236,52)]:
            im=Image.new('RGB',(width,height),BG);d=ImageDraw.Draw(im)
            d.rounded_rectangle((0,0,width-1,height-1),radius=8,fill=col,outline='#4C6785' if state!='disabled' else '#425166',width=1)
            d.line((12,1,width-13,1),fill='#6EA6C4' if state=='pressed' else '#355C7C')
            im.save(OUT/f'{kind}_{state}_{width}x{height}.png')

# Replace only displayed strings. No changes to command/session/state machines.
k=math.pi*.185/(60*(8/3))
speed_meta={'rpm_to_mps':k,'rpm_to_kmh':k*3.6,'mmps_to_equivalent_rpm':.001/k}
(OUT/'speed-units.json').write_text(json.dumps(speed_meta,indent=2),encoding='utf-8')

def linear(expr):return f'floatToString(({expr})*{k:.14f},3)+" m/s | "+floatToString(({expr})*{k*3.6:.14f},3)+" km/h"'
def rpmtext(label,expr):return f'"{label}"+intToString({expr})+" rpm\\n"+{linear(expr)}'
view=(BASE/'HMI13界面刷新.txt').read_text(encoding='utf-8-sig')
# Actual rpm stays large; its secondary units are derived only when original data is valid.
for prefix in ['left','right']:
    for field,label in [('Target','最终目标：'),('Output','执行输出：')]:
        old=f'"{label}"+intToString(uiValue)+" rpm"'
        # Both axis branches use the same presentation expression.
        view=view.replace(old,rpmtext(label,'uiValue'))
# Convert the original linear-target overrides without changing their predicates.
view=re.sub(r'"(整车目标：|最终目标：|执行输出：)"\+intToString\(uiValue\)\+" mm/s"',lambda m: '"'+m[1]+'等效转速 "+floatToString(uiValue*'+f'{.001/k:.12f}'+',1)+" rpm\\n"+floatToString(uiValue*0.001,3)+" m/s | "+floatToString(uiValue*0.0036,3)+" km/h"',view)
view=view.replace('"左轴实际：--"','"左轴实际：-- rpm"').replace('"右轴实际：--"','"右轴实际：-- rpm"')

tail=['// UI30_PRESENTATION_BEGIN: read-only units; control properties are untouched.']
for side,prefix in enumerate(['left','right']):
    tail += [f'P8_ServoControl.{prefix}ActualUnits.txt="-- m/s | -- km/h";',f'if(P8_ServoControl.h13.vals[86]==1 && P8_ServoControl.h13.vals[1]-P8_ServoControl.h13.vals[21]<300 && P8_ServoControl.h13.vals[1]-P8_ServoControl.h13.vals[73]<300 && (P8_ServoControl.h13Status.vals[9]&{1<<side})!=0 && (P8_ServoControl.h13Status.vals[25]&{1<<side})!=0 && P8_ServoControl.h13Status.vals[{46+side}]<500 && P8_ServoControl.h13Status.vals[{23+side}]!=32767){{',f' uiValue=P8_ServoControl.h13Status.vals[{23+side}];if(uiValue>=32768){{uiValue=uiValue-65536;}}',f' P8_ServoControl.{prefix}ActualUnits.txt='+linear('uiValue')+';','}']
tail += ['P8_ServoControl.commonTarget.txt='+rpmtext('设定：','P8_ServoControl.h13.vals[10]')+';','P8_ServoControl.acceptedTarget.txt="主控接受：-- rpm\\n-- m/s | -- km/h";', 'if(P8_ServoControl.h13.vals[86]==1 && P8_ServoControl.h13.vals[1]-P8_ServoControl.h13.vals[21]<300 && P8_ServoControl.h13.vals[1]-P8_ServoControl.h13.vals[73]<300){',' if(P8_ServoControl.h13Status.vals[18]!=32767){uiValue=P8_ServoControl.h13Status.vals[18];if(uiValue>=32768){uiValue=uiValue-65536;}P8_ServoControl.acceptedTarget.txt='+rpmtext('主控接受：','uiValue')+';}',' if(P8_ServoControl.h13Status.vals[59]==1 && P8_ServoControl.h13Status.vals[8]!=1){uiValue=P8_ServoControl.h13Status.vals[58];if(uiValue>=32768){uiValue=uiValue-65536;}P8_ServoControl.acceptedTarget.txt="整车目标：等效 "+floatToString(uiValue*'+f'{.001/k:.12f}'+',1)+" rpm\\n"+floatToString(uiValue*0.001,3)+" m/s | "+floatToString(uiValue*0.0036,3)+" km/h";}','}']
tail += ['// UI30_PRESENTATION_END']
view += '\n'+'\n'.join(tail)+'\n'
(ROOT/'HMI13界面刷新.txt').write_text(view,encoding='utf-8')
g=(BASE/'全局计龄刷新.txt').read_text(encoding='utf-8-sig')
start=g.index('P8_ServoControl.linkStatus.txt=')
g=g[:start]+view[view.index('P8_ServoControl.linkStatus.txt='):]
g=g.replace('屏幕工程：29','屏幕工程：30')
# Main secondary values inherit exactly the validity path of the existing field.
for field in ['leftSpeed','rightSpeed','vehicleSpeed','targetSpeed']:
    g=g.replace(f'P1_Main.{field}.txt="',f'P1_Main.{field}Units.txt="-- km/h | 等效转速 -- rpm";P1_Main.{field}.txt="')
    pattern=rf'(P1_Main\.{field}\.txt="[^"]*"\+floatToString\(speedValue\*0\.001,3\)\+" m/s";)'
    g=re.sub(pattern,lambda m:m[1]+f'P1_Main.{field}Units.txt=floatToString(speedValue*0.0036,3)+" km/h | 等效转速 "+floatToString(speedValue*{.001/k:.12f},1)+" rpm";',g)
(ROOT/'全局计龄刷新.txt').write_text(g,encoding='utf-8')

# Neutral defaults; apply a small set of exact existing status strings, not new validity rules.
colors={'正常':'#5DE0AE','故障':'#FF657A','通信超时':'#FF657A','断开':'#FF657A','警告':'#FFC766','紧急':'#FF657A','低电':'#FFC766','受限':'#FFC766','激活':'#5DE0AE','STOPPING':'#FFC766','RUNNING':'#5DE0AE','FAULT':'#FF657A','OFFLINE':'#A9BBD0'}
def colornum(s):
    v=int(s[1:],16)|0xff000000
    return v-(1<<32)
def tint(src):
    # An assignment always resets the color, so old colors never leak into new states.
    def replace(m):
        control,label=m[1],m[2]
        color=MUTED if any(x in label for x in ['未知','过期','未配置','未接入','无效','等待']) else INK
        for suffix,c in colors.items():
            if label==suffix or label.endswith('：'+suffix):color=c
        if label.startswith(('拒绝：','失败：','历史首错：','故障锁定','停车失败','恢复失败','急停状态：触发')):color='#FF657A'
        if label.startswith(('正在停车','全局停车中','停车：','换向：','申请中','恢复中')):color='#FFC766'
        if label.startswith(('已授权：','已停车','停车完成','恢复完成','可恢复')):color='#5DE0AE'
        if label in ['告警等级：无','错误：无','告警状态：不活跃','急停状态：未触发']:color='#5DE0AE'
        return f'{control}.fontColor={colornum(color)};'+m[0]
    return re.sub(r'(P[0-8]_[A-Za-z_]+\.[A-Za-z0-9_]+)\.txt="([^"\n]*)"[^;]*;',replace,src)
# Keep the P8 display section byte-identical between the two source files.
view=tint(view);g=tint(g)
(ROOT/'HMI13界面刷新.txt').write_text(view,encoding='utf-8')
(ROOT/'全局计龄刷新.txt').write_text(g,encoding='utf-8')

# All static and dynamic text stays editable in the VP project.
(OUT/'layout.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
chars=set(chr(i) for i in range(32,127))
for p in baseline:
    for c in p['Elements']:chars.update(c.get('Text') or '')
for file in [*ROOT.glob('*.txt'),* (ROOT/'事件').glob('*.txt')]:
    for s in re.findall(r'"([^"\n]*)"',file.read_text(encoding='utf-8-sig')):chars.update(s.replace('\\n',''))
for pg in json.loads((EVID/'29号完整模型基线.json').read_text(encoding='utf-8-sig'))['Pages']:
    for obj in [pg,*pg['Controls']]:
        for e in obj['Events']:
            for s in re.findall(r'"([^"\n]*)"',e['Code'] or ''):chars.update(s.replace('\\n',''))
chars.update(json.dumps(manifest,ensure_ascii=False)); chars.update('负向正向零点等效转速主控接受轮径总传动比线速度由轮速换算非独立地面测速')
chars={c for c in chars if c>=' ' and c not in '\r\n' and (ord(c)<127 or '\u4e00'<=c<='\u9fff' or c in '，。：；、（）±×—→')}
(OUT/'characters.txt').write_text(''.join(sorted(chars)),encoding='utf-8')
print('UI30 manifest, nine backgrounds, button skins, display scripts and',len(chars),'font glyphs generated')
