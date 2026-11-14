# 8×8中文按需字库（2026-11-03）

184个Unicode汉字，字模1472字节、16位索引368字节，共1840字节，全部const存Flash。ASCII直接使用OLED_F6x8；旧16×16和6×16资源保持兼容。

实际使用指定PCtoLCD的副本，路径为D:/CodexBuild/Remote_control/pctolcd_small_20261103，原始资料未改。字符模式、宋体常规、字宽8、字高8、阴码、列行式、逆向低位在前、C51十六进制；每字8列，每列一字节，bit0为顶部。从左到右，无运行时缩放，未隔点压缩16×16。

- characters.txt：按Unicode排序的用字清单。
- glyphs_raw.json / pctolcd_raw.txt：软件实际原始输出。
- glyphs.json：最终运行数据；16个字做了7列笔画修整，末列留白。
- manual_edits.json：逐字记录修整后的八行点阵，构建生成器核对最终数据可由原始输出加这些编辑复现。
- pctolcd_main.png / pctolcd_options.png / PCtoLCD.INI：取模及配置证据。
- glyph_atlas_raw.png / glyph_atlas.png：修整前后真实点阵，图中文字标签仅用于校对。

8像素高度对复杂汉字的辨识力有物理限制；固定短标签及相邻数据提供上下文，实际0.96寸屏可读性仍需现场验收，不能由放大预览推断。

维护：修改APP/remote_display.template，新增汉字时用相同配置的PCtoLCD副本运行Tests/export_small_font.py --tool 工具绝对路径；导出只写raw资料，审查后更新glyphs.json及manual_edits.json，运行Tests/generate_oled_font.py。--check只核验。禁止从中断或重入调用页面格式化函数。
