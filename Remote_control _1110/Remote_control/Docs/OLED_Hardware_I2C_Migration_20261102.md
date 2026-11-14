# 遥控端 OLED 硬件 I2C 移植交付记录（2026-11-02）

本次已完成源码移植、主机验证和 Keil 全量重建。现场验收待执行，未烧录、未提交 Git。HEAD 仍为 `c11e7302d6f6c6145e022a17f01e5d8cb9bcd94f`；实施前已有的 CubeMX 7 项修改、5 个新增文件均保留。

## 1. 改动范围

| 文件 | 最终行为 |
|---|---|
| `BSP/OLED/OLED.c` | 删除软件 I2C GPIO 时序，使用 I2C1 `HAL_I2C_Mem_Write()`；设备地址 `0x78`（7位 `0x3C`），命令控制字节 `0x00`，数据控制字节 `0x40` |
| `BSP/OLED/OLED.h` | 保留既有接口，新增 `OLED_Service()`、`OLED_IsReady()` |
| `APP/app_main.c` | 每循环一页，发送成功后推进；失败丢弃快照；恢复时重绘当前逻辑页面，从第0硬件页开始；离线时继续按100ms衰减按键提示 |
| `Remote_control.ioc` | 补齐 `MX_TIM1_Init`、`MX_I2C1_Init` 的初始化顺序记录，与现有 `main()` 一致 |
| `Tests/` | 真实 OLED HAL 模拟测试、真实应用+OLED+NRF驱动联动测试、离线恢复/提示衰减测试，接入原测试脚本 |

CubeMX 配置维持 PB8=SCL、PB9=SDA、复用开漏、I2C1重映射、400kHz、7位地址。`main()` 在 `app_init()` 前调用 `MX_I2C1_Init()`；MSP负责引脚、重映射和时钟。I2C1只供OLED使用。不更改无线协议、按键协议、JOY/KEY业务、九页字段、ADC DMA、SPI、优先级或主控工程。

原1024字节显存、23字节初始化参数、对比度、方向、字模和绘图代码保留；原 `OLED_Clear()` 起的绘图源码与HEAD逐字节相同。软件I2C回退路径已删除。

## 2. 时序与恢复

- 页定位三条命令合并一次发送，再发送128字节数据。400kHz理论总线耗时约3.04ms（含地址及控制字节的135个字节、每字节9时钟，未计起停和软件开销）。
- 每个硬件页共用6ms截止时间，命令/数据不分别累计完整超时。F1 HAL使用 `elapsed > Timeout`，传入剩余时间减1以预留最后一个tick；启动命令同样批量发送且有界。1ms tick及中断会影响真实墙钟耗时，最终仍需仪器测量。
- 调用HAL前检查句柄状态、BUSY和PB8/PB9，最多等待1ms，该等待计入页预算。此处用于规避HAL固定25ms BUSY等待。仅本主循环使用I2C1；未关闭中断，未增加DMA/I2C中断、无限等待或驱动内喂狗。
- 首次失败立即停止后续写入，保存 `oled_last_status` / `oled_last_error` 供调试器查看。HAL调用已返回后DeInit并复位I2C1，PB8/PB9恢复输入，依靠外部上拉释放线路，GPIOB时钟保持使能。
- 状态为初始化/可用/恢复等待，每500ms最多尝试一次。持续低电平时只探测、不发时钟。线路释放后直接检查 `HAL_I2C_Init(&hi2c1)` 返回值，复用保留的CubeMX配置，避免运行期调用会进入 `Error_Handler()` 的 `MX_I2C1_Init()`；屏幕初始化失败继续退避。
- 上电 `OLED_Init()` 等待100ms并清屏一次。运行期恢复不重复等待100ms，不做同步整屏清写。恢复初始化与第0页数据发送分处不同主循环，之间继续无线服务。
- 应用顺序保持“无线服务 → 显示服务 → 无线服务”。100ms生成冻结快照，正常刷新期间不覆盖显存；屏幕恢复后按当前逻辑页面重建完整快照，再从第0硬件页刷新。
- `OLED_Update()` 与多页 `OLED_UpdateArea()` 保留同步接口，兼容原调用；每页预算独立。正常运行的应用始终只请求一个硬件页。

## 3. 软件验证

运行命令：`pwsh -NoProfile -ExecutionPolicy Bypass -File Tests/run_remote_tests.ps1 -RenderPreview`，退出0。

| 验证 | 结果 |
|---|---|
| 真实OLED+HAL模拟 | 地址、控制字节、23字节命令、页列位置、128字节数据、显存一致、整屏兼容、边界裁剪通过 |
| OLED故障注入 | 命令/数据NACK、HAL_BUSY、HAL_TIMEOUT、句柄忙、BUSY标志、SCL/SDA低、瞬时释放、初始化失败、恢复失败、500ms退避及32位计时回绕通过 |
| 真实应用+OLED+NRF驱动联动 | 模拟无线初始化、2ms建立和CE后1ms ACK；OLED命令1ms、数据3ms；正常发送最大间隔11ms，OLED故障/恢复最大14ms，无新增超周期中止 |
| 输入与快照 | 故障期间K5、K9两次点击、K1/K2累计步数、摇杆、按键扫描调度持续；翻页/恢复不覆盖未完成正常快照；恢复第0页重画及离线提示衰减通过 |
| 现有回归 | 按键扫描、状态协议/模型、九页/诊断页、ACK驱动、应用诊断开关两种配置、主控真实编码对照全部通过 |
| 字模预览 | 21个场景重新生成并目视检查，正常九页、OLD/LOST、告警、极值、JOY/KEY W/R无新增布局变化 |

这些时间是模拟值，未测量真实I2C、SPI和中断耗时。HAL官方I2C源未改；主机测试用HAL模拟层而非寄存器电气模型。GCC对原有绘图库的符号比较及平铺字模初始化单独关闭警告，其他主机警告仍作为错误；Keil全量构建没有警告。

## 4. Keil构建与资源

执行原目标 `Remote_control` 全量重建，ARMCC V5.06 update7 build960：**0 Error(s), 0 Warning(s)**。使用 `Tests/build_snapshot.ps1` 隔离输出，正式工程和RTE文件已恢复，构建前后生产源码SHA-256一致。

| 资源 | 占用 |
|---|---:|
| Code | 38980 bytes |
| RO-data | 3928 bytes |
| RW-data | 164 bytes |
| ZI-data | 6988 bytes |
| ROM总量 | 43072 bytes / 65536（42.06 KiB） |
| RW+ZI | 7152 bytes / 20480（6.98 KiB，含链接器预留区） |
| 配置栈 / 堆 | 1024 / 512 bytes，保持原配置 |
| 静态最大栈 | 648 bytes + Unknown；原显示格式化路径仍为最大路径 |
| OLED页更新 / 恢复调用链 | 216 / 200 bytes（入口计起） |

调用图的Unknown包含库函数/间接调用/递归分析，且不包含所有实际中断叠加；不能将648视为严格上界。TIM1路径静态96字节，现场需记录嵌套情况下栈高水位。

最终交付采用 `snapshot_20261102_133418` 的构建，其全部生产源码哈希与当前工程一致。后续测试/文档修改不改变该固件。既有 `REMOTE_BUILD_ID` 包含 `__DATE__/__TIME__`，因此相同源码再次构建HEX哈希也可能变化。

HEX SHA-256：`9C6E0DDEE377DA9AEF8DD68B0BC353B4582CCCCD270EBF76587F10A7BEF736F9`。

## 5. 交付与回退

- 证据/固件：`D:/CodexBuild/Remote_control/oled_hw_i2c_delivery_20261102`。包括build.log、host_tests.log、HEX/AXF、map/调用图、预览、working-tree.patch、implementation.patch、源码及固件哈希。`implementation.patch` 对比实施前快照，只反映本次改动；`working-tree.patch` 对比HEAD，含用户CubeMX改动，新文件由源码包另行保存。
- 实施前快照：`D:/CodexBuild/Remote_control/oled_hw_i2c_before_20261102_125247`，包含HEAD、差异、原始文件、完整恢复ZIP及源码哈希。恢复ZIP含用户CubeMX配置成果，先解压到独立目录比对，避免直接重置HEAD丢失用户改动。
- 九页参数说明：`6.方案设计/遥控器九页显示参数说明.md` 第13/15/16节同步驱动时序、软件验证和未执行实物项，业务字段保持原定义。
- 现场核验表：同目录 `OLED_Hardware_I2C_Field_Checklist_20261102.md`。尚无实物测量结论。
