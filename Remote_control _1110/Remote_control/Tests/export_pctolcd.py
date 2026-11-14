"""Export the UI character list using the user's PCtoLCD (GUI, Windows only).
Run on a COPY of the vendor tool configured like Docs/Font/pctolcd_options.png.
Font: Song/Simsun, normal, 16x16; negative/column-page/LSB-first/C51/hex.
Use --pid for the PCtoLCD process after selecting character mode and settings.
"""
from pathlib import Path
import argparse,ctypes,json,re
from pywinauto import Application
parser=argparse.ArgumentParser();parser.add_argument('--pid',type=int,required=True);args=parser.parse_args()
root=Path(__file__).resolve().parent.parent;out=root/'Docs/Font'
chars=''.join(sorted({c for c in (root/'APP/remote_display.template').read_text(encoding='utf8') if '\u4e00'<=c<='\u9fff'}))
app=Application(backend='win32').connect(process=args.pid);window=app.window(class_name='TForm1')
entry=window.descendants(class_name='TComboBox')[0].children()[0]
button=window.descendants(class_name='TButton')[2]
user32=ctypes.windll.user32
user32.SendMessageW.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_size_t,ctypes.c_void_p]
user32.SendMessageW.restype=ctypes.c_ssize_t
data=[];raw=[]
for char in chars:
 user32.SendMessageW(entry.handle,0x000C,0,ctypes.c_wchar_p(char))
 # Trigger the ANSI application's character-mode preview rebuild, then export.
 user32.SendMessageW(entry.handle,0x0102,32,None);user32.SendMessageW(entry.handle,0x0102,8,None)
 button.click();text=window.child_window(class_name='TMemo').window_text()
 values=[int(v,16) for v in re.findall(r'0x([0-9A-Fa-f]{2})',text)]
 if len(values)!=32:raise RuntimeError('Expected 32 bytes for '+char)
 data.append(dict(character=char,codepoint=ord(char),data=values));raw.append('U+%04X %s\n%s'%(ord(char),char,text))
(out/'characters.txt').write_text(chars,encoding='utf8')
(out/'glyphs.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf8')
(out/'pctolcd_raw.txt').write_text('\n'.join(raw),encoding='utf8')
print('Exported',len(data),'glyphs. Inspect glyph_atlas and options before regenerating C.')
