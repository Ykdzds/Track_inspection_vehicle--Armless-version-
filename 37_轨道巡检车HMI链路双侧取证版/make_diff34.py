from pathlib import Path
import hashlib

HERE = Path(__file__).resolve().parent
OLD = HERE.parent / "33_轨道巡检车调度与会话稳定修复版"
OUT = HERE / "运行核验" / "34号实施_20261103" / "34号相对33源码差异摘要.md"
names = [
    "HMI34事务服务.txt", "HMI34显示刷新.txt", "HMI34自定义函数.txt",
    "事件/急停_按下.txt", "事件/急停_弹起.txt", "事件/滑块_滑动.txt",
]
lines = ["# 34号相对33号屏端源码差异摘要", "", "34号从33号目录复制建立，以下为本轮生成/绑定的主要差异文件。"]
for name in names:
    new = HERE / name
    old_name = name.replace("34", "33")
    old = OLD / old_name
    def info(p):
        d = p.read_bytes()
        return len(d), hashlib.sha256(d).hexdigest().upper()
    n = info(new)
    o = info(old) if old.exists() else ("缺失", "")
    lines.append(f"- `{name}`：34号 {n[0]} 字节 / `{n[1]}`；33号 {o[0]} / `{o[1]}`")
lines += [
    "", "功能差异：跨页控制会话全局保持；滑块候选值和漏回调补偿；急停请求独立锁存与同序号重试；提示控件按滑块几何移动。",
    "F407源码未修改，R2线协议未改变；主控沿用33号配套固件。",
]
OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
print(OUT)
