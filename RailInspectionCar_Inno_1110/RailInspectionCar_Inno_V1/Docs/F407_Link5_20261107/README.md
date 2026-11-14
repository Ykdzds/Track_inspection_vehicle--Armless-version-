# F407五路独立诊断交付（link5-r1）

本轮只修改底盘F407。实施前已审阅上一轮源码、测试、文档及用户关闭HMI自动日志的配置，排除编译产物后完成本地基线提交：`dd916c6 fix: 按窗口编码HMI状态并优化接收调度与超时取证`。未推送；遥控、F103及34/37号屏保持当前版本。正式工程仍为 `MDK-ARM/RailInspectionCar_V1.uvprojx`，target=`RailInspectionCar_V1`。

## 开关与使用

所有开关位于 `APP/app_config.h`，交付默认全部为 `0U`。修改后使用上述正式Keil工程**全量重新编译**并烧录，不能使用先前宏配置留下的对象文件。构建工具也支持target define覆盖，配置以每个产物附带的project snapshot为准。

| 自动诊断 | 宏 | 输出source |
| --- | --- | --- |
| HMI屏 | `APP_HMI_LINK_DIAG_ENABLED` | HLD，旧字段格式保留 |
| 遥控/无线 | `APP_NRF_DIAGNOSTIC_ENABLED` | RF |
| CAN底盘伺服 | `APP_CAN_SERVO_DIAG_ENABLED` | CS |
| CAN机械臂 | `APP_CAN_ARM_DIAG_ENABLED` | CA |
| 全来源伺服RS485 | `APP_SERVO_RS485_DIAG_ENABLED` | S485 |

排查遥控电机突然停止：开启 NRF + RS485。排查CAN速度异常：开启 CAN_SERVO + RS485。排查机械臂：开启 CAN_ARM，需核对CAN物理帧时可另开 `APP_CAN_DEBUG_ENABLED`。排查双轴同步曲线可另开 `APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED`。这两个旧专项也通过公共writer输出；SYNC从原长行改为带逻辑组id、相位和part的分段投影，格式版本为link5-r1。

`APP_SERVO_CLI_DEBUG_ENABLED`仍是CLI运动控制授权开关，不能当日志开关。上述诊断开关不授予运动控制权，不启用机械臂配置、无线ACK回传或额外查询，不改变TTL、租约、STOP优先级、轮询和超时。USART2继续伺服业务通信。

USART1：PA9=TX、PA10=RX，115200 bit/s、8N1。串口助手在F407上电前开始保存原始文本，保留BOOT、异常前至少数十秒及停车完成后的内容；同时记录操作者动作、两轴选中情况、测试工况和视频。无需向USART1发送指令即可持续采集。仅开启任一路也会输出BOOT、RS485/Modbus初始化结果、内存、栈水位和首次停车。

## 记录的证据与解释

- RF：合法/拒绝帧、发布/消费、授权代数、目标、批次、K5、失联和恢复；ACK装载只能证明本机装载，不能证明遥控端收到。
- CS：generation/token/序号、受理与真正消费、具体拒因、目标期限、写入结果和停车；受理不是消费。
- CA：协商/联锁、operation/generation、排队、COMMIT真实TX_OK、单侧结果与匹配的聚合结果、STOP和释放。TX_OK不是机械臂执行完成。
- S485：提交、出队、TX启动、UART真实TC、DMA RX捕获、校验和消费；来源在提交时关联，协议owner不冒充HMI/遥控/CAN。无法关联明确为unknown。
- 驱动请求：kind20/21关联原始请求、健康限幅、最终提交、授权代数、写序号、CAN批次及transaction。驱动器ACK值与实际速度单独记录。
- 有效零速：在更新有效速度缓存前冻结前后样本、命令ACK、停车状态和完整7字节回复。无效/陈旧/未选轴的零值不代表驱动器报告零速；原始零回复也不能单独确定驱动器为何停止。
- 真实UART TC是中断完成证据；软件RX捕获不是物理末字节时刻。所有耗时包括任务抢占。

字段和原因码详见 [遥控](remote-fields.md)、[CAN](can-fields.md)、[RS485](rs485-fields.md)。HMI沿用上一轮window-r1字段；BOOT新增`impl=link5-r1 diag_rev=3 mask=HMI/RF/CS/CA/S485`。各开关mask按该固定顺序显示。通用事件`kind=3F0`的前两词是RS485、Modbus实际初始化返回，0均为成功。HMI关闭时，仲裁器公共停车事件使用保留kind `3E6/3E7/3E8`（首次/升级/完成），与RS485本地kind6取消和kind7投递失败分开；六词依次为event、reason、owner16/requester16、sequence、command16/deceleration16、result16/error16。HMI开启时保留旧HLD E6/E7/E8。

新增格式：

```text
LD <HAL_ms_hex> <boot_hex> <serial_hex> EVENT src=RF/CS/CA/S485 kind=<hex> w=<6 words>
LD ... FIRST src=... kind=... p=0/1 merged=<hex> w=<up to 8 words>
LD ... STATE/HEALTH src=... p=<phase> part=0/1 id=<logical group> sample=<HAL_ms> w=<up to 12 words>
```

新增数字采用十六进制、负数采用补码；旧HLD FIRST、MEM、STACK等仍按各字段原有进制，不能一概按十六进制解读。FIRST payload在全部页交给USART1前不可覆盖，重复异常仅增加累计merged；下次记录仍复用固定槽。RS485提交拒绝kind14及健康/安全变化kind22/23是6词单页，其余已知首因通常16词两页。首次停车独立保存，后续升级另记，真实升级请求者/命令/序号保留。

STATE/HEALTH每行重新采样，共同id只表示逻辑分组，不能拼成原子快照；SYNC速度/位置同理。对严格事务关联使用不可变FIRST和transaction ID。PC日志缺页时必须保留不完整标志。

## 输出与内存限制

共用原32项×36字节事件FIFO、256字节行缓冲和USART1 DMA。不增加任务、栈大小、串口缓存、Modbus队列容量或运行期分配。CLI互斥快照合为union（ARM最多448字节），删除旧CAN1536字节独立队列和SYNC768字节格式缓冲。

总自动输出不超过6000字节/秒，其中普通记录最多4000字节/秒；首次停车、屏端冻结及各路首次异常优先。各开启来源按配额共享FIFO，普通FIFO保持入队顺序，周期状态与首因按来源轮换。成功事务/高频成功事件主动采样，`LOGGER sampled`与`fifo_lost`分别表示采样和拥塞丢弃；不能把日志行数当总线事务数。`uart_drop`沿用旧字段名，累计writer拒绝尝试（包含后续重试），不能据此认定FIRST丢失。发送失败不会清除关键待发记录。长时间输出阻塞会累积merge/drop，日志容量有限。

单主路状态周期100ms、摘要1秒；多主路状态250ms、摘要2秒。受总预算和串口回压影响，周期摘要可延后；STATE与HEALTH使用独立游标，恢复后不会因状态插入而反复丢失后一part。机械臂与CAN伺服同时开启时只输出一次公共CAN健康摘要。

新增持久元数据预算详见 [metadata-budget.md](metadata-budget.md)。正式构建以执行区实际大小累计物理RAM，包含RAM执行代码，相对`82d6a0c`的174876字节基线增长必须不超过4096字节。CCM不能用于DMA，链接器余量不能当FreeRTOS堆空闲量。运行期任务历史空闲栈仍须至少256字节。

## 验证、产物与追溯

[最终软件验证与交付记录](delivery.md)汇总最终数值；[验证目录](验证/)保存主机回归及重验历史、八配置Keil日志、project snapshot、map/htm、HEX/AXF哈希和生产源码清单。`build.ps1`始终构建指定正式工程，临时改target define，在finally恢复工程/RTE原始字节；构建前后核对源码哈希，避免产物混入并发修改。默认最后构建alloff，因此正式工程输出目录最终为全部日志关闭版本。

完整主机回归可重跑：

```powershell
python -B Tests/run_link5_regression.py --powershell 'C:/Users/15056/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe' --compiler 'D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe'
```

Windows PowerShell 5执行中文路径的单个旧脚本时，显式传入`-Compiler`；新增聚合runner使用参数传递，避免无BOM脚本默认字符串的编码歧义。最后一次汇总包含29组生产路径/共享writer套件及1组分析器套件（15项），共30组。

- 主机共享writer：五主宏32组合及五主宏+旧专项并开；覆盖三位kind、极值行宽、回压、6000B/s、首因不覆盖、跨来源升级和摘要最终part。
- 各生产路径：RF、CAN/ARM、RS485及原HMI/LCD/联合控制回归；结果在`验证/regression/summary.json`，历史失败及修正后的重验保留，最终状态取各项当前exit。
- 日志分析器：[工具用法](log-analyzer.md)。仅使用Python标准库，支持HLD/LD、分页、混合进制、RS485有效零速证据及HMI真正TC全程耗时，不猜缺失关联。
- Keil：alloff、hmi、remote、canservo、canarm、rs485、all、legacy全量编译；每项要求0错误/0警告，并检查RAM分区、DMA、故障保留区和12任务静态栈。最终数值见`验证/Keil构建结果.json`与交付清单。

本轮只完成软件及编译验证，**未烧录、未完成新增版本的实物30分钟/2小时验收**。此前boot634日志用于分析器兼容性核验，不代替五路全开验收。

## 实物验收与回退

1. 先低速分别验证五路输出和宏关闭后静默，再按需要成对开启；覆盖HMI、遥控、CAN、CLI和独立安全通道来源。
2. 覆盖约751rpm、跨页、调速、重复授权、K5/K3、RF失联、CAN断连及伺服受控故障注入。保留完整原始日志、视频与固件哈希。
3. 五路全开30分钟初验，再2小时连续测试。HMI请求捕获至UART实际TC的完整回复全部小于50ms，目标不超过40ms；条件允许时用逻辑分析仪核对物理末字节。任一次达到50ms即未通过。
4. 所有任务历史空闲栈至少256字节，堆建立稳定基线、无无法解释的持续下降；核对boot/复位、FIRST、merge/drop和反馈年龄。编译静态调用图不替代水位实测。
5. 遇到停转，先看首次停车及各路FIRST，再关联原始目标/限幅/ACK/有效反馈/使能/故障；证据不足时继续测驱动器告警、供电和总线。

回退版本为`dd916c6`。保留当前修改后可在独立目录检出该基线，使用同一正式Keil工程全量编译；不对正在调试的工作区执行硬重置。五路宏全关仅关闭诊断；需回退整份程序时使用基线源码重编译。禁止混用不同宏配置的旧产物。

## 实施前最近10条本地提交

```text
dd916c6 fix: 按窗口编码HMI状态并优化接收调度与超时取证
3eaa1fa fix: 修复HMI关键状态源采样与LCD DMA误超时并补齐诊断
ce2a698 feat: 增加HMI37双侧链路取证与首次停车原因诊断
82d6a0c fix: 修复Flash保存门禁诊断与NRF故障恢复状态
8883212 feat: 完善ARM3 R2 CAN控制链路与三端输入安全互锁
01b1751 feat: 增加双轴参数Flash追加保存与增强配对遥测
4cccd62 fix: 修复双轴动态补偿步长并补充同步时序诊断
da12838 chore: 同步Keil调试配置中的诊断与运行时模块
1c9b1f0 fix: 隔离USART1调试通信并保留复位故障诊断
15db06e chore: 关闭RF周期串口诊断并更新ST-LINK调试配置
```
