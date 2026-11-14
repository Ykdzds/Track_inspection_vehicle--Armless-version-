"""Summarize formal SRAM profiles without mixing linker RAM and heap allocations."""
import argparse, hashlib, json, re
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'Docs/F407_SRAM_20261108/验证'
MATRIX_VARIANTS={'compat','lcd-off','hmi','remote','canservo','canarm','rs485','all','lcd-off-all','maintenance','tune-only','pulse-back','legacy','default'}
DEFAULTS=dict(APP_CHASSIS_LCD_ENABLED=1,APP_DEBUG_CLI_ENABLED=0,APP_TUNE_UART_ENABLED=0,
    APP_SERVO_CLI_DEBUG_ENABLED=0,APP_SERVO_PULSE_ENABLED=0,APP_HMI_LINK_DIAG_ENABLED=0,
    APP_NRF_DIAGNOSTIC_ENABLED=0,APP_CAN_SERVO_DIAG_ENABLED=0,APP_CAN_ARM_DIAG_ENABLED=0,
    APP_SERVO_RS485_DIAG_ENABLED=0,APP_CAN_DEBUG_ENABLED=0,APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED=0,
    APP_UART_RX_ACTIVE=0,APP_CLI_SERVO_ACTIVE=0)

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest().upper()
def load(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def verify_hashes(folder,hashes):
    for filename,expected in hashes.items():assert digest(folder/filename)==expected,(folder.name,filename)

def verify_matrix(builds):
    assert len(builds)==14 and {r['Variant'] for r in builds}==MATRIX_VARIANTS,'Incomplete/duplicate 14-configuration matrix'
    assert builds[-1]['Variant']=='default','Matrix default must remain its last independent record'
    restore=load(OUT/'restore-status.json');source=load(OUT/'production-source-sha256.json')
    assert all(restore[k] is True for k in ['Completed','ProjectRestored','RteRestored','ProductionUnchanged'])
    assert restore['LastSuccessfulVariant']=='default'
    assert len({row['SourceDigest'] for row in builds})==1
    assert builds[0]['SourceDigest']==source['Digest']
    for row in builds:
        assert row['Exit']==row['Errors']==row['Warnings']==0,(row['Variant'],'build status')
        assert load(OUT/row['Variant']/'manifest.json')==row,(row['Variant'],'matrix/manifest mismatch')
    return restore,source

def verify_source_default(builds):
    restore,source=verify_matrix(builds)
    folder=OUT/'source-default';build=load(folder/'manifest.json')
    assert build['Variant']=='source-default' and build['Exit']==build['Errors']==build['Warnings']==0
    assert build['SourceDigest']==build['MatrixSourceDigest']==source['Digest']
    assert build['MatrixConfigurationsVerified']==14
    for key in ['ProjectNeverWritten','ProductionUnchanged','OriginalMatrixMetadataUnchanged',
                'SourceDefaultsPreprocessed','FormalOutputsMatchArchive']:
        assert build[key] is True,('source-default',key)
    assert build['ProjectSha256']==restore['ProjectSha256'] and build['RteSha256']==restore['RteSha256']
    verify_hashes(folder,build['Hashes'])
    required={'RailInspectionCar_V1.'+extension for extension in ['map','htm','hex','axf']}
    required.update(['keil-rebuild.log','project.uvprojx.snapshot','RTE_Components.h.snapshot','configuration.json',
        'memory-stack.log','matrix-metadata-sha256.json','source-default-probe.c','source-default-probe.i',
        'preprocess-arguments.json','object-macro-evidence.json','source-switch-definition-sites.json'])
    assert required<=build['Hashes'].keys(),('source-default','missing hashed evidence')
    assert digest(ROOT/'MDK-ARM/RailInspectionCar_V1.uvprojx')==digest(folder/'project.uvprojx.snapshot')==restore['ProjectSha256']
    assert digest(ROOT/'MDK-ARM/RTE/_RailInspectionCar_V1/RTE_Components.h')==digest(folder/'RTE_Components.h.snapshot')==restore['RteSha256']
    metadata=load(folder/'matrix-metadata-sha256.json')
    assert set(metadata)=={'restore-status.json','production-source-sha256.json','Keil构建结果.json','baseline.json'}
    verify_hashes(OUT,metadata)
    source_json=json.dumps(source['Files'],ensure_ascii=False,separators=(',',':'))
    assert hashlib.sha256(source_json.encode('utf-8')).hexdigest().upper()==source['Digest'],'Source manifest digest mismatch'
    for item in source['Files']:
        assert digest(ROOT/Path(item['Path'].replace('\\','/')))==item['SHA256'],item['Path']
    config=load(folder/'configuration.json')
    assert config==build['Configuration'] and config['Variant']=='source-default'
    assert config['Profile']==[1,0,0,0,0] and config['Defaults']==DEFAULTS
    assert config['Defines']==['USE_HAL_DRIVER','STM32F407xx']
    assert config['ProjectWritten'] is False and config['UsesSourceDefaults'] is True
    assert config['MatrixSourceDigest']==source['Digest']
    evidence_kind='project-preprocess-fresh-object-dwarf-macros-dependencies'
    assert config['EvidenceKind']==build['EvidenceKind']==evidence_kind
    assert config['ActualObjectMacroEvidence'] is True and build['ActualObjectMacroEvidence'] is True
    assert config['ActualCompilerCommandsVerified']==build['ActualCompilerCommandsVerified']==0
    assert config['ActualCompilerCommandsAvailability']==build['ActualCompilerCommandsAvailability']=='unavailable'
    assert config['NoUartRx'] is True and config['MinimalWorker'] is True
    assert all(config[key] is False for key in ['DiagnosticEnabled','NoLcd','DebugCli','TuneUart','ServoCli','Pulse'])
    project_xml=ET.parse(folder/'project.uvprojx.snapshot')
    assert [node.text for node in project_xml.iter('Define') if node.text]==['USE_HAL_DRIVER,STM32F407xx']
    for tag in ['Undefine','MiscControls','CustomArgument']:
        assert not [node.text for node in project_xml.iter(tag) if node.text],('project override',tag)
    directive=re.compile(r'^\s*#\s*(define|undef)\s+('+'|'.join(map(re.escape,DEFAULTS))+r')\b([^\r\n]*)',re.M)
    sites=[]
    for item in source['Files']:
        if not item['Path'].endswith(('.c','.h')):continue
        text=(ROOT/Path(item['Path'].replace('\\','/'))).read_text(encoding='utf-8-sig',errors='replace')
        sites += [dict(Name=m[2],Directive=m[1],Path=item['Path'],Expression=m[3].strip()) for m in directive.finditer(text)]
    assert sites==load(folder/'source-switch-definition-sites.json')
    for name in DEFAULTS:
        definitions=[site for site in sites if site['Name']==name]
        assert len(definitions)==1 and definitions[0]['Directive']=='define'
        assert definitions[0]['Path'].replace('\\','/')=='APP/app_config.h',('local TU override',name)
    override=re.compile(r'(?:-D|--define(?:=|\s)|-U|--undefine(?:=|\s))\s*["\']?APP_|--preinclude',re.I)
    arguments=load(folder/'preprocess-arguments.json')
    assert '-E' in arguments and not override.search(' '.join(arguments))
    probe=(folder/'source-default-probe.c').read_text(encoding='utf-8-sig')
    preprocessed=(folder/'source-default-probe.i').read_text(encoding='utf-8-sig')
    assert '#include "app_config.h"' in probe and not re.search(r'^\s*#\s*error\b|Error:',preprocessed,re.M)
    for name,value in DEFAULTS.items():
        assert re.search(r'#if\s+'+name+r'\s*!=\s*'+str(value)+r'\s*\n#error\b',probe),(name,'preprocessing assertion')
        assert re.search(r'\bsource_default_'+name+r'\[\]\s*=\s*"[^"]+"',preprocessed),(name,'preprocessed evidence')
    log=(folder/'keil-rebuild.log').read_text(encoding='utf-8-sig',errors='replace')
    assert '0 Error(s), 0 Warning(s)' in log and 'Target not created' not in log
    objects=load(folder/'object-macro-evidence.json')
    assert isinstance(objects,list) and len(objects)==build['ActualObjectMacroEvidenceCount']==3
    assert {item['Object'] for item in objects}=={'app_rtos_create.o','debug_uart.o','app_hmi_link_diag.o'}
    macro_line=re.compile(r'^\s*[0-9a-fA-F]+:\s+line\s+\d+\s+(define|undef)\s+('+'|'.join(map(re.escape,DEFAULTS))+r')\b(.*)$')
    derived=dict(APP_UART_RX_ACTIVE='(APP_DEBUG_CLI_ENABLED || APP_TUNE_UART_ENABLED)',
        APP_CLI_SERVO_ACTIVE='(APP_DEBUG_CLI_ENABLED && APP_SERVO_CLI_DEBUG_ENABLED)')
    for item in objects:
        assert all(item[key] is True for key in ['DependsOnAppConfig','FreshObjectAndDependency',
            'RebuildCompilationRecorded','ActualObjectMacroEvidence','DefaultValuesVerified'])
        for key,hash_key in [('Object','ObjectSha256'),('Dependency','DependencySha256'),('Trace','TraceSha256')]:
            name=item[key];assert Path(name).name==name
            path=folder/'object-macro-evidence'/name
            assert digest(path)==item[hash_key]==build['Hashes']['object-macro-evidence/'+name]
        stem=Path(item['Object']).stem
        assert re.search(r'^compiling '+re.escape(stem)+r'\.c\.\.\.\s*$',log,re.M)
        dependencies=(folder/'object-macro-evidence'/item['Dependency']).read_text(encoding='utf-8-sig',errors='replace')
        assert re.search(r':[^\r\n]*[\\/]APP[\\/]app_config\.h\s*$',dependencies,re.M|re.I)
        trace=(folder/'object-macro-evidence'/item['Trace']).read_text(encoding='utf-8-sig').splitlines()
        records=[]
        for line in trace:
            match=macro_line.fullmatch(line)
            assert match,('Unexpected text in selected macro trace',line)
            records.append(dict(Name=match[2],Directive=match[1],Expression=match[3].strip()))
        assert records==item['MacroRecords'] and len(records)==14
        assert {record['Name'] for record in records}==set(DEFAULTS)
        for record in records:
            name=record['Name'];expression=record['Expression']
            assert record['Directive']=='define',(stem,name,'undefined or redefined')
            if name in derived:assert re.sub(r'\s','',expression)==re.sub(r'\s','',derived[name]),(stem,name)
            else:assert re.sub(r'[()\s]','',expression)==str(DEFAULTS[name])+'U',(stem,name)
    assert set(build['FormalOutputHashes'])=={'RailInspectionCar_V1.'+extension for extension in ['map','htm','hex','axf']}
    for filename,expected in build['FormalOutputHashes'].items():
        assert digest(ROOT/'MDK-ARM/RailInspectionCar_V1'/filename)==build['Hashes'][filename]==expected,('official output',filename)
    return build

def read_map(path):
    text=path.read_text(encoding='utf-8',errors='replace')
    objects={m[7]:int(m[4])+int(m[5]) for m in re.finditer(r'^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\S+\.o)\s*$',text,re.M)}
    heap=re.search(r'^\s*ucHeap\s+(0x[\da-fA-F]+)\s+Data\s+(\d+)\s+heap_4\.o\(',text,re.M)
    assert heap and int(heap[2])==49152
    base=int(heap[1],16); usable=((base+49152)&~7)-((base+7)&~7)-8
    return objects,dict(base=heap[1],array_bytes=49152,initial_usable_bytes=usable)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--strict',action='store_true');args=parser.parse_args()
    builds=load(OUT/'Keil构建结果.json')
    baseline=load(OUT/'baseline.json')
    # Older build scripts use a descriptive filename for baseline metadata.
    baseline_map=Path(baseline['Map']); base_objects,base_heap=read_map(baseline_map)
    assert digest(baseline_map)==baseline['MapSha256']
    final=None
    if (OUT/'source-default/manifest.json').exists():final=verify_source_default(builds)
    if args.strict:
        verify_matrix(builds)
        assert final is not None,'Restored-project source-default formal build is required in strict mode'
    entries=[];changes=[]
    for build in builds+([final] if final else []):
        variant=build['Variant']; folder=OUT/variant; objects,heap=read_map(folder/'RailInspectionCar_V1.map')
        assert build['Exit']==build['Errors']==build['Warnings']==0,(variant,'build status')
        verify_hashes(folder,build['Hashes'])
        margins=[int(m) for m in re.findall(r'margin=(\d+) bytes',(folder/'memory-stack.log').read_text(encoding='utf-8-sig'))]
        assert margins and min(margins)>=256
        allocations_saved=(3160 if build['Configuration']['NoUartRx'] else 0)+(3952 if build['Configuration']['NoLcd'] else 0)
        entries.append(dict(variant=variant,origin='restored-project-build' if variant=='source-default' else 'matrix',
            official_artifact=variant=='source-default',physical_ram=int(build['PhysicalRam']),static_saved=-int(build['DeltaBaselinePhysical']),
            ordinary_free=sum(build['Ram']['Regions'][key]['Free'] for key in ['RW_IRAM1','RW_IRAM2']),
            ccm_free=build['Ram']['Regions']['RW_CCM']['Free'],heap=heap,
            startup_allocation_reduction=allocations_saved,
            estimated_startup_free_increase=allocations_saved+heap['initial_usable_bytes']-base_heap['initial_usable_bytes'],
            current_heap_free=None,minimum_ever_heap_free=None,minimum_static_stack_margin=min(margins),
            hardware_acceptance='pending',hex_sha256=build['Hashes']['RailInspectionCar_V1.hex']))
        changes.append(dict(variant=variant,objects=[dict(object=obj,baseline=base_objects.get(obj,0),current=objects.get(obj,0),saved=base_objects.get(obj,0)-objects.get(obj,0))
            for obj in sorted(set(base_objects)|set(objects)) if base_objects.get(obj,0)!=objects.get(obj,0)]))
    report=dict(baseline_commit=baseline['Commit'],baseline_map_sha256=baseline['MapSha256'],baseline_physical=baseline['Ram']['PhysicalRam'],
        baseline_heap=base_heap,matrix_profile_count=len(builds),official_variant='source-default' if final else None,
        source_default_status='verified' if final else 'pending',profiles=entries,object_changes=changes,hardware_acceptance='pending')
    (OUT/'SRAM汇总.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    lines=['# SRAM正式构建汇总','',f"基线：`{baseline['Commit']}`，物理RAM {baseline['Ram']['PhysicalRam']} B。",'',
        '| 配置 | 静态RAM B | 静态释放 B | 普通SRAM余量 B | CCM余量 B | 启动分配减少 B | 最小静态栈余量 B |',
        '|---|---:|---:|---:|---:|---:|---:|']
    for row in entries:
        label=row['variant']+('（正式产物）' if row['official_artifact'] else '')
        lines.append('| '+' | '.join([label]+[str(row[k]) for k in ['physical_ram','static_saved','ordinary_free','ccm_free','startup_allocation_reduction','minimum_static_stack_margin']])+' |')
    lines += ['',('14配置矩阵独立保留；第15条source-default为恢复正式工程配置后、无附加宏覆盖的全量构建，也是当前MDK四项正式产物。'
        if final else '恢复正式工程配置后的source-default独立全量构建尚待完成，当前矩阵产物不标记为最终正式交付。'),'',
        '48KiB FreeRTOS堆与故障保留区均保持原值。启动分配减少是固定堆中的空闲增加，不能加到静态释放量。',
        '实际当前/历史最低空闲堆尚无本版实物数据，JSON以null记录；初始堆首尾对齐的8 B差异已单独计入理论启动空闲差。',
        '全部软件构建通过仍为待实物验收，响应截止时间和栈高水位不能由静态报告替代。','',
        '| 配置 | HEX SHA256 |','|---|---|']
    lines += [f"| {r['variant']} | `{r['hex_sha256']}` |" for r in entries]
    (OUT/'SRAM汇总.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print('PASS report profiles=',len(entries),'strict=',args.strict)
    for row in entries:print(row['variant'],row['physical_ram'],'saved',row['static_saved'],'heap allocation reduction',row['startup_allocation_reduction'])
if __name__=='__main__':main()
