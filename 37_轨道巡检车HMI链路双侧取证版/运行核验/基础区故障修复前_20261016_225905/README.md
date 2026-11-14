# 13号共同RPM控制工程

从08只读基线独立复制；HMI13-R2，协议主版本1、字典修订2。

## 打开与编译

- 使用本地Visual-PIX 1.7.2.179打开13_轨道巡检车.pix。
- P0～P7保留，主页新增伺服控制入口，P8只有一个共同RPM目标。
- 使用软件编译菜单生成13_轨道巡检车.pix.bin；不要使用08或历史BIN。
- 软件已完成真实保存、重开、事件全文核对和编译。最终哈希及证据见运行核验目录。

## 验证与工具

- `node --test hmi13-failures.test.cjs`：25项协议/事件等价回归。
- `node 全模式只读显示回归测试.cjs`：32项原只读显示回归。
- `node 调度压力记录.cjs`：两分钟理想50ms事件模型，不能替代实屏时序。
- `node r2-simulator.cjs`：本机RTU字节透传模拟从站。
- `python serial-rtu-bridge.py --port COMxx`：仅在明确连接屏的端口上启动。
- 故障注入使用`--fault crc`、`--fault short`或`--fault truncate`，需要单独记录真实拒收结果。

## 必须保留的现场边界

未下载实屏、未烧录主控、未打开物理串口、未执行伺服运动；不宣称实物对接通过。
厂商定时器最小interval=5，单位10ms，采用候选50ms服务；实屏最大目标/状态间隔、CRC/短帧拒收及静默仍须抓帧。软件受控急停不是物理急停或STO。
详见联调操作说明.md、联合验收矩阵.md和实施记录.md。

## 2026-10-16全量修复

优先阅读全量修复说明_20261016.md；本轮产物哈希以运行核验/全量修复交付哈希_20261016.json为准，历史主站配置修复哈希不再代表当前版本。回归命令：node --test hmi13-failures.test.cjs hmi13-link-recovery.test.cjs hmi13-audit-repair.test.cjs hmi13-capture.test.cjs config-artifact.test.cjs；只读显示继续执行全模式只读显示回归测试.cjs。仍须单独完成实屏下载与六阶段验收。
