# USART1 通信隔离与故障诊断 1.2

本次基线15db06e。主控与上位机协同修复，保留原v1帧，新增能力0x10和kind18；完整说明位于协作资料8.双轴调试上位机/docs/通信稳定性修复与验收.md、通信协议.md。

USART1 RX DMA2 Stream2 Channel4 circular512B，TX DMA2 Stream7 Channel4 normal；IRQ6，USART2 IRQ5不变。组行移到CLI任务，256B/200μs上限，每轮一完整命令；损坏残行丢至换行。TX复制可抢占、短BASEPRI6发布，HAL启动和异步中止恢复只在CLI，不新增RS485查询。拥塞只丢诊断数据并计数。

MDK工程使用RailInspectionCar_retained.sct，最后512B SRAM 0x2001FE00 UNINIT独占FAULT_RETAIN；不能让CubeMX覆盖为自动scatter。四个128B槽保存上次/本次首次故障和异常，CRC与最后提交标记；冷上电/欠压失效。Cortex-M4F基本和扩展帧均从SP前8words读取基本寄存器；扩展FPU区域在其后，校验全帧范围及stacking error。异常入口先保存原SP再切512B独立栈，记录后等待原IWDG，不打印、不喂狗。MSP预算2048B，key任务224words，其余任务优先级/运动权限和安全时序保留。

新诊断需PC明确diag1，并共享stream三秒租约；1Hz四页缓存、250ms错开发送，242B/帧。旧PC未订阅时不发送。UART、Modbus、心跳、栈与服务间隔读缓存；Modbus失败时间、遥测和保留故障用HAL毫秒原点。last_failure_attempt从0计数。未知word为0xFFFFFFFF，不能将无效速度看作零速。

验证入口：Tests/run_debug_uart_tests.ps1、run_current_core_tests.ps1、run_tune_tests.ps1、run_tune_diagnostics_tests.ps1、run_runtime_metrics_tests.ps1、run_fault_retention_tests.ps1、run_system_snapshot_tests.ps1以及既有联动/安全/看门狗回归。最终Keil全量构建后运行Tests/check_tune_memory.ps1，验证Flash身份区边界、UNINIT、故障栈和任务静态预算。实际水位、中断屏蔽≤10μs与P99服务间隔需目标实测。

新固件独立交付到8.双轴调试上位机/firmware/releases/sync-tune-1.2，原历史固件和回退材料保留。需要重新烧录；本轮未连接车辆或烧录，未完成两小时及NRST/3.3V/串口波形验证。保持驱动器1000ms通信保护、IWDG和原安全门禁，不能用上位机重连作为整车问题已解决的证据。
