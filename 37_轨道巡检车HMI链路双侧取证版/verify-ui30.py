"""Read back native VP properties and font bitmaps; render equivalent script states."""
from pathlib import Path
from PIL import Image, ImageDraw
import json,base64,math,hashlib,csv
ROOT=Path(__file__).resolve().parent;E=ROOT/'运行核验/30号实施_20261101'
read=lambda f:json.loads((E/f).read_text(encoding='utf-8-sig'))
model=read('30号实际工程读回.json');base=read('29号完整模型基线.json');fonts=read('实际字模.json')
font_by_size={f['Height']:f for f in fonts};font_by_id={f['Id']:f for f in fonts}
glyphs={f['Id']:{g['Char']:g for g in f['Glyphs']} for f in fonts}
issues=[];layout_checks=0
def color(n):return '#'+format(int(n)&0xffffff,'06x')
def rows(text,font,width,wrap=True):
    gs=glyphs[font['Id']];lines=[''];length=0;missing=set()
    for c in text.replace('\r',''):
        if c=='\n':lines.append('');length=0;continue
        g=gs.get(c)
        if not g:missing.add(c);continue
        w=g['Width']
        if wrap and length+w>width and lines[-1]:lines.append('');length=0
        lines[-1]+=c;length+=w
    return lines,missing
def draw_text(im,text,box,font,fg,wrap,center=False):
    lines,missing=rows(text,font,box[2],wrap);gs=glyphs[font['Id']]
    # PixelFont uses row-aligned 4-bit alpha, high nibble first.
    y=box[1]+(max(0,(box[3]-len(lines)*font['Height'])//2) if center else 0)
    for line in lines:
        x=box[0]+(max(0,(box[2]-sum(gs[c]['Width'] for c in line))//2) if center else 0)
        for c in line:
            g=gs[c];w=g['Width'];h=font['Height'];data=base64.b64decode(g['Data']);stride=(w+1)//2
            alpha=bytes(((data[j*stride+i//2]>>(4 if i%2==0 else 0))&15)*17 for j in range(h) for i in range(w))
            mask=Image.frombytes('L',(w,h),alpha);im.paste(fg,(x,y,x+w,y+h),mask);x+=w
        y+=font['Height']
    return len(lines)*font['Height'],missing
image_names={x['Id']:x['Name'] for x in model['Images']}
out=E/'等价状态预览';out.mkdir(exist_ok=True)
for state in read('等价显示状态.json'):
    page=model['Pages'][state['page']];pg=page['Name'];scenario=state['scenario']
    im=Image.open(ROOT/'ui30'/f'{pg}.jpg').convert('RGB')
    for c in page['Controls']:
        p=c['Properties'];t=c['Type'].split('.')[-1];v=state['pages'][pg].get(c['Name'],{})
        if not all(k in p for k in ['X','Y','Width','Height']) or not p.get('Visbile',True):continue
        x,y,w,h=(p[k] for k in ['X','Y','Width','Height']);box=(x,y,w,h)
        if w<=0 or h<=0:continue
        if t in ['TextCtrl','ButtonCtrl','ToggleButtonCtrl']:
            text=v.get('txt',p.get('Text') or '');f=font_by_id.get(p.get('FontId'))
            if not f:issues.append([scenario,pg,c['Name'],'font missing']);continue
            if t!='TextCtrl':
                key='DisableBackgroundImageId' if not v.get('enable',p['Enable']) else ('ImageId2' if v.get('press',0)==1 else 'ImageId1')
                skin=ROOT/'ui30'/(image_names[p[key]]+'.png');im.paste(Image.open(skin),(x,y))
            else:ImageDraw.Draw(im).rectangle((x,y,x+w-1,y+h-1),fill=color(p['BackgroundColor']))
            needed,missing=draw_text(im,text,box,f,color(v.get('fontColor',p['FontColor'])),p.get('WordWrap',False),t!='TextCtrl')
            layout_checks+=1
            if needed>h:issues.append([scenario,pg,c['Name'],f'height {needed}>{h}',text])
            if missing:issues.append([scenario,pg,c['Name'],'missing '+''.join(sorted(missing))])
            if x<0 or y<0 or x+w>1024 or y+h>600:issues.append([scenario,pg,c['Name'],'outside canvas'])
        elif t=='SliderCtrl':
            d=ImageDraw.Draw(im);d.rectangle((x,y,x+w-1,y+h-1),fill=color(p['BackgroundColor']));cx=x+(w-p['ThumbWidth'])//2;d.rectangle((cx,y,cx+p['ThumbWidth']-1,y+h-1),fill=color(p['ThumbColor']))
        elif t=='ProgressBarCtrl':
            d=ImageDraw.Draw(im);d.rectangle((x,y,x+w-1,y+h-1),fill=color(p['BackgroundColor']))
            value=v.get('val',p.get('Value',0))
            if value>0:d.rectangle((x,y,x+int(w*value/100),y+h-1),fill=color(p['ForegroundColor']))
    im.save(out/f'{pg}_{scenario}.png')
# Native VP screenshots are editor evidence; crops retain exact displayed pixels.
cropdir=E/'VP页面';cropdir.mkdir(exist_ok=True)
for f in E.glob('*_VP.png'):
    im=Image.open(f)
    ys=[y for y in range(220,1150) if (lambda c:c[2]>c[0]+15 and c[2]>c[1]+5)(im.getpixel((650,y)))]
    top=min(ys)-1
    im.crop((636,top,1865,top+720)).resize((1024,600)).save(cropdir/f.name.replace('_VP',''))
for scenario in ['normal','unknown','fault','long']:
    montage=Image.new('RGB',(1536,3*320),'#080f1d');d=ImageDraw.Draw(montage)
    for i,p in enumerate(model['Pages']):
        montage.paste(Image.open(out/(p['Name']+'_'+scenario+'.png')).resize((512,300)),((i%3)*512,(i//3)*320))
        d.text(((i%3)*512+8,(i//3)*320+302),p['Name']+' / '+scenario,fill='white')
    montage.save(E/f'九页_{scenario}.png')
# All original event codes except refreshTick must remain identical.
diffs=[];ev=[]
for p,q in zip(base['Pages'],model['Pages']):
    for c in p['Controls']:
        z=next(v for v in q['Controls'] if v['Id']==c['Id'])
        for k,v in c['Properties'].items():
            if v!=z['Properties'].get(k):diffs.append([p['Name'],c['Id'],c['Name'],k,str(v),str(z['Properties'].get(k))])
        for a,b in zip(c['Events'],z['Events']):
            if a!=b:ev.append({'Page':p['Name'],'Control':c['Name'],'Event':a['Name'],'BeforeSHA256':hashlib.sha256((a['Code'] or '').encode()).hexdigest(),'AfterSHA256':hashlib.sha256((b['Code'] or '').encode()).hexdigest()})
    for c in q['Controls'][len(p['Controls']):]:diffs.append([p['Name'],c['Id'],c['Name'],'APPENDED','','TextCtrl'])
with (E/'控件差异表.csv').open('w',encoding='utf-8-sig',newline='') as f:
    w=csv.writer(f);w.writerow(['Page','Id','Control','Property','29','30']);w.writerows(diffs)
(E/'事件差异.json').write_text(json.dumps(ev,ensure_ascii=False,indent=2),encoding='utf-8')
protected=read('保护基线SHA256.json');changed=[]
for f in protected:
    p=Path(f['File'])
    if not p.exists() or hashlib.sha256(p.read_bytes()).hexdigest().upper()!=f['SHA256']:changed.append(str(p))
result={'LayoutChecks':layout_checks,'Issues':issues,'ProtectedFiles':len(protected),'ProtectedChanged':changed,'OnlyChangedEvent':ev,'NoPhysicalValidation':True}
(E/'布局与保护核验.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'LayoutChecks':layout_checks,'Issues':issues,'ProtectedFiles':len(protected),'ProtectedChanged':changed},ensure_ascii=False,indent=2))
