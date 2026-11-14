# OLED按需字库

2026-11-03：当前八行10页界面使用[Small8字库](Small8/README.md)，184个8×8汉字及原6×8 ASCII。下述16×16/6×16资料保留用于旧接口兼容和23页基线追溯。

196个16×16宋体常规汉字，使用指定PCtoLCD.exe的独立副本生成，原资料目录未改动。配置见pctolcd_options.png：阴码、列行式、逆向低位在前、十六进制、每行16字节；每字先上8行16列，再下8行16列。从左到右，亮点为1，无抗锯齿。

characters.txt为UTF-8去重清单；glyphs.json记录Unicode码点及32字节数据；pctolcd_raw.txt保留软件原始输出（旧程序注释可能显示编码替代字符，以显式Unicode标题及glyphs.json映射为准）；PCtoLCD.INI及截图保存实际配置；glyph_atlas.png为实际字模图集，上方小字是校对标签。

中文数据6272字节，16位索引392字节；ASCII6×16由原6×8字模各行纵向重复两次，95字1140字节。资源全部const存Flash。

维护中文排版请编辑APP/remote_display.template（UTF-8），然后运行python Tests/generate_oled_font.py；生成的remote_display.c使用UTF-8八进制转义，避免ARMCC编码歧义。python Tests/generate_oled_font.py --check只核验，不修改文件。改用新汉字前先用PCtoLCD同配置补入glyphs.json、原始输出和清单，禁止用不同字体静默替换。

新增混排接口见BSP/OLED/OLED_Text.h，长度以字节计；原GB2312 OLED_ShowChinese接口保留兼容，中文业务不再调用它。静态格式化暂存仅供主循环，禁止从中断/重入调用remote_display_build。
