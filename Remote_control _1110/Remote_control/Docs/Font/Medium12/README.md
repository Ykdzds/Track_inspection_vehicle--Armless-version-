# 12×12专用字库取模记录

2026-11-03使用用户指定PCtoLCD.exe的独立副本（D:/CodexBuild/Remote_control/pctolcd_medium_20261103）实际取模，未更改原工具目录。宋体常规，宽12、高12，阴码、列行式、逆向低位在前、从左到右、C51十六进制；设置和主界面截图及INI保存在本目录。

182个去重字符，Unicode码点排序。glyphs_raw.json和pctolcd_raw.txt为原始导出，glyphs.json为使用资源，manual_edits.json为空数组：未进行手工改笔画，也未从8×8缩放。每字24字节，前12字节是上8行，后12字节是下4行，下半部高4位清零。原始导出本身满足掩码，生成器核对原始/最终一致。索引364字节、点阵4368字节，共4732字节Flash；旧8×8/16×16模式资源保留兼容。

key_glyphs.png为“转速电流负载授权故障恢复”的最终点阵样张，glyph_atlas.png为全部字形。Tests/generate_oled_font.py --check校验24字节、末页掩码、Unicode映射和页面覆盖。

复现：使用独立工具副本打开字符模式，确认上述尺寸/宋体/输出选项后运行Tests/export_medium_font.py --tool <副本exe>。导出后核对原始数据并更新glyphs.json，运行生成器和全套回归。
