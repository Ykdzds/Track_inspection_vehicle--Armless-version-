# Link5 串口日志分析工具

脚本 `tools/analyze_link5_log.py` 只读原始日志，使用 Python 标准库，无需安装第三方包。它保留每条有效记录，并输出可快速浏览的摘要及详细 JSON。工具不会控制设备或修改固件。

在 F407 工程根目录运行：

```powershell
python -B tools/analyze_link5_log.py 'C:/日志/实物测试.txt'
python -B tools/analyze_link5_log.py 'C:/日志/实物测试.txt' --json 'C:/日志/实物测试分析.json'
```

默认识别 UTF-8、带 BOM 的 UTF-16、常见 UTF-16LE 及 GB18030；乱码处允许替换，但不会把乱码解释成通信故障。也可用 `--encoding utf-8-sig` 等显式指定编码。`--json -` 仅在标准输出写 JSON，便于其他工具读取。

解析回归：

```powershell
python -B Tests/test_link5_log_analyzer.py
```

## 数字与身份规则

- `HLD/LD` 头部时间、boot ID、事件编号均按十六进制处理；`E1/EA/EABC` 等事件类型不受两位宽度限制。
- 旧 `HLD FIRST at/owner/requester/reason/seq/cmd/decel/gen` 按源码的十进制格式处理，不能受 BOOT 中的 `units=hex` 提示影响。
- `HLD MEM/STACK/GAPS/LOGGER` 继续按各字段原有格式处理；`LD` 数值、`w=` 数组及新增 `id=` 按十六进制处理。
- 原始首因按 `boot + src + serial + kind` 组装分页。通常已知 Link5 首因为 16 字，**S485 `kind=14/22/23` 提交拒绝、健康变化、安全变化是单页 6 字例外**；仅有完整且长度正确的 `p=0` 即完成。缺页、字段不足、多余页或重复页冲突会明确标识，未知类型保留原始数据，不猜布局。
- 无关文本及有效头部前的噪声被跳过；同一物理行中粘连的多条有效记录可以分别提取。不能恢复损坏或缺失的完整记录。

## 如何读取结果

`first_stops` 保存主控仲裁器首次停车原因，`route_first` 保存 RF、CAN 伺服、CAN 机械臂、RS485 的冻结首因及分页完整性。`records` 包含原始结构化记录与行号，未知字段仍保留原文。

`common_stop_events` 单独保存公共仲裁器停车事件：HMI 诊断开启时为原 `HLD E6/E7/E8`；HMI 诊断关闭时，经当前开启的 RF/CS/CA/S485 来源输出 `LD EVENT kind=3E6/3E7/3E8`，分别是首次/升级/完成。六词依次解码为 stop event、reason、owner16/requester16、request sequence、command16/deceleration16、result16/error16。它们**不是RS485本地事务**，不会与 S485 `kind=6` 取消或 `kind=7` 队列投递/旧代数丢弃混淆；后两者分别存为 `rs485.cancellation_events/drop_events`。`initializations` 保留通用 `kind=3F0` 初始化返回，前两词为实际 RS485、Modbus 返回值，0 表示成功；后四词仍为保留字段。

`periodic_observations` 中的 STATE/HEALTH 每行都是单独观察，含其自身 `sample`、`p`、`part`。同一 `id` 的行只在 `periodic_groups` 中建立逻辑关联；**不会拼接为原子快照**。SYNC 速度与位置两个相位即使同组也分别采样。相位 2 的 RS485 两个周期 part 同样不会拼成一条完整事务，因为两行可能观察到不同事务；需要完整证据时使用冻结 FIRST。

`hmi.max_complete` 优先从 `TIMING tc=last/max` 累计计数和 `SLOW` 的实际 capture/TC 时刻取得。50ms 为失败界限，达到或超过 50ms 会在摘要标注 FAIL。DWT 频率取自对应记录；日志缺少频率时可明确提供 `--dwt-hz 168000000`。工具不会自行假定 MCU 时钟。

E1、E5、EA 都保留，但 EA 没有请求事务 ID，且各类事件存在采样及丢弃可能，**不能仅按文本相邻关系把它们配成精确事务**。只有 E1/E5/EA 而没有可信 TIMING/SLOW 证据时，全程最大值报告 UNKNOWN。capture 是软件捕获时刻，完整耗时不能代替逻辑分析仪测得的物理请求末字节到响应末字节时限。TIMING 的累计最大值可能来自当前日志开始前，工具保留其证据来源，不把它虚构成文件内某条请求。

`rs485.transactions` 解析完整冻结事务，包括入队、消费请求、DMA 启动、实际 UART TC、RX 捕获、校验、伺服消费时刻。`wire_events/validation_events/consumer_events` 分别解析单条事件的有效证据，不借用其他事务时刻。时间为 32 位无符号差，支持一次回绕；超过半范围的歧义值报告未知。有效位缺失时，相应耗时保留 null；时间值为 0 并不自动表示缺失。

`rs485.zero_evidence` 区分“固件有效零反馈钩子被触发”和“原始回复充分证明驱动器反馈零”。后者要求首因完整、实际长度 7、`0x0B00` 地址、合法 FC03/单寄存器零值、CRC 正确以及 RX/校验有效位；缺失或截断不会被认定为完整零反馈。它仍不能单独证明驱动器为什么返回零，更不能把显示缓存零当成真实零速。

`rs485.submit_rejections` 解码 S485 FIRST `kind=14` 的拒绝时刻、返回状态、协议身份及请求参数。它没有成功入队时间、实际运动所有者或原始回复，相关字段保持 null/false。null 请求的 metadata-valid 为 0，事务与请求参数也保持未知，不把 `FFFFFFFF` 当成真实事务编号或寄存器。

`rs485.speed_requests` 解码事件 `kind=20` 的原始请求、首次健康限幅、上次 ACK 速度、轴与 speed-write sequence；`rs485.speed_submissions` 解码 `kind=21` 的最终健康检查前值、实际提交值及事务。RPM 按有符号 16 位解释，CAN batch 仅在 validity 标志成立时有值。两者是成功接受后的证据，日志出现请求不能单独证明线上发送或驱动器执行；工具保留各阶段身份，不跨事件猜测因果。

`rs485.health_transitions` 解码 `kind=22` 的 EVENT 和完整单页 FIRST。字段包括 owner/item/axis、变化前后故障码/使能/速度有效/故障有效/使能有效/在线/锁存/降级/健康级别/安全停车/离线/baseline 标志、三项失败数及 feedback-stop-pending、所选 item 最近反馈事务。item 为 0/1/2 时分别对应速度/故障/使能。`stored_tick_age_ms` 仅是存储 tick 到观察时刻的差；**所选 item 当前有效位或 baseline 无效时，`valid_feedback_age_ms` 为 null**，不会用初始化 tick 的差伪装成有效样本年龄。缺少旧 baseline 的首次观察保留 `baseline_observation=true`，不自动判为运行故障。

`rs485.safety_transitions` 解码 `kind=23` 的 EVENT 和完整单页 FIRST，保留新旧安全 phase/failed-task/boot-lock/baseline 标志、完整 32 位 failure、generation、pending transaction 及 parameter/zero 掩码和 pending axis/step。EVENT 与 FIRST 可能描述同一次变化，脚本保留各自证据来源，不当成两个独立故障。上述变化能记录软件健康与安全接管过程，**不能凭这些位推断电源、电磁干扰、驱动器电气故障等实际根因**；`physical_root_cause` 保持 null。

堆与栈摘要仅报告日志中观察到的范围和最低值。未观察到下降不等于证明不存在内存破坏；记录数量也不等于总线事务总数。`LOGGER` 丢失计数、FIRST 合并计数及缺页状态都应同时核对。

## 回归与实录核验

固定 fixture `Tests/fixtures/link5_mixed.log` 覆盖噪声、粘连记录、两种进制、跨 boot 同号事件、三位事件类型、FIRST 缺页及冲突、不同 sample 的周期 part、SYNC 同组分相位、DWT/毫秒回绕、CRC 正确/破坏的有效零反馈、缺少时钟及 CLI UTF-16/JSON；S485 `3E7` 公共停车升级与 `7` 投递失败独立判读，`3F0` 保留初始化返回。额外测试覆盖三种单页 6 字记录、null 请求、缺页/字段不足/多余字段不解码，原始请求→首次限幅→最终提交的有符号速度和 CAN batch 有效性，健康变化有效年龄与完整安全 failure 位。

2026-11-07 对用户 boot `634` 的既有实录执行工具：识别 34266 条记录、0 条解析警告，TIMING 记录的累计请求捕获至实际 UART TC 最大值为约 **27.138ms**；观察到 379 个堆样本，当前余量均为 7344 字节，所有记录的任务最小空闲栈为 404 字节。该实录属于此前 HMI 版本，只用于兼容性核验；它不构成新增五路日志版本的实物验收。
