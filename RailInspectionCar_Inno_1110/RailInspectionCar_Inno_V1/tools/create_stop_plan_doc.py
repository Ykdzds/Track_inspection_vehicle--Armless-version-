from pathlib import Path
from math import pi
from PIL import Image, ImageDraw, ImageFont
from docx import Document
from docx.shared import Mm, Pt, Inches, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK
from docx.enum.section import WD_SECTION
from docx.enum.table import WD_TABLE_ALIGNMENT, WD_CELL_VERTICAL_ALIGNMENT
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.enum.style import WD_STYLE_TYPE
from docx.enum.text import WD_LINE_SPACING

ROOT = Path(r'D:\wtzn_sx_\同济大学-轨道巡检车\同济大学轨道巡检车协作资料\5.工程代码\RailInspectionCar_Inno_V1')
OUTDIR = Path(r'D:\wtzn_sx_\同济大学-轨道巡检车\同济大学轨道巡检车协作资料\6.方案设计\轨道巡检车全停车方案')
OUTDIR.mkdir(parents=True, exist_ok=True)
DOCX = OUTDIR / '轨道巡检车全停车方案.docx'
FLOW = OUTDIR / '轨道巡检车全停车路径流程图.png'

FONT = 'Microsoft YaHei'
BLUE = '1F4E79'
FLOW_BLUE = '#1F4E79'
LIGHT_BLUE = 'D9EAF7'
LIGHT_GREEN = 'E2F0D9'
LIGHT_YELLOW = 'FFF2CC'
LIGHT_RED = 'FCE4D6'
GRAY = 'F2F2F2'
DARK = '1F1F1F'


def font_for(size=10.5, bold=False, color=DARK):
    f = ImageFont.truetype(r'C:\Windows\Fonts\msyh.ttc', size)
    return f

def set_cell_shading(cell, fill):
    tcPr = cell._tc.get_or_add_tcPr()
    shd = tcPr.find(qn('w:shd'))
    if shd is None:
        shd = OxmlElement('w:shd')
        tcPr.append(shd)
    shd.set(qn('w:fill'), fill)

def set_cell_text(cell, text, bold=False, color=DARK, size=9):
    cell.text = ''
    p = cell.paragraphs[0]
    p.paragraph_format.space_after = Pt(0)
    p.paragraph_format.line_spacing = 1.0
    r = p.add_run(str(text))
    r.bold = bold
    r.font.name = FONT
    r._element.rPr.rFonts.set(qn('w:eastAsia'), FONT)
    r.font.size = Pt(size)
    r.font.color.rgb = RGBColor.from_string(color)
    cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER

def set_cell_margins(cell, top=60, start=80, bottom=60, end=80):
    tc = cell._tc
    tcPr = tc.get_or_add_tcPr()
    tcMar = tcPr.first_child_found_in('w:tcMar')
    if tcMar is None:
        tcMar = OxmlElement('w:tcMar')
        tcPr.append(tcMar)
    for m, v in [('top', top), ('start', start), ('bottom', bottom), ('end', end)]:
        node = tcMar.find(qn('w:' + m))
        if node is None:
            node = OxmlElement('w:' + m)
            tcMar.append(node)
        node.set(qn('w:w'), str(v))
        node.set(qn('w:type'), 'dxa')

def set_repeat_table_header(row):
    trPr = row._tr.get_or_add_trPr()
    tblHeader = OxmlElement('w:tblHeader')
    tblHeader.set(qn('w:val'), 'true')
    trPr.append(tblHeader)

def set_table_borders(table, color='B7C9D6', size='6'):
    tbl = table._tbl
    tblPr = tbl.tblPr
    borders = tblPr.first_child_found_in('w:tblBorders')
    if borders is None:
        borders = OxmlElement('w:tblBorders')
        tblPr.append(borders)
    for edge in ('top', 'left', 'bottom', 'right', 'insideH', 'insideV'):
        tag = 'w:' + edge
        el = borders.find(qn(tag))
        if el is None:
            el = OxmlElement(tag)
            borders.append(el)
        el.set(qn('w:val'), 'single')
        el.set(qn('w:sz'), size)
        el.set(qn('w:space'), '0')
        el.set(qn('w:color'), color)

def set_col_widths(table, widths_mm):
    for row in table.rows:
        for cell, width in zip(row.cells, widths_mm):
            cell.width = Mm(width)

def add_table(doc, headers, rows, widths=None, header_fill=BLUE, font_size=8.5):
    table = doc.add_table(rows=1, cols=len(headers))
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = False
    set_table_borders(table)
    hdr = table.rows[0]
    set_repeat_table_header(hdr)
    for i, h in enumerate(headers):
        set_cell_text(hdr.cells[i], h, bold=True, color='FFFFFF', size=font_size)
        set_cell_shading(hdr.cells[i], header_fill)
        set_cell_margins(hdr.cells[i])
    for row in rows:
        cells = table.add_row().cells
        for i, val in enumerate(row):
            set_cell_text(cells[i], val, size=font_size)
            set_cell_margins(cells[i])
            if len(table.rows) % 2 == 0:
                set_cell_shading(cells[i], 'F8FBFD')
    if widths:
        available_width = 174.0
        total_width = float(sum(widths))
        if total_width > available_width:
            scale = available_width / total_width
            widths = [max(10.0, width * scale) for width in widths]
        set_col_widths(table, widths)
    doc.add_paragraph().paragraph_format.space_after = Pt(1)
    return table

def add_bullet(doc, text, level=0):
    p = doc.add_paragraph(style='List Bullet' if level == 0 else 'List Bullet 2')
    p.paragraph_format.space_after = Pt(2)
    p.paragraph_format.line_spacing = 1.15
    r = p.add_run(text)
    r.font.name = FONT
    r._element.rPr.rFonts.set(qn('w:eastAsia'), FONT)
    r.font.size = Pt(10.5)
    return p

def add_para(doc, text, bold_prefix=None):
    p = doc.add_paragraph()
    p.paragraph_format.space_after = Pt(5)
    p.paragraph_format.line_spacing = 1.25
    if bold_prefix and text.startswith(bold_prefix):
        r = p.add_run(bold_prefix)
        r.bold = True
        r.font.name = FONT
        r._element.rPr.rFonts.set(qn('w:eastAsia'), FONT)
        r.font.size = Pt(10.5)
        text = text[len(bold_prefix):]
    r = p.add_run(text)
    r.font.name = FONT
    r._element.rPr.rFonts.set(qn('w:eastAsia'), FONT)
    r.font.size = Pt(10.5)
    return p

def add_callout(doc, title, text, fill=LIGHT_YELLOW):
    table = doc.add_table(rows=1, cols=1)
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = False
    set_table_borders(table, color='D6B656', size='8')
    cell = table.cell(0,0)
    set_cell_shading(cell, fill)
    set_cell_margins(cell, top=120, start=140, bottom=120, end=140)
    p = cell.paragraphs[0]
    p.paragraph_format.space_after = Pt(2)
    r = p.add_run(title)
    r.bold = True; r.font.name = FONT; r._element.rPr.rFonts.set(qn('w:eastAsia'), FONT); r.font.size = Pt(10.5)
    p2 = cell.add_paragraph()
    p2.paragraph_format.space_after = Pt(0); p2.paragraph_format.line_spacing = 1.15
    r2 = p2.add_run(text)
    r2.font.name = FONT; r2._element.rPr.rFonts.set(qn('w:eastAsia'), FONT); r2.font.size = Pt(10)
    doc.add_paragraph().paragraph_format.space_after = Pt(1)

def add_heading(doc, text, level):
    p = doc.add_heading(text, level=level)
    p.paragraph_format.space_before = Pt(8 if level == 1 else 4)
    p.paragraph_format.space_after = Pt(4)
    for r in p.runs:
        r.font.name = FONT
        r._element.rPr.rFonts.set(qn('w:eastAsia'), FONT)
    return p

def create_flowchart(path):
    W, H = 1800, 2300
    im = Image.new('RGB', (W, H), 'white')
    d = ImageDraw.Draw(im)
    title_font = ImageFont.truetype(r'C:\Windows\Fonts\msyhbd.ttc', 44)
    head_font = ImageFont.truetype(r'C:\Windows\Fonts\msyhbd.ttc', 28)
    body_font = ImageFont.truetype(r'C:\Windows\Fonts\msyh.ttc', 24)
    small_font = ImageFont.truetype(r'C:\Windows\Fonts\msyh.ttc', 21)
    def center_text(box, text, font, fill='#1F1F1F', spacing=6):
        x1,y1,x2,y2=box
        lines=[]
        for para in str(text).split('\n'):
            words=list(para)
            cur=''
            for ch in words:
                test=cur+ch
                if d.textbbox((0,0), test, font=font)[2] > (x2-x1-30):
                    lines.append(cur); cur=ch
                else: cur=test
            if cur: lines.append(cur)
        total=sum(d.textbbox((0,0), line, font=font)[3]-d.textbbox((0,0), line, font=font)[1] for line in lines)+(len(lines)-1)*spacing
        y=y1+(y2-y1-total)/2
        for line in lines:
            bb=d.textbbox((0,0),line,font=font)
            d.text(((x1+x2-(bb[2]-bb[0]))/2,y),line,font=font,fill=fill)
            y += bb[3]-bb[1]+spacing
    def flow_color(value):
        return value if value.startswith('#') or value in ('white', 'black') else '#' + value
    def box(x,y,w,h,text,fill,outline='#6B7C93',font=body_font):
        fill = flow_color(fill)
        outline = flow_color(outline)
        d.rounded_rectangle((x,y,x+w,y+h), radius=24, fill=fill, outline=outline, width=4)
        center_text((x,y,x+w,y+h), text, font)
    def diamond(cx,cy,w,h,text,fill):
        pts=[(cx,cy-h//2),(cx+w//2,cy),(cx,cy+h//2),(cx-w//2,cy)]
        d.polygon(pts, fill=flow_color(fill), outline='#6B7C93')
        center_text((cx-w//2+25,cy-h//2+25,cx+w//2-25,cy+h//2-25), text, small_font)
    def arrow(x1,y1,x2,y2,label=None):
        d.line((x1,y1,x2,y2), fill='#34495E', width=5)
        import math
        ang=math.atan2(y2-y1,x2-x1)
        ah=18
        p1=(x2-ah*math.cos(ang-math.pi/6),y2-ah*math.sin(ang-math.pi/6))
        p2=(x2-ah*math.cos(ang+math.pi/6),y2-ah*math.sin(ang+math.pi/6))
        d.polygon([(x2,y2),p1,p2], fill='#34495E')
        if label:
            mx=(x1+x2)//2; my=(y1+y2)//2
            d.rounded_rectangle((mx-60,my-20,mx+60,my+20), radius=8, fill='#FFFFFF')
            bb=d.textbbox((0,0),label,font=small_font)
            d.text((mx-(bb[2]-bb[0])/2,my-(bb[3]-bb[1])/2),label,font=small_font,fill='#34495E')
    center_text((0,20,W,90),'轨道巡检车全停车路径流程图',title_font,fill=FLOW_BLUE)
    box(600,120,600,120,'启动 / 当前存在运动授权',LIGHT_BLUE,font=head_font)
    arrow(900,240,900,320)
    diamond(900,400,900,170,'停车请求或异常事件？',LIGHT_YELLOW)
    # branches
    box(70,530,460,160,'遥控端\n回中 / STOP / K5 / 失联',LIGHT_GREEN)
    box(670,530,460,160,'车载屏端\n归零 / STOP / RELEASE / 超时',LIGHT_GREEN)
    box(1270,530,460,160,'CAN端\n正常STOP / 快停 / 心跳租约超时',LIGHT_GREEN)
    arrow(510,400,300,530,'正常入口')
    arrow(900,485,900,530,'正常入口')
    arrow(1290,400,1500,530,'正常入口')
    box(220,800,1360,150,'统一控制仲裁器\n撤销新目标、锁定控制源、禁止旧目标恢复',LIGHT_BLUE,font=head_font)
    arrow(300,690,500,800); arrow(900,690,900,800); arrow(1500,690,1300,800)
    box(80,1050,500,170,'RS485单次超时 / CRC / 帧错\n短暂反馈过期：保持历史有效样本并重试',LIGHT_YELLOW)
    box(650,1050,500,170,'RS485连续异常达到运行阈值\n或反馈年龄超过500 ms / 运行中使能反馈异常',LIGHT_YELLOW)
    box(1220,1050,500,170,'明确危险故障\n驱动器故障码、输出失败、超速/过压/过流、停车超时',LIGHT_RED)
    arrow(800,950,420,1050,'RS485')
    arrow(900,950,900,1050,'状态判定')
    arrow(1000,950,1470,1050,'危险证据')
    box(160,1370,580,190,'受控停车\nRS485速度模式逐步写入递减速度；\n按当前速度和减速度计算期限；保持使能等待零速',LIGHT_GREEN,font=head_font)
    box(1060,1370,580,190,'立即安全停车\n停止运动规划和PWM，关闭软件输出门禁，\n请求双轴失能并锁存故障',LIGHT_RED,font=head_font)
    arrow(420,1220,450,1370,'先减速')
    arrow(900,1220,560,1370,'短暂异常')
    arrow(1400,1220,1350,1370,'危险故障')
    box(160,1690,580,150,'零速反馈连续确认\n通常要求至少2个新样本；通信/故障状态正常',LIGHT_BLUE)
    box(1060,1690,580,150,'驱动器故障或受控停车失败\n升级立即安全停车',LIGHT_RED)
    arrow(450,1560,450,1690)
    arrow(1350,1560,1350,1690)
    box(420,1960,960,150,'最终状态：清除全部授权，锁存故障，红灯常亮，三端禁止再次授权\n恢复需要人工确认、静止、失能和通信正常；不得自动续跑',LIGHT_BLUE,font=head_font)
    arrow(450,1840,750,1960,'完成')
    arrow(1350,1840,1050,1960,'锁存')
    box(80,2160,1640,90,'CPU关键任务心跳超时 / IWDG复位 / HardFault：软件不能保证连续减速，依赖驱动器通信超时停车、STO、安全继电器或机械制动',LIGHT_RED,font=small_font)
    im.save(path)

create_flowchart(FLOW)

doc = Document()
sec = doc.sections[0]
sec.top_margin = Mm(18); sec.bottom_margin = Mm(18); sec.left_margin = Mm(18); sec.right_margin = Mm(18)
styles = doc.styles
for name in ['Normal','Title','Heading 1','Heading 2','Heading 3','List Bullet','List Bullet 2']:
    try:
        st=styles[name]
        st.font.name=FONT
        st._element.rPr.rFonts.set(qn('w:eastAsia'), FONT)
    except Exception: pass
styles['Normal'].font.size=Pt(10.5)
styles['Normal'].paragraph_format.line_spacing=1.25
styles['Title'].font.size=Pt(24); styles['Title'].font.bold=True; styles['Title'].font.color.rgb=RGBColor.from_string(BLUE)
styles['Heading 1'].font.size=Pt(16); styles['Heading 1'].font.bold=True; styles['Heading 1'].font.color.rgb=RGBColor.from_string(BLUE)
styles['Heading 2'].font.size=Pt(13); styles['Heading 2'].font.bold=True; styles['Heading 2'].font.color.rgb=RGBColor.from_string(BLUE)

p=doc.add_paragraph(style='Title'); p.alignment=WD_ALIGN_PARAGRAPH.CENTER; p.add_run('轨道巡检车全停车方案')
p=doc.add_paragraph(); p.alignment=WD_ALIGN_PARAGRAPH.CENTER
r=p.add_run('底盘 F407 主控停车路径、判定条件与安全边界分析'); r.bold=True; r.font.name=FONT; r._element.rPr.rFonts.set(qn('w:eastAsia'), FONT); r.font.size=Pt(13); r.font.color.rgb=RGBColor.from_string('555555')
p=doc.add_paragraph(); p.alignment=WD_ALIGN_PARAGRAPH.CENTER
r=p.add_run('工程版本：RailInspectionCar_Inno_V1    文档日期：2026年10月24日'); r.font.name=FONT; r._element.rPr.rFonts.set(qn('w:eastAsia'), FONT); r.font.size=Pt(10)
doc.add_paragraph()
add_callout(doc, '核心结论', '当前工程已经将停车分为正常停车、受控安全停车和立即安全停车三类。遥控器、车载屏和 CAN 的正常停车通常按各自减速度逐步降速并确认零速；RS485 单次错误并不等同于立即急停，运行期关键反馈连续失败达到 5 次、或关键反馈年龄超过 500 ms 后进入受控安全停车；驱动器明确故障、输出失败或受控停车超时则升级为立即安全停车。看门狗复位和 CPU 异常不能依赖 F407 软件继续发送减速指令，必须依赖驱动器通信超时停车、STO、安全继电器或机械制动。', LIGHT_BLUE)

add_heading(doc,'一 文档范围和使用说明',1)
add_para(doc,'本文依据当前 F407 底盘主控工程源码，对遥控端、车载屏端、CAN 总线端、RS485 伺服通信、关键任务心跳、看门狗、CPU 异常和驱动器故障等全部停车路径进行归纳。本文给出的停车时间和停车距离分为“源码配置对应的理论值”和“必须通过实物测试确认的实际值”，二者不能混用。')
add_para(doc,'本文不修改 F103 遥控器、车载屏工程和 CAN 协议；文档中的“当前工程行为”用于现场排查、测试验收和后续修改评审。')
add_callout(doc,'重要安全边界','软件受控停车只能覆盖 F407 仍在运行且能够继续向驱动器发送命令的场景。HardFault、任务完全卡死、IWDG 已经复位、RS485 物理链路彻底断开等场景不能由 F407 软件本身保证平滑减速，必须验证驱动器通信超时停机、独立 STO、安全继电器和机械制动。',LIGHT_RED)

add_heading(doc,'二 当前机械参数和时间基准',1)
params=[
('项目','当前配置','说明'),('电机最高运行转速','1600 rpm','所有控制源统一运行上限'),('车轮直径','0.185 m','185 mm'),('总传动比','2.6667','电机转速 / 钢轮转速，3×16/18'),('1600 rpm对应车速','约5.812 m/s（约20.92 km/h）','理论换算，未含打滑和机械误差'),('设计加速度','0.15 m/s²','运动规划默认值；CAN加速配置为0.12 m/s²'),('设计减速度','0.30 m/s²','底层全局受控安全停车使用'),('遥控器失联停车减速度','0.40 m/s²','APP_REMOTE_SERVO_LINK_STOP_DECEL_MPS2'),('遥控器K5人工安全停车','0.50 m/s²','APP_REMOTE_SERVO_K5_STOP_DECEL_MPS2'),('CAN正常停车','0.40 m/s²','APP_CAN_NORMAL_STOP_DECEL_MPS2'),('CAN快速停车','0.50 m/s²','APP_CAN_QUICK_STOP_DECEL_MPS2'),('外部脉冲模式减速度','100 rpm/s，约0.363 m/s²','只适用于外部脉冲模式，不影响RS485速度模式'),('RS485响应超时','50 ms；最多重试2次','单次 Modbus 事务判定参数'),('RS485关键轮询周期','20 ms','速度和故障关键状态轮询'),('伺服状态最大年龄','500 ms','速度/故障/使能关键反馈有效期'),('运行中连续通信失败阈值','5次','速度、故障、使能反馈分别计数'),('非运行离线连续失败阈值','3次','可标记相应轴 OFFLINE'),('遥控链路超时','150 ms','触发遥控失联受控停车'),('CAN心跳周期/链路超时','100 ms / 1000 ms','链路超时进入 CAN 失联停车'),('CAN速度刷新/租约','100 ms / 300 ms','速度目标超时触发停车'),('HMI会话租约/速度保持','1000 ms / 500 ms','会话或目标超时触发停车'),('关键任务心跳超时','500 ms','健康任务判定任务失活'),('IWDG刷新任务周期','250 ms；IWDG重装值2000','任务失活时停止刷新，等待硬件复位'),('看门狗恢复减速时间','10000 ms，另加2000 ms确认余量','仅适用于已复位后的恢复流程')]
add_table(doc,params[0],params[1:], widths=[42,54,78], font_size=8)

add_heading(doc,'三 1600 rpm理论停车时间和距离',1)
add_para(doc,'按当前机械参数，1600 rpm约等于5.812 m/s。理论匀减速关系为：停车时间 t=v/a，停车距离 s=v²/(2a)。下表仅用于选择量级和制定测试初值，未包含驱动器内部斜坡、通信周期、任务调度、反馈确认、轮轨打滑、机械间隙和制动器响应。')
theory=[('减速度','理论停车时间','理论停车距离','典型用途'),('0.30 m/s²','19.37 s','56.30 m','设计减速度/RS485受控安全停车'),('约0.363 m/s²','16.00 s','46.50 m','外部脉冲模式100 rpm/s'),('0.40 m/s²','14.53 s','42.22 m','遥控器失联、CAN正常停车'),('0.50 m/s²','11.62 s','33.78 m','遥控器K5、CAN快速停车')]
add_table(doc,theory[0],theory[1:],widths=[36,38,38,62],font_size=8.5)
add_callout(doc,'实物验收要求','现场必须记录实际减速度、停车时间、停车距离、母线电压峰值、驱动器报警、两轴同步误差、USART2错误计数、连续失败次数和 F407 最终故障原因。未完成实测前，不应把上述理论值写入操作规程作为保证值。',LIGHT_YELLOW)

add_heading(doc,'四 遥控端全部停车路径',1)
remote=[
('遥控路径','触发条件','当前停车方式','减速度/时间','伺服状态与结果'),
('正常回中或目标转速降为零','遥控目标值归零，链路正常','通过运动规划和速度写入逐步降速；零速反馈确认后释放遥控授权','设计减速度0.30 m/s²；时间按实际速度/减速度计算，另加反馈确认','通常保持使能至零速；完成后释放授权，不锁存故障'),
('遥控普通 STOP','人工停车命令，不属于K5立即安全动作','进入遥控受控停车状态，目标转速置零，等待双轴零速确认','0.40 m/s²；停车期限按当前最大实际/命令转速计算并加确认余量，最低2 s','保持使能等待零速；成功后释放遥控源'),
('遥控器 K5 人工安全停车','K5事件或全局优先停车事件','受控减速；若停车中再次触发K5，升级减速度但不重置停车期限；完成后锁存 K5 停止','0.50 m/s²；按当前速度计算并加确认余量','保持使能到零速，再执行失能/锁存；红灯点亮，需人工确认恢复'),
('遥控无线失联','遥控快照年龄达到150 ms','撤销授权和新目标，进入失联受控停车；禁止旧目标恢复','0.40 m/s²；按当前速度计算并加确认余量','正常完成后锁存失联停车结果；通信恢复不能自动续跑'),
('遥控停车期间反馈异常','关键反馈不新鲜、写入失败、零速确认失败或停车超时','遥控模块报告失败并由统一安全仲裁器接管；必要时从受控停车升级立即安全停车','先按当前受控减速度；故障/超时后立即停车','清除全部授权；红灯点亮；三端禁止授权')]
add_table(doc,remote[0],remote[1:],widths=[35,44,59,50,62],font_size=7.5)

add_heading(doc,'五 车载屏端全部停车路径',1)
hmi=[
('HMI路径','触发条件','当前停车方式','减速度/时间','伺服状态与结果'),
('正常调速归零','HMI速度目标写为0','HMI控制状态进入 stopping，逐步写入减小目标，零速连续确认后完成','通常0.40 m/s²（请求带500 mm/s²停车参数，最终由受控状态机执行）；具体时间按当前速度计算','保持使能至零速；HMI会话释放'),
('HMI显式 STOP','收到 CMD_STOP','提交全局优先停车事件，目标归零，双轴零速确认','500 mm/s²接口参数；实际受控停车由统一仲裁和底层模式执行','停止成功返回成功；不能恢复旧目标'),
('HMI RELEASE','释放会话或授权','等价于受控停车请求，不直接清零PWM','通常0.40 m/s²；HMI会话租约1000 ms','零速确认后释放HMI控制权'),
('HMI目标/会话超时','速度保持超过500 ms，或会话租约超过1000 ms','自动提交受控停车；撤销HMI授权','通常0.40 m/s²；停车期限按速度计算','成功后会话清除；超时/反馈失败进入故障收尾'),
('HMI执行中反馈不安全','反馈过期、轴未使能、写事务失败、反向等待超时、停车确认超时','HMI报告停车失败；先请求受控安全停车，若未建立或无法继续则立即急停','先受控减速；无法保证继续输出时立即安全停车','锁存故障；HMI应显示主控故障/反馈过期/通信失败，不得误归因于HMI按钮')]
add_table(doc,hmi[0],hmi[1:],widths=[35,46,58,48,64],font_size=7.5)

add_heading(doc,'六 CAN总线端全部停车路径',1)
can=[
('CAN路径','触发条件','当前停车方式','减速度/时间','伺服状态与结果'),
('CAN正常 STOP','有效CAN会话内收到普通STOP','CAN控制服务进入减速阶段，取消旧速度目标，等待零速确认','0.40 m/s²；最大停车确认期限不超过30 s','保持使能至零速；CAN结果字段返回完成/失败'),
('CAN快速 STOP','STOP命令参数要求快速停车且功能已启用','使用CAN快速停车减速度，不改变协议和授权优先级','0.50 m/s²；按当前速度计算并加2 s确认余量','零速后保持安全锁定，不能自动恢复旧目标'),
('CAN链路心跳超时','CAN心跳超过1000 ms未更新','撤销CAN授权，进入链路失联受控停车','APP_CAN_LINK_STOP_DECEL，当前继承遥控失联0.40 m/s²','停车完成后CAN控制失效，需重新授权'),
('CAN速度租约超时','速度租约通常为100~300 ms，超过租约未刷新','进入速度目标超时停车；首次授权还受1 s首个速度零值窗口约束','通常0.40 m/s²；按当前速度计算','禁止旧速度恢复；CAN状态转为停车或故障'),
('CAN控制源被其他源仲裁','遥控/HMI优先事件或安全锁接管','CAN事务取消，控制权撤销；由全局停车请求的减速度执行','按全局停车事件，通常0.40/0.50 m/s²','CAN返回被抢占/失败，不影响安全停车')]
add_table(doc,can[0],can[1:],widths=[35,48,57,48,64],font_size=7.5)

add_heading(doc,'七 RS485伺服通信失联和异常路径',1)
add_para(doc,'RS485路径必须区分“单次事务错误”“运行期连续异常”“有效反馈真正过期”和“驱动器明确危险故障”。当前工程对运行期关键反馈单独维护速度、故障和使能反馈计数，每一类连续错误达到5次才认为运行反馈不可接受；非运行场景的通用离线阈值为3次。单次错误会增加诊断计数并尽快重试，不应直接等同于立即急停。')
rs=[
('异常类型','判定条件','当前停车方式','停车后状态'),
('单次响应超时','单个Modbus事务超过50 ms无有效响应；最多重试2次','保留仍在500 ms内的有效速度/故障样本，20 ms后重新轮询；不立即停车','继续运行或等待下一次有效反馈；记录最近结果、地址和失败次数'),
('单次CRC/帧错误/DMA接收异常','响应状态非MODBUS_OK或帧上下文不匹配','同上；关键对象快速重试；后台诊断项失败不直接放大为安全故障','不立即清除全部运动授权；记录错误类型和地址'),
('运行期连续关键通信失败','速度、故障或使能反馈任一类别连续达到5次','触发受控安全停车；不再接受新的运动目标；保持伺服使能并递减速度','双轴进入MOTION_STOPPING；零速确认后执行最终故障收尾，清除授权并锁存'),
('反馈年龄超过500 ms','关键速度/故障反馈或运行中使能反馈不再新鲜；同时结合连续失败和有效性判定','进入受控安全停车；受控停车期间若无法继续获得有效反馈则升级立即安全停车','初始为受控停车；超时/输出失败/危险故障时立即安全停车'),
('非运行时连续离线','相应轴连续失败达到3次，且未处于允许运行的遥控特殊探测条件','禁止进入READY；通常不带载运行，进入OFFLINE/安全状态','红灯点亮，三端不能授权，等待通信和人工恢复'),
('运行中使能反馈读回为0','使能回读有效且明确为0','运行中不把单次读回异常立即当作故障；关键反馈异常达到运行判定后受控停车；如果确认伺服已失能则立即安全停车','受控停车或立即安全停车取决于是否仍能安全控制；故障锁存且不允许自动续跑')]
add_table(doc,rs[0],rs[1:],widths=[38,63,67,64],font_size=7.7)
add_callout(doc,'当前源码中的关键区别','“单次RS485错误”与“连续5次运行期关键反馈失败”不是同一条件。前者一般只是重试和诊断记录；后者才会触发运行中的受控安全停车。另一方面，驱动器有效故障码、输出失败、受控停车超时等属于危险证据，可绕过容错直接进入立即安全停车。',LIGHT_GREEN)

add_heading(doc,'八 受控安全停车的内部步骤',1)
steps=[('步骤','动作','说明'),('1','统一停车仲裁','health_task、aimtor_service、遥控、HMI、CAN不应各自重新定义急停；统一记录停车类别、原因、轴和控制源。'),('2','撤销运动授权','清除遥控、HMI、CAN、CLI授权和旧目标代次；禁止在停车期间恢复旧目标。'),('3','保持伺服使能','只要RS485通信和驱动器状态仍允许，保持使能，按控制模式逐步写入递减速度。'),('4','双轴轮询减速','RS485速度模式逐步写入递减RPM；通信服务按轴轮转，避免单轴长期占用总线。'),('5','零速确认','要求新鲜速度反馈，通常需要每轴至少2个新样本显示实际速度为0且事务不忙。'),('6','最终故障收尾','受控停车完成后调用既有故障收尾：停止规划、最终清除授权、申请失能、锁存故障。'),('7','升级条件','读到非零故障码、输出写入连续失败、模式不支持、受控停车超时或停车期间再次出现危险状态，立即安全停车。')]
add_table(doc,steps[0],steps[1:],widths=[20,42,170],font_size=8.5)

add_heading(doc,'九 伺服驱动器故障码处理',1)
faults=[('读取结果','当前处理','是否允许受控减速','最终状态'),('故障码为0且反馈新鲜','正常运行或执行当前停车流程','是','按入口完成正常停车或受控停车'),('故障码非0且有效确认','立即安全停车；不等待通信恢复、不把故障码当作普通反馈过期','否','立即关闭软件输出/申请失能，锁存MOTION_FAULT，红灯常亮'),('故障码读取失败但历史样本仍在500 ms内','保留旧样本并重试，计入反馈失败次数','是，达到运行阈值后受控停车','根据后续反馈决定继续或停车'),('故障码读取持续失败或反馈过期','受控安全停车；停车期间无法获得新鲜反馈或输出失败则升级','先是，失败后否','MOTION_FAULT或MOTION_OFFLINE，禁止自动恢复'),('停车写零失败达到阈值','判定输出故障','否','立即安全停车并锁存')]
add_table(doc,faults[0],faults[1:],widths=[45,80,58,52],font_size=8)

add_heading(doc,'十 关键任务超时和看门狗复位',1)
wdg=[('异常','判定条件','F407动作','停车方式和安全边界'),('运动任务心跳超时','健康任务检查关键心跳年龄超过500 ms','健康任务记录APP_DIAG_HEARTBEAT_TIMEOUT，并尽力调用aimtor_emergency_stop；停止刷新IWDG','在F407仍能执行时为立即安全停车；如果任务已卡死，软件不能保证继续发送减速命令'),('伺服通信任务心跳超时','SERVO_COMM心跳超过500 ms','同上，优先取消事务并尽力停机','依赖驱动器通信超时停机/STO/机械制动兜底'),('健康任务心跳超时','HEALTH心跳超过500 ms','看门狗任务停止刷新IWDG；先记录故障，等待硬件复位','IWDG复位不是受控减速过程；必须验证独立硬件安全链'),('NRF任务心跳超时','NRF关键任务超过500 ms','停止刷新IWDG；无线链路不能再提供可靠授权','IWDG复位或既有硬件超时停车；不可把软件恢复当作安全制动'),('K6看门狗测试','测试请求锁存后主动停止刷新IWDG','等待IWDG复位；复位原因保留在RCC_CSR','仅用于验证保护链，不能在载车高速状态随意测试'),('IWDG复位后','启动读取IWDG复位原因','进入看门狗恢复专用流程；先停止本地脉冲和旧授权，等待伺服在线并验证','恢复流程使用10000 ms减速时间和2000 ms确认余量；若驱动器已因通信超时停机，软件完成失能和锁存；若恢复失败，红灯保持')]
add_table(doc,wdg[0],wdg[1:],widths=[38,54,72,70],font_size=7.8)

add_heading(doc,'十一 HardFault、断言、栈溢出和上电停车',1)
exc=[('异常','当前动作','能否保证平滑减速'),('HardFault、BusFault、UsageFault等CPU异常','中断处理先调用servo_pulse_stop_all；故障现场不能依赖任务调度；随后由系统复位或停留在故障状态','不能。依赖驱动器通信超时停车、STO、安全继电器或机械制动'),('FreeRTOS断言','停止本地脉冲，记录断言诊断，点亮红灯','不能保证；属于不可恢复故障'),('任务栈溢出','停止本地脉冲，记录任务哈希和诊断，点亮红灯，交由IWDG复位','不能保证；需要硬件安全链'),('普通上电安全停车','启动阶段保持输出禁用，核验速度零、失能、参数和通信后才允许READY','上电阶段不允许有效运动输出；若核验失败保持安全/故障'),('看门狗恢复停车失败','恢复步骤或驱动器失能确认失败','立即锁存故障，禁止三端重新授权，红灯常亮')]
add_table(doc,exc[0],exc[1:],widths=[48,88,96],font_size=8.2)

add_heading(doc,'十二 红色指示灯和系统状态',1)
led=[('条件','软件状态/诊断','红灯','三端授权'),('MOTION_FAULT','驱动器故障、输出失败、停车超时、受控停车最终故障等','常亮','禁止'),('MOTION_OFFLINE','伺服或通信连续失败导致轴离线','常亮','禁止'),('看门狗恢复失败','恢复阶段失败、失能或核验失败','常亮','禁止'),('启动保持失败','上电安全保持/零速/失能核验失败','常亮','禁止'),('遥控器K5锁存','人工K5停车完成并锁存','常亮','禁止，需按既有人工恢复条件'),('断言/栈溢出/CPU异常诊断','诊断位已置位或硬件异常路径','常亮或保持故障态','禁止'),('普通受控停车中','MOTION_STOPPING且尚未完成','通常不因停车过程单独点亮红灯；蓝灯/绿灯由状态逻辑决定','禁止新增授权'),('正常停车完成','零速确认后释放控制源，无故障','红灯熄灭','可按既有授权流程重新授权')]
add_table(doc,led[0],led[1:],widths=[48,82,38,64],font_size=8.3)

add_heading(doc,'十三 全部停车路径汇总表',1)
summary=[('编号','触发源','典型触发条件','停车分类','主要减速度','是否保持使能','最终结果/红灯'),('P01','遥控正常回中','目标转速归零','正常受控','0.30 m/s²','是，至零速','释放遥控授权；无故障不亮红灯'),('P02','遥控普通STOP','人工普通停车','正常受控','0.40 m/s²','是，至零速','释放遥控授权；无故障不亮红灯'),('P03','遥控K5','人工安全停车','受控安全停车并锁存','0.50 m/s²','是，至零速；随后失能','红灯常亮，三端禁授权'),('P04','遥控失联','150 ms无遥控快照','受控安全停车并锁存','0.40 m/s²','是，至零速','通信恢复不自动续跑；红灯按锁存状态'),('P05','HMI归零/STOP/RELEASE','HMI停车命令或会话释放','正常受控','约0.40~0.50 m/s²','是，至零速','HMI返回完成；不恢复旧目标'),('P06','HMI超时','目标500 ms或会话1000 ms超时','受控安全停车','约0.40 m/s²','是，至零速','故障/超时则红灯常亮'),('P07','CAN正常STOP','普通CAN停车','正常受控','0.40 m/s²','是，至零速','CAN完成/失败结果'),('P08','CAN快速STOP','快速停车参数=1','正常受控','0.50 m/s²','是，至零速','安全锁定，不恢复旧目标'),('P09','CAN链路/租约超时','心跳1000 ms或速度租约300 ms','受控安全停车','0.40 m/s²','是，至零速','禁止自动续跑'),('P10','RS485单次异常','50 ms事务超时、CRC、帧错','不停车，重试诊断','无','保持','继续运行或等待下一次有效反馈'),('P11','RS485运行连续异常','关键反馈连续5次失败','受控安全停车','0.30 m/s²','是，至零速','完成后故障锁存，红灯常亮'),('P12','RS485真正离线','反馈超过500 ms且无法恢复/非运行3次失败','受控后可能升级立即','先0.30，失败后立即','先保持，升级时失能','MOTION_OFFLINE/FAULT，红灯常亮'),('P13','驱动器非零故障码','有效故障码确认','立即安全停车','不适用','否/申请失能','MOTION_FAULT，红灯常亮'),('P14','输出或停车失败','写零失败、模式不支持、停车超时','立即安全停车','不适用','否/申请失能','MOTION_FAULT，红灯常亮'),('P15','关键任务超时','心跳年龄>500 ms','尽力立即停车后IWDG','无法保证','依赖硬件链','诊断锁存；红灯保持'),('P16','IWDG复位','IWDG停止刷新后硬件复位','看门狗恢复停车','恢复减速10 s+2 s确认','恢复流程内按驱动器能力','成功则保持锁定；失败红灯常亮'),('P17','HardFault/断言/栈溢出','CPU异常或RTOS保护触发','硬件安全停车路径','无法由软件保证','依赖STO/机械制动','红灯常亮或复位后锁存'),('P18','上电安全停车','启动未完成安全核验','保持安全，不授权','无运动输出','失能/门禁关闭','核验失败红灯常亮')]
add_table(doc,summary[0],summary[1:],widths=[18,35,57,48,33,41,52],font_size=6.8)

add_heading(doc,'十四 全部停车路径流程图',1)
add_para(doc,'下图将三类正常控制源、RS485异常分级、危险故障升级、零速确认、故障锁存和 CPU/看门狗硬件边界放在同一张图中。图中的“受控停车”不代表立即切断PWM，而是保持可控输出并让速度按设定减速度逐步趋近零；“立即安全停车”才是停止运动规划、关闭软件输出门禁、申请失能和锁存故障。')
p=doc.add_paragraph(); p.alignment=WD_ALIGN_PARAGRAPH.CENTER
p.add_run().add_picture(str(FLOW), width=Mm(170))
cap=doc.add_paragraph('图1 轨道巡检车全停车路径总览'); cap.alignment=WD_ALIGN_PARAGRAPH.CENTER
for r in cap.runs:
    r.font.name=FONT; r._element.rPr.rFonts.set(qn('w:eastAsia'), FONT); r.font.size=Pt(9); r.font.italic=True

add_heading(doc,'十五 当前工程安全边界和测试建议',1)
add_para(doc,'第一，当前工程的“运行期5次连续失败”只针对关键反馈分项计数；它不是“所有异常累计5次”的总计数。速度、故障、使能反馈分别计数，成功收到相应对象后应清除对应连续失败计数。')
add_para(doc,'第二，500 ms是反馈样本最大年龄门槛，不等于允许车辆继续高速运行500 ms。达到年龄门槛后，工程会进入受控安全停车判定；受控停车期间若不能继续获得反馈、不能写入递减速度或读到危险故障，则升级立即安全停车。')
add_para(doc,'第三，单次RS485超时通常不会直接导致立即停车，但若当前已经处于受控停车、看门狗恢复、启动保持或其他安全序列，通信失败可能被视为无法完成安全序列，从而升级立即安全停车。')
add_para(doc,'第四，红灯点亮表示主控进入故障、离线、K5锁存、启动保持失败或恢复失败等锁定状态，不表示一定读取到了驱动器故障码。必须结合LCD中的故障原因、轴号、反馈年龄、Modbus结果、连续失败次数和驱动器故障码判断。')
add_para(doc,'第五，若现场仍出现“高速后逐步减速至零，随后红灯常亮、三端禁止授权”，该现象与当前受控安全停车后最终故障锁存路径一致。下一步应从F407诊断快照确认：初始原因、最终原因、两轴触发时RPM、命令RPM、speed_age_ms、fault_age_ms、enable_feedback_age_ms、Modbus结果/地址、连续失败次数和是否发生升级。')
add_heading(doc,'建议的实物验证顺序',2)
for t in ['固定车辆或抬轮，先验证零速待机和人工正常停车。','依次在200、600、1200、1600 rpm验证正常回中、遥控K5、HMI STOP、CAN STOP。','在每个速度点分别注入单次超时、连续2次异常、连续5次异常、反馈年龄超过500 ms和驱动器真实故障码。','记录实际减速度、停车时间、停车距离、两轴同步误差、母线电压、驱动器报警和F407诊断快照。','最后单独验证IWDG、HardFault和RS485物理断线，确认硬件通信超时/STO/机械制动承担安全停车，禁止把CPU异常测试与载车高速运行混合。']:
    add_bullet(doc,t)

add_heading(doc,'十六 工程参考文件',1)
refs=[('源码文件','用途'),(str(ROOT/'APP/app_config.h'),'全局机械参数、减速度、超时和任务周期配置'),(str(ROOT/'APP/motion_control.c'),'运动状态机、正常/受控/立即停车接口'),(str(ROOT/'APP/app_remote_servo.c'),'遥控正常停车、K5、无线失联和授权恢复'),(str(ROOT/'APP/HMI/app_hmi_control.c'),'车载屏调速、STOP、RELEASE、会话/目标超时'),(str(ROOT/'APP/app_can_control.c'),'CAN授权、正常STOP、快速STOP、链路和租约超时'),(str(ROOT/'Device/aimtor_servo.c'),'RS485轮询、反馈年龄、连续失败、受控停车和故障码处理'),(str(ROOT/'APP/app_health.c'),'关键任务心跳、看门狗刷新和复位诊断'),(str(ROOT/'Core/Src/iwdg.c'),'IWDG分频和重装值'),(str(ROOT/'Core/Src/stm32f4xx_it.c'),'HardFault、BusFault、UsageFault等CPU异常入口'),(str(ROOT/'APP/app_rtos_task.c'),'健康任务和红色指示灯状态')]
add_table(doc,refs[0],refs[1:],widths=[112,76],font_size=7.8)

# footer
for section in doc.sections:
    footer=section.footer
    p=footer.paragraphs[0]
    p.alignment=WD_ALIGN_PARAGRAPH.CENTER
    r=p.add_run('轨道巡检车全停车方案 | F407底盘主控 | 2026年10月24日')
    r.font.name=FONT; r._element.rPr.rFonts.set(qn('w:eastAsia'), FONT); r.font.size=Pt(8); r.font.color.rgb=RGBColor.from_string('777777')

doc.save(DOCX)
print(DOCX)
print(FLOW)


