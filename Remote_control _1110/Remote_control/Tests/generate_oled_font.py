"""Rebuild C resources from PCtoLCD exports, never from a substitute font."""
from pathlib import Path
import json,re,argparse
parser=argparse.ArgumentParser();parser.add_argument("--check",action="store_true");args=parser.parse_args()
root=Path(__file__).resolve().parent.parent
def output(path,text):
 if args.check: assert path.read_text(encoding="ascii")==text, str(path)+" is stale"
 else: path.write_text(text,encoding="ascii")
font=root/'Docs/Font'
small=json.loads((font/'Small8/glyphs.json').read_text(encoding='utf8'))
small.sort(key=lambda g:g['codepoint'])
assert len({g['codepoint'] for g in small})==len(small)
assert all(len(g['data'])==8 for g in small)
raw_small=json.loads((font/'Small8/glyphs_raw.json').read_text(encoding='utf8'))
edits=json.loads((font/'Small8/manual_edits.json').read_text(encoding='utf8'))
expected={g['codepoint']:g['data'] for g in raw_small}
for edit in edits:
 rows=edit['rows']
 assert len(rows)==8 and all(len(row)==7 and set(row)<={'0','1'} for row in rows)
 expected[edit['codepoint']]=[sum((rows[y][x]=='1')<<y for y in range(8)) if x<7 else 0 for x in range(8)]
assert {g['codepoint']:g['data'] for g in small}==expected, 'Unrecorded small-font edit'
medium=json.loads((font/'Medium12/glyphs.json').read_text(encoding='utf8'))
medium.sort(key=lambda g:g['codepoint'])
assert len({g['codepoint'] for g in medium})==len(medium)
assert all(len(g['data'])==24 and all(v<16 for v in g['data'][12:]) for g in medium)
raw_medium=json.loads((font/'Medium12/glyphs_raw.json').read_text(encoding='utf8'))
assert {g['codepoint']:g['data'] for g in medium}=={g['codepoint']:g['data'] for g in raw_medium}, 'Unrecorded medium-font edit'

glyphs=json.loads((font/'glyphs.json').read_text(encoding='utf8'))
glyphs.sort(key=lambda g:g['codepoint'])
assert len({g['codepoint'] for g in glyphs})==len(glyphs)
assert all(len(g['data'])==32 for g in glyphs)
header='#ifndef OLED_CHINESE_FONT_H\n#define OLED_CHINESE_FONT_H\n#include <stdint.h>\n#include "OLED_FontConfig.h"\n#define OLED_CHINESE_COUNT %dU\nextern const uint16_t OLED_ChineseCodepoints[OLED_CHINESE_COUNT];\nextern const uint8_t OLED_ChineseData[OLED_CHINESE_COUNT][32];\nextern const uint8_t OLED_F6x16[95][12];\n#endif\n'%len(glyphs)
header=header.replace('#endif', '#define OLED_SMALL_COUNT %dU\nextern const uint16_t OLED_SmallCodepoints[OLED_SMALL_COUNT];\nextern const uint8_t OLED_SmallData[OLED_SMALL_COUNT][8];\n#endif'%len(small))
header=header.replace('#endif','#define OLED_MEDIUM_COUNT %dU\nextern const uint16_t OLED_MediumCodepoints[OLED_MEDIUM_COUNT];\nextern const uint8_t OLED_MediumData[OLED_MEDIUM_COUNT][24];\n#endif'%len(medium))
output(root/'BSP/OLED/OLED_ChineseFont.h',header)
s='#include "OLED_ChineseFont.h"\n#if OLED_ENABLE_LEGACY_FONTS\nconst uint16_t OLED_ChineseCodepoints[OLED_CHINESE_COUNT] = {\n'+','.join('0x%04XU'%g['codepoint'] for g in glyphs)+'};\nconst uint8_t OLED_ChineseData[OLED_CHINESE_COUNT][32] = {\n'
s+='\n'.join('{'+','.join('0x%02X'%b for b in g['data'])+'}, /* U+%04X */'%g['codepoint'] for g in glyphs)+'\n};\n'
source=(root/'BSP/OLED/OLED_Data.c').read_bytes().decode('gb18030')
block=re.search(r'const uint8_t OLED_F6x8\[\]\[6\]\s*=\s*\{(.*?)\};',source,re.S).group(1)
a=[int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})',block)]
assert len(a)==95*6
s+='const uint8_t OLED_F6x16[95][12] = {\n'
for i in range(95):
 vals=[sum(3<<(j*2) for j in range(8) if b&(1<<j)) for b in a[i*6:i*6+6]]
 s+='{'+','.join('0x%02X'%((v>>shift)&255) for shift in [0,8] for v in vals)+'},\n'
s+='};\n'
s+='const uint16_t OLED_SmallCodepoints[OLED_SMALL_COUNT] = {'+','.join('0x%04XU'%g['codepoint'] for g in small)+'};\n'
s+='const uint8_t OLED_SmallData[OLED_SMALL_COUNT][8] = {\n'
s+='\n'.join('{'+','.join('0x%02X'%b for b in g['data'])+'}, /* U+%04X */'%g['codepoint'] for g in small)+'\n};\n'
s+='#endif\n'
s+='const uint16_t OLED_MediumCodepoints[OLED_MEDIUM_COUNT] = {'+','.join('0x%04XU'%g['codepoint'] for g in medium)+'};\n'
s+='const uint8_t OLED_MediumData[OLED_MEDIUM_COUNT][24] = {\n'
s+='\n'.join('{'+','.join('0x%02X'%b for b in g['data'])+'}, /* U+%04X */'%g['codepoint'] for g in medium)+'\n};\n'
output(root/'BSP/OLED/OLED_ChineseFont.c',s)
# Explicit UTF-8 octal escapes are independent of ARMCC source/execution charset.
template=(root/'APP/remote_display.template').read_text(encoding='utf8')
needed={ord(c) for c in template if '\u4e00'<=c<='\u9fff'}
assert needed <= {g['codepoint'] for g in medium},needed-{g['codepoint'] for g in medium}
encoded=''.join(''.join('\\%03o'%b for b in c.encode('utf8')) if ord(c)>127 else c for c in template)
output(root/'APP/remote_display.c',encoded)
print('Generated',len(glyphs),'PCtoLCD glyphs; font/index bytes',len(glyphs)*34,'ASCII 6x16 bytes',95*12)
print('Small 8x8:',len(small),'glyphs;',len(small)*10,'font/index bytes')

print('Medium 12x12:',len(medium),'glyphs;',len(medium)*26,'font/index bytes')
