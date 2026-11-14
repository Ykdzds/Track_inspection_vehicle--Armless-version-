# 双语显示测试

PowerShell7执行 `./Tests/run_remote_tests.ps1 -RenderPreview`，需现有GCC、Python/Pillow和Node。构建及显存输出位于 `$env:TEMP/remote_control_tests`。

- `test_bilingual.c`：当前生产配置；历史九页文本/质量/区域/像素比较、语言状态、事件过滤、冻结请求及UTF-8替代框。
- `english_baseline_3af92be`：Git原始源码；`english_reference.c`仅对测试重命名公共符号，与当前显示模块分属不同翻译单元，绝不加入Keil工程。
- `test_display.c`分别按产品和完整旧字体配置运行，保留中文14页、182字逐字像素、4096事件码及历史模式测试。
- `test_key.c`使用真实扫描器；`test_app.c`覆盖ACK/事件队列、传屏/离线切换和恢复；`test_oled_radio.c`使用真实APP、OLED及NRF驱动并对硬件HAL注入耗时和故障。
- `oled_bilingual_snapshot.txt`是实际1024字节显存；`bilingual_previews`含原尺寸、4倍及分类图。`en_*`为英文，其余为中文及切换场景。

模拟时序只用于软件回归，现场15/20ms门槛仍需实测。构建证据及现场表见 `Docs/OLED_Bilingual_20261103.md`。
