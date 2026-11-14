# F407 CLI、调参与公共日志维护配置

适用工程：`MDK-ARM/RailInspectionCar_V1.uvprojx`。资源配置修订标识：`sram-r1`。

本说明区分人工输入功能、自动通信日志与系统公共服务。关闭人工输入功能会在编译期移除对应缓存和状态机，USART1自动日志仍由独立开关控制。

## 宏及默认值

以下宏均位于 `APP/app_config.h`，只接受0或1。

| 宏 | 默认 | 用途 |
|---|---:|---|
| `APP_DEBUG_CLI_ENABLED` | 0 | USART1人工文本命令，包括状态查询、人工恢复命令及安全确认命令 |
| `APP_TUNE_UART_ENABLED` | 0 | USART1 `tune`调参与二进制遥测协议；可以独立于人工CLI启用 |
| `APP_SERVO_CLI_DEBUG_ENABLED` | 0 | 人工CLI伺服会话授权与运动；为1时要求人工CLI同时为1，否则编译拒绝 |
| `APP_SERVO_PULSE_ENABLED` | 0 | 外部脉冲硬件及对应调试功能；使用人工脉冲会话时，还需启用人工CLI及伺服CLI会话 |
| `APP_CHASSIS_LCD_ENABLED` | 1 | 底盘本机LCD及其任务，不影响车载HMI屏 |

派生宏不应手工修改：

- `APP_UART_RX_ACTIVE = APP_DEBUG_CLI_ENABLED || APP_TUNE_UART_ENABLED`。
- `APP_CLI_SERVO_ACTIVE = APP_DEBUG_CLI_ENABLED && APP_SERVO_CLI_DEBUG_ENABLED`。

以下五路自动日志开关与上述人工功能独立，按实物调试对象选择开启；它们不授予控制权：

| 调试对象 | 宏 |
|---|---|
| 车载HMI屏链路 | `APP_HMI_LINK_DIAG_ENABLED` |
| 遥控控制链路 | `APP_NRF_DIAGNOSTIC_ENABLED` |
| CAN伺服控制链路 | `APP_CAN_SERVO_DIAG_ENABLED` |
| CAN机械臂控制链路 | `APP_CAN_ARM_DIAG_ENABLED` |
| 下游RS485伺服通信 | `APP_SERVO_RS485_DIAG_ENABLED` |

`APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED`仍是原有双轴同步专项输出，默认关闭；它不能代替遥控主诊断宏。

## 选择维护构建

| 使用场景 | 人工CLI | UART调参 | 伺服CLI会话 | 脉冲 |
|---|---:|---:|---:|---:|
| 常规运行并按需自动记录日志 | 0 | 0 | 0 | 0 |
| 仅使用上位机UART调参 | 0 | 1 | 0 | 0 |
| 人工状态查询或安全确认 | 1 | 0 | 0 | 0 |
| 人工RS485伺服会话维护 | 1 | 按需 | 1 | 0 |
| 人工脉冲会话维护 | 1 | 按需 | 1 | 1 |

每次修改后使用正式Keil工程全量编译，并检查该配置的map、静态调用栈及运行水位。宏0与宏1对应不同固件，不能通过串口命令动态打开已编译移除的功能。

原人工 `servo left/right status` 的非调试轴回退输出仍包含固定的 `owner=none`，该旧字段不能用于判断三端当前的仲裁控制权；判断HMI、遥控或CAN来源应使用五路自动日志和对应控制状态查询。关闭人工伺服会话时，这类查询只读取反馈，不授予控制权。

`safety acknowledge`只有人工CLI维护构建提供。它调用现有安全确认流程，仍要求故障流程阶段、关键任务存活、参数校验、双轴零速及反馈新鲜度满足原条件，随后仍需重新运动授权。关闭人工CLI不会自动确认、自动清除锁存或把确认操作绑定到遥控K5。遇到该类安全锁存时，应先分析故障，再使用人工CLI维护固件执行原安全确认流程。`servo recover`同样属于人工命令；遥控、HMI、CAN各自已有的恢复入口继续保留。

## 公共worker与内存

原 `cli_task`入口、句柄、优先级2、任务ID6及历史栈水位槽保持一致。人工CLI与UART调参均关闭时，任务名称为`diag`，栈为256 words，即1024字节；任一输入功能开启时名称为`cli`，栈为832 words，即3328字节。运行期历史空闲栈验收线仍为256字节。以上是任务分配栈大小，未包含TCB和堆管理开销；最终余量以正式构建检查和实物水位为准。

每轮公共服务固定执行：USART1 TX服务→五路诊断服务→公共Flash维护→UART调参服务（关闭时为空实现）→USART1 TX服务→1ms让出。Flash维护从UART调参服务中移出；生产代码只有公共worker一处调用 `app_sync_storage_service()`，每轮一次。HMI、CAN、遥控共享的参数落盘不会因关闭UART调参而停止。

当两个输入功能均关闭时，不创建192字节×4的行队列，不保留行组装缓存，不启动USART1 RX DMA，512字节RX缓存及其接收状态不进入固件。USART1原8192字节TX缓存、整行非阻塞发布、DMA发送、重试及错误恢复继续保留。关闭UART调参还会移除调参重放、响应帧、遥测页面及调参诊断快照缓存。

系统授权门 `aimtor_cli_gate_init()`在 `aimtor_init()`之后无条件初始化，并在系统恢复隔离中重新初始化，不受人工CLI编译开关影响。关闭CLI会话时，状态查询仍返回有效的未授权空闲状态；请求不获取控制权。外部停车接口继续向仲裁器提交停车，公共紧急停车及既有控制来源的安全路径保留。

自动日志共享公共worker与原USART1 TX资源，不为每条链路创建任务或字节缓冲。开启多路日志时应同时核对日志丢弃计数、worker栈水位及HMI响应时限。启动日志开启时，`CONFIG memory=sram-r1 lcd=... cli=... tune=... servo_cli=... pulse=...`可以核对所烧录固件的资源配置。

## 软件验证范围

`Tests/run_cli_profile_tests.ps1`验证关闭会话的有效中立状态、拒绝授权、外部STOP及恢复门，四种人工CLI/UART调参组合的公共worker路由，USART1无RX但TX可重试恢复，关闭配置对象中不再包含相应缓存，以及合法/非法宏组合。D0/T1还使用完整生产 `app_debug_cli.c`，让实际调参协议回归通过生产行解析和公共worker运行。

原人工会话矩阵、USART1 DMA与生产解析器、UART调参、诊断映射、异步Flash与参数锁回归分别由既有runner继续覆盖。软件回归不替代实物验收；栈水位和持续运行堆基线应在最终宏组合下重新采集。
