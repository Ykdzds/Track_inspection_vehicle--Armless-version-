"""Assemble release evidence only after the final formal matrix has completed."""
from pathlib import Path
import datetime,hashlib,json,re,subprocess
root=Path(__file__).resolve().parents[2]
out=Path(__file__).resolve().parent/'验证'
builds=json.loads((out/'Keil构建结果.json').read_text(encoding='utf-8-sig'))
if isinstance(builds,dict):builds=[builds]
expected={'hmi','remote','canservo','canarm','rs485','all','legacy','alloff'}
assert {x['Variant'] for x in builds}==expected,'Incomplete formal build matrix'
assert len({x['SourceDigest'] for x in builds})==1,'Mixed production revisions'
source=json.loads((out/'production-source-sha256.json').read_text(encoding='utf-8-sig'))
assert builds[0]['SourceDigest']==source['Digest']
for entry in source['Files']:
 assert hashlib.sha256((root/entry['Path']).read_bytes()).hexdigest().upper()==entry['SHA256'],entry['Path']
tests=json.loads((out/'regression/summary.json').read_text(encoding='utf-8-sig'))
assert len(tests)==30 and not any(x['exit'] for x in tests),'Regression failure or missing suite'
for build in builds:
 folder=out/build['Variant']
 assert build['Errors']==build['Warnings']==0 and build['Delta82d6a0c']<=4096
 for extension,key in [('hex','HexSha256'),('axf','AxfSha256')]:
  file=folder/('RailInspectionCar_V1.'+extension)
  assert hashlib.sha256(file.read_bytes()).hexdigest().upper()==build[key]
 memory=(folder/'memory-stack.log').read_text(encoding='utf-8-sig')
 margins=[int(x) for x in re.findall(r'margin=(\d+) bytes',memory)]
 assert len(margins)==12 and min(margins)>=256
 build['MinimumStaticStackMargin']=min(margins)
 build['OrdinaryFree']=sum(build['Regions'][x]['Free'] for x in ['RW_IRAM1','RW_IRAM2'])
 final_file=root/'MDK-ARM/RailInspectionCar_V1/RailInspectionCar_V1.hex'
 if build['Variant']=='alloff':assert hashlib.sha256(final_file.read_bytes()).hexdigest().upper()==build['HexSha256']
assert subprocess.run(['git','diff','--quiet','--','MDK-ARM/RailInspectionCar_V1.uvprojx','MDK-ARM/RTE/_RailInspectionCar_V1/RTE_Components.h'],cwd=root).returncode==0,'Formal project/RTE were not restored'
manifest={'recorded_at':datetime.datetime.now().astimezone().isoformat(),'implementation':'link5-r1','diagnostic_format_revision':3,'baseline_git':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'source_revision':source['Digest'],'source_revision_format':'SHA256 of compressed JSON sorted production path/SHA entries; see production-source-sha256.json','implementation_in_worktree':True,'default_main_switches':[0,0,0,0,0],'fixed_ram_baseline_82d6a0c':174876,'fixed_ram_growth_limit':4096,'builds':builds,'host_suites':len(tests),'host_suites_passed':len(tests),'analyzer_test_cases':int(re.search(r'Ran (\d+) tests', (out/'regression/log-analyzer.log').read_text(encoding='utf-8-sig')).group(1)),'hardware_flashing_performed':False,'hardware_30min_passed':False,'hardware_2hour_passed':False,'hardware_hmi_deadline_validated':False,'final_formal_output':'alloff'}
(out/'delivery-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
lines=['# 最终软件验证与交付记录','','实现：`link5-r1`，格式版本3；Git基线：`'+manifest['baseline_git'][:7]+'`。本轮实现保留在工作区，完整生产修订指纹：`'+source['Digest']+'`。','','30组主机套件全部通过；共享writer含32种主宏组合及旧专项并开，分析器'+str(manifest['analyzer_test_cases'])+'项检查通过。','','| 正式全量配置 | 错误/警告 | 物理RAM B | 相对82d6a0c增量 B | 普通SRAM余量 B | CCM余量 B | 最小静态栈余量 B |','| --- | --- | ---: | ---: | ---: | ---: | ---: |']
for b in builds:lines.append('| '+b['Variant']+' | 0/0 | '+str(b['PhysicalRam'])+' | '+str(b['Delta82d6a0c'])+' | '+str(b['OrdinaryFree'])+' | '+str(b['Regions']['RW_CCM']['Free'])+' | '+str(b['MinimumStaticStackMargin'])+' |')
lines+=['','固定物理RAM包含RAM执行代码，链接器余量不是FreeRTOS堆余量。所有12个任务的静态调用图余量达到256字节；实物水位、函数指针/中断现场仍须验证。','','指定正式工程与RTE原始内容已恢复，七个诊断/专项开关默认均关闭；正式MDK输出目录中的HEX与alloff归档哈希一致。每个配置的HEX/AXF SHA256、target define、源修订指纹和时间均见 [delivery-manifest.json](验证/delivery-manifest.json)。map、调用图、编译日志和project snapshot位于各配置目录。','','新增版本尚未烧录，30分钟初验、2小时连续测试、HMI五路全开完整回复小于50ms、实物堆/栈验收均未执行。软件验证通过不等于实物验收完成。无推送。']
(Path(__file__).resolve().parent/'delivery.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('PASS final evidence: eight formal variants, identical source, thirty suites, restored project/RTE, final alloff hash')
