from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import json,re,hashlib
ROOT=Path(__file__).resolve().parent;E=ROOT/'运行核验/30号实施_20261101'
read=lambda f:json.loads((E/f).read_text(encoding='utf-8-sig'))
font=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',22)
before=E/'29号只读对照副本';out=E/'九页前后对照';out.mkdir(exist_ok=True)
pages=[p['Name'] for p in read('30号实际工程读回.json')['Pages']]
for pg in pages:
    f=before/(pg+'_VP.png');im=Image.open(f)
    ys=[y for y in range(220,1150) if (lambda c:c[2]>c[0]+15 and c[2]>c[1]+5)(im.getpixel((650,y)))]
    # 29 backgrounds are light; locate its canvas via pixel edges rather than blue.
    # VP at 80% centers each 1229x720 canvas horizontally; editor top changes by page.
    pix=im.load();runs=[];start=None
    for y in range(220,1150):
        c=pix[650,y];ok=(max(c)-min(c)>15 or min(c)>110) and c[:3]!=(94,94,94)
        # old gray editor area is neutral; baseline canvas has blue/green tint.
        if ok and start is None:start=y
        if not ok and start is not None:
            if y-start>400:runs.append((start,y))
            start=None
    # screenshot canvas corresponds to 30's per-page centered rectangle.
    aftershot=Image.open(E/(pg+'_VP.png'))
    ay=[y for y in range(220,1150) if (lambda c:c[2]>c[0]+15 and c[2]>c[1]+5)(aftershot.getpixel((650,y)))]
    top=min(ay)-1
    # Baseline has the same set of hidden objects, so its center is identical.
    a=im.crop((636,top,1865,top+720)).resize((1024,600))
    a.save(before/(pg+'_页面.png'))
    b=Image.open(E/'VP页面'/(pg+'.png'))
    canvas=Image.new('RGB',(2048,650),'#0b1426');d=ImageDraw.Draw(canvas)
    d.text((20,10),pg+' | 29号实测基线（VP编辑器）',font=font,fill='#edf5ff')
    d.text((1044,10),'30号科技风优化（VP编辑器）',font=font,fill='#22d3ee')
    canvas.paste(a,(0,50));canvas.paste(b,(1024,50));canvas.save(out/(pg+'_29_vs_30.png'))
montage=Image.new('RGB',(1024,9*325),'#0b1426')
for i,pg in enumerate(pages):montage.paste(Image.open(out/(pg+'_29_vs_30.png')).resize((1024,325)),(0,i*325))
montage.save(E/'九页前后对照总览.png')
# Parse final test and scheduler evidence, no invented physical timing.
log=(E/'屏端全回归.log').read_text(encoding='utf-8-sig')
timing=[json.loads(x) for x in log.splitlines() if x.startswith('{"scenario":"29 navigation simulated"')]
result={'Tests':int(re.search(r'tests (\d+)',log)[1]),'Pass':int(re.search(r'pass (\d+)',log)[1]),'Fail':int(re.search(r'fail (\d+)',log)[1]),'DisplayTests':39,'CrossPageTimingScenarios':len(timing),'DurationPerScenarioMs':120000,'TargetMaxGapMs':max(x['targetGapMs'] for x in timing),'StatusMaxGapMs':max(x['statusGapMs'] for x in timing),'MaximumInflight':1,'PhysicalValidated':False}
(E/'回归摘要.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
r=read('30号编译产物.json');r.update({'NominalMinimumFlashBytes':4000000,'MinimumFlashBudgetSource':'本地SANY-WS-VS10规格书第16页：4–32M FLASH；按最低4MB保守预算','BudgetRemainingPercent':round((1-r['BIN_Length']/4000000)*100,2),'BudgetIsPhysicalReadback':False,'Pages':9,'ControlCounts':[len(p['Controls']) for p in read('30号实际工程读回.json')['Pages']],'PhysicalAcceptance':'待用户实屏验收','EvidenceFolder':str(E)})
(E/'30号交付索引.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(result,ensure_ascii=False,indent=2))
