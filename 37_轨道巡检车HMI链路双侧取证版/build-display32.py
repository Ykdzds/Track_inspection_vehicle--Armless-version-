"""Generate the HMI32 presentation transaction; never edits transport/control sources.

The old renderer becomes a candidate producer. Only validated final candidates are
committed, so no intermediate placeholder is ever written to a live text control.
Run in the HMI32 directory. The sibling HMI31 is a read-only input.
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BASE = ROOT.parent / '31_轨道巡检车日夜双主题切换版'
E = ROOT / '运行核验/32号实施_20261102'
E.mkdir(parents=True, exist_ok=True)
original = (BASE / '全局计龄刷新.txt').read_text(encoding='utf-8-sig')
model = json.loads((BASE / '运行核验/31号实施_20261101/31号实际工程读回.json').read_text(encoding='utf-8-sig'))
H = 'P8_ServoControl.h13.vals'
S = 'P8_ServoControl.h13Status.vals'
A = 'P0_Boot.ui32State.vals'
F = 'P0_Boot.ui32Field.vals'
T = 'P0_Boot.ui32Text'
V = 'P0_Boot.ui32Candidate.vals'
WC = 'P0_Boot.ui32WorkColor.vals'
WF = 'P0_Boot.ui32WorkFont.vals'
RS = 'P0_Boot.ui32Source.vals'
AT = 'P0_Boot.ui32SourceAt.vals'

def remove_block(src, start):
    pos = src.index(start)
    begin = src.index('{', pos)
    depth = 1
    end = begin + 1
    quoted = False
    while depth:
        ch = src[end]
        if ch == '"' and src[end - 1] != '\\': quoted = not quoted
        if not quoted:
            if ch == '{': depth += 1
            elif ch == '}': depth -= 1
        end += 1
    return src[:pos] + src[end:]

src = original
# Never fall back from a reserved linear value to an unrelated RPM source.
for expr,linear in [('18','58'),('19+uiAxis','58'),('21+uiAxis','60+uiAxis')]:
    old=f'h13Status.vals[{expr}]!=32767)'
    new=f'h13Status.vals[{expr}]!=32767 && !(P8_ServoControl.h13Status.vals[8]!=1 && P8_ServoControl.h13Status.vals[59]==1 && P8_ServoControl.h13Status.vals[{linear}]==32767))'
    src=src.replace(old,new)
# A validity flag does not make the reserved linear target code numeric.
src=src.replace('h13Status.vals[59]==1){','h13Status.vals[59]==1 && P8_ServoControl.h13Status.vals[58]!=32767){')
src=src.replace('h13Status.vals[59]==1 && P8_ServoControl.h13Status.vals[8]!=1)', 'h13Status.vals[59]==1 && P8_ServoControl.h13Status.vals[58]!=32767 && P8_ServoControl.h13Status.vals[8]!=1)')
a=src.index('// 已收到但过期的区域');b=src.index('// 启动状态索引',a);src=src[:a]+src[b:]
src = remove_block(src, 'if(P8_ServoControl.h13.vals[96]>0 && P8_ServoControl.h13.vals[1]%4000>=2000)')
src = remove_block(src, 'if(sys_pid==P4_Info.id && P8_ServoControl.h13.vals[86]==1')
src = src.replace(' && (P8_ServoControl.h13.vals[1]/2000)%2==0', '')
# A fixed meaning for each diagnostic label: torque/load, I/V, CRC, sample age.
for key in ['reqCount', 'respCount', 'retryCount', 'timeoutCount', 'frameCount']:
    src = re.sub(r'P2_Diag\.' + key + r'\.(fontColor|txt)=[^;]*;', '', src[:src.index('// 13号控制页刷新')]) + src[src.index('// 13号控制页刷新'):]
# The above fields are supplied by the unified status block at the end only.
src = re.sub(r'^.*P8_ServoControl\.linkStatus\..*$', '', src, flags=re.M)
src = src.replace('屏幕工程：31', '屏幕工程：32')
# Capability identity is configuration history. Its slower poll must not veto a
# new position/alarm/extension sample; each candidate still checks its own region.
src = src.replace('regionValid.vals[4]==1 && regionBadRun.vals[4]<10', f'{RS}[4]==1')
# Evaluate every page's candidates; history must also be collected off-page.
src = re.sub(r'sys_pid==P[1-7]_\w+\.id', '1==1', src)
# Keep original age maintenance and every control/safety expression. Display
# history is committed independently of that strict validity state.


# Skip unrelated page formatters when their input source did not change.
page_sources={7:[3,4],5:[3,4],6:[2,4],3:[2,4],2:[0,1,4,7],4:[0,1],1:[0,4]}
markers={7:'// 各模块由',5:'// 告警区',6:'// 同步诊断',3:'// 原始位置',2:'// 系统区'}
# Source-level scope guards include capability changes and new controller epochs.
def changed(regions):
    return ' || '.join(f'({RS}[{x}]==1 && {AT}[{x}]==u32now)' for x in regions)
for page,nextpage in [(7,5),(5,6),(6,3),(3,2)]:
    start=src.index('if(1==1){',src.index(markers[page]))
    src=src[:start]+src[start:].replace('if(1==1){',f'if(u32reset==1 || {changed(page_sources[page])}){{',1)
# Remaining 3 main blocks are adjacent and not preceded by unique comments.
start=src.index('// 系统区')
for page in [2,4,1]:
    pos=src.index('if(1==1){',start)
    src=src[:pos]+src[pos:].replace('if(1==1){',f'if(u32reset==1 || {changed(page_sources[page])}){{',1)
    start=pos+1
# Unified servo numeric snapshot and fixed diagnostic values only need conversion
# when the accepted status timestamp changes. Local authorization stays live.
start=src.index('P8_ServoControl.leftTarget.font=')
end=src.index('P8_ServoControl.authorityStatus.fontColor=',start)
src=src[:start]+f'if(u32reset==1 || {changed([7])}){{\n'+src[start:end]+'\n}\n'+src[end:]

keys = sorted(set(re.findall(r'(P[1-8]_\w+\.\w+)\.txt=', src)))
keys.append('P8_ServoControl.linkStatus')
keys = sorted(set(keys))
N = len(keys)
index = {k: i for i, k in enumerate(keys)}

def source_for(key):
    page, name = key.split('.')
    if page == 'P1_Main': return 4 if name in ['targetSpeed','targetSpeedUnits','controlSource','alarmSummary'] else 0
    if page == 'P2_Diag':
        if name in ['reqCount','respCount','retryCount','timeoutCount','frameCount']: return 7
        if name == 'crcCount': return 1
        if any(x in name for x in ['SpeedValid','FaultValid','Zero','CommFail']): return 4
        return 0
    if page == 'P3_Position': return 4 if name == 'lostInterval' else 2
    if page == 'P4_Info': return 8 if name == 'screenVersion' else 1 if name == 'minStack' else 0
    if page == 'P5_Alarm': return 8 if name == 'alarmHistory' else 3
    if page == 'P6_Sync': return 2
    if page == 'P7_System_Ext': return 4 if name in ['cameraState','storageState','extensionComm','extensionReadonly'] else 3
    return 8 if name in ['authorityStatus','commonTarget','sliderSpeedHint','errorStatus','recoveryButton'] else 7

defaults = {}
for k in keys:
    values = re.findall(re.escape(k) + r'\.txt="([^"\n]*)";', original)
    defaults[k] = next((x for x in values if any(v in x for v in ['未知','--','等待有效'])), values[0] if values else '通信：等待主控 / HMI13-R2')
defaults['P4_Info.screenVersion'] = '屏幕工程：32'
# These labels now have fixed semantics, even before their first valid packet.
for k, text in {'reqCount':'轴1转矩/负载：--', 'respCount':'轴2转矩/负载：--', 'retryCount':'轴1I/V：--', 'timeoutCount':'轴2I/V：--', 'frameCount':'诊断值年龄：未知'}.items(): defaults['P2_Diag.'+k] = text
defaults['P8_ServoControl.stopStatus'] = '停车阶段：未知'

rows = []
for k in keys:
    p, c = k.split('.')
    ctrl = next(x for x in next(x for x in model['Pages'] if x['Name']==p)['Controls'] if x['Name']==c)
    rows.append(dict(index=index[k], key=k, page=next(x['Id'] for x in model['Pages'] if x['Name']==p), source=source_for(k), default=defaults[k], font=next(i+1 for i,x in enumerate(model['Fonts']) if x['Id']==ctrl['Properties']['FontId']), width=ctrl['Properties'].get('Width'), height=ctrl['Properties'].get('Height')))

# Convert whole fontColor/font/text statement groups into candidate writes.
# Invalid candidates do not clear history; later valid assignments supersede them.
pat = re.compile(r'(?P<key>P[1-8]_\w+\.\w+)\.(?P<prop>fontColor|font|txt)=(?P<expr>[^;]+);')
def transform(m):
    k, prop, expr = m['key'], m['prop'], m['expr']
    if k not in index: return m[0]
    i = index[k]
    if prop == 'fontColor': return f'{WC}[{i}]={expr};'
    if prop == 'font': return f'{WF}[{i}]={expr};'
    append = k+'.txt' in expr
    expr = expr.replace(k+'.txt', f'{T}.get({N+i})')
    out = f'{T}.set({N+i},{expr});'
    if append: return out
    literals = re.findall(r'"([^"\n]*)"', expr)
    text = ''.join(literals)
    bad = any(x in text for x in ['未知','--','过期','越界','无效','UNKNOWN','等待有效数据'])
    # Fresh explicit field quality is a state, rather than a numeric sentinel.
    if any(x in k for x in ['SpeedValid','FaultValid']) and '无效' in text: bad = False
    if k == 'P8_ServoControl.recoveryDetail' and text.startswith('拒绝：'): bad = False
    if k == 'P8_ServoControl.recoveryStatus' and text == '等待有效数据': bad = True
    if k == 'P8_ServoControl.stopStatus' and text == '停车阶段：无':
        return out + f'{V}[{i}]={RS}[7];P0_Boot.ui32CandidateSource.vals[{i}]=7;'
    if bad: return f'{V}[{i}]=0;'
    source = 4 if '未配置' in text else source_for(k)
    if k=='P8_ServoControl.recoveryDetail' and any(x in text for x in ['拒绝：','本代恢复','恢复结果未知','恢复申请被拒绝','恢复失败，原因']): source=8
    if k in ['P2_Diag.diagAxis1SpeedAge','P2_Diag.diagAxis2SpeedAge'] and '反馈年龄' in text: source=7
    # Invalid/incomplete position data belongs in a quality row, not a number.
    if k in ['P3_Position.startupDisplacement','P3_Position.startupTravel','P3_Position.segmentTravel','P3_Position.axisPositionDiff'] and any(x in text for x in ['不完整','未建立']):
        return f'{V}[{i}]=0;'
    # The candidate records its actual source. Capability messages use region4.
    return out + f'{V}[{i}]=1;P0_Boot.ui32CandidateSource.vals[{i}]={source};'

tail=src.index('if(1==1 && P8_ServoControl.h13.vals[86]==1',src.index('P8_ServoControl.errorStatus'))
end=src.index('// UI31_PRESENTATION_END',tail)
src=src[:tail]+f'if(u32reset==1 || {changed([7])}){{\n'+src[tail:end]+'}\n'+src[end:]
src = pat.sub(transform, src)
# Local display candidates are expensive only when local state or snapshot changes.
pattern=re.compile(r'P0_Boot\.ui32(?:Text\.set\([^;]+|(?:Candidate|CandidateSource|WorkColor|WorkFont)\.vals\[[^]]+\]=[^;]+);')
def guard_local(m):
    nums=re.findall(r'ui32Text\.set\((\d+)|\.vals\[(\d+)\]',m[0])
    if not nums:return m[0]
    n=next(int(a or b) for a,b in nums)%N
    if keys[n].startswith('P8_ServoControl.'):
        return f'if({A}[44]==1){{'+m[0]+'}'
    return m[0]
src=pattern.sub(guard_local,src)

# Do not let the renderer reset cached typography before a valid candidate.
# The original safety enable/press/guard statements are intentionally untouched.

trip = [index[k] for k in keys if k.startswith('P3_Position.') and k.split('.')[1] in ['startupDisplacement','startupTravel','segmentTravel','axisPositionDiff','mileageValid','positionIncomplete','positionBaseline','positionSample']]
session = [index[k] for k in keys if k in ['P1_Main.targetSpeed','P1_Main.targetSpeedUnits','P1_Main.controlSource','P1_Main.controlAuthority','P8_ServoControl.acceptedTarget','P8_ServoControl.leftTarget','P8_ServoControl.rightTarget','P8_ServoControl.leftOutput','P8_ServoControl.rightOutput','P8_ServoControl.ownerStatus','P8_ServoControl.axisMode']]
debounced = [index[k] for k in keys if k in ['P3_Position.positionValid','P3_Position.mileageValid','P3_Position.positionIncomplete','P6_Sync.syncPairValid','P6_Sync.syncRejectReason'] or any(x in k for x in ['SpeedValid','FaultValid'])]

pre = f'''// HMI32_DISPLAY_BEGIN: isolated display history; never consumed by control.
int u32i;
int u32s;
int u32now;
int u32delta;
int u32reset;
int u32newStatus;
int u32diff;
int u32age;
int u32color;
int u32themeChanged;
u32now={H}[1];u32reset=0;u32newStatus=0;{A}[35]=0;
if({A}[0]==0){{{A}[0]=1;u32reset=1;}}
// The service periodically shifts its local clock. Shift display timestamps too.
if(u32now<{A}[1]){{u32delta={A}[1]-u32now+50;for(u32i=0;u32i<{N};u32i++){{{F}[{N}+u32i]={F}[{N}+u32i]-u32delta;}}for(u32i=0;u32i<9;u32i++){{{AT}[u32i]={AT}[u32i]-u32delta;}}}}
{A}[1]=u32now;
if(P0_Boot.metaSeen.vals[0]==1 && P0_Boot.metaRegs.vals[0]==21065 && P0_Boot.metaRegs.vals[1]==1){{
 if({A}[2]==0 || {A}[3]!=P0_Boot.metaRegs.vals[4] || {A}[4]!=P0_Boot.metaRegs.vals[5] || {A}[5]!=P0_Boot.metaRegs.vals[1] || {A}[6]!=P0_Boot.metaRegs.vals[3]){{
  u32reset=1;{A}[2]=1;{A}[3]=P0_Boot.metaRegs.vals[4];{A}[4]=P0_Boot.metaRegs.vals[5];{A}[5]=P0_Boot.metaRegs.vals[1];{A}[6]=P0_Boot.metaRegs.vals[3];
 }}
}}
if(u32reset==1){{
 {A}[7]={A}[7]+1;{A}[8]=0;{A}[12]=0;{A}[16]=0;
 for(u32i=0;u32i<{N};u32i++){{{F}[u32i]=0;{F}[{N*4}+u32i]={A}[7];P0_Boot.ui32Debounce.vals[u32i]=0;}}
 for(u32i=0;u32i<9;u32i++){{{RS}[u32i]=0;{AT}[u32i]=u32now;}}
 for(u32i=0;u32i<5;u32i++){{{A}[20+u32i]={H}[109+u32i];}}
 {A}[25]={H}[114];
}}
// Accepted transport counters are only advanced by the unchanged parser.
for(u32i=0;u32i<5;u32i++){{
 if({A}[2]==1 && {H}[109+u32i]!={A}[20+u32i]){{{A}[20+u32i]={H}[109+u32i];{RS}[u32i]=1;{AT}[u32i]=u32now;}}
}}
if({A}[2]==1 && {H}[114]!={A}[25]){{{A}[25]={H}[114];{RS}[5]=1;{AT}[5]=u32now;}}
if({A}[2]==1 && {H}[86]==1 && {S}[0]==21325 && {S}[1]==2 && {S}[2]=={A}[3] && {S}[3]=={A}[4]){{
 u32diff=({S}[4]-{A}[9]+65536)%65536;
 if({A}[8]==0 || (u32diff>0 && u32diff<32768) || (u32diff==0 && {S}[5]>{A}[10])){{
  u32newStatus=1;{A}[8]=1;{A}[9]={S}[4];{A}[10]={S}[5];{RS}[7]=1;{AT}[7]=u32now;
 }}
}}
// A new control session invalidates command history, never physical odometry.
if(u32newStatus==1){{
 if({A}[12]==1 && ({A}[13]!={S}[12] || {A}[14]!={S}[13] || {A}[15]!={S}[8])){{
 {A}[35]=1;SESSION_RESET
  {RS}[4]=0;{RS}[0]=0;
 }}
 {A}[12]=1;{A}[13]={S}[12];{A}[14]={S}[13];{A}[15]={S}[8];
 {A}[30]={S}[4];{A}[31]={S}[5];{A}[32]={S}[6];{A}[33]={S}[7];
}}
// A confirmed new position baseline is the only position-only cache boundary.
if({RS}[2]==1 && {AT}[2]==u32now && P0_Boot.rawRegs.vals[124]!=65535 && P0_Boot.rawRegs.vals[127]!=65535){{
 if({A}[16]==1 && {A}[17]!=P0_Boot.rawRegs.vals[127]){{TRIP_RESET}}
 {A}[16]=1;{A}[17]=P0_Boot.rawRegs.vals[127];
}}
{RS}[8]=1;{AT}[8]=u32now;
INIT_FIELDS
'''.replace('SESSION_RESET', ''.join(f'{F}[{i}]=0;' for i in session)).replace('TRIP_RESET', ''.join(f'{F}[{i}]=0;' for i in trip))

# Restrict presentation work to pages with new input. All pages still collect
# history on their own accepted packets even when they are not visible.
local_indices=[2,3,10,26,30,32,56,66,116,125,126,133,136,138,161,162,163,164,167,168,170]
local_guard=f'{A}[48]=0;'
for j,x in enumerate(local_indices):
    local_guard+=f'if({A}[{50+j}]!={H}[{x}]){{{A}[48]=1;{A}[{50+j}]={H}[{x}];}}'
local_guard+=f'if({A}[74]!=P8_ServoControl.rpmSlider.val){{{A}[48]=1;{A}[74]=P8_ServoControl.rpmSlider.val;}}'
pre+=local_guard
init=[]
for page in range(1,9):
    cond=f'u32reset==1 || {A}[48]==1 || '+changed([7]) if page==8 else 'u32reset==1 || '+(f'{A}[35]==1 || ' if page==1 else '')+changed(page_sources[page])
    init.append(f'{A}[{36+page}]=0;if({cond}){{{A}[{36+page}]=1;}}')
    init.append(f'if({A}[{36+page}]==1){{')
    for row in rows:
        if row['key'].startswith(model['Pages'][page]['Name']+'.'):
            i=row['index'];init.append(f'{V}[{i}]=0;{WC}[{i}]=P0_Boot.themeInk.val;{WF}[{i}]=P0_Boot.ui32Font.vals[{i}];')
    init.append('}')
pre=pre.replace('INIT_FIELDS','\n'.join(init))

# Link and quality have one owner, after all business candidates are evaluated.
linki=index['P8_ServoControl.linkStatus']
ci=index['P1_Main.ctrlComm']
post = f'''// HMI32_LINK: one stable communication row, snapshot details stay internal.
u32age=2147483647;
for(u32i=0;u32i<8;u32i++){{if({RS}[u32i]==1 && u32now-{AT}[u32i]<u32age){{u32age=u32now-{AT}[u32i];}}}}
{A}[34]=u32age;
{T}.set({N+linki},"通信：等待主控 / HMI13-R2");{WC}[{linki}]=P0_Boot.themeMuted.val;
{T}.set({N+ci},"控制器通信：等待主控回复");{WC}[{ci}]=P0_Boot.themeMuted.val;
if({A}[2]==1 && {A}[8]==1){{
 {T}.set({N+linki},"通信：正常 / R2接口匹配");{WC}[{linki}]=P0_Boot.themeGood.val;
 {T}.set({N+ci},"控制器通信：正常");{WC}[{ci}]=P0_Boot.themeGood.val;
 if(u32age>=3000){{{T}.set({N+linki},"通信：超时");{T}.set({N+ci},"控制器通信：通信超时");{WC}[{linki}]=P0_Boot.themeFault.val;{WC}[{ci}]=P0_Boot.themeFault.val;}}
}}
if(P0_Boot.metaSeen.vals[0]==1 && (P0_Boot.metaRegs.vals[0]!=21065 || P0_Boot.metaRegs.vals[1]!=1 || P0_Boot.metaRegs.vals[3]!=2)){{
 {T}.set({N+linki},"通信：协议不匹配");{T}.set({N+ci},"控制器通信：协议不匹配");{WC}[{linki}]=P0_Boot.themeFault.val;{WC}[{ci}]=P0_Boot.themeFault.val;
}}
{V}[{linki}]=1;P0_Boot.ui32CandidateSource.vals[{linki}]=8;
{V}[{ci}]=1;P0_Boot.ui32CandidateSource.vals[{ci}]=8;
'''
post+=f'if({T}.get({linki})!={T}.get({N+linki})){{{A}[44]=1;}}\n'
post+=f'if({T}.get({ci})!={T}.get({N+ci})){{{A}[37]=1;}}\n'
# Commit only a page whose candidate inputs changed, whose theme/page just
# changed, or whose quality age needs a one-second update. No full-field sweep.
post+=f'u32themeChanged=0;if({A}[45]!=P0_Boot.themeMode.val || {A}[46]!=sys_pid){{u32themeChanged=1;}}\n'
for page in range(1,9):
    pn=model['Pages'][page]['Name']
    post+=f'if({A}[{36+page}]==1 || (sys_pid=={pn}.id && (u32themeChanged==1 || u32now/1000!={A}[47]))){{\n'
    for row in rows:
        i,k=row['index'],row['key']
        if not k.startswith(pn+'.'):continue
        post+=f'u32i={i};u32s=P0_Boot.ui32CandidateSource.vals[{i}];\n'
        if i in debounced:
            post+=f'''if({V}[{i}]==1 && {RS}[u32s]==1 && {F}[{i}]==1 && {AT}[u32s]>{F}[{N+i}] && {T}.get({N+i})!={T}.get({i})){{
 if(P0_Boot.ui32DebounceAt.vals[{i}]!={AT}[u32s]){{
  if({T}.get({N*2+i})=={T}.get({N+i})){{P0_Boot.ui32Debounce.vals[{i}]=P0_Boot.ui32Debounce.vals[{i}]+1;}}else{{{T}.set({N*2+i},{T}.get({N+i}));P0_Boot.ui32Debounce.vals[{i}]=1;}}
  P0_Boot.ui32DebounceAt.vals[{i}]={AT}[u32s];
 }}
 if(P0_Boot.ui32Debounce.vals[{i}]<3){{{V}[{i}]=0;}}
}}else{{if({AT}[u32s]==u32now){{P0_Boot.ui32Debounce.vals[{i}]=0;}}}}
'''
        post+=f'''if({V}[{i}]==1 && {RS}[u32s]==1 && ({F}[{i}]==0 || {AT}[u32s]>{F}[{N+i}] || (u32s==8 && {T}.get({i})!={T}.get({N+i})))){{
 {T}.set({i},{T}.get({N+i}));{F}[{i}]=1;{F}[{N+i}]={AT}[u32s];{F}[{N*4+i}]={A}[7];
 P0_Boot.ui32Color.vals[{i}]={WC}[{i}];P0_Boot.ui32Font.vals[{i}]={WF}[{i}];
}}
{F}[{N*2+i}]=u32now-{F}[{N+i}];{F}[{N*3+i}]=0;if({F}[{i}]==1 && {F}[{N*2+i}]>=3000){{{F}[{N*3+i}]=1;}}
'''
        q=json.dumps(row['default'],ensure_ascii=False)
        post+=f'''if({F}[{i}]==0){{{T}.set({i},{q});P0_Boot.ui32Color.vals[{i}]=P0_Boot.themeMuted.val;P0_Boot.ui32Font.vals[{i}]={row['font']};}}
u32color=P0_Boot.ui32Color.vals[{i}];
if(u32color==-15438781 || u32color==-10624850){{u32color=P0_Boot.themeGood.val;}}
else if(u32color==-7714048 || u32color==-14490){{u32color=P0_Boot.themeWarn.val;}}
else if(u32color==-4971752 || u32color==-39558){{u32color=P0_Boot.themeFault.val;}}
else if(u32color==-12429715 || u32color==-5653552){{u32color=P0_Boot.themeMuted.val;}}
else{{u32color=P0_Boot.themeInk.val;}}
if({k}.txt!={T}.get({i})){{{k}.txt={T}.get({i});}}
if({k}.fontColor!=u32color){{{k}.fontColor=u32color;}}
if(P0_Boot.ui32Font.vals[{i}]>0 && {k}.font!=P0_Boot.ui32Font.vals[{i}]){{{k}.font=P0_Boot.ui32Font.vals[{i}];}}
'''
    post+='}\n'
post+=f'{A}[45]=P0_Boot.themeMode.val;{A}[46]=sys_pid;{A}[47]=u32now/1000;\n'
post+='// HMI32_DISPLAY_END\n'

# Dedicated, compact quality rows do not replace a business field. Only seconds
# change while stale, keeping the main numbers calm and their provenance clear.
quality=[]
for page,region in [(2,0),(3,2),(4,0),(5,3),(6,2),(7,3)]:
    pn=model['Pages'][page]['Name']
    quality.append(f'''if(sys_pid=={pn}.id){{
 {pn}.dataQuality.bgColor=-1;if(P0_Boot.themeMode.val==1){{{pn}.dataQuality.bgColor=-16051162;}}
 if({RS}[{region}]==0){{{pn}.dataQuality.txt="等待有效数据";{pn}.dataQuality.fontColor=P0_Boot.themeMuted.val;}}
 else{{
  {pn}.dataQuality.txt="数据正常";{pn}.dataQuality.fontColor=P0_Boot.themeGood.val;
  if(u32now-{AT}[{region}]>=3000){{{pn}.dataQuality.txt="历史数据 "+intToString((u32now-{AT}[{region}])/1000)+" s";{pn}.dataQuality.fontColor=P0_Boot.themeWarn.val;}}
  if(u32age>=3000){{{pn}.dataQuality.txt="通信超时 / 历史值";{pn}.dataQuality.fontColor=P0_Boot.themeFault.val;}}
 }}
}}''')
    if page==3:
        i=index['P3_Position.positionIncomplete']
        quality.append(f'''if(sys_pid==P3_Position.id && {F}[{i}]==1 && {T}.get({i})=="数据不完整：是" && u32age<3000){{P3_Position.dataQuality.txt="数据不完整 / 历史值";P3_Position.dataQuality.fontColor=P0_Boot.themeWarn.val;}}''')
post+='\n'.join(quality)+'\n'

# Guards for display candidate quality preserve all original validation gates.
# Unlike extending safety timeouts, history survives when these gates are false.
decl_end=src.index('// 先维护年龄')
out=src[:decl_end]+pre+src[decl_end:]+post
# The text controls already retain their last committed string. Use that native
# storage directly, avoiding a second copy and four interpreter calls per field.
for row in rows:
    j,k=row['index'],row['key']
    if k=='P8_ServoControl.linkStatus': continue
    out=out.replace(f'{T}.get({j})',f'{k}.txt')
    out=out.replace(f'{T}.set({j},',f'{k}.txt=')
    # A set-to-control replacement needs only removal of its closing parenthesis.
    out=re.sub(re.escape(k)+r'\.txt=([^;]+)\);',lambda m:m[0][:-2]+';',out)
    out=out.replace(f'P0_Boot.ui32Color.vals[{j}]',f'{k}.fontColor')
    out=out.replace(f'P0_Boot.ui32Font.vals[{j}]',f'{k}.font')
    # Theme entry events map native colors; no duplicate per-field palette pass.
    a=out.index(f'u32color={k}.fontColor;')
    b=out.index(f'if({k}.font>0 && ',a)
    b=out.index('}',b)+1
    out=out[:a]+out[b:]
    out=out.replace(f'{k}.txt={T}.get({N+j});',f'if({k}.txt!={T}.get({N+j})){{{k}.txt={T}.get({N+j});}}')
# Cached palette colors are now the existing native properties; page events
# perform the theme mapping. Candidate fields still carry explicit new colors.

(ROOT/'全局计龄刷新.txt').write_text(out,encoding='utf-8-sig')
# Historical fixture entrypoint is deliberately inert: only refreshTick renders.
(ROOT/'HMI13界面刷新.txt').write_text('// HMI32: display is owned exclusively by P0_Boot.refreshTick.\n',encoding='utf-8-sig')
(E/'32号显示字段索引.json').write_text(json.dumps(dict(count=N, fields=rows, positionReset=trip, sessionReset=session, debounce=debounced),ensure_ascii=False,indent=2),encoding='utf-8')
print('Generated',N,'transactional display fields;',len(out),'characters')
