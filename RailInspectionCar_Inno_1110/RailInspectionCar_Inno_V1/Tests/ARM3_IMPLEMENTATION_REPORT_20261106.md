# F407 ARM3 CAN软件实施记录

日期：2026-11-06。基线：分支 `fix/hmi33-scheduling-session`，原HEAD `01b17511a5af1ad911ab356dc9f2943f54f3e78b` 加已有ARM3候选工作区。本次提交包含该候选及本轮补充，不把此前工作全部计成本轮新写。

## 已完成

- 核对F407→CAN→RK→USB架构；USART3/USART6无应用机械臂收发，生成配置与原初始化保留。
- 纳入已有ARM3管理器、owner/许可/操作编号、逐侧结果和运输证明；CAN停止关联账本、不可变记录队列、停止资源、实际TX/RX时刻；底盘授权、Flash维护和最终伺服非零写入互锁；NRF v2和HMI rev3适配。
- 本轮新增 `can arm3 status`，由CAN任务发布一致只读缓存，CLI不直接进入可变管理器；初始未发布返回false，快照年龄随时间增长且支持Tick回绕。不改变CAN/NRF/HMI线上布局，也不授owner、发动作或续租。
- 补齐端口诊断测试、生产保护/冷启动夹具的arm_lock依赖；驱动单测弱桩在链接真实仲裁器时被强实现替代，避免重定义。
- 修正Keil兼容脚本中CAN关闭/伺服只读变体未同时关闭CAN控制宏的问题；新增CompatibilityOnly续跑选项，生产编译保护未改。
- 增加README_ARM3_READINESS.md，给出远程联调命令、分阶段实物条件和验收记录要求。

## 本轮验证

| 检查 | 结果及边界 |
|---|---|
| run_arm3_tests.ps1 | PASS，真实C管理器/codec主机验证 |
| run_arm3_input_tests.ps1 | PASS，输入会话/去重/点动预算/一致状态 |
| run_arm3_port_tests.ps1 | profile0/1均PASS；含未知启动、只读缓存、不读可变manager、陈旧年龄和Tick回绕 |
| run_arm3_joint_tests.ps1 | 8/8 PASS，实际C↔实际RK Python业务记录，设备和安全输入为模拟 |
| run_arm3_vectors.py | 7组双向分片/CRC/manifest黄金向量PASS |
| run_can_protocol_tests.ps1 | PASS，协议/分片/身份/缓存/状态/APP配置矩阵/BSP/向量 |
| run_can_control_tests.ps1 | PASS，宏0与单/双轴、授权/停车/去重/关联/互锁 |
| run_nrf_host_tests.ps1 | PASS，旧NRF与遥测配置矩阵；未运行设备无线 |
| run_can_hmi_joint_tests.ps1 | PASS，原三源控制、K5、停车释放及恢复 |
| run_hmi_modbus_tests.ps1 / run_hmi_control_tests.ps1 | PASS，旧邮箱/控制状态/配置矩阵 |
| run_sync_storage_tests.ps1 | PASS，Flash故障注入/恢复/维护锁/配置 |
| run_dual_axis_tests.ps1 | PASS，双轴运动及同步模型；模型指标不是整车实测 |
| run_production_guard_tests.ps1 | PASS，锁定时拒绝左右非零速度，生产保护仍有效 |
| run_field_regressions.ps1 -Quick | PASS，LCD释放/重新授权与BOTH+SYNC换向；更大原功能集合由下行整套脚本覆盖 |
| run_tests.ps1（未跳过位置监控） | 最终完整运行exit0；包含冷启动、协议/伺服/无线/HMI/CAN/CLI/位置/同步/运行时等原回归 |
| Keil build_can.ps1 | 6组axis1/2/3×debug0/1全部0错误0警告；修正脚本后CompatibilityOnly的关闭/只读/最终当前3组全部0错误0警告 |
| Flash边界/静态栈预算 | 各成功构建检查PASS；函数指针和实测栈高水位仍需现场验证 |
| git diff --check | PASS，仅Git已有换行转换提示 |

首次全套回归发现的夹具缺依赖/重定义及首次兼容构建的非法宏组合均已修正后重新验证。没有降低断言、跳过位置监控或关闭生产安全门禁来取得PASS。

最终当前工程宏恢复 `USE_HAL_DRIVER,STM32F407xx`，工程差异仅新增ARM3源码登记；未遗留测试轴/只读/CAN关闭宏。

最终当前构建资源：ROM 370100B；LR_IROM1结束 `0x0805AFF8` 小于 `0x080A0000`；CCM 62024/65536B；IRAM1 105528/114688B；IRAM2 6708/15872B；保留故障区512B位于 `0x2001FE00`，未改变。CCM只放CPU状态，DMA缓冲不迁入；剩余空间有限，后续扩大缓存必须复核MAP。

固件SHA256（本轮最终当前产物，未烧录）：

| 文件 | SHA256 |
|---|---|
| RailInspectionCar_V1.hex | `3dd0849e5218794164b28cd7866e29861df641e636cad62426c710521898f150` |
| RailInspectionCar_V1.axf | `8b105767da88cbc7d6ee7be705d0c031dc8eb35b229e9f879009d0fcb4405093` |
| RailInspectionCar_V1.map | `5ab01e93b15164d31c593940655096e4696ccb1022fe176d3b589053f3e2875d` |

## 尚未完成的现场工作

本轮没有烧录F407、部署RK、连接CAN/USB、运行机械臂或修改遥控/HMI工程。`APP_ARM3_PROFILE_ENABLED=0`，真实安全采样仍是缺省false，机械臂运动未开放。

硬件接线/GPIO/极性/去抖、独立停止与承载保持尚未提供，不能据未知引脚实现真实绑定。上层实际运行版本、设备限位/预设/运输位、厂家启动和T102停止证据需共同核实。

下一步：先模拟对端及无运动真实CAN抓包；真实安全和设备行为取证完成后再添加正式采样绑定、重建回归，按CAN实动→遥控→HMI36→冲突与原功能整车回归推进。最终冻结须实机动作/停止/双臂运输证明及原功能回归全部通过。本记录仅证明软件候选验证，不证明全部原功能已在实物稳定运行。
