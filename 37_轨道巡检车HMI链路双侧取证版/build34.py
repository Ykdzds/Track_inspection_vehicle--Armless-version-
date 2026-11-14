"""Generate HMI34 only. Protected HMI31/32/33 sources are read-only inputs."""
from pathlib import Path
import json,re
R=Path(__file__).resolve().parent
assert R.name=='34_轨道巡检车跨页运行与触控响应修复版'
E=R/'运行核验/34号实施_20261103';E.mkdir(parents=True,exist_ok=True)
(R/'事件34').mkdir(exist_ok=True)
base=(R.parent/'33_轨道巡检车调度与会话稳定修复版/build33.py').read_text(encoding='utf-8-sig')
base=base.replace('33号实施_20261102','34号实施_20261103').replace('33号','34号').replace('HMI33','HMI34').replace('事件33','事件34').replace('屏幕工程：33','屏幕工程：34')
exec(compile(base,'protected-build33-adapter','exec'),{'__file__':str(R/'build34.py')})
H='P8_ServoControl.h13.vals';S='P8_ServoControl.h13Status.vals';Q='P0_Boot.s33.vals';U='P0_Boot.u34.vals'
def sub(s):return s.replace('@H',H).replace('@S',S).replace('@Q',Q).replace('@U',U)
def read(n):return (R/n).read_text(encoding='utf-8-sig')
def put(n,s):(R/n).write_text(s,encoding='utf-8-sig')
names=['observedSlider','candidateRpm','candidateGeneration','candidateValid','candidateBootHi','candidateBootLo','candidateSessionHi','candidateSessionLo','sentRpm','sentGeneration','sentSequenceHi','sentSequenceLo','sentBootHi','sentBootLo','sentSessionHi','sentSessionLo','zeroConfirmed','sentValid','acceptedRpm','acceptedGeneration','acceptedValid','sessionHi','sessionLo','bootHi','bootLo','activeBaseline','stopLatch','stopTransportAck','stopSequenceHi','stopSequenceLo','stopBootHi','stopBootLo','stopGeneration','stopSent','stopTouchDown','stopDone','hintDirty','candidatePending','inputArmed']
names+=['hintSeen','hintSlider','hintMode','hintTheme']
(E/'34号输入索引.json').write_text(json.dumps({'length':len(names),'indices':dict(zip(names,range(len(names))))},ensure_ascii=False,indent=2),encoding='utf8')
reset=sub('''@U[0]=P8_ServoControl.rpmSlider.val;@U[3]=0;@U[17]=0;@U[16]=0;@U[20]=0;@U[37]=0;@U[36]=1;@U[25]=0;
if(@H[2]==3){@U[21]=@H[6];@U[22]=@H[7];@U[23]=@H[4];@U[24]=@H[5];@U[25]=1;}
''')
input_code=sub('''int in34;
in34=P8_ServoControl.rpmSlider.val;
if(@H[2]!=3 || @H[11]==3 || @H[11]==16 || @H[116]==1 || @Q[0]!=@Q[1] || @U[26]==1){
 @U[0]=in34;@U[3]=0;@U[17]=0;@U[20]=0;@U[37]=0;@U[25]=0;@U[16]=0;
}else{
 if(@U[25]==0 || @U[21]!=@H[6] || @U[22]!=@H[7] || @U[23]!=@H[4] || @U[24]!=@H[5]){
  @U[0]=in34;@U[3]=0;@U[17]=0;@U[20]=0;@U[37]=0;@U[16]=0;@U[21]=@H[6];@U[22]=@H[7];@U[23]=@H[4];@U[24]=@H[5];@U[25]=1;
 }
 if(in34!=@U[0]){
  @U[0]=in34;
  if(sys_pid==P8_ServoControl.id && @U[38]==1 && @H[32]==0 && in34>=0 && in34<=3200){
   @U[1]=in34-1600;@U[2]=@U[2]+1;@U[3]=1;@U[4]=@H[4];@U[5]=@H[5];@U[6]=@H[6];@U[7]=@H[7];@U[37]=1;@U[36]=1;@Q[42]=@Q[42]+1;
  }
 }
 if(@U[3]==1 && @U[4]==@H[4] && @U[5]==@H[5] && @U[6]==@H[6] && @U[7]==@H[7] && @U[16]==1 && @H[138]==0){@H[10]=@U[1];}
}
''')
hint=sub('''int hv34;int hp34;int hx34;int ha34;int hm34;int hw34;int hf34;int mode34;
if(sys_pid==P8_ServoControl.id){
 hv34=P8_ServoControl.rpmSlider.val;if(hv34<0){hv34=0;}if(hv34>3200){hv34=3200;}
 mode34=0;if(@H[2]==3 && @U[26]==0 && @H[116]==0 && @Q[0]==@Q[1] && @U[20]==1 && @U[18]==hv34-1600 && @U[19]==@U[2]){mode34=1;}
 if(@U[39]==0 || @U[40]!=hv34 || @U[41]!=mode34 || @U[42]!=P0_Boot.themeMode.val){
 hp34=hv34-1600;hx34=24+hv34*944/3200;ha34=0;
 if(hp34>0){hx34=hx34-204;ha34=2;}else{hx34=hx34+44;}
 if(P8_ServoControl.sliderSpeedHint.x!=hx34){P8_ServoControl.sliderSpeedHint.x=hx34;}
 if(P8_ServoControl.sliderSpeedHint.txtHorAgn!=ha34){P8_ServoControl.sliderSpeedHint.txtHorAgn=ha34;}
 hm34=hp34;if(hm34<0){hm34=0-hm34;}hm34=hm34*13+(hm34*768794+5000000)/10000000;
 hw34=hm34/1000;hf34=hm34-hw34*1000;P0_Boot.displayScratch.txt="预览：";
 if(mode34==1){P0_Boot.displayScratch.txt="设定：";}
 if(hp34<0){P0_Boot.displayScratch.txt=P0_Boot.displayScratch.txt+"-";}
 P0_Boot.displayScratch.txt=P0_Boot.displayScratch.txt+intToString(hw34)+".";
 if(hf34<100){P0_Boot.displayScratch.txt=P0_Boot.displayScratch.txt+"0";}if(hf34<10){P0_Boot.displayScratch.txt=P0_Boot.displayScratch.txt+"0";}
 P0_Boot.displayScratch.txt=P0_Boot.displayScratch.txt+intToString(hf34)+"km/h";
 if(P8_ServoControl.sliderSpeedHint.txt!=P0_Boot.displayScratch.txt){P8_ServoControl.sliderSpeedHint.txt=P0_Boot.displayScratch.txt;}
 if(P8_ServoControl.sliderSpeedHint.fontColor!=P0_Boot.themeInk.val){P8_ServoControl.sliderSpeedHint.fontColor=P0_Boot.themeInk.val;}
 @U[36]=0;@U[39]=1;@U[40]=hv34;@U[41]=mode34;@U[42]=P0_Boot.themeMode.val;
 }
}
''')
ack=sub('''
if(@Q[4]==1 && @Q[34]==1 && @H[2]==3 && @U[26]==0 && @H[116]==0 && @U[17]==1 && @U[12]==@H[4] && @U[13]==@H[5] && @U[14]==@S[12] && @U[15]==@S[13] && (@S[16]>@U[10] || (@S[16]==@U[10] && @S[17]>=@U[11]))){
 v33=@S[18];if(v33>32767){v33=v33-65536;}
 if(v33==@U[8]){
  @U[18]=v33;@U[19]=@U[9];@U[20]=1;@U[17]=0;@U[36]=1;
  if(v33==0 && @H[138]==1){@H[138]=0;@U[16]=1;}
  if(@U[2]==@U[9] && @U[1]==v33){@U[37]=0;}
 }
}
''')
s=read('HMI34事务服务.txt')
s=s.replace(sub('@H[116]==0 && @H[2]!=2 && @H[2]!=3)'),sub('@H[116]==0 && @H[2]!=1 && @H[2]!=5 && @H[2]!=2 && @H[2]!=3)'))
s=s.replace('P8_ServoControl.rpmSlider.val=1600;', 'P8_ServoControl.rpmSlider.val=1600;'+reset)
needle=sub('}else{@Q[34]=0;@H[86]=0;}')
s=s.replace(needle,needle+ack+'\nHMI34Input();\n')
s=s.replace(sub('if(@Q[0]!=@Q[1] || @H[116]==1)'),sub('if(@Q[0]!=@Q[1] || @H[116]==1 || (@U[26]==1 && @U[27]==0))'))
write_gate=sub('if(cmd33!=0 && @H[74]==1 && @Q[44]==0){')
preempt=sub('''if(cmd33==33 && (@U[26]==1 || @Q[0]!=@Q[1] || @H[116]==1 || @H[11]==3 || @H[11]==16)){cmd33=0;@H[10]=0;}
if(cmd33==16 && (@U[26]==0 || @U[32]!=@Q[0])){@U[26]=1;@U[27]=0;@U[33]=0;@U[32]=@Q[0];@U[35]=0;}
''')
s=s.replace(write_gate,preempt+write_gate)
increment=sub('@H[9]=@H[9]+1;if(@H[9]>65535){@H[9]=0;@H[8]=@H[8]+1;}')
s=s.replace(increment,sub('if(cmd33!=16 || @U[33]==0){')+increment+sub('}else{@H[8]=@U[28];@H[9]=@U[29];}'))
s=s.replace('if(cmd33==33){v33=',sub('if(cmd33==33){if(@H[138]==1){@H[10]=0;}@U[8]=@H[10];@U[9]=@U[2];@U[10]=@H[8];@U[11]=@H[9];@U[12]=@H[4];@U[13]=@H[5];@U[14]=@H[6];@U[15]=@H[7];@U[17]=1;v33='))
s=s.replace('if(cmd33==16 || cmd33==3){','if((cmd33==16 && '+U+'[33]==0) || cmd33==3){')
send=sub('''if(cmd33==16 && @U[33]==0){@U[28]=@H[8];@U[29]=@H[9];@U[30]=@H[4];@U[31]=@H[5];@U[33]=1;}
''')
s=s.replace('r33=modbusWrites(',send+'r33=modbusWrites(')
s=s.replace(sub('if(cmd33==16 && @Q[0]==@Q[43]){@Q[1]=@Q[43];@H[116]=0;}'),sub('if(cmd33==16 && @Q[0]==@Q[43]){@Q[1]=@Q[43];@H[116]=0;@U[27]=1;}'))
s=s.replace(sub('if(cmd33==33){@H[138]=0;}'),'// HMI34: application status, not transport ACK, confirms a target.')
s=s.replace(sub('@H[83]=1;@H[77]=@H[77]+1;}'),sub('@H[83]=1;@H[77]=@H[77]+1;if(cmd33==16){@H[2]=4;}}'))
s=s.replace(sub('@H[74]=1;@H[8]=0;'),sub('@H[74]=1;@U[33]=0;@U[27]=0;@H[8]=0;'))
# Every write failure stays strict. Emergency transport retry remains separately latched.
s=s.replace(sub('@H[80]=0;'),sub('''if(@U[26]==1 && @U[27]==1 && @Q[25]==1 && @Q[8]>=2 && @Q[0]==@Q[1] && @H[116]==0 && (@H[2]==1 || @H[2]==5) && @H[26]==0){@U[26]=0;@U[35]=1;@U[33]=0;}
@H[80]=0;'''))
put('HMI34事务服务.txt',s)
m=read('HMI34自定义函数.txt')
m=re.sub(r'void HMI34Field129\(\).*?(?=void HMI34Field130)', 'void HMI34Field129(){}\n',m,flags=re.S)
m=m.replace(sub('u33valid=1;P0_Boot.displayScratch.txt="未授权";'),sub('u33valid=1;P0_Boot.displayScratch.txt="未授权";'))
# Insert emergency feedback before authority text is committed, retaining its sole writer.
mark='if(u33valid==1){if(P8_ServoControl.authorityStatus.txt'
m=m.replace(mark,sub('if(@U[26]==1 || @H[116]==1){P0_Boot.displayScratch.txt="急停请求已锁存 / 等待停车确认";u33color=P0_Boot.themeWarn.val;}')+mark)
m+='\nvoid HMI34Input(){'+input_code+'}\nvoid HMI34Hint(){'+hint+'}\n'
# Replace theme sweeps with one binary-dispatched control per idle display batch.
model=json.loads((R/'运行核验/33号实施_20261102/33号实际工程读回.json').read_text(encoding='utf-8-sig'))
resources=json.loads((R/'运行核验/31号实施_20261101/资源索引绑定.json').read_text(encoding='utf-8-sig'))
def choose(items):
 if not items:return ''
 if len(items)==1:return items[0][1]
 mid=len(items)//2
 return f'if({Q}[16]<{items[mid][0]})'+'{'+choose(items[:mid])+'}else{'+choose(items[mid:])+'}'
for p in model['Pages']:
 name=p['Name'];page=p['Id'];items=[]
 for c in p['Controls']:
  key=name+'.'+c['Name'];tp=c['Type'].split('.')[-1];a=c['Properties'];body=''
  if tp=='TextCtrl':
   body=f'int bg34;bg34=-1;if(P0_Boot.themeMode.val==1){{bg34=-16051162;}}if({key}.bgColor!=bg34){{{key}.bgColor=bg34;}}'
   # Data fields recolor through their versioned field writer; static captions follow theme here.
   body+=f'if({key}.fontColor==-15438781 || {key}.fontColor==-10624850){{{key}.fontColor=P0_Boot.themeGood.val;}}else if({key}.fontColor==-4971752 || {key}.fontColor==-39558){{{key}.fontColor=P0_Boot.themeFault.val;}}else{{{key}.fontColor=P0_Boot.themeInk.val;}}'
  elif tp in ['ButtonCtrl','ToggleButtonCtrl']:
   kind='danger' if c['Name']=='estop' else 'primary' if c['Name'] in ['authorize','servoEntry','themeToggle'] else 'nav'
   for th,cond in [('Day','==0'),('Night','!=0')]:
    body+=f'if(P0_Boot.themeMode.val{cond})'+'{'
    for prop,state in [('bgImg','normal'),('pbgImg','pressed'),('dbgImg','disabled')]:
     token=f'user.image.{th}_{kind}_{state}_{a["Width"]}x{a["Height"]}'
     if token in resources:body+=f'if({key}.{prop}!={token}){{{key}.{prop}={token};}}'
    body+=f'{key}.fontColor='+('-1;' if kind in ['danger','primary'] else 'P0_Boot.themeInk.val;')+'}'
  elif tp=='SliderCtrl':body=f'if({key}.tbColor!=P0_Boot.themeGood.val){{{key}.tbColor=P0_Boot.themeGood.val;}}'
  if body:items.append((len(items),body))
 body=choose(items)+sub(f'@Q[16]=@Q[16]+1;if(@Q[16]>={len(items)}){{@Q[17]=0;P0_Boot.t34.vals[{page}]=P0_Boot.themeMode.val;}}')
 # Native custom function declarations must precede executable statements.
 body=body.replace('int bg34;','');body='int bg34;'+body
 m=re.sub(r'void HMI34Theme'+str(page)+r'\(\).*?(?=void HMI34|\Z)',lambda _:f'void HMI34Theme{page}(){{{body}}}\n',m,flags=re.S)
put('HMI34自定义函数.txt',m)
f=read('HMI34显示刷新.txt').replace('HMI34Field129();','').replace('for(u33n=0;u33n<4;u33n++)','for(u33n=0;u33n<3;u33n++)')
f=f.replace(sub('@Q[17]=1;'),sub('@Q[17]=0;if(P0_Boot.t34.vals[sys_pid]!=P0_Boot.themeMode.val){@Q[17]=1;}'))
# Count the single prompt writer in the six-field business budget.
f=f.replace(sub('@Q[40]=0;'),sub('@Q[40]=0;if(sys_pid==P8_ServoControl.id){HMI34Hint();@Q[40]=1;}'))
f=f.replace(' && '+Q+'[17]==1)',' && '+Q+'[17]==1 && '+Q+'[27]==0 && '+U+'[26]==0)')
put('HMI34显示刷新.txt',f)
latch=sub('''@Q[0]=@Q[0]+1;@U[26]=1;@U[27]=0;@U[33]=0;@U[35]=0;@U[3]=0;@U[37]=0;@U[20]=0;@H[116]=1;@H[11]=16;@H[10]=0;@Q[42]=@Q[42]+1;P0_Boot.dRev.vals[7]=P0_Boot.dRev.vals[7]+1;HMI34Field125();''')
put('事件/急停_按下.txt',sub('@U[34]=1;')+latch+'\n')
put('事件/急停_弹起.txt',sub('if(@U[34]==0){')+latch+sub('}@U[34]=0;\n'))
put('事件/滑块_滑动.txt',sub('if(@H[32]==0){HMI34Input();HMI34Hint();}\n'))
a=read('事件/授权_按下.txt');put('事件/授权_按下.txt',sub('if(@U[26]==0){')+a+'}\n')
for p in model['Pages']:
 name=p['Name'];page=p['Id']
 entry=sub(f'@Q[14]=0;@Q[16]=0;@Q[17]=0;if(P0_Boot.t34.vals[{page}]!=P0_Boot.themeMode.val){{@Q[17]=1;}}@Q[13]=0-1;\n')
 entry+=f'if(P0_Boot.b34.vals[{page}]!=P0_Boot.themeMode.val){{if(P0_Boot.themeMode.val==0){{{name}.bgImg=user.image.Day_{name};}}else{{{name}.bgImg=user.image.Night_{name};}}P0_Boot.b34.vals[{page}]=P0_Boot.themeMode.val;}}\n'
 if page==8:entry+=sub('''@H[32]=1;vEntry34=@H[10]+1600;if(@H[2]!=3){vEntry34=1600;}if(@H[2]==3 && @U[3]==1 && @U[6]==@H[6] && @U[7]==@H[7]){vEntry34=@U[1]+1600;}P8_ServoControl.rpmSlider.val=vEntry34;@U[0]=vEntry34;@U[38]=1;@H[32]=0;HMI34Hint();\n''');entry='int vEntry34;\n'+entry
 put(f'事件34/{name}_加载.txt',entry)
 put(f'事件34/{name}_离开.txt','// HMI34 navigation changes presentation only; global control continues.\n'+(sub('@U[38]=0;\n') if page==8 else ''))
put('事件34/主题切换.txt','if(P0_Boot.themeMode.val==0){P0_Boot.themeMode.val=1;}else{P0_Boot.themeMode.val=0;}\n'+read('事件34/P1_Main_加载.txt'))
print('HMI34 generated: input schema',len(names),'theme steps cached; strict control timeouts retained')
