import fs from "node:fs/promises";
import path from "node:path";
import { FileBlob, PresentationFile } from "@oai/artifact-tool";
import { pathToFileURL } from "node:url";

const root = process.env.BUILD_DIR;
const templatePath = path.join(root, "reference.pptx");
const outputPath = process.env.OUT_PPTX;
const workspaceDir = process.env.WORKSPACE_DIR;
const skillDir = "C:/Users/15056/.codex/plugins/cache/openai-primary-runtime/presentations/26.1009.11318/skills/presentations";
const font = "微软雅黑";
const C = {
  navy: "#44546A",
  blue: "#4874CB",
  text: "#595659",
  muted: "#7B7D85",
  line: "#D9DFEA",
  paleBlue: "#F1F5FD",
  paleBlue2: "#EAF1FF",
  paleGreen: "#EAF6EF",
  green: "#4A9A72",
  paleOrange: "#FFF3E5",
  orange: "#D58A35",
  white: "#FFFFFF",
};

function addText(slide, text, x, y, w, h, size = 16, color = C.text, bold = false, align = "left") {
  const shape = slide.shapes.add({
    geometry: "textbox",
    position: { left: x, top: y, width: w, height: h },
    fill: "none",
    line: { style: "solid", fill: "none", width: 0 },
  });
  shape.text = text;
  shape.text.style = {
    typeface: font,
    fontSize: size,
    color,
    bold,
    alignment: align,
    verticalAlignment: "middle",
    wrap: "square",
    insets: { left: 0, right: 0, top: 0, bottom: 0 },
    autoFit: "none",
  };
  return shape;
}

function addBox(slide, x, y, w, h, fill, line = C.line, radius = "rounded-lg") {
  return slide.shapes.add({
    geometry: "roundRect",
    position: { left: x, top: y, width: w, height: h },
    fill,
    line: { style: "solid", fill: line, width: 1 },
    borderRadius: radius,
  });
}

function addTag(slide, text, x, y, w, fill, color = C.text) {
  addBox(slide, x, y, w, 24, fill, fill, "rounded-md");
  addText(slide, text, x, y + 1, w, 20, 12, color, true, "center");
}

function addMetric(slide, x, label, value, caption, accent = C.blue) {
  addBox(slide, x, 128, 238, 82, C.white, C.line);
  addText(slide, label, x + 14, 139, 210, 20, 12, C.muted, true);
  addText(slide, value, x + 14, 158, 210, 32, 25, accent, true);
  addText(slide, caption, x + 14, 193, 210, 14, 11.5, C.text, false);
}

function addSection(slide, num, title, body, x, y, w, h, statusText, statusFill) {
  addBox(slide, x, y, w, h, C.paleBlue, C.line);
  addText(slide, `${num}  ${title}`, x + 14, y + 10, w - 28, 24, 17, C.blue, true);
  addText(slide, body, x + 14, y + 39, w - 28, h - 50, 13.5, C.text, false);
  addTag(slide, statusText, x + w - 105, y + 10, 91, statusFill, statusFill === C.paleOrange ? C.orange : C.green);
}

function addFlowNode(slide, text, x, y, w, h, fill = C.paleBlue2, color = C.blue) {
  addBox(slide, x, y, w, h, fill, C.line);
  addText(slide, text, x + 8, y + 8, w - 16, h - 16, 13.5, color, true, "center");
}

function addArrow(slide, text, x, y, w, h) {
  addText(slide, text, x, y, w, h, 23, C.blue, true, "center");
}

function setChrome(slide, titleText, subtitleText, titleSize = 48) {
  const title = slide.shapes.items[0];
  const body = slide.shapes.items[1];
  const subtitle = slide.shapes.items[2];
  title.position = { left: 126, top: 24, width: 1028, height: 72 };
  title.text = titleText;
  title.text.style = { typeface: font, fontSize: titleSize, bold: true, color: C.navy, autoFit: "none", insets: { left: 0, right: 0, top: 0, bottom: 0 } };
  body.text = "";
  body.position = { left: 126, top: 115, width: 1028, height: 540 };
  subtitle.position = { left: 136, top: 102, width: 1000, height: 24 };
  subtitle.text = subtitleText;
  subtitle.text.style = { typeface: font, fontSize: 13, color: C.text, autoFit: "none", insets: { left: 0, right: 0, top: 0, bottom: 0 } };
}

const presentation = await PresentationFile.importPptx(await FileBlob.load(templatePath));
const original = presentation.slides.items.slice();
for (let i = original.length - 1; i >= 1; i -= 1) presentation.slides.remove(i);
const slides = [original[0], ...Array.from({ length: 4 }, () => original[0].duplicate())];
for (let i = 0; i < slides.length; i += 1) slides[i].moveTo(i);

// 第1页：成果总览与状态分层。
{
  const s = slides[0];
  setChrome(s, "14天核心研发成果", "汇报周期  2026.09.28 至 10.11    刘德志", 55);
  addMetric(s, 126, "轴配置", "3种", "LEFT_ONLY / RIGHT_ONLY / BOTH", C.blue);
  addMetric(s, 384, "工程质量", "0 / 0", "错误 / 警告，多配置全量构建", C.green);
  addMetric(s, 642, "CAN速率", "500 kbit/s", "STM32F4 ↔ RK3588", C.blue);
  addMetric(s, 900, "业务证据", "Link=4", "连续通信窗口 30秒", C.orange);
  addSection(s, "01", "底盘控制安全化", "伺服三模式、双轴遥控、K1急停、失联停车、K2看门狗复位测试和K1零速使能保持已形成阶段性闭环。", 126, 240, 1028, 74, "阶段性通过", C.paleOrange);
  addSection(s, "02", "车载屏状态可视化", "正式屏端完成32项只读回归测试；主控形成R0至R4统一状态快照，并通过D1版本获得UART5自动恢复和间歇性超时证据。", 126, 328, 1028, 74, "可诊断", C.paleBlue2);
  addSection(s, "03", "上层通信工程化", "CAN V1.0基础双向链路已打通，V1.1新增0x47、0x48、0x55只读缓存和0x53无许可联锁；0x54动作请求保持关闭。", 126, 416, 1028, 74, "已打通", C.paleGreen);
  addBox(s, 126, 514, 1028, 64, C.paleBlue, C.line);
  addText(s, "本周期总判断", 146, 528, 110, 22, 14, C.blue, true);
  addText(s, "从“单项功能可运行”推进到“控制安全、状态可信、通信可诊断”。", 270, 525, 650, 30, 17, C.navy, true);
  addTag(s, "已完成：软件 / 编译", 926, 524, 107, C.paleGreen, C.green);
  s.speakerNotes.textFrame.setText("这一页先给出14天的总判断：底盘控制、车载屏状态发布和上层CAN通信三条主线都完成了阶段性推进。重点不是简单增加了多少功能，而是把功能放进了控制权、安全门禁、实际反馈和诊断证据体系。需要特别说明，CAN的30秒窗口和UART5自动恢复都属于阶段性证据，长时间稳定性验收仍在下一阶段。");
}

// 第2页：控制安全闭环和实物数据。
{
  const s = slides[1];
  setChrome(s, "底盘控制从“能运行”到“可控、可停、可恢复”", "控制主线：输入授权 → 统一执行 → 实际反馈 → 安全停车 → 人工恢复", 43);
  const flowY = 158;
  const nodes = [
    ["CLI / 遥控器", 126, 118],
    ["控制权\n安全门禁", 276, 118],
    ["统一运动\n执行器", 426, 118],
    ["RS485 / PWM\n伺服控制", 576, 138],
    ["速度 / 位置 /\n故障反馈", 746, 138],
  ];
  for (const [text, x, w] of nodes) addFlowNode(s, text, x, flowY, w, 56);
  addArrow(s, "→", 246, flowY + 12, 26, 32);
  addArrow(s, "→", 396, flowY + 12, 26, 32);
  addArrow(s, "→", 546, flowY + 12, 26, 32);
  addArrow(s, "→", 716, flowY + 12, 26, 32);
  addFlowNode(s, "停车确认\n状态回传", 914, flowY, 240, 56, C.paleGreen, C.green);
  addArrow(s, "→", 884, flowY + 12, 26, 32);
  addTag(s, "K1：受控停车 + 零速使能保持", 126, 236, 255, C.paleOrange, C.orange);
  addTag(s, "失联：受控停车，不续跑", 392, 236, 224, C.paleBlue2, C.blue);
  addTag(s, "K2：制造IWDG复位条件", 627, 236, 226, C.paleOrange, C.orange);
  addTag(s, "故障：优先安全锁存", 864, 236, 198, C.paleOrange, C.orange);
  addBox(s, 126, 286, 630, 250, C.white, C.line);
  addText(s, "阶段性实物证据", 146, 302, 240, 24, 19, C.blue, true);
  addText(s, "• 左轴RS485位置模式完成约10圈真实动作验证\n• 右轴地址2完成LV调速实物验证\n• 双轴实际速度差约0至2 rpm\n• RS485总通信成功率约99.844%\n• K1急停、无线失联停车、K2看门狗测试完成\n• 脉冲停止后关闭PWM和AM26C31门控", 146, 340, 560, 165, 15, C.text, false);
  addBox(s, 786, 286, 368, 250, C.paleBlue, C.line);
  addText(s, "安全边界", 806, 302, 180, 24, 19, C.blue, true);
  addText(s, "K1新长按必须满足回中、零速、链路新鲜和门禁条件后重新授权。\n\nK2只用于验证看门狗复位后的安全恢复，不恢复旧运动。\n\n真实故障、通信异常和停车失败仍优先进入原有锁存路径。", 806, 342, 328, 145, 14.5, C.text, false);
  addBox(s, 126, 560, 1028, 54, C.paleBlue, C.line);
  addText(s, "工程价值", 146, 574, 96, 20, 14, C.blue, true);
  addText(s, "控制链路具备异常停车、复位后不续跑、人工恢复和故障优先级处理能力，为整车低速测试提供安全基础。", 250, 570, 860, 30, 15.5, C.navy, true);
  s.speakerNotes.textFrame.setText("第二页重点讲底盘控制的层次变化。过去是先让电机运行起来，现在已经把输入授权、统一运动执行器、实际反馈和停车确认串成了闭环。这里用左轴约10圈、双轴0至2 rpm、RS485约99.844%等数据证明控制链路不是停留在代码层面。K1、K2和失联流程分别验证了人工停车、复位安全恢复和无线异常场景，但仍然要把长时间运行和整车低速验收作为下一阶段任务。");
}

// 第3页：CAN链路与UART5证据对照。
{
  const s = slides[2];
  setChrome(s, "跨设备通信已打通，进入稳定性验收", "CAN基础链路已形成实物证据；UART5已具备诊断证据但根因尚未闭环", 45);
  addText(s, "CAN双向通信闭环", 126, 139, 250, 24, 19, C.blue, true);
  addFlowNode(s, "STM32F4\n底盘主控", 126, 174, 210, 66);
  addArrow(s, "↔", 350, 184, 60, 42);
  addFlowNode(s, "500 kbit/s\nCAN链路", 430, 174, 210, 66, C.paleGreen, C.green);
  addArrow(s, "↔", 654, 184, 60, 42);
  addFlowNode(s, "RK3588\n上层感知层", 738, 174, 210, 66);
  addTag(s, "Link=4", 980, 174, 88, C.paleGreen, C.green);
  addTag(s, "30秒窗口", 1076, 174, 78, C.paleBlue2, C.blue);
  addText(s, "握手完成 · 健康心跳门禁通过 · CRC通过 · 序号连续 · Bus-off累计为0", 126, 250, 850, 22, 14, C.text, false);
  addBox(s, 126, 292, 1028, 80, C.paleBlue, C.line);
  addText(s, "V1.1只读适配", 146, 306, 150, 22, 16, C.blue, true);
  addText(s, "0x47机械臂摘要   0x48关节反馈   0x55结果缓存   0x53无许可联锁", 310, 306, 625, 22, 15, C.text, false);
  addText(s, "permit_mask=0 · valid_for_ms=0 · 0x54动作请求关闭 · 不新增伺服写请求", 310, 334, 760, 22, 14, C.orange, true);
  addText(s, "UART5车载屏诊断闭环", 126, 397, 300, 24, 19, C.blue, true);
  addFlowNode(s, "D1诊断版\n实体下载", 126, 434, 175, 56);
  addArrow(s, "→", 310, 446, 34, 28);
  addFlowNode(s, "现场持续\n运行", 352, 434, 175, 56);
  addArrow(s, "→", 536, 446, 34, 28);
  addFlowNode(s, "自动恢复\n证据", 578, 434, 175, 56, C.paleGreen, C.green);
  addArrow(s, "→", 762, 446, 34, 28);
  addFlowNode(s, "间歇性超时\n定位", 804, 434, 175, 56, C.paleOrange, C.orange);
  addBox(s, 1000, 416, 154, 94, C.white, C.line);
  addText(s, "71秒窗口", 1014, 428, 126, 18, 13, C.muted, true, "center");
  addText(s, "1479 / 101", 1014, 448, 126, 24, 22, C.blue, true, "center");
  addText(s, "读完成 / 超时", 1014, 477, 126, 16, 11.5, C.text, false, "center");
  addBox(s, 126, 530, 1028, 72, C.paleOrange, C.line);
  addText(s, "当前边界", 146, 545, 100, 20, 14, C.orange, true);
  addText(s, "CAN：30秒基础链路阶段性通过，长时间稳定性待验收。  UART5：通信可自动恢复，但超时根因未完全确定。车载屏、CAN运动和机械臂0x54均保持关闭。", 250, 538, 875, 42, 14, C.text, true);
  s.speakerNotes.textFrame.setText("第三页要把两条通信线放在一起讲，但明确它们处于不同验收阶段。CAN已经有500 kbit/s、Link=4、30秒、CRC、序号和Bus-off等完整基础链路证据；V1.1则完成了只读缓存和无许可联锁，不等于开放机械臂动作。UART5这边通过D1版本取得了自动恢复证据，但71秒内仍有101次超时，所以结论应当是“可诊断、待定位”，不能说问题已经解决。");
}

// 第4页：执行计划与可验收门槛。
{
  const s = slides[3];
  setChrome(s, "下一阶段重点：从阶段性通过到可验收交付", "计划原则：每项任务同时给出执行步骤、证据要求和通过条件", 43);
  const cols = [126, 470, 814];
  const widths = 310;
  const heads = [
    ["P0  UART5问题定位", C.paleOrange, C.orange],
    ["P0  CAN V1.1稳定性", C.paleGreen, C.green],
    ["P1/P2  安全与工程收口", C.paleBlue2, C.blue],
  ];
  const bodies = [
    "执行\n1. 同步D1与hmi status\n2. 对齐六区请求响应\n3. 对比RX/TX恢复和UART错误\n4. 定位停滞责任端\n\n验收\n• 六区持续刷新\n• 统计可对应\n• 连续至少30分钟\n• 拔屏重连不影响底盘",
    "执行\n1. 延长连续运行\n2. 覆盖任意顺序上电\n3. 验证重启和拔线重连\n4. 检查心跳超时与过期\n5. 记录CRC、序号、Bus-off\n\n验收\n• Link=4可建立和恢复\n• Bus-off保持0\n• 断线后状态失效\n• 不新增伺服写请求",
    "执行\n• 低速伺服和双轴同步\n• 核对K1/K2/真实故障\n• 确认BMS、接触器、预充\n• 完善V1.1向量和验收模板\n\n验收\n• 证据可追溯\n• 阻塞原因明确\n• 不绕过安全门禁",
  ];
  for (let i = 0; i < 3; i += 1) {
    const [head, fill, color] = heads[i];
    addBox(s, cols[i], 146, widths, 390, C.white, C.line);
    addBox(s, cols[i], 146, widths, 48, fill, fill);
    addText(s, head, cols[i] + 14, 158, widths - 28, 24, 16, color, true);
    addText(s, bodies[i], cols[i] + 16, 211, widths - 32, 300, 13.5, C.text, false);
    addTag(s, i === 0 ? "定位" : i === 1 ? "稳定" : "收口", cols[i] + widths - 74, 158, 60, fill, color);
  }
  addBox(s, 126, 560, 998, 54, C.navy, C.navy);
  addText(s, "管理层判断", 146, 574, 110, 20, 14, C.white, true);
  addText(s, "下一阶段不再只看“功能是否存在”，而看能否持续运行、异常能否恢复、状态是否可信、证据是否完整。", 270, 570, 830, 30, 15.5, C.white, true);
  s.speakerNotes.textFrame.setText("第四页把下一阶段计划改成验收矩阵。P0是先把UART5和CAN的稳定性证据补齐，P1/P2是安全和硬件收口。每一列都同时给出执行步骤和通过条件，主管可以直接看到后续工作不是泛泛而谈，而是有可观察的结果、有明确的关闭条件，也明确哪些能力在验收前不会开放。");
}

// 第5页：长期路线和项目价值。
{
  const s = slides[4];
  setChrome(s, "长期目标：可追溯、可验证、可扩展的整车控制基线", "从单点功能迭代，走向统一控制、安全闭环和长期可靠性验收", 40);
  const layers = [
    ["01 统一控制与安全基线", "统一遥控器、CLI、车载屏和上层感知层控制源；明确授权代次、来源互斥、失联停车、人工恢复和真实故障优先级。", C.paleBlue2, C.blue],
    ["02 可信状态与通信体系", "统一车载屏R0至R4、CAN V1.1只读记录和无许可联锁；为UART5、RS485、NRF、CAN建立有效性与恢复测试矩阵。", C.paleGreen, C.green],
    ["03 硬件安全与可靠性验收", "依据母线电流、BMS能力、驱动器输入电容和STO/E_STOP定型供电与急停；以固定车辆、低速、无负载条件推进标定、连续运行和正式投用评审。", C.paleOrange, C.orange],
  ];
  for (let i = 0; i < layers.length; i += 1) {
    const [head, body, fill, color] = layers[i];
    const y = 144 + i * 105;
    addBox(s, 126, y, 1028, 86, fill, C.line);
    addText(s, head, 148, y + 15, 280, 24, 17, color, true);
    addText(s, body, 430, y + 12, 690, 52, 14, C.text, false);
  }
  addBox(s, 126, 474, 1028, 124, C.navy, C.navy);
  addText(s, "项目价值", 150, 492, 120, 24, 18, C.white, true);
  addText(s, "降低现场调试对人工操作的依赖        降低异常通信造成误动作的风险\n提升跨设备状态一致性和故障定位效率        为车载屏控制、机械臂动作和整车自动化保留安全接口", 150, 531, 950, 50, 15, C.white, true);
  addTag(s, "当前未开放动作能力", 956, 493, 170, C.paleOrange, C.orange);
  s.speakerNotes.textFrame.setText("最后一页讲长期价值。当前工作不是零散地修复问题，而是在建立统一控制权、可信状态发布、跨设备通信和硬件安全闭环。这样做的价值是后续无论接入车载屏控制、机械臂动作还是更高层自动化，都必须经过同一套授权和安全门禁。当前仍然保持只读和无许可，动作能力要等协议、硬件联锁和实物验收共同冻结后再开放。");
}

const staging = path.join(root, ".codex-finalizer");
await fs.mkdir(staging, { recursive: true });
const candidate = path.join(staging, "candidate.pptx");
await (await PresentationFile.exportPptx(presentation)).save(candidate);
const { finalizePresentation } = await import(pathToFileURL(path.join(skillDir, "container_tools/artifact_tool_utils.mjs")).href);
const receipt = path.join(staging, "enhanced.validation.json");
const result = await finalizePresentation({
  workspaceDir,
  candidatePath: candidate,
  finalPath: outputPath,
  pythonExecutable: "D:/Openmv_Python/python.exe",
  integrityValidatorPath: path.join(skillDir, "container_tools/inspect_presentation_package_integrity.py"),
  layoutValidatorPath: path.join(skillDir, "container_tools/inspect_presentation_layout_geometry.py"),
  layoutArgs: ["--expected-slide-size-emu", "12192000,6858000", "--validate-bullet-geometry", "--validate-heading-fit"],
  requiredNativeTableOwnerSlides: [],
  fontPolicy: { basis: "design", families: [font, "Calibri"] },
  verifyArtifactToolImport: true,
  receiptPath: receipt,
});
console.log(JSON.stringify(result));
