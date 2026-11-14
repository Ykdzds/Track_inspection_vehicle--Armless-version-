"""Build the HMI33 delivery documentation from the frozen validation evidence."""
from pathlib import Path
import json,hashlib,shutil,subprocess,difflib
R=Path(__file__).resolve().parent
ROOT=R.parents[3]
M=ROOT/'5.工程代码/RailInspectionCar_Inno_V1'
E=R/'运行核验/33号实施_20261102'
D=M/'Docs/HMI33_20261102'
P=R/'33号配套调试交付'
P.mkdir(exist_ok=True)
def read(p):return p.read_text(encoding='utf-8-sig')
def put(p,s):p.write_text(s,encoding='utf-8-sig')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
compiled=json.loads(read(E/'33号编译产物.json'))
perf=json.loads(read(E/'33号原生显示性能.json'))
assert sha(R/'17_轨道巡检车.pix')==compiled['PIX_SHA256']
assert sha(R/'17_轨道巡检车.pix.bin')==compiled['BIN_SHA256']
variant='hmi_control_axis_3_hold_1_hmi_1_readonly_0'
F=D/'firmware'/variant
for ext in ['bin','hex','axf']:
 shutil.copy2(F/(variant+'.'+ext),P/('F407_33号配套_双轴_启动保持.'+ext))
for src,dst in [('17_轨道巡检车.pix','33号屏端.pix'),('17_轨道巡检车.pix.bin','33号屏端.pix.bin')]:shutil.copy2(R/src,P/dst)
put(P/'请先阅读_版本状态.txt','33号配套台架调试版。软件回归与编译通过；实屏时序、台架、实车均待验收。\n屏端PIX/BIN与F407双轴hold1固件必须作为同一组记录和核验。\n宿主原生显示尚未达到5ms，不可标记为实车验收通过。\n完整说明见上级目录33号修复与回归报告.md、33号配套烧录与回退说明.md。\n')
report=f'''# 33号修复与回归报告

本轮已建立33号独立屏端工程，完成屏端调度、会话状态机及F407联动源码修改、软件回归和编译。当前交付等级为**配套台架调试版**。尚未执行实屏、低速双轴台架或实车验证，不能认定视频中的全部实物问题已消除。

## 保护基线与版本

- 31、32号保持原样；2493个受保护文件SHA256复核无变化，证据见运行核验/33号实施_20261102/31_32最终保护复核.json。
- 主控修改前HEAD：f33b011a753c6a923345d06ea7b7c773c6ec9883；工作区干净，没有创建空提交。
- 主控修复分支：fix/hmi33-scheduling-session。只做本地提交，不推送；最终提交号和近10条记录见同证据目录。
- 主控R2字典版本2、地址、命令和96寄存器布局保持；补丁号1→2。
- 配套主控配置：双轴（APP_SERVO_QUERY_MODE=3、APP_SERVO_CONTROL_AXIS=3）、启动保持=1、HMI控制=1、只读=0。六配置矩阵产物不应混选。
- 工程内部文件沿用17_轨道巡检车.pix，归属以33号目录和SHA256为准。33号目录复制保留了旧版历史资料；本轮依据仅为HMI33文件、事件33、33号实施证据及33号配套调试交付。

## 问题与修改

| 问题 | 本轮处理 | 验证范围 |
|---|---|---|
| h13长度179却访问179，h13Status长度95却访问95 | 实际PIX容量180/96；数组测试采用真实长度，越界立即失败 | 9292处静态引用、实际模型读回、动态边界 |
| 两个全局事件及九页全量刷新阻塞 | 仅保留一个全局50ms事件；串口单写入者；只刷新当前页，最多6个业务字段；主题分批 | 串行模型、原生显示、绑定审计 |
| 固定每拍+50ms被用于新鲜度 | 拍数只安排任务；续发前同步读取主控时间、快照、目标年龄和租约 | 100/300/600ms独立主控时钟回归 |
| 历史停车成功被当作新会话当前停车 | 主控当前phase/flags/zero_mask与保留的stop历史分开；屏端按操作边界结算 | 新授权历史停车回归、真实状态编码测试 |
| 急停意图可能被旧停车结算清除 | 急停独立请求代次，停止结果不能清除较新请求；按钮弹起无业务动作 | 各本地状态及同拍新急停回归 |
| S6不能退出 | 持续同步，两个推进的安全零速快照后允许人工授权；遗留HMI会话先无令牌停车 | S6恢复、重复授权和他源仲裁回归 |
| 展示值频繁失效 | dRaw/dStatus保存最近合法数据，当前页字段版本标记；合法0和负速度提交；里程质量独立 | 10秒断帧、哨兵值、基准变化回归 |
| 通信文本互相覆盖 | linkStatus仅由一个字段函数写入，约3秒失联显示超时；不写快照调试文本 | 全工程写入审计 |

屏端事务优先级为急停、取消/停车、授权/恢复确认、运行状态/目标、辅助采样、显示。运行每3个调度拍安排64寄存器关键状态读取并串行续发目标，确认过程读取96寄存器，后32项仅在完整样本时更新。每事务50ms超时；冻结或倒退快照、启动标识或会话变化、主控确认间隔达到300ms、目标年龄达到300ms、租约不足时停止非零续发并重新同步。安全TTL500ms及租约1000ms未放宽，心跳不替代目标续期。

有效授权始终从零开始，须重新滑动。显示历史不参与授权、急停、停车、恢复、返回主页或所有权判断。当前授权提示优先刷新，业务缓存不维持旧授权。

## 首次自动停车仍需实物取证

视频中的首次停车不能只凭文字跳变判定为目标超时。F137=0x89包含无活动会话及停车成功历史，不能直接解释成硬件故障锁。已经补充64条主控事件和目标间隔统计，用于区分目标TTL、租约、主动释放、急停及历史状态误关联。原32号视频及其SHA256见证据索引；未伪造33号视频或UART记录。

## 软件和编译结果

| 层级 | 结果 | 证据 |
|---|---|---|
| 屏端串行事件回归 | 22/22通过 | 33号回归最终.log |
| 最终PIX保存重读 | 149个函数、数组真实容量、事件与文本一致 | 33号保存源码一致性.json |
| 数组/绑定静态审计 | 9292处引用无错误，唯一串口写入P0_Boot.refreshTick | 33号边界绑定核验.json |
| 原生VM显示 | 9页×日夜=18场景，原生执行无错误 | 33号原生执行.log、33号VP原生显示状态.json |
| 字模及布局 | 390处样本文本，未发现溢出、缺字，最小对比度约5.95 | 33号布局字模核验.json、33号九页日夜总览.png |
| Visual-PIX | 最终编译成功，新BIN {compiled['BIN_Length']}字节 | 33号VisualPIX编译.log、33号编译产物.json |
| 主控宿主回归 | control/state/remote/CAN/K5/snapshot/Modbus/watchdog通过 | 主控Docs/HMI33_20261102/tests |
| Keil全量构建 | 单轴1/2、双轴×hold0/1共6配置，0错误0警告 | build-matrix.log及各配置日志 |
| Flash/静态栈 | 程序末端最大0x0804E338，低于0x080E0000；静态栈检查通过 | MAP/HTM、build-matrix.log |
| 实屏时序、内存、触摸 | 待验收 | 不以宿主或模型代替 |
| 双轴台架、实车30分钟 | 待验收 | 不以软件回归代替 |

模型计入串口等待、显示、导航、触摸耗时并独立推进主控时间。健康模型使用约10ms串口回复和4–5ms显示假设；1000次切页属于模型验证，不能当作实屏1000次操作通过。原生VM执行范围是实际显示/主题/加载事件，未执行真实串口，也未把生产PIX改为测试脚本。

## 性能门槛尚未通过

最终压力样本的Windows原生VM单批最高{max(x['MaxMs'] for x in perf):.3f}ms，稳定阶段最高{max(x['SteadyMaxMs'] for x in perf):.3f}ms。数据反映宿主VM执行与初次加载/JIT等开销，不能换算为实屏耗时；也不能作为实屏5ms已经通过的证据。当前版本仅具备字段数量预算及失败时暂停普通业务显示，尚无已验证屏端毫秒API来强制按真实执行时间中止一批工作。

需要在模拟从站＋实屏阶段测量全部调度路径，若任一显示批次超过5ms或健康目标/关键状态间隔超过200ms，应继续减少单批工作或改造瓶颈，然后重新编译、冻结哈希和复验。未通过前不升级为实车验收版。首次/稳定批次、触摸响应、串口真实间隔及主控栈高水位均须实测。

## 可复现入口

屏端：build33.py生成源码；apply33.ps1写入33号模型；编译33号工程.ps1调用Visual-PIX编译菜单；apply33.ps1 -AuditOnly及verify-saved33.ps1核对保存模型。audit33.py审计边界，node --test hmi33.test.cjs只执行33号用例。native33.ps1及audit-layout33.py核验原生显示和布局。PS5.1脚本保存为UTF-8 BOM。不要裸跑node --test，以免混入复制保留的旧版测试。

主控：Tests/run_hmi_control_tests.ps1、run_hmi_state_tests.ps1、run_hmi_modbus_tests.ps1及联合/快照/看门狗测试入口；Tests/build_hmi_control.ps1 -Artifacts <绝对输出目录>重建六配置。AXF经ARMCC fromelf --bin转换为同配置BIN。生成器所用31号文本和32号布局基线保持原样。

最终屏端PIX SHA256：{compiled['PIX_SHA256']}

最终屏端BIN SHA256：{compiled['BIN_SHA256']}

配套双轴主控BIN SHA256：{sha(F/(variant+'.bin'))}

全部产物及源码以33号最终SHA256.json、配套交付SHA256.json和主控firmware/SHA256SUMS.txt核验。
'''
burn='''# 33号配套烧录与回退说明

当前是配套台架调试版，已完成软件及编译验证；实屏时序、台架及实车待验收。文件已生成，本轮未执行硬件烧录。

## 本轮配套文件

33号配套调试交付目录集中放置本轮文件：33号屏端.pix、33号屏端.pix.bin，以及F407_33号配套_双轴_启动保持.hex/.bin/.axf。它们与33号工程根目录PIX/BIN及主控Docs/HMI33_20261102/firmware/hmi_control_axis_3_hold_1_hmi_1_readonly_0完全一致，哈希见配套交付SHA256.json。

屏端使用原型号SANY-WS-VS10-04M-R，1024×600、GBK、115200及原连接方式。通过原Visual-PIX流程下载本轮屏端BIN。修改PIX后必须重新编译并重新生成哈希，不能沿用旧BIN。不要把同目录复制保留的31/32历史烧录文件误当33号产物。

配套F407固件采用双轴、启动保持启用、HMI控制启用、非只读配置；HEX自带地址，BIN基地址为0x08000000。程序链接末端0x0804E330。下载只擦写程序所需扇区，保留Flash扇区11的0x080E0000～0x080FFFFF启动身份/配置追加日志，禁止全片擦除破坏启动身份记录。

先在模拟从站＋实屏验证，再低速双轴台架，最后实车。屏端和主控分别烧录后的实际文件SHA256、主控版本、日期、连接参数及UART日志须记入同一验收编号。未通过实屏200ms/5ms/150ms等门槛不能认定实车验收通过。

## 上电与重新授权

主控和屏端同步后，从无活动会话、无锁定且连续零速确认状态人工授权。每次新授权从零开始，重新滑动才产生非零目标。失联、冻结快照、主控重启或会话不匹配后不恢复旧目标。其他控制源占用时不自动抢权。

急停为软件受控停车，不等同硬件急停/STO。原有硬件安全操作流程继续有效。

## 回退

原31/32目录、BIN及哈希保留。需要复现基线时，使用32号原BIN和主控f33b011a753c6a923345d06ea7b7c773c6ec9883对应配置固件作为一组；32号存在本次已确认的越界及状态风险，回退用途是受控对比或恢复开发基线，不是推荐生产版本。

主控历史源码通过本地Git基线检出到独立目录重建，不用git reset --hard覆盖当前工作区。配置须与当前车辆一致，并继续保留身份日志区。回退后断电重启并人工重新授权，不能复用新版本会话。未从既有目录挑选未经核验的旧主控BIN，也未在本轮伪造一个基线烧录产物。
'''
accept='''# 33号台架验收与UART视频索引

所有硬件条目当前均为待验收。本轮没有新33号实屏录像、UART5实物抓帧或实车报告。

## 分阶段门槛

依次实施模拟从站＋实屏、低速双轴台架、实车。每阶段均冻结屏端与主控文件哈希，视频和UART使用同一编号及共同时间标记。

| 测量项 | 通过要求 | 当前结果 |
|---|---|---|
| 健康关键状态更新/目标接受间隔 | 最大≤200ms，记录分布及最大值 | 待实测 |
| 单批显示脚本 | 实屏最大≤5ms，包含首次及主题路径 | 待实测；宿主未达此值 |
| 急停触摸→UART请求起始 | ≤150ms，多次无丢失 | 待实测 |
| 普通按钮可见反馈 | ≤150ms | 待实测 |
| 页面切换 | 300ms内可见，500ms内业务字段完成 | 待实测 |
| 导航/主题/诊断混合操作 | ≥1000次，无卡死、内存不持续增长 | 待实测 |
| 运行及逐页操作 | ≥30分钟，无无操作自动撤权 | 待实测 |
| 断链/丢帧/恢复 | 原TTL停车、历史值保持、恢复后人工授权 | 待实测 |
| 急停/取消/恢复/授权/K5/CAN | 三端停车恢复及仲裁不退化 | 待实测 |

若首次显示/主题批次或健康间隔失败，停留在该阶段继续修复，不能以平均值、宿主耗时或模型结果替代。还需记录伺服/HMI/CLI等任务栈高水位。

## 主控诊断

CLI执行只读命令hmi trace。统计行包括total/count、max_target_gap_ms、first_anomaly_ms、gap_ms。事件包括at（真实HAL毫秒）、kind、session、seq、age（目标年龄）、lease（剩余租约）、stop（事件号）、reason和error。kind1=授权建立，2=目标接受，3=停车触发，4=停车完成，5=拒绝。停车reason3=目标TTL，4=会话租约，其他值按当前仲裁源码解释。

环固定64条，目标高频更新会覆盖较早事件；异常发生后立即保存CLI和UART。max_target_gap_ms统计同会话相邻有效目标的接收时间间隔，第一次>200ms异常另存，重启清除。若目标永久断流、不再接收下一个目标，最大间隔不会凭空增加，应结合停车reason和UART最后目标时间判断。CLI逐条读取有临界区，但整个64条输出不是全环原子快照；运行时可能滚动。

UART5应被动记录双向RTU原始字节、帧起止时间、方向、CRC、站号/功能码、关键读取64/96长度、启动标识、快照、会话、目标序号、目标年龄、租约、停车事件/原因。串口抓帧设备不得成为额外主站并发发包。显示质量3秒窗口与运动300/500/1000ms安全期限分开核对。

## 索引模板

| 编号 | 阶段/场景 | PIX/BIN/主控SHA256 | 视频路径/时间标记 | UART路径 | CLI路径 | 最大间隔/响应 | 结论 |
|---|---|---|---|---|---|---|---|
| H33-S01 | 模拟从站＋实屏、主题及导航 | 待填 | 待填 | 待填 | 待填 | 待填 | 待验收 |
| H33-B01 | 低速双轴30分钟与逐页操作 | 待填 | 待填 | 待填 | 待填 | 待填 | 待验收 |
| H33-F01 | 断链/急停/取消/重授权 | 待填 | 待填 | 待填 | 待填 | 待填 | 待验收 |
| H33-V01 | 实车30分钟 | 待填 | 待填 | 待填 | 待填 | 待填 | 待验收 |

32号原始实物视频索引见运行核验/33号实施_20261102/视频与UART证据索引.json。它是故障输入证据，不是33号验收证据。
'''
for name,body in [('33号修复与回归报告.md',report),('33号配套烧录与回退说明.md',burn),('33号台架验收与UART视频索引.md',accept)]:
 put(R/name,body);put(D/name,body);shutil.copy2(R/name,P/name)
screen=D/'screen-source';screen.mkdir(exist_ok=True)
source_files=['build33.py','apply33.ps1','verify-saved33.ps1','编译33号工程.ps1','hmi33.test.cjs','audit33.py','native33.ps1','prepare-native33.cjs','audit-layout33.py','HMI33事务服务.txt','HMI33显示刷新.txt','HMI33自定义函数.txt','全局计龄刷新.txt','17_轨道巡检车.pix']
for f in source_files:shutil.copy2(R/f,screen/f)
for directory in ['事件33','事件']:
 dest=screen/directory;dest.mkdir(exist_ok=True)
 names=[p.name for p in (R/directory).glob('*.txt')] if directory=='事件33' else ['授权_按下.txt','授权_弹起.txt','急停_按下.txt','滑块_滑动.txt','故障恢复按钮_按下.txt']
 for name in names:shutil.copy2(R/directory/name,dest/name)
checks=['33号回归最终.log','33号保存源码一致性.json','33号边界绑定核验.json','33号原生显示性能.json','33号原生执行.log','33号快照高字回退修复前.log','33号布局核验.log','33号布局字模核验.json','33号VisualPIX编译.log','33号编译产物.json','31_32最终保护复核.json','主控基线.txt','主控修改前近10条提交.txt','33号状态索引.json','33号显示字段.json','33号九页日夜总览.png']
dest=D/'screen-validation';dest.mkdir(exist_ok=True)
for name in checks:shutil.copy2(E/name,dest/name)
baseline=R.parent/'32_轨道巡检车状态显示稳定修复版'
parts=[]
for name in ['全局计龄刷新.txt','事件/授权_按下.txt','事件/授权_弹起.txt','事件/急停_按下.txt','事件/滑块_滑动.txt','事件/故障恢复按钮_按下.txt']:
 if (baseline/name).exists():parts.extend(difflib.unified_diff(read(baseline/name).splitlines(True),read(R/name).splitlines(True),fromfile='32/'+name,tofile='33/'+name))
put(E/'33号屏端相对32源码差异.patch',''.join(parts));shutil.copy2(E/'33号屏端相对32源码差异.patch',D/'33号屏端相对32源码差异.patch')
patch=subprocess.run(['git','diff','HEAD','--binary','--','APP','Tests'],cwd=M,stdout=subprocess.PIPE,check=True).stdout
(D/'source-before-commit.diff').write_bytes(patch);(E/'主控源码差异.patch').write_bytes(patch)
videos=[Path('D:/15056/微信/xwechat_files/wxid_6j6lt9vq6gut22_5eaf/temp/RWTemp/2026-11/9e20f478899dc29eb19741386f9343c8/77b66153b6ede8a23241ec10a75796ba.mp4')]
entries=[]
for v in videos:entries.append({'Purpose':'32号故障输入视频；不是33号验收','Path':str(v),'Exists':v.exists(),'SHA256':sha(v) if v.exists() else None})
evidence={'InputVideos':entries,'HMI33Videos':[],'HMI33UARTCaptures':[],'HMI33PhysicalAcceptance':'待验收'}
put(E/'视频与UART证据索引.json',json.dumps(evidence,ensure_ascii=False,indent=2));shutil.copy2(E/'视频与UART证据索引.json',dest/'视频与UART证据索引.json')
put(D/'README.md','''# HMI33配套联动修复

交付等级：台架调试版。屏端22项软件测试及最终Visual-PIX编译通过；主控六配置全量编译0错误0警告。宿主显示压力单批最高171.339ms、稳定阶段最高34.932ms，实屏5ms等全部硬件门槛尚待验收。

先读33号修复与回归报告.md，再读配套烧录与回退说明及台架验收索引。screen-source保存冻结的33号PIX及修改源码，screen-validation保存软件证据。完整可重现工程在硬件资料/用户代码/33_轨道巡检车调度与会话稳定修复版，生成器依赖受保护31号源码和32号布局及继承工具。

firmware六配置构建输出保存在本机；仓库按现有规则忽略AXF/HEX/BIN/MAP/HTM，仅提交日志、SHA256及屏端源码/PIX。双轴hold1为本轮配套配置，主控补丁号2。没有执行推送或实物烧录。
''')
firmware_root=D/'firmware'
put(firmware_root/'SHA256SUMS.txt','\n'.join(f'{sha(p)}  {p.relative_to(firmware_root).as_posix()}' for p in sorted(firmware_root.rglob('*')) if p.is_file() and p.name!='SHA256SUMS.txt')+'\n')
manifest=[{'File':p.relative_to(P).as_posix(),'Bytes':p.stat().st_size,'SHA256':sha(p)} for p in sorted(P.iterdir()) if p.is_file() and p.name!='配套交付SHA256.json']
put(P/'配套交付SHA256.json',json.dumps(manifest,ensure_ascii=False,indent=2));shutil.copy2(P/'配套交付SHA256.json',D/'配套交付SHA256.json')
put(E/'33号版本状态.json',json.dumps({'Level':'配套台架调试版','SoftwareRegression':{'Passed':22,'Failed':0},'PIXCompile':'passed','F407BuildVariants':6,'F407BuildErrors':0,'F407BuildWarnings':0,'HostDisplayMaxMs':max(x['MaxMs'] for x in perf),'HostDisplaySteadyMaxMs':max(x['SteadyMaxMs'] for x in perf),'ScreenTimingAcceptance':'pending','BenchAcceptance':'pending','VehicleAcceptance':'pending','Baseline':'f33b011a753c6a923345d06ea7b7c773c6ec9883','Branch':'fix/hmi33-scheduling-session'},ensure_ascii=False,indent=2))
shutil.copy2(E/'33号版本状态.json',dest/'33号版本状态.json')
files=source_files+['33号修复与回归报告.md','33号配套烧录与回退说明.md','33号台架验收与UART视频索引.md']+[str(p.relative_to(R)) for p in E.iterdir() if p.is_file() and p.name not in ['33号最终SHA256.json','desktop33.png']]
hashes=[{'File':name,'SHA256':sha(R/name),'Bytes':(R/name).stat().st_size} for name in sorted(set(files))]
put(E/'33号最终SHA256.json',json.dumps(hashes,ensure_ascii=False,indent=2))
print(json.dumps({'Delivery':str(P),'ScreenBIN':compiled['BIN_SHA256'],'F407BIN':sha(F/(variant+'.bin')),'Status':'debug; physical acceptance pending'},ensure_ascii=False))
