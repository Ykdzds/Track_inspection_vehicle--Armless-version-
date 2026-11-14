"""Export dedicated 12x12 SimSun glyphs from the user's PCtoLCD COPY.

Select character mode, width/height 12, SimSun regular, negative,
column-page, LSB first, C51 hexadecimal and confirm the options dialog.
This deliberately does not resample the 16x16 font.
"""
from pathlib import Path
import argparse, ctypes, json, re
from pywinauto import Application

parser = argparse.ArgumentParser()
parser.add_argument('--tool', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
out = root / 'Docs/Font/Medium12'
out.mkdir(parents=True, exist_ok=True)
chars = ''.join(sorted({c for c in (root/'APP/remote_display.template').read_text(encoding='utf8')
                        if '\u4e00' <= c <= '\u9fff'}))
app = Application(backend='win32').connect(path=args.tool)
w = app.window(class_name='TForm1')
combos = w.descendants(class_name='TComboBox')
assert combos[3].window_text() == '12' and combos[4].window_text() == '12'
assert combos[5].window_text() == '\u5b8b\u4f53'
entry = combos[0].children()[0]
button = next(c for c in w.descendants(class_name='TButton') if '\u751f\u6210' in c.window_text())
u = ctypes.windll.user32
u.SendMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_void_p]
u.SendMessageW.restype = ctypes.c_ssize_t
data, raw = [], []
for char in chars:
    u.SendMessageW(entry.handle, 0x000C, 0, ctypes.c_wchar_p(char))
    u.SendMessageW(entry.handle, 0x0102, 32, None)
    u.SendMessageW(entry.handle, 0x0102, 8, None)
    button.click()
    text = w.child_window(class_name='TMemo').window_text()
    values = [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{2})', text)]
    if len(values) != 24:
        raise RuntimeError('Expected 24 C51/hex bytes for U+%04X: %r' % (ord(char), text))
    data.append(dict(character=char, codepoint=ord(char), data=values))
    raw.append('U+%04X %s\n%s' % (ord(char), char, text))
(out/'characters.txt').write_text(chars, encoding='utf8')
(out/'glyphs_raw.json').write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf8')
(out/'pctolcd_raw.txt').write_text('\n'.join(raw), encoding='utf8')
w.capture_as_image().save(str(out/'pctolcd_main.png'))
print('Exported', len(data), '12x12 glyphs from PCtoLCD; inspect before using.')
