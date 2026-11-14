# OLED中文界面改造交付记录

## 结果与版本

实施前已提交硬件I2C基线：`3af92be`，2026-11-02T18:14:54+08:00，备注“feat: 将OLED迁移至硬件I2C并增加故障恢复与回归验证”。近10条见OLED_PreChinese_Git_Log_20261102.md。中文改动保持未提交；未推送、未烧录。

实施前恢复快照：`D:/CodexBuild/Remote_control/oled_chinese_before_20261102_181328`，含HEAD、全部受控源码与新增文件ZIP、二进制差异、暂存差异和SHA-256清单。回退先保存后续工作，再以该快照或3af92be基线为准；不覆盖资料目录中的用户原始工具。

## 实现

- 23固定页面，四行首页保留系统、输入模式、公共目标、电压和电流。页面名称/字段/枚举中文化，K7/K8及全部控制输入不变。字段迁移与缩写见资料目录6.方案设计/遥控器中文界面显示参数说明.md。
- 使用指定PCtoLCD实际导出196个16×16宋体字模，数据6272、索引392字节；95个6×16 ASCII共1140字节，由原6×8纵向重复生成。常规ASCII优先8×16，超宽行改用6×16；超出128像素时显示溢出，不截断数值。第5页rpm统一标于第一行业务行，两轴行不重复单位。
- 新增OLED_Text混排API：UTF-8有界解码、Unicode二分索引、缺字方框、8像素对齐直接写显存。中文布局模板为UTF-8，生成C代码使用UTF-8八进制转义，避免ARMCC字符集差异。原GB2312接口保留，中文业务改用新接口。
- 固定容量文本片段与像素区域替代8×21文本矩阵；陈旧数值以虚线下划线标记。页码/链路/质量/告警小字固定在右上；等级3/4当前告警保持500ms反显。串口使用独立ASCII标题。
- 保持100ms快照、冻结显存、每循环一硬件页、硬件I2C6ms预算、500ms退避。绘制当次不发送页，恢复丢弃旧帧并从第0页重画。格式化暂存为主循环静态内存，不可在中断或重入调用。
- 状态模型、协议、按键扫描、NRF驱动、I2C底层、ADC及主控源码与基线一致；已逐项核对。无新RTOS、DMA、菜单或控制报文。

## 软件验证

`Tests/run_remote_tests.ps1 -RenderPreview`全量通过，GCC按-Wall -Wextra -Werror构建；原第三方绘图库的signed-compare/missing-braces豁免保留。覆盖真实I2C驱动故障注入、实际NRF ACK等待、真实扫描器、协议模型、中文显存、应用事件重发/优先级/冻结/恢复、串口开关和双工程真实编码→解码。

中文测试覆盖23页、所有字模与UTF-8非法/截断输入、数值极值、逐字段缺失/陈旧组合、单/双/无轴、会话、历史接纳、新旧0x88、元数据版本、N/C、不适用、告警匹配/未知码及年龄溢出。原字段完整性回归已迁移，未放宽实时有效性。

真实OLED+无线模拟遍历23页，对每个文本绘制额外注入1ms，正常最大发送间隔12ms，故障/恢复15ms，未出现超周期中止；K5/K9、累计步数及摇杆持续上传。注入耗时是压力模型，不能作为72MHz实测耗时。

由实际1024字节显存生成101场景PNG（23正常＋23无数据＋23失联＋23极值＋9专项），字库图集和页面预览已目视检查。恢复/翻页在真实OLED集成测试验证冻结与第0页重画。

## Keil与资源

原Keil Remote_control目标、ARMCC5.06 update7全量重建：0错误、0警告。

| 项目 | 最终结果 | 预算/配置 |
|---|---|---|
| Code | 39084 bytes | |
| RO-data | 12492 bytes | |
| RW-data | 164 bytes | |
| ZI-data | 8964 bytes | |
| ROM（Code+RO+RW） | 51740 bytes，50.53KiB | ≤62KiB；器件64KiB |
| RAM（RW+ZI） | 9128 bytes，8.91KiB | ≤10KiB；器件20KiB |
| 静态最大栈估计 | 640 bytes + Unknown | 保持1024字节栈；现场测高水位 |
| 堆 | 512 bytes，原配置 | 本改动无动态分配 |

构建目录：`D:/CodexBuild/Remote_control/snapshot_20261102_185150`。HEX SHA-256：`75EC634773C7EFBF78BCF306B9AC1BD8DB9FBFB5403ABB4E5C8EDDAAD2D688AC`。

最终交付目录：`D:/CodexBuild/Remote_control/oled_chinese_delivery_20261102`，含固件、map/调用图、完整日志、恢复快照链接、源代码ZIP、差异、新增文件、源文件/固件/交付哈希、中文参数说明和全部预览。资料目录的两份Markdown不属于Remote_control仓库；交付目录另存副本。

## 未执行与现场验收

本次未连接/烧录设备，未测真实绘制时间、I2C波形/单页时间、无线间隔、栈高水位、23页实物以及正常/故障恢复各30分钟观察。现场表见OLED_Chinese_Field_Checklist_20261102.md，未执行栏保持空白；软件通过与实物验收分开记录。

## 维护与复现

1. 字库依据Docs/Font/glyphs.json和取模原始输出；新增字运行PCtoLCD同配置补取，记录Unicode映射。Tests/export_pctolcd.py提供Windows GUI导出流程，操作副本。
2. 修改APP/remote_display.template后运行`python Tests/generate_oled_font.py`；日常核验用`--check`，不会写文件。
3. 执行`Tests/run_remote_tests.ps1 -RenderPreview`；使用`Tests/build_snapshot.ps1`输出隔离固件，正式工程配置不改写。
4. 新增页面/字段同步更新参数说明、用字、迁移表、极值与显存预览测试。
