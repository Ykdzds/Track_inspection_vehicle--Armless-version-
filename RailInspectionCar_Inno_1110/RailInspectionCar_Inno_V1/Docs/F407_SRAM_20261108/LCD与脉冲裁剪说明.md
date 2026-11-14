# LCD、脉冲与人工调试裁剪复核

复核日期：2026-11-08。源码基线提交 `a2c49d3`。正式固件、各配置静态 RAM 与调用栈结果以同目录“验证”下全量 Keil 构建记录为准；本说明核查的是裁剪边界及 FreeRTOS 启动分配量，不代替实物水位。

## 功能边界

- `APP_CHASSIS_LCD_ENABLED=0`：不创建 LCD 任务，不执行 LCD 初始化，不初始化 SPI1，不创建 `hspi1/hdma_spi1_tx`，不启用 DMA2 Stream3 IRQ；LCD 模块工作区、640 B SPI DMA缓冲和行缓冲均不进入编译。保留对外中性 getter，以便系统快照和诊断照常读取。`APP_CHASSIS_LCD_ENABLED=1` 走原有显示、传输和故障恢复实现。
- `APP_SERVO_PULSE_ENABLED=0`：不创建 TIM9/TIM11 句柄，不初始化其时基/PWM/NVIC，不编译其 IRQ，不编译脉冲 BSP、规划器与人工调试脉冲状态。非零频率、使能、方向和运动请求明确拒绝；停止、故障与公共安全状态继续有效。
- `APP_CLI_SERVO_ACTIVE=0`：人工伺服调试状态机和大快照字段不编译，getter 返回有效中性状态，运动/使能/模式请求返回 `SERVO_NOT_CONFIGURED`。`servo_debug_stop()` 保持原 IDLE 状态的立即关闭脉冲及 `motion_stop(IMMEDIATE)` 语义；不会额外建立受控停车请求或覆盖首停来源。独立 emergency-stop 继续调用原 `aimtor_emergency_stop()`。
- 枚举值未改动；脉冲宏打开后仍受原有 `APP_UNPROTECTED_MOTION_TEST_ENABLED` 等实际运动门禁限制。

## GPIO、共享外设及故障路径

`Core/Src/gpio.c` 在配置输出模式前预装 LCD 禁用电平：PD3 背光控制高（有效低，故关闭）、PD4 RESET低（复位有效）、PD7 CS高（未选中）。PB4 DC低；禁用时将 PB3/PB5配置为低电平普通输出，SPI1 MSP不会把它们切到复用模式。LCD相关GPIO保持有源定义，而不是悬空等待屏模块内部拉电阻。

脉冲链路的 PC8/PC9 为 AM26C31 公共门控，关闭时 PC8低、PC9高；PE5/PB9为两路 PULSE，在PULSE0配置下始终保持推挽低电平。`MX_GPIO_Init()`先写低再配置输出。PULSE0 的 `servo_pulse_stop_all()`直接有限读写GPIO寄存器，关闭门控，将脉冲脚切回无上下拉推挽输出并置低；不依赖 FreeRTOS、TIM句柄、DMA或HAL传输状态。HardFault/MemManage/BusFault/UsageFault 及栈溢出/分配失败路径原有硬关断调用保留。

TIM2是UART5 RTU静默定时器，初始化、句柄与IRQ均保留。SPI2及其DMA用于NRF，保留。DMA2公共时钟、USART1 TX DMA2 Stream7和RX所需资源未因LCD关闭而删除。HAL IRQ重复消费隔离保持原状。

PULSE1仍走原生产BSP、planner及精确计数IRQ；新增启动GPIO低电平配置与原TIM MSP最终保持低电平一致，不启动PWM。LCD1仍走原SPI1 MSP和专用DMA中断，未修改传输期限或恢复行为。

## PULSE0公共安全状态与原实现对照

保留 SAFE/READY/FAULT/OFFLINE、就绪条件、首个controlled故障原因、STOPPING锁存、完成后故障分类及人工恢复门禁。内部 `motion_stop_locked()`与公共 `motion_stop()`分开：就绪撤销、故障上报和清故障调用locked版本，避免额外改写停车分类。重复受控安全停车请求不覆盖首因；正在受控停止时就绪状态不得把STOPPING改成READY。生产立即安全停车继续请求driver fallback，未改成额外串口骤停/失能。

新增disabled回归编译真实生产路径，验证上述状态、首因、故障清除门禁、GPIO硬关闭以及CLI拒绝。完整脉冲算法继续由显式启用 P1/D1/S1 的历史回归验证。

## FreeRTOS堆分配精确核算

当前 `configTOTAL_HEAP_SIZE=49152`，`StackType_t`为4字节，`portBYTE_ALIGNMENT=8`。`heap_4.c` 的块头为8字节，单次申请 `N` 实际占用 `align_up(N+8,8)`。以下均为启动静态创建序列中的分配差额，无运行期新分配。

当前任务TCB为92字节：基线正式map的 `Idle_TCB`、`Timer_TCB` 直接列为92 B，且 FreeRTOS `StaticTask_t`由内核保证与实际TCB大小/对齐一致。TCB单独申请的堆占用为 `align_up(92+8,8)=104 B`。Queue结构在当前宏配置下80 B：指针/联合区16 B，两份List各20 B，三份32位计数12 B，锁/静态标志及对齐4 B，trace字段8 B。

| 改动 | 原堆分配 | 新堆分配 | 启动堆释放 |
|---|---:|---:|---:|
| CLI栈832→诊断worker栈256 words | 3336 B | 1032 B | 2304 B |
| worker TCB | 104 B | 104 B | **0 B** |
| CLI行队列4×192 B（一次申请Queue+存储） | `align_up(80+768+8,8)=856 B` | 0 B | 856 B |
| RX全关闭小计 | 4296 B | 1136 B | **3160 B** |
| LCD任务960 words栈块 | 3848 B | 0 B | 3848 B |
| LCD任务TCB | 104 B | 0 B | 104 B |
| LCD任务删除小计 | 3952 B | 0 B | **3952 B** |
| RX关闭且LCD关闭合计 | — | — | **7112 B** |

诊断worker保留，因此不能声称再省一个TCB的104 B。`APP_DEBUG_CLI_ENABLED=0` 且 `APP_TUNE_UART_ENABLED=0` 时才没有RX队列并采用小worker栈；仅关闭一个宏而另一个仍开启时，上述3160 B释放不成立。

这些字节来自既有49152 B堆中的空闲增加，**不额外减小链接器物理RAM占用**。LCD缓冲/对象、脉冲状态和HMI工作区等编译裁剪造成的静态物理RAM减少另由正式map统计，不能与7112 B重复相加。还必须单独核对 `ucHeap` 链接地址：数组未显式按8 B对齐，`heap_4`会同时对齐首部和尾部。基线map的 `ucHeap=0x200013C0` 初始可用49144 B，当前compat map的 `ucHeap=0x200018EC` 初始可用49136 B，差8 B。因此3160/3952/7112 B是分配负担净减少量，跨map的实际启动free差额还要加初始可用堆的变化；不得简单用旧日志水位直接相加。运行中实际free/minfree须重新建立基线；本核算不能承诺历史最小水位与启动空闲恰好相同。

## 已验证与后续验收

已通过 `run_disabled_local_feature_tests.ps1`：真实禁用路径动作/状态；对象符号检查确认LCD/debug/pulse无可写状态及移除外设句柄；CLI1/PULSE0和CLI1/PULSE1均在-Wall/-Wextra/-Werror下编译。已通过LCD transport、批量字形、行缓存及恢复回归，current core完整矩阵、safety stop、ramp driver、RS485诊断，IRQ/DMA与pulse静态集成检查。

正式Keil配置矩阵及map/静态栈结果由主流程统一记录；实物仍需确认背光与复位电平、PULSE无毛刺、控制与停车正常、开启日志后的响应耗时，以及所有任务至少256 B历史空闲栈。LCD关闭时没有LCD任务，不应把“缺少LCD水位”计作异常。
