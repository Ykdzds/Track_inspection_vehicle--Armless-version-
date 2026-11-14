# 新增持久诊断元数据预算

## 核算口径与当前结论

以实施前基线`dd916c6`为比较对象，核对本轮生产源码新增的静态/全局持久诊断对象；测试程序、局部栈变量、Flash常量不计入SRAM持久元数据。**只累计新增对象，不使用移除旧缓存的收益抵扣768字节上限。**

最终`all`与`legacy`正式map已逐一验证33个新增符号，大小合计均为 **575字节**；将每个新独立符号都向上取整到4字节后按 **608字节**保守记账，距离768字节上限余 **160字节**。该保守算法包括结构体/数组既有内部填充，并再次为独立小符号保留对齐空间。

核对产物为`验证/all/RailInspectionCar_V1.map`（2026-11-07 23:27:51）及`验证/legacy/RailInspectionCar_V1.map`（23:29:37），同一冻结生产源码digest：`8D24A3758F71C6625C0067DA7E50B6BD35DCE70FC73C4699FAF91B7EAF04627E`。两种配置均0错误、0警告，元数据和总链接RAM预算通过；静态调用栈检查通过。**该结论不替代实物历史栈水位、堆基线和持续运行验收。** 静态元数据上限、总物理RAM和任务栈是三个独立指标。

## 共享writer：394字节

文件`APP/HMI/app_hmi_link_diag.c`。

| 新符号 | ARM布局大小B | 最终all / legacy map大小B | 用途 |
| --- | ---: | ---: | --- |
| `normal_used` | 2 | 2 | 普通输出预算；原`budget_used`复用 |
| `priority_output` | 1 | 1 | 输出优先级 |
| `source_cursor` | 1 | 1 | 周期source轮询 |
| `first_cursor` | 1 | 1 | 首条记录公平轮询 |
| `compact_phase` | 1 | 1 | HEALTH服务状态 |
| `compact_at` | 4 | 4 | STATE节拍 |
| `health_at` | 4 | 4 | HEALTH节拍 |
| `sampled` | 4 | 4 | 主动采样累计数 |
| `hmi_sample_at[3]` | 12 | 12 | HMI成功事件采样时间 |
| `hmi_sample_seen` | 1 | 1 | HMI采样时间有效位 |
| `can_sample_at[3]` | 12 | 12 | CS收取/消费及正常CA ALIVE采样时间 |
| `can_sample_seen` | 1 | 1 | CAN采样时间有效位 |
| `queued_by_source[7]` | 7 | 7 | 既有FIFO各来源占用数；没有增加FIFO描述符 |
| `projection_cursor[2]` | 16 | 16 | STATE/HEALTH独立游标，每个8字节 |
| `health_source` | 1 | 1 | HEALTH来源轮询 |
| `health_page` | 1 | 1 | HEALTH阶段轮询 |
| `state_page` | 1 | 1 | STATE分批服务 |
| `stop_merged` | 4 | 4 | 待发首停车记录后续事件合并数 |
| `route_first[4]` | 320 | 320 | RF/CS/CA/RS485每路一条不可覆盖的待发首因 |
| **合计** | **394** | **394** | 两份最终map逐符号一致 |

`route_first_t`布局：`at/number/words[16]/merged`共76字节，`kind`2字节，`length/page`各1字节，合计80字节；生产源码已有80字节编译期断言。四路固定320字节，没有按每条异常分配对象。

`projection_cursor_t`包含`id`4字节与`part/source/phase`3字节，按4字节对齐为8字节；两个16字节。当前按全16字节累计，没有抵扣初轮临时实现中被替换的旧游标。

## RS485、轴反馈与安全状态：144字节

| 文件/新符号 | ARM布局大小B | 最终all / legacy map大小B | 用途 |
| --- | ---: | ---: | --- |
| `Protocol/modbus_rtu.c: s_enqueued[2]` | 32 | 32 | 普通/安全入口两个入队时间sidecar，各16字节 |
| `Protocol/modbus_rtu.c: s_link_transaction` | 64 | 64 | 当前事务时间与上下文，含8字节固定原始片段 |
| `Protocol/modbus_rtu.c: s_link_unknown` | 4 | 4 | 不能匹配的入队时间次数 |
| `Protocol/modbus_rtu.c: s_link_late` | 4 | 4 | 延迟计数 |
| `Protocol/modbus_rtu.c: s_link_response_drop` | 4 | 4 | 响应分发失败数 |
| `Protocol/modbus_rtu.c: s_link_sample_at` | 4 | 4 | 普通事务采样时间 |
| `BSP/RS485/bsp_rs485.c: s_wire_time` | 16 | 16 | 当前TX启动/TC/RX时间及有效位 |
| `Device/aimtor_servo.c: s_link_health_flags[2]` | 8 | 8 | 两轴上次反馈状态位，用于0x22变化取证 |
| `APP/app_health.c: s_link_safety_flags` | 4 | 4 | 安全状态0x23的上次状态位 |
| `APP/app_health.c: s_link_safety_failure` | 4 | 4 | 安全状态0x23的上次failure |
| **合计** | **144** | **144** | |

`diag_enqueue_t`：3个32位字段12字节，加两个字节及2字节尾部填充，16字节。`diag_transaction_t`：9个32位字段36字节、4个16位字段8字节、8个单字节字段8字节、原始片段8字节、request_owner 1字节及3字节尾部填充，共64字节。

`modbus_rtu.c`已有新增sidecar总量不超过112字节的编译期断言；另加BSP的16字节为128，轴反馈/安全状态只读观察再加16字节。没有增加业务队列长度、RTU字节缓冲或UART DMA缓冲。

## 遥控反馈与状态投影：37字节，按40字节记账

文件`APP/app_remote_servo.c`。

| 新符号 | ARM布局大小B | 最终all / legacy map大小B | 用途 |
| --- | ---: | ---: | --- |
| `s_remote_diag_feedback` | 28 | 28 | 发布时间、两轴sequence/age、RPM对及有效位，共7个32位词 |
| `s_remote_diag_consumed` | 4 | 4 | 上次已记录消费帧号 |
| `s_remote_diag_stop_frozen` | 1 | 1 | 首停冻结标志 |
| `s_remote_diag_before` | 4 | 4 | 单一控制任务调用范围内保存service前状态，使service外层不跨业务深调用保留多参数输出栈帧 |
| **符号合计** | **37** | **37** | 保守另留3字节对齐，按40字节 |

NRF驱动、遥控接收器和遥测调度器诊断投影读取现有状态，不新增完整snapshot副本或每类型统计数组。CAN伺服与ARM manager同样窄读现有published state，不新增持久业务状态副本。

## 对齐预算与不重复计费

| 项目 | 新符号原始合计B | 每个独立符号按4字节向上取整B |
| --- | ---: | ---: |
| 共享writer | 394 | 424 |
| RS485、轴反馈/安全状态 | 144 | 144 |
| 遥控投影 | 37 | 40 |
| **总计** | **575** | **608** |
| **相对768字节余量** | **193** | **160** |

以下对象不重复当作新增元数据：

- 原`events[32]`事件FIFO仍为相同数量和相同36字节描述符，合1152字节；`kind`只重新分配内部位域，没有扩大事件结构。
- 原`line[256]`仍为256字节；没有新增输出字符串缓冲。
- `scratch`是把原多个CLI临时对象合并的union；两份最终map均为444字节、ARM编译期上限448字节。`app_link_diag_snapshot_t`只是该union新增候选成员，不另占一份静态RAM。不将444字节错误算成额外持久元数据，也不把合并收益用于抵扣本表575字节。
- `first_stop/lcd_pending/screen/slow_first`等不可变待发对象沿用基线，仍在总链接RAM中，但不是本轮新增。
- `enabled_sources[7]`和writer `names[7]`指针表分别为7/28字节，两份最终map均位于`app_hmi_link_diag.o(.constdata)`、地址`0x080xxxxx`，属于RO/Flash，没有落入RW/ZI。
- CAN旧` s_logs[64]`队列已移除；原限速` s_log_lines/s_log_window/s_log_skipped`为保留旧状态，不计新增。该释放收益只影响最终总RAM，不抵扣768字节上限。
- `uint32_t words[16]`等现场构造局部数组计入调用栈；不能用本表证明任务栈安全。正式HTM静态深度及现场历史栈水位须分别通过。
- Tests中的capture数组及fixture未编入正式Keil工程。

## 最终map与总RAM实证

`all`开启HMI/RF/CS/CA/RS485五路；`legacy`另开启raw/sync辅助开关。以下为四个实际RAM执行区的Size之和，含对齐、原静态对象、RAM执行代码与故障保留区。

| 项目 | all | legacy |
| --- | ---: | ---: |
| RW_CCM占用B | 62024 | 62040 |
| RW_IRAM1占用B | 107232 | 107232 |
| RW_IRAM2占用B | 8408 | 8408 |
| RETAINED_FAULT B | 512 | 512 |
| **总物理RAM B** | **178176** | **178192** |
| **相对82d6a0c基线174876B增量** | **3300** | **3316** |
| **4096B增量预算余量** | **796** | **780** |
| 普通SRAM剩余B | 14920 | 14920 |
| CCM剩余B | 3512 | 3496 |

两配置新增元数据均575字节，保守608字节；没有在legacy额外出现第二份业务状态缓存。all与legacy总物理RAM差16字节，落在CCM。空闲物理区与FreeRTOS堆剩余不能混为一谈，固定48KiB堆已包含在上述物理占用中。

两配置静态检查的最小任务余量均为servo_comm_task **264字节**（分配3072、深度2808），其余任务也通过256字节门限；仍须实物测量历史栈水位。

最终map SHA256：

- all：`132CE5BED206E6576A80943C31C2F6FB53B3566A5B65231E09BCD3E2F7E21212`
- legacy：`F8623B536614378068981090E6A481FAAB0DB948307ADE5B0FE55531C463BE69`

构建配置、源码digest、HEX/AXF hash和编译结果见`验证/Keil构建结果.json`；分区/DMA/栈检查见各配置`memory-stack.log`。本记录只读核对正式产物，不改写生产源码或Keil工程。
