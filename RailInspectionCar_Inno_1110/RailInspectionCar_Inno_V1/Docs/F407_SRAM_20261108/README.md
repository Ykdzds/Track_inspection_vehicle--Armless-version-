# F407 SRAM裁剪与缓存优化交付

本轮仅修改F407正式工程 `MDK-ARM/RailInspectionCar_V1.uvprojx`，资源配置标识为 `sram-r1`。遥控端、F103及34/37号屏固件未修改；没有烧录或推送。

## 配置与使用

所有资源宏位于 `APP/app_config.h`，仅接受0/1。当前源码默认：

| 宏 | 默认值 | 用途 |
|---|---:|---|
| `APP_CHASSIS_LCD_ENABLED` | 1 | 本机LCD；设为0后裁剪显示缓存、LCD任务及SPI1专用DMA，并关闭背光、保持复位 |
| `APP_DEBUG_CLI_ENABLED` | 0 | USART1人工文本查询、维护及命令解析 |
| `APP_TUNE_UART_ENABLED` | 0 | USART1在线调参与遥测缓存，可独立于人工CLI启用 |
| `APP_SERVO_CLI_DEBUG_ENABLED` | 0 | 人工伺服会话；设为1必须同时启用人工CLI |
| `APP_SERVO_PULSE_ENABLED` | 0 | TIM9/TIM11脉冲控制及规划资源；设为1恢复原实现，仍受原授权和保护门禁约束 |

关闭人工输入功能后，USART1公共worker、8192 B TX DMA缓冲、参数Flash维护及五路自动诊断继续保留。自动日志使用以下独立宏，默认均为0：

| 链路 | 宏 |
|---|---|
| HMI | `APP_HMI_LINK_DIAG_ENABLED` |
| 遥控 | `APP_NRF_DIAGNOSTIC_ENABLED` |
| CAN伺服 | `APP_CAN_SERVO_DIAG_ENABLED` |
| CAN机械臂 | `APP_CAN_ARM_DIAG_ENABLED` |
| RS485伺服 | `APP_SERVO_RS485_DIAG_ENABLED` |

例如排查CAN调速后反馈突变，开启CAN伺服与RS485两个宏，保持人工CLI和UART调参为0即可。每次改宏后重新全量编译、烧录；这不是运行期串口切换。

持久安全故障继续锁存。需要执行 `safety acknowledge` 时，先排查故障，再使用人工CLI维护构建执行原确认流程；不会自动解除，也不新增三端确认协议。详见[CLI维护配置说明](CLI维护配置说明.md)。

## 实现边界

HMI编码输出改为请求相对地址，工作区仅125寄存器；不恢复后台整表编码。外部Modbus协议、合法窗口、保留字、无效值、单位及舍入不变。源有效期100 ms、HMI门禁300 ms、目标TTL500 ms、租约1000 ms和停车规则保留。

位置监测改用同一临界观察窗口内的双轴窄投影，保留每轴4条历史样本、配对、序号/代数、回绕及未选轴规则，不新增硬件查询。

LCD关闭仍保留NULL任务槽；不影响车载HMI屏、TIM2静默计时器、SPI2无线通信或共享DMA时钟。脉冲关闭保留运动任务、公共故障状态及无状态硬关断，旧脉冲请求在修改运动状态前被拒绝。48 KiB FreeRTOS堆、安全队列、任务优先级和保护期限未缩减。

## 正式内存与固件

基线物理RAM为175136 B。14配置矩阵和恢复工程后的源默认正式构建均已通过，全部0错误、0警告。最终15份成功归档已通过 `Tests/report_sram_profiles.py --strict` 校验；当前正式MDK输出与源默认归档的HEX/AXF/map/调用图四项哈希全部一致。逐配置原始证据位于 `验证/`。

| 配置 | 物理静态RAM | 相对基线释放 | 普通SRAM余量 | CCM余量 | 启动堆分配减少 |
|---|---:|---:|---:|---:|---:|
| 默认，LCD开启、人工功能关闭、自动日志全关 | 164496 B | 10640 B（10.39 KiB） | 28600 B | 3512 B | 3160 B |
| LCD关闭，其余同默认 | 162196 B | 12940 B（12.64 KiB） | 30900 B | 3512 B | 7112 B |
| LCD开启，五路自动日志全开 | 167544 B | 7592 B | 25552 B | 3512 B | 3160 B |
| LCD关闭，五路自动日志全开 | 165056 B | 10080 B | 28040 B | 3512 B | 7112 B |
| CLI维护，LCD/CLI/调参/伺服CLI开启，脉冲关闭 | 171432 B | 3704 B | 21664 B | 3512 B | 0 B |

相对默认配置单独关闭LCD，本次map实际再释放2300 B静态RAM，另减少3952 B启动堆分配。脉冲回退与维护配置仅脉冲宏不同，两者物理RAM差528 B；该受控比较不能再与其他重叠裁剪重复相加。完整对象差异见[SRAM汇总](验证/SRAM汇总.md)和同名JSON。

交付产物与配置：

- 默认正式固件：[HEX](验证/source-default/RailInspectionCar_V1.hex)，[宏配置](验证/source-default/configuration.json)，[manifest](验证/source-default/manifest.json)，SHA256：`A35411F4F5FA5970D20274C2AD2B8DCA8B5528A6426CB64F434961610E0ADC1E`。与 `MDK-ARM/RailInspectionCar_V1/RailInspectionCar_V1.hex` 完全一致。
- LCD关闭固件：[HEX](验证/lcd-off/RailInspectionCar_V1.hex)，[宏配置](验证/lcd-off/configuration.json)，SHA256：`D0CC79AE2E232BDCDA8058BB707944791CC86A7695DA0103F18A5F5FA368F461`。
- CLI维护固件：[HEX](验证/maintenance/RailInspectionCar_V1.hex)，[宏配置](验证/maintenance/configuration.json)，SHA256：`FA8DD917E8688E2FC468EA6DA6FC711C93E55DF089C797D318481A8DCC795695`。
- 五路日志全开固件：[HEX](验证/all/RailInspectionCar_V1.hex)，[宏配置](验证/all/configuration.json)。其余单开、调参单开、脉冲回退与旧专项配置见对应子目录。

静态RAM采用执行区域实际占用之和，包含136 B RAM执行代码及512 B故障保留区。FreeRTOS堆数组本身仍为49152 B；任务和队列释放的是堆内空闲，不能重复加到静态释放量。

精确固定缓存变化：

| 项目 | 基线 | 修改后 | 静态符号释放 |
|---|---:|---:|---:|
| HMI寄存器工作区 | 2432 B | 250 B | 2182 B |
| 位置监测工作快照 | 1232 B | 228 B | 1004 B |

人工CLI和调参都关闭时，worker栈及行队列启动分配减少3160 B；LCD关闭另减少3952 B，合计7112 B。这是堆分配负担差额，`heap_4`首尾对齐另有最多8 B差异；实际当前及历史最低空闲堆须用本版实物日志重新建立基线。详细推导见[LCD与脉冲裁剪说明](LCD与脉冲裁剪说明.md)。

## 软件验证与实物验收

软件回归使用生产路径，覆盖HMI全部134356个合法子范围的相对/绝对编码对照、100 ms边界与回绕、窄位置getter、CLI关闭后的安全与维护、脉冲硬关断/拒绝及回退、LCD、Flash、三端控制、五路诊断32种组合、拥塞/重试/首条记录保留。42套回归当前全部通过，原始日志和保留的重跑记录见 `验证/regression/summary.json`。

正式Keil配置矩阵包含默认、LCD关闭、五路分别单开、五路全开、LCD关闭且全开、维护、调参单开、脉冲回退、旧专项日志并开及兼容配置。矩阵结束后已恢复工程/RTE，再不附加任何APP宏覆盖完成源默认全量编译。源默认归档包含ARMCC实际预处理、三个新生成目标文件的DWARF宏定义记录、依赖记录及工程快照，交叉确认全部14个相关开关。原工程没有启用逐文件命令清单，本交付不声称持有该清单。

`source-default-attempt-1`保留了一次编译0错误/0警告、但归档脚本错误预期 `.__i` 文件而中止的记录；该次不作为完整验收归档。支持脚本已改用实际对象宏记录，重新构建后的 `source-default`及严格汇总已全部通过。生产源码在此过程中未改变。

**实物验收状态：待实物验收。** 尚未取得本版30分钟初验和2小时连续运行数据。应先低速，再覆盖约751 rpm、跨页、调速、重新授权、K5/K3停车，并核验：

- 所有已创建任务历史空闲栈至少256 B，当前/最低堆无无法解释的持续下降。
- 五路全开时64/96寄存器完整回复从请求末字节至UART TC全部小于50 ms，目标不超过40 ms；条件允许时用逻辑分析仪核对物理末字节。
- LCD关闭后的背光/复位状态和PULSE0输出电平正确，三端授权、停车及恢复不受影响。

静态调用图不覆盖全部函数指针、异常现场及抢占组合，不能替代实物水位。开启诊断的 `servo_comm_task` 最低静态余量264 B，接近256 B验收线，应重点采集其运行水位。任一硬性指标不合格均判该配置实物验收失败。

## Git与追溯

已先完成本地基线提交 `a2c49d354a1c61a1636eda2d91d2398337ab5688`：`feat: 增加五路共享链路诊断与RS485零速取证`。提交后展示了实际最新10条记录，随后才实施本轮修改；没有推送或修改历史。本轮实现保留在工作区，构建manifest记录基线HEAD及工作区变更，不能仅凭HEAD或某个文件的 `__DATE__/__TIME__`判断固件内容。

本轮基线提交完成后的实际近10条记录：

| 提交 | 备注 |
|---|---|
| `a2c49d3` | feat: 增加五路共享链路诊断与RS485零速取证 |
| `dd916c6` | fix: 按窗口编码HMI状态并优化接收调度与超时取证 |
| `3eaa1fa` | fix: 修复HMI关键状态源采样与LCD DMA误超时并补齐诊断 |
| `ce2a698` | feat: 增加HMI37双侧链路取证与首次停车原因诊断 |
| `82d6a0c` | fix: 修复Flash保存门禁诊断与NRF故障恢复状态 |
| `8883212` | feat: 完善ARM3 R2 CAN控制链路与三端输入安全互锁 |
| `01b1751` | feat: 增加双轴参数Flash追加保存与增强配对遥测 |
| `4cccd62` | fix: 修复双轴动态补偿步长并补充同步时序诊断 |
| `da12838` | chore: 同步Keil调试配置中的诊断与运行时模块 |
| `1c9b1f0` | fix: 隔离USART1调试通信并保留复位故障诊断 |

生产源文件集摘要：`04A4BEACD4FB44F1FE72EC48F82D74806FBBCF9D4F4F6A867550BCC41BC7302B`。各文件哈希及算法输入保存在 `验证/production-source-sha256.json`；各配置manifest记录完整固件/AXF/map/调用图/日志哈希。

需要回退时，保留当前工作区与本轮产物，在独立工作区检出 `a2c49d3`，重新使用该工作区的正式工程全量编译并核对宏和固件哈希；不要对当前未提交实现执行破坏性reset。基线map引用及哈希保存在 `验证/baseline.json`。

## 复现入口

```powershell
python -B Tests/run_sram_regression.py
pwsh -NoProfile -File Docs/F407_SRAM_20261108/build.ps1
pwsh -NoProfile -File Docs/F407_SRAM_20261108/build_source_default.ps1
python -B Tests/report_sram_profiles.py --strict
```

构建脚本按归档目录保存证据，运行前先保留已有成功产物；`source-default`存在成功manifest时拒绝直接覆盖。自动报告位于被忽略的 `验证/`，不纳入源码提交。进一步说明见[HMI与位置缓存说明](HMI与位置缓存说明.md)。
