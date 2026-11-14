# F407 ARM3 R2 软件实施与分阶段联调

日期：2026-11-06。状态：软件候选，未烧录、未连接 CAN/USB 设备、未完成实机验收。

## 链路与本次提交范围

机械臂实际连接 RK3588；F407 经 CAN 请求 RK 的规范化执行器。

```text
遥控 NRF v2 ─┐
             ├─ F407 owner/许可/作业锁 ─ CAN ARM3 R2 ─ RK唯一执行器 ─ USB机械臂
HMI36 邮箱 ─┘
PC TCP9000 arm ─ RK本机执行器 ─ CAN申请F407 owner/许可
```

本次将既有工作区 ARM3 候选一并纳入版本：管理器、0x53～0x5F 编解码、32条车辆停止关联、分段不可变队列、停止独立资源、逐侧结果/运输证明、遥控 v2、HMI rev3 及底盘/Flash/最终伺服写入互锁。新增只读诊断发布与 `can arm3 status`，补齐其测试及生产保护夹具。

USART3（PD8/PD9）、USART6（PC6/PC7）仅保留原生成配置、初始化和 IRQ，没有机械臂应用收发，不增加直接串口控制。USART1 调试、USART2 伺服、UART4 原业务及 UART5 HMI 保持原入口。

原车辆 CAN 字段、旧 NRF v1、35号屏地址、Flash/身份区和保留 SRAM 布局保持；ARM3启用后新增作业/运输锁是计划内的安全互斥。35号屏可以继续调试原电机，但不能操作 ARM3；36号屏须使用匹配的新BIN。

## 默认构建和真实安全绑定

- `APP_ARM3_PROFILE_ENABLED=0`；未启用机械臂的原底盘继续按既有门禁使用。
- `app_arm3_board_sample()` 缺省返回 false。未完成真实接线时，安全版本为0、许可为0。
- 已启用 ARM3 的构建启动时立即锁定未知运输状态；取得双侧新 kind8 运输证明后才能解除，不可通过关闭功能恢复行车。
- 不开放 `0x54 opcode2` 精确物理角速度功能。T123 档位不是已标定的物理速度。
- T102 停止证据未通过时，RK必须关闭预设和六轴目标；F407只采用真实能力交集，不能把示例配置当成验收。

正式绑定前须由底盘、硬件/机械和上层确认 GPIO/端子、极性、去抖、采样周期、独立停止、保持/失电行为、供电熔断、接线/策略 CRC、安全版本及负责人。然后另加经签认的强符号 `app_arm3_board_sample()` 实现；不能仅改profile或verified字段。

## 只读诊断

使用原 USART1 CLI，单次执行：

```text
can status
can stat
can control status
can arm status
can arm3 status
```

`can arm3 status` 不提交动作、不授予控制权、不续租，也不直接读取正在改变的管理器状态。CAN任务完成本轮业务后将管理器与平台状态发布为一致缓存，CLI只复制该缓存。

- `present=0`：尚未发布快照，不能据其余字段判断健康。
- `age_ms`：自快照采集的时间，32位Tick回绕安全。所有输出均为采集时状态；年龄超过200ms应先排查CAN任务，不得把旧 `permit/online/proof` 当作当前许可。
- `negotiated/profile/safety/safety_ver`：协议匹配、部署开关及真实安全状态。默认 profile0/safety0 是预期结果。
- `owner/permit/work_lock/reason`：当前仲裁、许可、作业锁和管理器原因。
- `config_match/chassis_busy/parameter_lock`：配置、底盘停车及参数互锁。
- `platform_flags` 顺序为 hardware_bitmap / mapped_input_mask / config_flags；`sample_age`、`zero_stable` 和 `feedback_age` 均描述采集时状态。
- 每侧输出在线、活动、停止中、反馈年龄、operation、phase/result、身份/配置、运输预设批准和 proof_state。`transport` 仅表示预设已批准；必须结合 proof=2 和双侧证明检查实际运输成立。

命令仅按需执行；不要用高频串口打印替代CAN抓包或时延测量。

## 本机可执行验证

在工程根目录，使用 PowerShell 7（`pwsh`，避免Windows PowerShell 5的UTF-8路径解析问题）：

```powershell
pwsh -NoProfile -File Tests/run_arm3_tests.ps1
pwsh -NoProfile -File Tests/run_arm3_input_tests.ps1
pwsh -NoProfile -File Tests/run_arm3_port_tests.ps1
pwsh -NoProfile -File Tests/run_arm3_joint_tests.ps1
pwsh -NoProfile -File Tests/run_can_protocol_tests.ps1
pwsh -NoProfile -File Tests/run_can_control_tests.ps1
pwsh -NoProfile -File Tests/run_production_guard_tests.ps1
pwsh -NoProfile -File Tests/run_sync_storage_tests.ps1
pwsh -NoProfile -File Tests/run_dual_axis_tests.ps1
pwsh -NoProfile -File Tests/run_nrf_host_tests.ps1
pwsh -NoProfile -File Tests/run_tests.ps1
```

`run_arm3_joint_tests.ps1` 使用同级 RK3588_ARM3_20261106 生产Python模块与模拟设备；详细边界见 README_ARM3_JOINT.md。黄金向量：`D:/Openmv_Python/python.exe -B Tests/run_arm3_vectors.py`，依赖同级RK候选golden_vectors文件。以上结果不代表真实CAN或设备运动通过。

Keil兼容构建：

```powershell
pwsh -NoProfile -File Tests/build_can.ps1
```

该脚本顺序构建单/双轴、CAN调试开关、CAN关闭、只读及最终当前配置，过程中临时改变工程宏，finally恢复原工程字节。不要同时运行其他Keil构建或修改uvprojx；构建失败后先确认恢复，再使用最终当前产物。脚本检查Flash边界和静态栈预算，动态栈高水位仍需板上实测。

`-CompatibilityOnly` 仅执行CAN关闭、只读和最终当前配置，用于在前六组已通过后续跑兼容项。关闭CAN或伺服只读的变体同时关闭车辆CAN控制宏；编译期保护本身保持有效。

## 远程联调到最终验收

| 阶段 | 进入条件与操作 | 必须留下的证据/退出条件 |
|---|---|---|
| 1 软件候选 | 核对协议SHA、R1解释、双方构建和配置身份；先用模拟对端 | 主机测试、黄金向量、Keil/MAP通过；不标成部署/实机 |
| 2 无运动真实CAN | F407/RK物理同CAN，500k、标准帧；车辆固定、伺服不输出，RK不打开机械臂串口 | READY、manifest、53 permit0、45/46、原080/CRC及5E10/kind7；重放、丢片、断链日志 |
| 3 设备行为取证 | 现场固定、限能、独立停止及承载保持在位，由现场人员操作 | 上电/串口打开/USB复位/重启无未经批准运动；USB身份/epoch、方向/限位、T123/T102各阶段停止、反馈/位移和保持记录 |
| 4 正式安全绑定 | 真实安全GPIO及配置签认；强符号采样实现；上层唯一writer和安全配置生效 | 重建身份和完整回归；未配置/失效/断线撤许可；无写入/仲裁旁路 |
| 5 CAN动作闭环 | 2～4通过，仅按已验证能力逐项开放 | 单侧六关节点动/1～5档/停止；预设/六轴/灯/示教；双臂一侧失败两侧停止；双侧kind8 state2后才解锁行车 |
| 6 遥控 | CAN动作和停止通过，遥控新固件身份固定 | session/challenge、申请/释放、松键/换轴/换侧/切页、断无线/重连及旧车辆按键回归 |
| 7 HMI36 | 遥控通过；36号新BIN/字典身份固定 | 邮箱/暂存/六轴/姿态/示教/停止；100～300ms点动；活性不能续动作；35号旧屏回归 |
| 8 整车与冻结 | CAN、遥控和HMI真实运动均通过 | PC/遥控/HMI竞争；CAN/RK/USB/实体停止故障；原双轴、换向、停车、同步、Flash恢复；30分钟持续运行；资源及时延测量；双方签认 |

阶段2、5及最终时延验收必须有真实物理CAN。异地可以分别运行模拟对端并交换日志；公网转发不能替代同车CAN和现场停止/保持试验。

每次测试记录协议SHA、实际烧录/运行构建SHA、配置版本/CRC、USB epoch、owner/generation、operation、stop_sequence、原080 CRC、CAN与USB原始日志、反馈序号/年龄、停止时间/位移、故障与恢复。未得到的字段写“未测/未知”，不得填默认通过。

当前不能执行的后续项：真实安全绑定、设备启动/停止取证、CAN接线抓包、机械臂运动、遥控/HMI实物及最终整车验收。须在对应实物和配置到位后继续，软件候选提交不是最终冻结。
