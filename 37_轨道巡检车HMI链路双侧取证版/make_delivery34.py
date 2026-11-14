from pathlib import Path
import hashlib
import json
import subprocess
from datetime import datetime

HERE = Path(__file__).resolve().parent
EVIDENCE = HERE / "运行核验" / "34号实施_20261103"
DELIVERY = HERE / "34号交付"
ROOT = HERE.parents[3]
REPO = ROOT / "5.工程代码" / "RailInspectionCar_Inno_V1"
DELIVERY.mkdir(exist_ok=True)

def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return {"path": str(path), "bytes": path.stat().st_size, "sha256": h.hexdigest().upper()}

screen = [HERE / "17_轨道巡检车.pix", HERE / "17_轨道巡检车.pix.bin"]
fwroot = REPO / "Docs" / "HMI33_20261102" / "firmware" / "hmi_control_axis_3_hold_1_hmi_1_readonly_0"
firmware = [p for p in fwroot.glob("*") if p.suffix in {".bin", ".hex", ".axf", ".log"}]
for p in screen + firmware:
    target = DELIVERY / p.name
    target.write_bytes(p.read_bytes())
manifest = [digest(DELIVERY / p.name) for p in screen + firmware]
(EVIDENCE / "34号最终SHA256.json").write_text(json.dumps({"generatedAt": datetime.now().isoformat(), "artifacts": manifest}, ensure_ascii=False, indent=2), encoding="utf-8")

status = subprocess.check_output(["git", "status", "--short"], cwd=REPO, text=True, encoding="utf-8", errors="replace")
head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO, text=True, encoding="utf-8").strip()
last10 = subprocess.check_output(["git", "log", "-10", "--oneline", "--decorate"], cwd=REPO, text=True, encoding="utf-8", errors="replace").splitlines()
(EVIDENCE / "主控基线复核.json").write_text(json.dumps({"head": head, "status": status, "last10": last10, "sourceChangedFor34": False, "pushed": False}, ensure_ascii=False, indent=2), encoding="utf-8")

(HERE / "34号修复与回归报告.md").write_text("""# 34号车载屏跨页运行、触控响应与滑块提示修复报告

本轮从33号工程复制建立34号独立工程，31～33号目录和产物保持不变。修改集中在屏端：单一全局调度、跨页持续运行、滑块候选值补偿、急停锁存重试和随滑块移动的速度提示。没有修改F407源码，没有改变R2协议或300/500/1000 ms安全期限，沿用33号配套双轴主控固件。

## 已实现

- 离开页面和返回主页只改变显示页，不发送释放、零目标或停车命令；返回伺服页恢复当前会话滑块位置。
- 滑块单击、拖动和漏回调通过采样补偿；候选值按代次合并，零目标确认期间的最后输入在确认后提交。
- 急停按下锁存独立代次，弹起不重复生成；写失败或丢应答用同一序号重试，确认前禁止非零目标。
- 提示按滑块位置即时计算，保持12像素间距；未确认显示预览，确认当前候选后显示设定。
- 实际数组容量 h13=180、h13Status=96、u34=43；串口写入者只有 P0_Boot.refreshTick。

## 验证结果

- 新增边界用例17/17通过，继承33号回归22/22通过。
- Visual-PIX保存重读通过；事件绑定、151个34号自定义函数和数组容量一致。
- 静态审计9989处引用，0越界，唯一串口写入者为P0_Boot.refreshTick。
- Visual-PIX编译成功；PIX 963118字节，BIN 3115147字节。
- 原生触摸模型：12804个滑块值、21个命中点、9次跨页生命周期全部通过。
- 原生显示9页乘日夜18场景，390项文字检查，最小对比度5.9477，无溢出、缺字或颜色问题。
- F407既有HMI、CAN/HMI、遥控联动和K5回归通过。状态测试脚本曾受仓库编码影响，已用PowerShell 7重新执行并记录结果。

## 验收边界

当前是软件、编译和模拟验证通过的34号调试产物。硬件尚未访问，实屏5 ms显示批次、150 ms急停响应、200 ms关键通信间隔、30分钟运行、1000次导航和实车返回主页停车原因仍待逐级验收。宿主和模型结果不替代实测。

返回主页导致减速的现场原因仍需UART5、hmi trace和视频时间轴确认；34号离页路径不主动停车，但在现场取证前不宣称该实物问题完全闭环。
""", encoding="utf-8")

(HERE / "34号烧录与回退说明.md").write_text("""# 34号烧录与回退说明

可以烧录本目录下的 17_轨道巡检车.pix.bin，工程源文件为 17_轨道巡检车.pix。两者由Visual-PIX重新编译，哈希见运行核验/34号实施_20261103/34号最终SHA256.json。

配套主控文件沿用33号双轴、启动保持、HMI控制、非只读配置，作为未修改副本放在34号交付目录。如果主控已经是该固件，本轮无需重新烧录；版本不确定时先核对哈希。

推荐顺序为模拟从站加实屏、低速双轴台架、实车。每阶段记录屏端BIN、主控固件哈希、视频、UART5和hmi trace编号。保护停车后必须重新人工授权和滑动，不能恢复旧目标。

回退使用冻结的31～33号原文件及对应固件，核对哈希后操作；全程未推送Git。
""", encoding="utf-8")

(HERE / "34号台架验收与UART视频索引.md").write_text("""# 34号台架验收与UART视频索引

本轮尚无新的硬件证据，所有条目保持待验收。

| 阶段 | 场景 | 门槛 | 当前 |
|---|---|---|---|
| H34-S01 | 模拟从站加实屏，跨页和主题 | 关键间隔不超过200 ms，页面300/500 ms | 待验收 |
| H34-S02 | 滑块单击和拖动 | 主控接受不超过200 ms，提示不超过100 ms | 待验收 |
| H34-S03 | 单次急停 | 触摸至UART不超过150 ms，无丢失 | 待验收 |
| H34-B01 | 低速双轴台架30分钟逐页操作 | 无导航引起的停车 | 待验收 |
| H34-F01 | 断链、恢复和重新授权 | 原期限保护停车，不恢复旧目标 | 待验收 |
| H34-V01 | 实车运行和返回主页 | 确认现场停车原因 | 待验收 |

UART应记录双向RTU帧、64/96寄存器长度、启动标识、快照、会话、目标序号、目标年龄、租约、停车事件和原因；急停响应与减速完成时间分开记录。
""", encoding="utf-8")
print(json.dumps({"delivery": str(DELIVERY), "artifacts": manifest, "head": head}, ensure_ascii=False, indent=2))
