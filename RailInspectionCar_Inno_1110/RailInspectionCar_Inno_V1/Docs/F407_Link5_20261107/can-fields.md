# F407 CAN伺服与机械臂串口诊断字段

## 开关与采集范围

以下开关位于 `APP/app_config.h`，彼此独立，交付默认均为 `0`：

| 宏 | 日志source | 取证范围 |
| --- | --- | --- |
| `APP_CAN_SERVO_DIAG_ENABLED` | `CS`，内部值2 | 底盘CAN运动授权、SPEED收取/消费/拒绝、目标租约、双轴命令、停车前现场、结果与公共CAN链路 |
| `APP_CAN_ARM_DIAG_ENABLED` | `CA`，内部值3 | ARM3输入、owner/permit/代数、释放前现场、业务结果、CAN记录收取/拒收、最终COMMIT发送及公共CAN链路 |
| `APP_CAN_DEBUG_ENABLED` | `CANRAW`，内部值5 | 可选原始CAN帧，最多20个事件/秒；不能替代以上业务诊断 |

这些都是 **F407侧** 开关。打开CAN机械臂开关时，也记录遥控/HMI适配器提交到F407 ARM3 manager的输入，`source`区分来源。日志没有声称已经采集F103内部调度、USB总线物理接收或机械臂设备内部执行时间。

所有输出由现有USART1 CLI writer统一完成，共享整行非阻塞发送、6000字节/秒总预算及首次记录优先级。不增加任务、串口字节缓冲、描述符、运行期分配或协议收发次数；不读取并消费业务结果、不续租、不刷新目标、不改变保护期限。

字段值和`kind`均为十六进制。下表词序从0开始；EVENT六词也记为a～f。`lo16/hi16`表示同一词低/高16位；带符号量采用二进制补码。RPM对低16位为左轴、高16位为右轴，分别按`int16_t`解释；独立有符号量按`int32_t`解释。轴mask bit0=左、bit1=右。

`LD <at> <boot> <serial>`使用HAL毫秒时间；事件中的CAN接收时间来自现有帧捕获点，**不是对物理总线上最后一位到达时间的承诺**。`STATE/HEALTH sample=`为既有published sample时间，空闲或非owner时可能长期不更新，须同时核对实际年龄和当前owner。

`STATE/HEALTH`每行最多12词，用`part=0/1`续行。周期投影每次重新读取既有发布值，分行之间可能更新，不能视为同一不可变事务。`FIRST`每页8词，在最后一页成功交给USART1发送前保持payload不变；后续异常只增加`merged`，不会覆盖待发首条。USART1接收成功不等于PC已经保存。

成功SPEED收取/消费和正常ALIVE可由共享writer采样，`sampled`为主动采样计数。拒绝、停车、异常及首次冻结不按成功事件策略采样。普通FIFO仍受共享拥塞丢弃约束；核对累计overflow/合并计数，不能把没有普通EVENT当作没有发生。

公共仲裁器停车事件使用独立保留类型：HMI诊断开启时保留原`HLD E6/E7/E8`；HMI关闭时，经当前开启来源输出`LD EVENT kind=3E6/3E7/3E8`（首次/升级/完成）。六词依次为event、reason、owner16/requester16、request sequence、command16/deceleration16、result16/error16。该区间与CS `100～110`、CA `200～208`、CANRAW `300`及通用初始化`3F0`均无冲突，须先按source与kind解释，不能把公共事件当作CAN业务结果。

## CS STATE，phase 0，16词

| 词 | 含义 |
| --- | --- |
| 0 | state bits0～7；authorized bit8；first_zero_done bit9；strategy_ready bit10 |
| 1～3 | 控制generation；motion_token；speed_sequence |
| 4～5 | 已生效目标年龄ms；有效剩余时间ms（含心跳剩余约束） |
| 6～7 | target_mm_s；actual_mm_s，均符号扩展至32位 |
| 8～9 | commanded_rpm左/右；pending_rpm左/右 |
| 10 | batch ID |
| 11 | pending_mask bits0～7；axis_mask bits8～15；actual_valid bit16 |
| 12～13 | 左/右驱动写入sequence |
| 14 | stop_generation |
| 15 | stop_phase bits0～7；stop_reason bits8～15；stop_result bits16～23 |

state：0 IDLE，1 PREPARING，2 FIRST_ZERO，3 READY，4 RUNNING，5 REVERSING，6 DRAIN，7 DECEL，8 ZERO，9 HOLD，A DONE，B FAULT。`actual_valid`是发布时的有效性，不能单独证明采集时反馈仍新鲜。

## CS HEALTH，phase 1，15词

| 词 | 含义 |
| --- | --- |
| 0～4 | rejected；duplicates；conflicts；queue_full；grants累计次数 |
| 5～6 | detail；limit_mm_s |
| 7～8 | stop_elapsed_ms；stop_timeout_ms |
| 9～11 | first_command_ms；zero_ms；complete_ms；未发生时可为FFFFFFFF |
| 12 | zero_mask bits0～7；failed_mask bits8～15；stop_owner bits16～23；cross_stop_result bits24～31 |
| 13～14 | 同步速度配对拒绝次数；有效配对次数 |

cross_stop_result：0无；1执行中；2成功停车并释放；3安全失败。它表示F407本地停车事务状态，不能仅据此推断机械臂物理姿态。

## CS EVENT

| kind | 触发点 | a～f |
| --- | --- | --- |
| 100 | CONTROL入队/拒绝 | received_ms，当前ms，原始data[0:4]，原始data[4:8]，why，state/authorized |
| 101 | SPEED收取/拒绝 | 同100 |
| 102 | STOP收取/拒绝 | 同100 |
| 103 | SPEED通过消费门禁 | 同100；why=0；此后仍需看实际batch及写入结果 |
| 109 | SPEED已从邮箱取出但未采纳 | 同100；why=1未授权，2控制generation不符，3消费时已过期 |
| 10B | STOP实际执行入口 | 同100；why=0 |
| 104 | 业务结果生成 | 原请求接收ms，当前ms，seq bits0～7/cmd bits8～15，result bits0～7/phase bits8～15/detail bits16～31，请求link_generation，当前control_generation |
| 105 | 开始停车或已有停车升级之前 | 当前ms，reason/已有stop标记bit8，control_generation，旧stop_generation，state/authorized/stop_phase逐字节，speed_sequence |
| 106 | fail进入前 | 同105；第1词改为code bits0～7/reason bits8～15 |
| 108 | 已发布状态、授权或停车阶段变化 | sample_ms，旧state bits0～7/新state bits8～15/authorized bit16，control_generation，stop_generation，stop_phase/stop_reason/stop_result逐字节，complete_ms |
| 110 | 公共链路变化 | 见下文公共事件 |

100/101/102/103/109/10B的f：state bits0～7、authorized bit8。原始字节按现有CAN协议 **小端** 打包，例如字节`01 02 03 04`显示`04030201`，不是调换线缆字节。

100 why：0受理；1恢复门禁；2命令参数不合法；3不支持的命令；4优先请求槽忙；5队列满。这里why是内部诊断分支，不是wire返回码。

101 why（表中数字用十进制便于阅读，日志显示为十六进制）：

| why | 首个不满足的条件 |
| --- | --- |
| 0 | 有效新SPEED已写入邮箱 |
| 1～4 | 恢复门禁；未授权；链路未ready；strategy未就绪 |
| 5～8 | link generation不符；upper boot不符；旧目标期限已到；心跳期限已到 |
| 9～13 | token不符；租约范围；速度上限；反向禁止；首条非零 |
| 14 | 同序号同内容重复，保持原不续租行为，不冻结首次拒绝 |
| 15～17 | 同序号内容冲突；旧序号；收取时目标已过期 |

102 why：0受理，1token/参数非法，2重复同包（不冻结首次），3同序号冲突，4已用/旧STOP序号。

### CS FIRST布局

拒绝类100/101/102/109：前6词与该EVENT相同，后10词依次为published control_generation、motion_token、incoming link_generation、incoming upper boot、心跳年龄、旧accepted_speed_ms、旧lease、qcount bits0～7/resultcount bits8～15、授权时link_generation、授权时upper boot。

105/106停车前现场16词：当前ms、why、control_generation、旧stop_generation、state/authorized/stop_phase逐字节、speed_sequence、speed_age_ms、remaining_ms、target_mm_s、commanded_rpm对、授权link_generation、授权upper boot、当前link token、心跳时间、旧stop_reason、旧stop_result。这些是在撤销授权/清目标前抓取的现场，当前真正首次停车事件/reason/requester/deceleration以仲裁器的共享首次停车记录为准；升级不会伪装为新首因。

## CA STATE，phase 0，18词

| 词 | 含义 |
| --- | --- |
| 0 | published有效bit0；link_ready bit1；negotiated bit2；profile_enabled bit3；safety_valid bit4；config_match bit5 |
| 1 | ARM generation |
| 2 | owner、state、permit_mask、work_locked，各占一个字节 |
| 3～5 | 保留reason；arm_version；safety_version |
| 6 | owner_remaining_ms lo16；input_remaining_ms hi16 |
| 7～10 | source_age_ms；last_operation；rejected；queue_full |
| 11 | input_queue_depth bits0～7；stop_pending bit8；chassis_busy bit9；parameter_locked bit10 |
| 12 | platform.sample_age_ms |
| 13 | chassis_zero_stable_ms lo16；chassis_feedback_age_ms hi16 |
| 14～17 | hardware_bitmap；mapped_input_mask；config_flags；snapshot_sequence |

CA投影只读`app_arm3_port`既有双缓冲发布页；诊断读取不调用manager service或重新计算运动许可。

## CA HEALTH，phase 1，24词

左臂词0～11，右臂词12～23。每侧12词布局：

| 侧内词 | 含义 |
| --- | --- |
| 0～4 | operation；generation；version；sequence；result_sequence |
| 5 | phase、result、result_flags、proof_state，各占一个字节 |
| 6～9 | feedback_age_ms；detail；raw_fault；usb_epoch |
| 10 | online bit0；active bit1；stopping bit2；feedback_real bit3；identity_valid bit4；config_valid bit5；transport_approved bit6 |
| 11 | proof_id |

## CA EVENT

| kind | 触发点 | a～f |
| --- | --- | --- |
| 200 | 输入处理结果 | 当前ms，source bits0～7/cmd bits8～15/mask bits16～23/held bit24/page_active bit25，session，sequence，operation，result bits0～7/site bits8～23 |
| 201 | 输入拒绝首次现场 | 当前ms，result/site，operation，sequence，generation，owner/state/permit/worklock逐字节 |
| 202 | begin_release清owner前现场 | 当前ms，release reason，release operation，owner sequence，generation，owner/state/permit/worklock逐字节 |
| 203 | 已解码业务结果 | record start_ms，commit_ms，当前ms，operation，generation，phase bits0～1/result bits2～5/flags bits6～8/mask bits9～10/record type bits16～23 |
| 204 | CAN记录收取结论 | record start_ms，commit_ms，当前ms，record type bits0～7/length bits8～23，accepted（1/0），当时保留reason |
| 205 | 首次transport unknown | 当前ms，保留reason，0，0，generation，owner/state/permit/worklock逐字节 |
| 206 | PC查询/owner请求结果 | 当前ms，opcode bits0～7/status bits8以上，query ID，0，generation，owner/state/permit/worklock逐字节 |
| 207 | CAN记录拒收首次现场 | 当前ms，record type/length，payload+4处operation（length至少8，否则0），commit-start，generation，owner/state/permit/worklock逐字节 |
| 208 | 动作/查询结果的最后COMMIT已经TX_OK | 当前ms，record type，record sequence，length，payload[0:4]，payload[4:8] |
| 110 | 公共链路变化 | 见下文公共事件 |

source：1遥控、3HMI、4PC/CAN。cmd：1 APPLY，2 ALIVE，3 RELEASE，4 STOP，5 JOG，6 PRESET，7 SIX_AXIS，8 LIGHT_ON，9 LIGHT_OFF，A SAVE_ACTUAL，B SAVE_TARGET，C READ_POSE。

200 site：1基本参数；2同序号内容冲突；3重放返回；4旧序号；5输入缓存满；6STOP mask；7STOP operation分配失败；8非owner释放；9owner竞争；A释放中；B profile未就绪；C页面/held参数；D姿态读前置条件；E发送队列满；F非owner/页面无效；10状态禁止；11配置版本；12permit；13 motion_submit结果；14（十进制20）为 **ALIVE返回成功但held证据不匹配、触发停止JOG**。site0且result0才可作为普通成功ALIVE采样；page_active=false的ALIVE可能发起release，不属于普通周期事件。

206 status：0正常，1～8为现有wire业务状态；`100|opcode`为查询仍pending的重复请求，`200|opcode`为缓存结果重放，`800|opcode`为同ID内容冲突。数字均十六进制。

204的accepted仅表示协议/manager受理，不表示业务动作成功；尤其正常结构的故障摘要也可能accepted=1。207只承诺记录拒收，不凭空声称是schema、序号、配置或年龄中哪个细分原因。203明确带record type，区分聚合55与单侧5F。208只证明F407最后COMMIT收到CAN控制器TX_OK，**不等于上层软件已消费，也不等于机械臂物理动作完成**；应继续关联203结果、proof及反馈年龄。

### CA FIRST布局

201/202/205/207及需冻结的206采用同一16词现场：当前ms、why、operation、sequence、generation、owner/state/permit/worklock逐字节、arm_version、safety_version、token、owner_age_ms、适配器input_delay_ms、左active_operation、右active_operation、状态bitmap、lower_boot、upper_boot。

状态bitmap：ready bit0、negotiated bit1、profile_enabled bit2、safety_config_approved bit3、parameter_locked bit4、recovery_locked bit5、chassis_moving bit6。`why/operation/sequence`的含义跟对应EVENT；例如207的sequence位置承载commit-start，不能解释为输入序号。

## 公共CAN HEALTH，phase 2，24词

CS/CA同时开启时总线页只输出一份；这两个业务共用物理CAN链路，不能重复累计。

| 词 | 含义 |
| --- | --- |
| 0 | enabled bit0；task_created bit1；BSP bus.phase bits8～15；应用link.phase bits16～23 |
| 1～4 | F407 boot_id；upper_boot_id；link token；link generation |
| 5～8 | bus rx_count；tx_submitted；tx_success；tx_failed |
| 9～11 | bus_off_count；recovery_count；last_ESR |
| 12～13 | rx_dropped；rx_fifo_overrun |
| 14 | rx_high_water lo16；当前rx_queued hi16 |
| 15～19 | 协议rejected；queue_dropped；record_failed；upper_invalid；stale_rx |
| 20～23 | 原始帧限速log_skipped；tx_expired；init_timeouts；identity_result |

BSP phase：0 OFF，1 WAIT_INIT，2 WAIT_READY，3 READY，4 BACKOFF。应用link phase：0 IDENTITY_FAILED，1 WAIT_PEER，2 WAIT_CONFIRM，3 WAIT_HEARTBEAT，4 READY。

110 EVENT前6词：当前ms、旧link phase bits0～7/新link phase bits8～15/BSP phase bits16～23、link generation、token、bus_off_count、last_ESR。离开READY或BusOff/收包丢弃/record_failed递增时冻结FIRST。FIRST追加词6～15：rx_dropped、record_failed、upper_invalid、lower boot、upper boot、rejected、queue_dropped、stale_rx、recovery_count、旧link generation。

## 可选CANRAW kind 300

六词：帧捕获timestamp_ms；CAN ID bits0～15/DLC bits16～23/direction bits24～31；data[0:4]小端；data[4:8]小端；link generation；token。direction：0 RX，1 TX成功，2 TX失败，3提交发送。限速发生在写共享FIFO之前，超过20事件/秒增加`log_skipped`，因此raw日志不能作为逐帧无丢失总线抓包。

旧`s_logs[64]`原始日志队列已移除，释放1536字节；业务状态从现有published结构窄读，不为诊断再复制完整CAN/ARM3 manager。最终RAM结果以正式Keil map及交付构建记录为准。

## 回归依据

生产路径测试覆盖：CONTROL/SPEED/STOP处理、首拒绝精确分支、收取和消费分离、重复不续租、原始帧小端/20事件限速、首条冻结、ARM输入冲突与release前owner保存、ALIVE隐式停车不被普通成功采样、窄投影不调用manager、不二次消费业务结果。`Tests/run_link_can_diag_tests.ps1`及既有CAN control/protocol、ARM3、ARM3 port套件通过；这些软件结果不替代CAN接线、实际机械动作和长时间现场验收。
