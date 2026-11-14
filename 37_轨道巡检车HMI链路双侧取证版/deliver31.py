"""Write the final HMI31 handoff from measured, recorded evidence."""
from pathlib import Path
import json,hashlib,re,datetime
R=Path(__file__).resolve().parent;E=R/'运行核验/31号实施_20261101'
def read(n):return json.loads((E/n).read_text(encoding='utf-8-sig'))
def write(p,s):p.write_text(s,encoding='utf-8')
c=read('31号编译产物.json');a=read('31号布局字模与保护核验.json');v=read('31号最终产物核验.json');native=read('31号VP原生执行核验.json');reg=read('31号独立回归.json');timing=read('31号模拟时序汇总.json')
assert not a['Issues'] and not a['ProtectedChanged'] and reg['fail']==0
assert native['SliderValues']==6402 and native['TouchLayer']['AllRangeHitChecks']==16005
assert all(v[k] for k in ['ModelIdenticalAfterReopen','PIXHashMatchesCompile','BINHashMatchesCompile'])
for k in ['PIX','BIN']:assert hashlib.sha256((R/c[k]).read_bytes()).hexdigest().upper()==c[k+'_SHA256']
report=f'''# 31号双主题、单行速度与滑块提示回归报告

本版以用户已实测的30号为输入完整复制，只修改31号。正式工程为本目录 `17_轨道巡检车.pix`，固件为 `17_轨道巡检车.pix.bin`。VisualPIX 1.7.2.179实际编译成功，保存、关闭、重开后读回模型一致。**软件交付完成；实屏、车辆运动与物理UART验收待现场完成。**

## 完成的行为

九页均支持白色日间／深蓝夜间。上电变量 `P0_Boot.themeMode` 默认0；主页普通按钮位于(384,4)、176×52，仅弹起切换，跨页保持，无Flash持久化。标题宽344，其余入口位置保留。背景、文字底色、状态色、三态按钮和滑块游标同步更新。

P8左右轴实际、最终目标、执行输出、本地设定和主控接受值完整单行。实际反馈独立使用左右轴数据，原两个辅助单位控件ID和名称保留、隐藏。实际／原生rpm使用24px，线速度来源的等效转速使用20px；无效状态保留原提示。两栏x=32／536、宽456，授权／急停／恢复／主页的操作顺序和触摸尺寸保留。

滑块0～3200仍映射−1600～1600rpm，32px游标行程944px。提示仅读取当前滑块，显示三位小数；正值在左且右对齐，零／负值在右且左对齐，可见文字距游标12px。原调速guard和目标赋值完整保留；不允许时显示“预览”，不会写有效目标。页面加载、原刷新事件及值改变事件同步更新，无新定时器、串口请求或动画。

## 软件核验结果

| 核验 | 结果与证据范围 |
|---|---|
| 原屏端回归 | 477／477，无失败或跳过；`最终477项回归.txt`。其中历史产物检查仍读取对应历史基线，31号另有独立验收 |
| 只读显示回归 | 39／39；`最终39项显示回归.txt`，等价脚本执行 |
| 31号独立检查 | 6组通过，包含绑定、原业务保护、主题、6402次提示、guard和速度边界；`31号独立回归.json` |
| VP原生事件 | 6402次允许／预览提示，36次主题页面检查、4次切换及108组页面状态通过；直接加载最终PIX，用VP自带编译事件引擎执行，未打开物理端口 |
| 游标映射 | VP原生WPF模板五点绝对left=24／260／496／732／968，游标宽32 |
| 原生命中 | 全3201值×5位置=16005次，标签左／中／右及轨道两端的最上层对象均为rpmSlider |
| 字模／布局 | 两主题×九页×正常、未知、过期、故障、禁用、长文本=108组；{a['TextChecks']}项文字及边界检查，零缺字／溢出。速度极值宽度最大438／456px |
| 对比度 | 所有检查状态与按钮正常／按下／禁用皮肤，最低{a['MinContrast']:.3f}∶1，达到4.5∶1 |
| 同工况控制保护 | 30／31四个控制数组及逐字节请求帧、发送时间一致；唯一在途事务1 |
| 模拟跨页时序 | 21组、各页120秒；目标最大间隔{timing['MaxTargetGapMs']}ms，状态最大间隔{timing['MaxStatusGapMs']}ms。属于模拟50ms节拍结果 |
| 定时器 | refreshTick与h13Service读回Interval均为5，即50ms，未新增定时器 |
| 保护哈希 | 2520文件全部一致：30号641、F407 929、遥控315、F103 635 |
| 资源预留 | BIN {c['BIN_Length']:,}字节，按型号最低4,000,000字节规格预算余量{v['ReservePercent']:.2f}%，超过20%；未读取实屏容量 |

## 实施时发现并修正的问题

VP脚本水平对齐枚举中2才对应RIGHT；已由原生事件读回确认。提示三位小数改为整数定点，避免VP浮点格式化在极少边界误舍入，最大乘法不超int32。该改动只作用于滑块提示，原速度和控制换算沿用30号。

VP按控件ID绘制／命中，移动集合顺序无效。因此原rpmSlider名称和所有事件、值域、32px游标及触摸尺寸保留，**ID由22迁到36**，新增标签ID35置于其下。原两个辅助单位控件ID29／30保持。滑块使用透明静态图片，底层轨道由主题背景绘制，标签禁用输入、无键盘和业务触摸事件。工程所有引用按名称重新编译；此ID变化已列入差异，未发现外部使用22的协议依赖。

夜间主按钮按下色压深至#1A7195，满足白字对比度。删除同一刷新中会被最终文本覆盖的旧本地目标赋值，避免短暂冗余显示。两份显示源和实际事件绑定已同步，生成器在独立目录从30号生成的事件与交付源忽略空白后一致。

## 图像、差异与证据解释

九页日夜对照、108张状态图、P8典型速度／16位极值、滑块五点和−1／0／+1图，使用**VP原生事件状态、PIX实际内嵌图片及4位alpha字模重绘**。速度边界补充图使用等价脚本边界值。图像用于布局审阅，不能作为实屏截图或触摸测试记录。`最终VP编译界面.png`和`最终VP重开界面.png`为真实编辑器截图。

`30到31控件差异.csv`列全部属性变化、添加对象及唯一原控件ID迁移；`30到31事件差异.json`包含页面加载、刷新、滑块和新按钮事件源码及哈希。原215个控件保留，新加8个对象，总223。

外部无串口的原生WPF模板测试中，VP图片转换器因未初始化编辑器资源路径记录两条NullReference诊断；其后原生事件、几何和命中断言完成。正式VP编译与重开成功，图像审计直接从PIX导出嵌入素材。这些诊断保留在原生日志中供追溯。

## 最终产物哈希

PIX SHA256：`{c['PIX_SHA256']}`

BIN SHA256：`{c['BIN_SHA256']}`

编译时间：{c['CompiledAt']}。编译器报告内部总长度比BIN少104字节，为外层封装头；容量预算按完整BIN。

## 尚待现场验收

目标屏实际触摸／拖动（尤其从文字覆盖处起拖、快速往返、点击两端）、旧区域恢复无残影闪烁、两主题观感、断电默认白色、下载及长时运行、物理UART实际间隔和车辆各安全动作仍待测，详见`31号烧录与实屏验收.md`。原生模型命中与软件模拟不能替代这些测试。
'''
write(R/'31号回归报告.md',report)
handoff=f'''# 31号交付说明

正式工程：[17_轨道巡检车.pix](17_轨道巡检车.pix)；烧录固件：[17_轨道巡检车.pix.bin](17_轨道巡检车.pix.bin)。九页日夜主题、P8单行三单位及滑块跟随km/h提示已实现，VP实际编译与保存关闭重开通过。

已通过477项原回归、39项显示回归、31号6组独立核验、6402次VP原生提示、16005次原生命中及108组页面字模检查。30号和其他三端共2520保护文件哈希一致。BIN {c['BIN_Length']:,}字节，保守资源余量{v['ReservePercent']:.2f}%。

请先读[回归报告](31号回归报告.md)，再按[烧录与实屏验收表](31号烧录与实屏验收.md)现场填写。实屏触摸、断电恢复及物理串口／运动验收尚未执行。

界面审阅：[九页日夜总览](运行核验/31号实施_20261101/九页日夜总览.png)、[P8日夜对照](运行核验/31号实施_20261101/P8_ServoControl_日夜对照.png)、[典型速度与极值](运行核验/31号实施_20261101/P8典型速度与极值.png)、[滑块五点与跨零](运行核验/31号实施_20261101/P8滑块五点与跨零.png)。这些是正式资源／字模重绘，非实屏截图。

维护时注意原rpmSlider ID22→36用于保证标签下方触摸穿透，名称、业务事件和控制条件保持；辅助单位ID29／30保留隐藏。两份显示源为`HMI13界面刷新.txt`及`全局计龄刷新.txt`，实际绑定见报告与差异。两个原定时器均50ms。

当前交付以`31号交付索引.json`和`31号交付SHA256.json`为准。本目录从30号完整复制，保留13～30号历史文件，旧说明、旧哈希、探索脚本不能代表当前31号。不要运行历史重建／创建脚本覆盖正式工程。常用只读检查为`verify31.cjs`、`native-verify31.ps1`、`audit-layout31.py`；VP反射脚本需Windows PowerShell 5.1 -STA及UTF-8 BOM。

PIX SHA256：`{c['PIX_SHA256']}`

BIN SHA256：`{c['BIN_SHA256']}`
'''
write(R/'31号交付说明.md',handoff)
items=[('启动与默认','断电再上电，初始化／自动跳页与30号一致，默认白色；P4显示屏幕工程31'),('主题切换','主页按下不切换、弹起切换一次；连续20次日夜切换，九页背景／文字／按钮／游标同步，跨页保持；再次断电恢复白色'),('主题观感','日夜九页逐一查看正常、未知、过期、故障、禁用和最长文本；无白底白字、缺字、截断；白色舒适，深蓝清晰'),('左右独立','左540rpm显示1.962m/s／7.062km/h，右538显示1.954／7.035；改变任一反馈不得串栏'),('单行三单位','0、±1、±100、±540、±1600及有效16位极值分别验证；三单位完整单行，负号正确；32767无效哨兵及过期显示--／原失效状态'),('来源与字号','主控原生rpm24px，线速度来源标注等效转速且20px；主控接受值与本地设定分辨明确'),('五点游标','val=0／800／1600／2400／3200对应thumbLeft=24／260／496／732／968，32px宽；保留原范围'),('提示跟随','正值文字在左右对齐，负值／零在右左对齐；−1／0／+1切侧，间距12px，三位小数、负号和0.000正确'),('标签处起拖','从标签文字中心／边缘开始拖动、点击轨道及两端，快速往返；与30号触摸一致，无阻挡或跳变'),('重绘质量','标签移动后旧区域恢复轨道底色，无残影、闪烁、背景缺口；切页／切主题后同样检查'),('未授权预览','目标保持原有效值；提示仅预览，不冒充主控接受值；所有保护条件阻止调速'),('授权与归零','首次授权、停车、恢复、失联后归零及程序恢复滑块位置均同步提示；不意外写目标'),('跨页保持','授权调速后七个查询页各运行120秒，往返20轮，目标／会话保持且恢复已失效目标被禁止'),('单轴与控制源','左／右／双轴、HMI／遥控／CAN分别核对；未参与轴保留状态，屏端不夺权'),('停车与恢复','取消、安全急停、失联、反馈冻结、PB10、K5含重复触发按30号原流程停车；恢复条件、重新授权和零目标正确'),('物理UART时序','抓取30／31同工况：控制数组及帧一致、唯一在途事务、目标最大间隔≤200ms、状态读取最大间隔≤150ms；记录抓帧文件'),('容量及长时','确认实屏容量、下载成功；两主题切换及连续运行无资源错误，实际资源余量≥20%')]
table='\n'.join(f'| {name} | {step} | 待测 |' for name,step in items)
write(R/'31号烧录与实屏验收.md',f'''# 31号烧录与实屏验收表

全部现场项目尚未执行。软件、原生引擎和字模结果见[回归报告](31号回归报告.md)，不能替代目标屏和车辆验收。

型号：SANY-WS-VS10-04M-R，1024×600横屏。通信配置沿用30号，GBK、115200。只下载本目录`17_轨道巡检车.pix.bin`，其他三端继续当前实测程序。下载前用`Get-FileHash -Algorithm SHA256 -LiteralPath '完整BIN路径'`核对：

`{c['BIN_SHA256']}`

用VisualPIX打开本目录正式PIX下载；上电后P4应显示“屏幕工程：31”。需要回退时使用原30号正式BIN，无需改变其他三端。

| 项目 | 操作与通过标准 | 结果／证据 |
|---|---|---|
{table}

验收人员：________ 日期：________ 屏型号／序列号／容量：________

F407／遥控／F103版本哈希：________ BIN哈希：________ 抓帧路径：________

未通过项、现象、复测结果：________
''')
readme=R/'README.md';old=readme.read_text(encoding='utf-8-sig');cut=old.find('# 29号车载屏')
write(readme,'# 当前正式交付：31号日夜双主题切换版\n\n'+handoff.split('\n',1)[1]+'\n---\n\n以下为从30号复制的历史说明，仅供追溯。\n\n'+old[cut:] if cut>=0 else handoff)
index=dict(Version=31,CompletedSoftware=True,PhysicalAcceptanceComplete=False,Compile=c,FinalVerification=v,Regression={'Original':477,'ReadonlyDisplay':39,'IndependentGroups':6,'NativeSliderEvents':6402,'NativeHitChecks':16005,'NativePageStates':108},Protection={'Files':2520,'Changed':[]},Presentation={'MinContrast':a['MinContrast'],'TextChecks':a['TextChecks'],'Issues':[]},SliderIdMigration={'Name':'rpmSlider','From':22,'To':36,'Reason':'VP numeric-ID z-order; original input surface over transparent read-only text'},Artifacts=['17_轨道巡检车.pix','17_轨道巡检车.pix.bin','31号交付说明.md','31号回归报告.md','31号烧录与实屏验收.md','运行核验/31号实施_20261101/九页日夜总览.png','运行核验/31号实施_20261101/P8典型速度与极值.png','运行核验/31号实施_20261101/P8滑块五点与跨零.png','运行核验/31号实施_20261101/30到31控件差异.csv','运行核验/31号实施_20261101/30到31事件差异.json'])
write(R/'31号交付索引.json',json.dumps(index,ensure_ascii=False,indent=2))
files=[R/x for x in index['Artifacts']]+[R/'README.md',R/'31号交付索引.json']
files += sorted(E.glob('P*_日夜对照.png'))
files += [E/x for x in ['31号编译产物.json','31号最终产物核验.json','31号实际工程读回.json','保护基线SHA256.json','31号布局字模与保护核验.json','31号独立回归.json','31号VP原生执行核验.json','31号VP原生显示状态.json','最终477项回归.txt','最终39项显示回归.txt','31号VisualPIX编译.log']]
hashes=[dict(File=str(p.relative_to(R)),Bytes=p.stat().st_size,SHA256=hashlib.sha256(p.read_bytes()).hexdigest().upper()) for p in files]
write(R/'31号交付SHA256.json',json.dumps(hashes,ensure_ascii=False,indent=2))
print(json.dumps(dict(DeliveryFiles=len(hashes),BIN=c['BIN_Length'],ReservePercent=v['ReservePercent'],PhysicalPending=True)))
