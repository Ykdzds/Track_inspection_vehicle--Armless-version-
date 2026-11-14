# UART5只读适配软件验证记录

日期：2026-10-11

## 1. 基线

修改前HEAD：6e52de6e079722e0151e7a92b1cb336f674f0b2a。已跟踪文件无改动，已有12张visualpix截图保留。APP/app_config.h不修改，车载屏控制默认0，原轴配置和正常/停车/同步参数保留。

## 2. 主机测试

- Tests/run_tests.ps1完整回归已通过，最终运行输出保存在本机临时目录hmi_final_host_regression.log。
- Tests/run_hmi_state_tests.ps1通过：生产状态编码、共用启动身份（CAN0/1）、生产UART5/APP/快照完整读取路径（LEFT/RIGHT/BOTH × 伺服只读0/1）。
- HMI测试涵盖六区字节数与CRC、无效哨兵/端序、启动号/发布序号、来源失败、K1/CLI/自动映射、单轴/过期、RK告警、摄像存储、1750us边界、任务延迟、粘包/半帧/异常、接收溢出、发送忙/失败、DWT回绕及收帧预算。
- 共用身份验证一次启动一次写入、部分写入槽跳过、Flash耗尽/损坏失败闭锁，原CAN日志测试继续验证配置版本和令牌耗尽。
- 正式屏端全模式只读显示回归测试.cjs的32项等价脚本回归通过，屏端工程不修改；不是Visual-PIX实际运行或实物证据。
- 原有CAN、NRF、USART1 DMA、USART2调度、位置、同步、CLI、K1保持、K2看门狗、复位及只读控制回归通过。

## 3. Keil编译

最终源码九组Keil全量构建全部通过，均为0 Error(s)、0 Warning(s)：

| 配置 | 日志文件（本机TEMP目录） | 结果 |
|---|---|---|
| LEFT_ONLY / 待机0 | hmi_readonly_1_0.log | 通过 |
| LEFT_ONLY / 待机1 | hmi_readonly_1_1.log | 通过 |
| RIGHT_ONLY / 待机0 | hmi_readonly_2_0.log | 通过 |
| RIGHT_ONLY / 待机1 | hmi_readonly_2_1.log | 通过 |
| BOTH / 待机0 | hmi_readonly_3_0.log | 通过 |
| BOTH / 待机1 | hmi_readonly_3_1.log | 通过 |
| CAN关闭 | hmi_can_disabled.log | 通过 |
| 伺服只读 | hmi_servo_readonly.log | 通过 |
| 原工作区配置 | hmi_final_current.log | 通过 |

编译器ARMCC V5.06 update7 build960。矩阵使用同目录临时工程，不改原工程宏；原工程已重新构建，临时工程清理完成，RTE生成的工程名称恢复。

最终Program Size：Code=175212、RO-data=20820、RW-data=1600、ZI-data=72032字节；静态RW+ZI为73632字节。RX时间戳环形缓冲相比原字节缓冲增加1792字节，另有身份与反馈字段开销。不能将编译静态占用等同于运行期堆峰值。

最终Flash链接末端08030400，低于身份区080E0000。所有任务静态栈余量均至少256字节：HMI任务分配2048、静态深度1712、余量336字节；CLI余量760、LCD余量376、伺服任务余量984。仍需实物栈高水位验证。

APP/app_config.h SHA256：90D1DA4D768187BD63C0A124C25439D39377EBDBB725F4FC1B6FDB97F367DA3A，未修改。原Keil工程仅增加统一启动身份源文件，无宏/链接/优化选项变化。

## 4. 验收边界

软件结果不代表实际停车性能或车载屏全部页面验收。尚需烧录、UART5六区抓帧、四类实物复位、单区过期、屏幕断开回归、运行期栈高水位及错误增量记录。下载继续保留Flash扇区11。

本轮不提交、不推送Git，没有虚构实物日志。

## 5. UART5通信恢复补强（2026-10-11）

在上述状态发布改动基础上，本轮保留已有工作区，增加接收挂接核验/停挂自检、RX/TX分方向恢复、10ms失败重试、按线速加20ms余量的TX期限和完整只读诊断。未修改屏端、业务状态寄存器及控制参数。

生产链路新增初挂/逐字节重挂失败、有效BUSY/异常BUSY、返回OK但状态失配、Abort失败/假成功、恢复途中新事件、重复错误、TX丢回调及迟到回调、RX/TX隔离、错误分类、六区完成计数和HAL Tick回绕测试。168MHz测试用实际毫秒推进模拟至少120秒六区轮询，跨多次DWT回绕，并按照响应长度模拟传输时间；停请求后再次请求仍正常。测试并未使用真实UART或屏幕。

实物唯一根因仍未确认，30分钟稳定性、双向抓帧及拔线重连仍待验收。详细诊断字段、恢复原因和实物记录模板见《UART5通信恢复改进与实物验收说明》。


### 通信恢复补强验证状态

最新故障注入脚本已通过六组轴/伺服只读生产链路组合；全量主机回归通过，结果分别保存在TEMP/hmi_recovery_fault_tests.log和TEMP/hmi_recovery_host_regression.log。错误IRQ先触发RX完成再触发错误回调的顺序也已覆盖，避免重新挂接清除ErrorCode。

详细统计使用独立静态结构，未扩大HMI状态编码的业务输入栈；CLI诊断副本同样静态保存。原状态寄存器语义不变，HAL观察不会读取DR。

故障测试中的正常长时间场景（主机模拟，非实物）输出：uptime_ms=122467、requests=responses=602、各正式区域requests/responses=100/100，额外一次元数据读取和一次详细区读取成功；rx_bytes=dequeued=4832、armed=1，CRC/frame/overflow/timeout及RX/TX恢复次数均为0，最长模拟发送12ms、service_max_ms=6。调用完整hmi status未增加Abort或清DR次数。

最终源码九组Keil全量构建均0 Error(s)、0 Warning(s)：LEFT/RIGHT/BOTH × 上电待机0/1，CAN关闭，伺服只读及最终原配置。汇总日志为TEMP/hmi_recovery_build_matrix.log，各配置明细仍使用第3节的日志名。本轮最终全量主机回归exit=0，日志TEMP/hmi_recovery_host_regression.log；新增故障注入日志TEMP/hmi_recovery_fault_tests.log。

最终Program Size：Code=177612、RO-data=21492、RW-data=1616、ZI-data=72480字节，静态RW+ZI=74096字节。相比第3节上一轮最终固件，Code增加2400、RO增加672、RW增加16、ZI增加448，静态RAM增加464字节；未增任务栈。HMI静态深度1712/分配2048，余量336；CLI余量760、LCD余量376、伺服通信余量984；所有检查任务至少256字节。运行期栈高水位和堆峰值仍待实物测量。

最终Flash末端0x08031010低于身份区0x080E0000，CAN500kbit/s检查通过。原APP/app_config.h、uvprojx、uvoptx的SHA256分别为90D1DA4D768187BD63C0A124C25439D39377EBDBB725F4FC1B6FDB97F367DA3A、98EE0CA5C8DC67B48965CDC69C7E420B3BFA295BE91473FE8B19D0FB85FDB63E、C88A780A6B8FE4D3DF2A36A5E7626DA9020354393CDB9E2971D1191B06F24105，均与本轮实施前一致；矩阵临时工程无残留，RTE恢复，git diff --check通过。原配置HEX为MDK-ARM/RailInspectionCar_V1/RailInspectionCar_V1.hex。

结论：本轮UART5通信恢复软件补强及验证完成；没有烧录或执行30分钟实物测试，不宣称150/150现场停滞根因已确认或掉线已解决。正式屏端未修改，Git未提交/推送。
