"""Export actual OLED framebuffer snapshots to labeled PNG galleries and pages."""
from pathlib import Path
import argparse
from PIL import Image,ImageDraw
parser=argparse.ArgumentParser();parser.add_argument('snapshot',type=Path);parser.add_argument('output',type=Path);args=parser.parse_args()
args.output.mkdir(parents=True,exist_ok=True)
lines=args.snapshot.read_text(encoding='utf8').splitlines();groups={}
assert len(lines)%9==0
for i in range(0,len(lines),9):
 assert lines[i].startswith('PAGE ')
 name=lines[i][5:];assert name.replace('_','').isalnum()
 data=bytes.fromhex(''.join(lines[i+1:i+9]));assert len(data)==1024
 image=Image.new('RGB',(128,64),'black')
 for y in range(64):
  for x in range(128):
   if data[(y//8)*128+x]&(1<<(y%8)):image.putpixel((x,y),(80,225,255))
 image.save(args.output/(name+'_native.png'))
 image=image.resize((512,256),Image.Resampling.NEAREST);image.save(args.output/(name+'.png'))
 parts=name.split('_');language='en' if parts[0]=='en' else 'zh';scenario=parts[1:] if language=='en' else parts
 group=language+'_'+('stale_limits' if scenario[:2]==['stale','limits'] else scenario[0] if scenario[0] in ['normal','limits','lost','empty'] else 'special')
 groups.setdefault(group,[]).append((name,image))
for group,items in groups.items():
 image=Image.new('RGB',(3*528,((len(items)+2)//3)*290),(20,20,20));draw=ImageDraw.Draw(image)
 for i,(name,page) in enumerate(items):
  x=(i%3)*528;y=(i//3)*290;draw.text((x+8,y+3),name,fill='white');image.paste(page,(x+8,y+24))
 image.save(args.output/(group+'_pages.png'))
print('Exported',len(lines)//9,'actual framebuffers')
