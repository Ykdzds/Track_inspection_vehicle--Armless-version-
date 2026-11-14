# UART5通信恢复改进与实物验收说明

日期：2026-10-11

## 1. 结论与边界

本轮仅修改STM32F4主控，不修改正式屏端工程，不提交、不推送Git。基线HEAD为6e52de6e079722e0151e7a92b1cb336f674f0b2a，实施前已有状态发布修改及12张截图全部保留。

已补齐UART5接收挂接核验、接收停挂自检、分方向有界恢复、发送超时和只读诊断。现场唯一根因尚未确认：此前requests/responses停在150而publish持续增加，仍需判断屏端是否继续发请求。

保持APP_HMI_CONTROL_ENABLED=0U，地址1、115200、8N1及寄存器/字典不变。06H/10H继续拒绝，不新增HMI伺服写请求，不改变CAN协议、控制源、停车或同步参数。

## 2. 恢复行为

- RX挂接必须满足HAL返回OK或BUSY，且本模块单字节缓冲、RxState、RxXferCount/RxXferSize及RXNE/错误中断均匹配。BUSY不是无条件成功。
- 每轮任务检查挂接状态，不因长时间没有请求而重启UART。
- 初挂、重挂失败及UART错误请求RX恢复；错误挂起时屏蔽RX中断，任务终止RX后清残留，再重新挂接。
- RX/TX分别保存事件代次，首次立即尝试，失败后10ms限频重试。Abort返回失败、硬件/HAL状态未终止或发生更新事件均不能清除恢复请求。
- TX期限为ceil(字节数×10×1000/115200)+20ms，133字节响应为32ms。超时先Abort并验证旧TX终止，才释放静态发送槽；超时事务不计正常响应，迟到回调也不能补记该失败事务。
- RX线错误或停挂不终止正常TX；TX超时不重启正常RX。未复位UART外设或其他串口，不清零全局DWT。
- HAL错误IRQ可能先交付字节再执行错误回调；检测到ErrorCode时不重挂RX，避免Receive_IT清除原始错误码，错误字节不进入组帧。
- 错误和RX恢复丢弃残帧，通过接收代次通知组帧层，等待1750us静默后恢复。恢复后的首条过早请求可能被丢弃，需主站继续重试；主控不主动推送或重放旧响应。
- ISR只记录、搬运和挂起恢复；无打印、等待、运动操作。UART5采用IT，无DMA，本机HAL Abort在该路径无等待循环。

## 3. hmi status字段

通过USART1按需执行hmi status，无持续日志；请完整保存全部输出，不只保留最前两行。

| 信息 | 字段及解释 |
|---|---|
| 身份/发布 | boot、identity_valid/result、publish；不同于链路物理在线 |
| 运行/延迟 | uptime_ms、request_age_ms、service_max_ms；无合法请求时age=4294967295 |
| 解析延迟 | delay_max_cycles/clock_hz为请求末字节到响应提交的最大周期差；不是线上首字节延迟 |
| 接收 | rx_bytes、callbacks、dequeued、queue、ignored、armed、arm_fail、arm_result |
| 错误 | rx_errors、last_error、ore/fe/ne/pe；保留累计证据 |
| 发送 | tx_frames、callbacks、active、timeout、max_ms及原busy/errors；提交不等于完成 |
| 恢复 | rx/tx_recovery、reason、epoch、attempt/ok/fail、abort |
| HAL | gState/RxState/ErrorCode、rx_count/tx_count、CR1/CR3 |
| IRQ历史 | irq、SR_irq_history、SR_error_history；不是CLI读取瞬时SR，CLI不读DR |
| 区域 | region=0至5分别R0、R1、R2、R3、R4、元数据；6为其他合法读取 |

恢复reason：0无、1挂接失败、2状态失配、3UART错误、4发送超时、5发送提交失败。reason保留最后原因，是否仍恢复以recovery为准。HAL返回值：0 OK、1 ERROR、2 BUSY、3 TIMEOUT。

每个region的requests仅在合法读取成功构建时增加，responses仅在对应正常TX完成时增加。异常响应另计，不代表屏端已经接受。rx_bytes包括接收完成后因溢出/恢复丢弃的字节，dequeued是实际组帧取出的字节，不能强求两者相等。

## 4. 停滞定位

| 观察 | 下一步 |
|---|---|
| publish增加，rx_bytes不增，armed=1 | 高阻抓取PD2，区分屏停发、线路问题和IRQ入口问题 |
| armed=0、arm_fail或rx_recovery/fail增加 | 保存HAL状态和返回值，核验接收挂接/恢复 |
| rx_bytes/dequeued增加，requests停 | 查看ignored、CRC/frame/partial、队列及两向抓帧 |
| requests增，responses停，busy/timeout增 | 查看TX状态、Abort失败及PC12完整响应 |
| 六区responses正常，屏幕断开 | 核验屏超时、CRC接受、001C/元数据和实际烧录版本 |

不能仅凭软件统计断言屏端已停发，也不能把快照无效解释为物理断线。若确认屏TX不再输出请求，需要屏端继续排查轮询与重试；本轮不改屏端。

## 5. 实物验收模板（尚未执行）

1. 固定车辆、安全停车后烧录原配置HEX，保留Flash扇区11。
2. 六区持续03H轮询，至少30分钟；正常、异常和断开后间隔2至5秒分别保存完整hmi status。
3. 高阻监听屏TX→PD2及PC12→屏RX至少120秒，共地，不并接主动TX；保存波形及解码。
4. 核验R0请求01 03 00 00 00 40 44 3A，响应133字节、CRC、启动号和001C；其余区域按既有六区测试向量。
5. 分别记录静止、伺服上电待机和既有遥控低速运行；测量请求末字节至响应首字节时延，核实实际屏超时设置。
6. 断开/重连屏幕及屏幕重启，确认无需复位主控恢复；屏端需持续重试请求。不得用取消过期门禁掩盖失败。
7. 同时记录USART2错误增量、CAN/NRF、任务心跳、栈高水位和复位情况，确认拔屏不影响原控制。

记录字段：固件/屏版本、轴配置、工况、开始结束时间、六区请求/响应增量、恢复/错误增量、boot/publish、最大响应延迟、抓帧路径、栈余量、结论和未通过项。

实物30分钟稳定运行、断开重连和运动工况未完成前，只标记软件风险补强完成，不宣称现场故障已解决。

## 6. 软件验证结果

生产故障注入六组轴/只读组合及全部主机回归通过。最终九组Keil全量编译均0错误、0警告；原配置HEX已重新生成，配置及工程文件校验值与本轮开始一致，临时工程已清理。HMI静态栈余量336字节，静态RAM比上一轮增加464字节，Flash末端0x08031010未进入扇区11。详细结果见《UART5只读适配软件验证记录》第5节。当前状态为软件补强完成、现场根因未确认、实物验收待执行。
