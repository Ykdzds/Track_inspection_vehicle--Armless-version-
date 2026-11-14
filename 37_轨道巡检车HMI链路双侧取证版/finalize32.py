"""Produce delivery index from final saved PIX, actual BIN and verification logs."""
from pathlib import Path
import json,hashlib,re,shutil
R=Path(__file__).resolve().parent;E=R/'运行核验/32号实施_20261102';B=R.parent/'31_轨道巡检车日夜双主题切换版'
def j(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def put(p,x):p.write_text(json.dumps(x,ensure_ascii=False,indent=2),encoding='utf-8')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
m=j(E/'32号实际工程读回.json');b=j(B/'运行核验/31号实施_20261101/31号实际工程读回.json')
events=[]
for p,q in zip(b['Pages'],m['Pages']):
 for a,c in [(p,q)]+[(a,next(x for x in q['Controls'] if x['Name']==a['Name'])) for a in p['Controls']]:
  if a['Events']!=c['Events']:events.append([p['Name'],a['Name']])
assert events==[['P0_Boot','refreshTick']]
resources=j(B/'运行核验/31号实施_20261101/资源索引绑定.json')
source=(R/'全局计龄刷新.txt').read_text(encoding='utf-8-sig')
resolved=re.sub(r'user\.(image|font)\.[A-Za-z0-9_]+',lambda v:str(resources[v[0]]),source)
actual=next(c for c in m['Pages'][0]['Controls'] if c['Name']=='refreshTick')['Events'][0]['Code']
assert actual==resolved
protected=j(E/'31号保护基线SHA256.json');changes=[x['File'] for x in protected if sha(B/x['File'])!=x['SHA256']]
assert not changes
put(E/'32号绑定与基线保护核验.json',dict(ProtectedFiles=len(protected),Changed=changes,ChangedEvents=events,DisplayBindingMatches=True))
prod=j(E/'32号编译产物.json')
assert sha(R/prod['PIX'])==prod['PIX_SHA256'] and sha(R/prod['BIN'])==prod['BIN_SHA256']
native=j(E/'32号VP原生执行核验.json');layout=j(E/'32号布局字模核验.json')
assert layout['Issues']==[]
log=(E/'32号安全与显示回归.log').read_text(encoding='utf-8-sig')
assert 'ℹ fail 0' in log and 'ℹ tests 235' in log
report=f'''# 32号车载屏回归报告

32号由31号独立复制。31号全部1217个文件的SHA256保持不变。

## 修复行为

123个显示字段使用有效候选提交。屏幕原有文本控件保存最近有效文本和颜色，独立数组保存候选、待确认状态、everValid、最近有效时刻、年龄、stale及控制器代次。无新帧、轮询切换、CRC错误、错站、错功能码、错长度及哨兵值不清空已有效数据。字段从未有效或明确重启/会话/基准边界后才显示初始占位符。

伺服页linkStatus由全局刷新事务独占；3秒无有效来源才显示超时，统一快照不再覆盖它。位置里程数字保持，质量状态按3个独立样本确认。诊断字段取消2秒交替含义。查询页面右上角单独提示数据正常、历史年龄或通信超时。

显示使用屏端接收时刻；主控内部采样年龄仍保留在原诊断字段。会话变化清命令历史；物理里程仅在主控重启、明确基准代次变化时清理。合法零和负速度保留；单轴未参与明确显示未参与。

## 验证结果

- 最终235项软件测试通过（含11项显示专项、31/32帧与时间及安全数组等价、授权、急停、失联撤权、恢复、九页导航）。
- 最终保存PIX在Visual-PIX原生引擎通过{native['Assertions']}项断言；10秒断流仍保持最近值，恢复零速正确。
- 九页日夜18组原生状态、390项字模和布局检查无问题；最低文字对比度{layout['MinContrast']:.2f}。
- 事件模型对比：只改变P0_Boot.refreshTick；原事务服务、协议解析、输入、导航、急停、恢复事件逐字节一致。
- Visual-PIX 1.7.2.179实际Ctrl+B编译成功。BIN {prod['BIN_Length']}字节。
- 缓存字符串单项192字节；原生检查最长文本85个GBK字节。未超过编辑器数组总容量限制。

## 性能证据与限制

无新数据时已取消全字段遍历/重复字符串转换，最终200次原生刷新约{native['Native200RendersMs']}毫秒。独立压力测试记录位于32号原生刷新性能.json：无新帧约21.7ms/次、每150ms更新状态约47.8ms/次；所有区域同时每50ms更新的非实际轮询压力约285.8ms/次。Windows解释引擎耗时不能作为屏端处理器50ms调度保证；实际串口服务预算仍需实屏UART时间戳测量。模拟请求的目标/状态间隔150ms且唯一在途。

## 尚未执行的现场验收

尚未烧录实屏、未实车运行、未采集32号UART5或运行录像。需执行32号烧录与实屏验收.md；用户输入视频只作为31号问题基线。字体图是原生事件状态结合正式字模的重绘，不是实屏截图。
'''
(E/'32号回归报告.md').write_text(report,encoding='utf-8')
delivery=f'''# 32号车载屏交付说明

工程：`{R.name}`。使用本目录的PIX和BIN，31号保持原样。

|产物|SHA256|
|---|---|
|{prod['PIX']}|{prod['PIX_SHA256']}|
|{prod['BIN']}|{prod['BIN_SHA256']}|

BIN大小：{prod['BIN_Length']}字节。Visual-PIX实际编译成功。

修复：九页历史有效值保持、独立通信质量、位置状态滞回、伺服通信单一写入者。显示缓存不用于任何运动安全判断。

验证：235项软件回归、2616项原生断言、390项文字布局检查通过；31号1217个文件无变更。

实屏烧录、50ms实际调度测量、10分钟车辆运行、UART5抓帧和同步录像待现场验收。详见同目录回归报告和验收清单。

目录中继承的08至31号脚本、BIN及报告仅为历史材料；本轮证据只使用运行核验/32号实施_20261102。
'''
(E/'32号交付说明.md').write_text(delivery,encoding='utf-8')
(R/'32号交付说明.md').write_text(delivery,encoding='utf-8')
(R/'README_32.md').write_text('# 32号车载屏状态显示稳定修复版\n\n入口：32号交付说明.md。正式工程17_轨道巡检车.pix，烧录文件17_轨道巡检车.pix.bin。所有32号证据见运行核验/32号实施_20261102。31号工程未修改。\n',encoding='utf-8')
# Preserve compact problem-video evidence rather than copying the large original.
temp=Path('C:/Users/15056/AppData/Local/Temp/codex-hmi31-video-analysis/overview.png')
if temp.exists():shutil.copyfile(temp,E/'31号输入视频抽帧总览.png')
manifest=[]
for p in [R/prod['PIX'],R/prod['BIN'],R/'全局计龄刷新.txt',R/'build-display32.py',R/'apply-display32.ps1']:
 manifest.append(dict(File=str(p.relative_to(R)),Bytes=p.stat().st_size,SHA256=sha(p)))
put(E/'32号交付SHA256.json',manifest)
(E/'SHA256SUMS.txt').write_text('\n'.join(x['SHA256']+'  '+x['File'] for x in manifest)+'\n',encoding='utf-8')
print(json.dumps(dict(Tests=235,NativeAssertions=native['Assertions'],Protected=len(protected),Changed=len(changes),BIN=prod['BIN_Length'],PIX_SHA256=prod['PIX_SHA256'],BIN_SHA256=prod['BIN_SHA256']),ensure_ascii=False))
