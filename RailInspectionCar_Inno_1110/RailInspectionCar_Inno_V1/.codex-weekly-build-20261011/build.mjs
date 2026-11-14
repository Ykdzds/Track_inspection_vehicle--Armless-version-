import fs from "node:fs/promises";
import path from "node:path";
import { FileBlob, PresentationFile } from "@oai/artifact-tool";
import { pathToFileURL } from "node:url";

const root = process.env.BUILD_DIR;
const template = path.join(root, "reference.pptx");
const outPath = process.env.OUT_PPTX;
const skill = "C:/Users/15056/.codex/plugins/cache/openai-primary-runtime/presentations/26.1009.11318/skills/presentations";
const p = await PresentationFile.importPptx(await FileBlob.load(template));
const original = p.slides.items.slice();
for (let i = original.length - 1; i >= 1; i--) p.slides.remove(i);
const slides = [original[0], ...Array.from({length:4},()=>original[0].duplicate())];
for (let i=0;i<slides.length;i++) slides[i].moveTo(i);
const pages = [
  {
    title:"上周工作（一）  9.28-10.11",
    groups:[
      ["一、伺服三模式调试链路完善","完成左轴速度、RS485内部位置和外部脉冲统一调试流程，自动完成模式准备、参数配置、回读核验、使能和运动启动。","修复位置模式斜坡时间误拦截、位置模式启动序列、停止锁存及脉冲模式残留触发问题。"],
      ["二、右轴与双轴遥控控制扩展","完成右轴地址2适配、双轴共同速度控制、K1授权、短按急停、无线失联停车和故障联停。","双轴实际速度差约0至2 rpm，RS485总通信成功率约99.844%，位置与实际速度联合确认机制已建立。"],
      ["三、安全恢复形成验证闭环","完成K1停车后零速使能保持、K2短按看门狗复位测试、启动强制停车、残留运动清理和任务栈预算修复。","实物验证保留真实故障和通信门禁，工程多配置全量编译保持0错误、0警告。"]
    ],
    notes:"本页归纳2026年9月28日至10月11日的伺服、遥控与安全控制进展。关键数据来自日报：左轴位置约10圈验证、双轴速度差约0至2 rpm、RS485成功率约99.844%、K1/K2实物验证。"
  },
  {
    title:"上周工作（二）  通信与车载屏",
    groups:[
      ["一、串口与RS485非阻塞通信","完成USART1 TX DMA、环形发送缓存、USART2 DMA/IDLE接收和通信诊断，修复单次轮询异常引发的误停车。","补齐反馈年龄、失败寄存器、实际值、停车原因和通信状态，保持动作写入与故障恢复边界清晰。"],
      ["二、车载屏全模式只读状态发布","正式Visual-PIX工程完成位置里程、同步、告警、能力、控制源、初始化和数据年龄语义完善，32项回归测试全部通过。","主控侧形成R0至R4与元数据统一状态快照，APP_HMI_CONTROL_ENABLED保持0U，屏端不开放写控制。"],
      ["三、UART5异常诊断取得现场证据","完成09诊断副本和D1实体下载。启动号保持100，请求/响应停在150/150而发布序号继续增长。","71秒窗口新增1479次读完成、101次超时，超时占比约6.39%，属性不匹配为0；通信可自动恢复但稳定性根因未定。"]
    ],
    notes:"本页归纳串口、屏端显示和UART5现场排查。D1诊断版本通过COM16 Type-C下载至实体屏，正式08工程保持不变。当前结论是通信没有永久停止，但仍存在间歇性超时。"
  },
  {
    title:"上周工作（三）  CAN与工程协同",
    groups:[
      ["一、CAN双向链路阶段性打通","完成STM32F4与RK3588 500 kbit/s非阻塞CAN握手、心跳、状态上传、状态接收、查询应答和诊断链路。","实物连续30秒心跳和状态正常，扩展记录CRC通过、序号连续、Bus-off累计为0，主控诊断达到Link=4。"],
      ["二、CAN V1.1第一阶段只读适配","新增0x47机械臂摘要、0x48关节反馈、0x55结果记录缓存和0x53无许可联锁；0x54动作请求保持关闭。","实现数据年龄、配置变化清理、32条静态缓存和CLI查询；permit_mask=0、valid_for_ms=0，不新增伺服写请求。"],
      ["三、电源与硬件接口风险识别","完成伺服、机械臂、RS485、上层通信、车载屏及电源接口核对，识别两台伺服约96 A可能超过BMS 80 A持续放电能力。","形成主熔断器、支路熔断器、DC/DC保险、预充和主接触器方案，正式整车优先考虑12 V按钮加主接触器。"]
    ],
    notes:"Link=4表示握手完成、健康心跳门禁满足并进入CAN_LINK_READY业务就绪状态。V1.1本阶段只读和无许可联锁已完成阶段性软件适配，不代表机械臂动作安全验收完成。"
  },
  {
    title:"本周工作计划",
    groups:[
      ["一、P0：完成UART5问题定位与屏端验收","同步保存屏端D1与主控hmi status，同窗口核对六区请求响应、RX/TX恢复、UART错误和响应延迟。","补齐R0至R4及元数据抓帧，完成固定车辆条件下至少30分钟稳定性测试和逐页面验收。"],
      ["二、P0：延长CAN V1.1稳定性验证","覆盖至少30分钟运行、上下层任意顺序上电、重启、拔线重连、心跳超时、机械臂摘要过期和Link=4恢复。","记录Bus-off、CRC、序号、年龄、任务栈余量和异常恢复过程，保持0x54与机械臂动作关闭。"],
      ["三、P1/P2：安全、硬件与文档收口","继续开展低速伺服、双轴同步、K1/K2和位置里程验证；确认主接触器、预充、熔断器、DC/DC和STO/E_STOP。","完善V1.1测试向量、UART5联调说明和实物验收模板，不改变既有控制参数和安全门禁。"]
    ],
    notes:"本页为下一阶段执行计划。P0优先解决可观测、可复现的UART5与CAN联调问题；P1/P2完成硬件安全方案和文档归档。所有动作能力仍遵守现有只读与无许可边界。"
  },
  {
    title:"长期计划",
    groups:[
      ["一、建立统一控制权与安全基线","统一遥控器、CLI、车载屏和上层感知层控制源仲裁，明确授权代次、来源互斥、失联停车、人工恢复和真实故障优先级。","任何新增控制入口必须接入统一运动执行器和安全门禁，不能通过协议层直接触发伺服写入。"],
      ["二、形成可信状态发布与通信体系","将车载屏R0至R4、元数据、CAN V1.1只读记录和无许可联锁统一为带有效性、年龄和来源的快照。","持续完成CAN、UART5、RS485、NRF的重启、断线、过期、长时间和异常帧验证，保留可追溯证据。"],
      ["三、完成整车硬件安全闭环与可靠性验收","依据实际母线电流、BMS能力、驱动器输入电容和STO/E_STOP实测结果定型主接触器、预充、熔断和急停方案。","以固定车辆、低速、无负载和可复现条件推进整车标定、连续运行和正式投用评审，机械臂动作许可另行联合冻结。"]
    ],
    notes:"长期目标聚焦可追溯控制基线、可信状态发布、硬件安全闭环和长期可靠性。机械臂0x54动作、实体许可与硬件联锁需在后续联合冻结后再开放。"
  }
];
for (let i=0;i<pages.length;i++) {
  const s=slides[i], page=pages[i];
  const title=s.shapes.items[0], body=s.shapes.items[1];
  title.position={left:126,top:26,width:1029,height:81};
  title.text=page.title;
  title.text.style={typeface:"微软雅黑",fontSize:58.67,bold:true,color:"#44546A",autoFit:"none",insets:{left:0,right:0,top:0,bottom:0}};
  body.position={left:126,top:148,width:1029,height:530};
  body.text.style={typeface:"微软雅黑",fontSize:18,bold:false,color:"#595659",autoFit:"none",verticalAlignment:"top",wrap:"square",insets:{left:0,right:0,top:0,bottom:0}};
  const paras=[];
  for (const [heading,...lines] of page.groups) {
    paras.push({runs:[{run:`▎${heading}`,textStyle:{bold:true,fontSize:"21.33px",color:"#4874CB",typeface:"微软雅黑"}}],spaceBefore:900,spaceAfter:650});
    for (const line of lines) paras.push({runs:[{run:line,textStyle:{fontSize:"18px",color:"#595659",typeface:"微软雅黑"}}],spaceAfter:750});
  }
  body.text=paras;
  const subtitle = s.shapes.items[2];
  if (subtitle) subtitle.text = "工作回顾  2026.09.28 至 10.11    刘德志";
  s.speakerNotes.textFrame.setText(page.notes);
}
const staging=path.join(root,".codex-finalizer"); await fs.mkdir(staging,{recursive:true});
const candidate=path.join(staging,"candidate.pptx"); await (await PresentationFile.exportPptx(p)).save(candidate);
const {finalizePresentation}=await import(pathToFileURL(path.join(skill,"container_tools/artifact_tool_utils.mjs")).href);
const result=await finalizePresentation({workspaceDir:process.env.WORKSPACE_DIR,candidatePath:candidate,finalPath:outPath,pythonExecutable:"D:/Openmv_Python/python.exe",integrityValidatorPath:path.join(skill,"container_tools/inspect_presentation_package_integrity.py"),layoutValidatorPath:path.join(skill,"container_tools/inspect_presentation_layout_geometry.py"),layoutArgs:["--expected-slide-size-emu","12192000,6858000","--validate-bullet-geometry","--validate-heading-fit"],requiredNativeTableOwnerSlides:[],fontPolicy:{basis:"design",families:["微软雅黑","Calibri"]},verifyArtifactToolImport:true,receiptPath:path.join(staging,"weekly.validation.json")});
console.log(JSON.stringify(result));