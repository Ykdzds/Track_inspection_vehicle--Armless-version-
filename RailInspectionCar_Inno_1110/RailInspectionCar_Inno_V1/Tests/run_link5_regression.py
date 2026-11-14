"""Rebuild and run the Link5 production-path host regressions (no board access)."""
import argparse, datetime, json, shutil, subprocess, time
from pathlib import Path

RUNNERS = ['Tests/run_hmi_state_window_tests.ps1', 'Tests/run_hmi_state_tests.ps1', 'Tests/run_hmi_snapshot_freshness_tests.ps1', 'Tests/run_hmi_modbus_tests.ps1', 'Tests/run_hmi_control_tests.ps1', 'Tests/run_hmi_link_diag_tests.ps1', 'Tests/run_hmi_link_wire_tests.ps1', 'Tests/run_hmi_diag_hook_tests.ps1', 'Tests/run_system_snapshot_tests.ps1', 'Tests/run_hmi_remote_joint_tests.ps1', 'Tests/run_can_hmi_joint_tests.ps1', 'Tests/run_arm3_joint_tests.ps1', 'Tests/run_lcd_recovery_tests.ps1', 'Tests/run_normal_timing_tests.ps1', 'Tests/run_runtime_metrics_tests.ps1', 'Tests/run_production_guard_tests.ps1', 'Tests/check_irq_dma_integration.ps1', 'Tests/check_usart1_dma.ps1', 'Tests/check_nrf_dma.ps1', 'Tests/run_link_diag_tests.ps1', 'Tests/run_remote_link_diag_tests.ps1', 'Tests/run_link_can_diag_tests.ps1', 'Tests/run_rs485_diag_tests.ps1', 'Tests/run_can_control_tests.ps1', 'Tests/run_can_protocol_tests.ps1', 'Tests/run_arm3_tests.ps1', 'Tests/run_arm3_port_tests.ps1', 'Tests/run_current_core_tests.ps1', 'Tests/run_current_watchdog_tests.ps1']

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--powershell', default='pwsh')
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--python', default='python')
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    out=root/'Docs/F407_Link5_20261107/验证/regression'
    out.mkdir(parents=True, exist_ok=True)
    if (out/'summary.json').exists():
        archive=out/'history'/datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
        archive.mkdir(parents=True,exist_ok=False)
        for previous in [out/'summary.json', *out.glob('*.log')]:
            shutil.copy2(previous,archive/previous.name)
    entries=[]
    for runner in RUNNERS:
        path=root/runner
        command=[args.powershell, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(path)]
        if '$Compiler' in path.read_text(encoding='utf-8-sig'):
            command += ['-Compiler',args.compiler]
        start=time.monotonic()
        result=subprocess.run(command,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        log=out/(path.stem+'.log'); log.write_bytes(result.stdout)
        entries.append(dict(runner=runner,exit=result.returncode,seconds=round(time.monotonic()-start,3),log=str(log.relative_to(root))))
        print(('PASS ' if not result.returncode else 'FAIL ')+runner,flush=True)
        if result.returncode: print(result.stdout.decode('utf-8',errors='replace')[-4000:],flush=True)
        (out/'summary.json').write_text(json.dumps(entries,ensure_ascii=False,indent=2),encoding='utf-8')
    result=subprocess.run([args.python,'-B','Tests/test_link5_log_analyzer.py'],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (out/'log-analyzer.log').write_bytes(result.stdout)
    entries.append(dict(runner='python -B Tests/test_link5_log_analyzer.py',exit=result.returncode,log=str((out/'log-analyzer.log').relative_to(root))))
    (out/'summary.json').write_text(json.dumps(entries,ensure_ascii=False,indent=2),encoding='utf-8')
    failures=sum(row['exit']!=0 for row in entries)
    print('SUITES',len(entries),'FAILED',failures,flush=True)
    return int(failures!=0)

if __name__=='__main__':
    raise SystemExit(main())
