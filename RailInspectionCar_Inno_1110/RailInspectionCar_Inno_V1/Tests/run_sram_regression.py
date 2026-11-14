"""Run production-path SRAM regressions, retaining evidence for repeated suites."""
import argparse, importlib.util, json, subprocess, time
from pathlib import Path

def main():
    root=Path(__file__).resolve().parents[1]
    spec=importlib.util.spec_from_file_location('link5',root/'Tests/run_link5_regression.py')
    link5=importlib.util.module_from_spec(spec); spec.loader.exec_module(link5)
    parser=argparse.ArgumentParser()
    parser.add_argument('--compiler',default='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
    parser.add_argument('--only',nargs='+'); parser.add_argument('--retry-failed',action='store_true')
    args=parser.parse_args()
    out=root/'Docs/F407_SRAM_20261108/验证/regression'; out.mkdir(parents=True,exist_ok=True)
    all_runners=link5.RUNNERS+['Tests/run_position_projection_tests.ps1','Tests/run_disabled_local_feature_tests.ps1',
        'Tests/run_cli_profile_tests.ps1','Tests/run_cli_session_tests.ps1','Tests/run_debug_uart_tests.ps1',
        'Tests/run_tune_tests.ps1','Tests/run_tune_diagnostics_tests.ps1','Tests/run_sync_storage_tests.ps1',
        'Tests/run_current_diagnostic_tests.ps1','Tests/run_safety_stop_tests.ps1','Tests/run_ramp_driver_tests.ps1',
        'Tests/run_disabled_diag_tests.ps1','Tests/test_link5_log_analyzer.py']
    ledger=out/'summary.json'
    entries=json.loads(ledger.read_text(encoding='utf-8-sig')) if ledger.exists() else []
    runners=args.only or ([e['runner'] for e in entries if e['exit']] if args.retry_failed else all_runners)
    for runner in runners:
        assert runner in all_runners,runner
        path=root/runner
        command=['pwsh','-NoProfile','-ExecutionPolicy','Bypass','-File',str(path)] if path.suffix=='.ps1' else ['python','-B',str(path)]
        if path.suffix=='.ps1' and '$Compiler' in path.read_text(encoding='utf-8-sig'):command+=['-Compiler',args.compiler]
        old=next((e for e in entries if e['runner']==runner),None)
        history=list(old.get('previous_attempts',[])) if old else []
        log=out/(path.stem+'.log')
        if old and log.exists():
            archived=out/(path.stem+'.attempt'+str(len(history)+1)+'.log')
            log.replace(archived)
            history.append(dict(exit=old['exit'],seconds=old.get('seconds'),log=str(archived.relative_to(root))))
        start=time.monotonic();result=subprocess.run(command,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        log.write_bytes(result.stdout)
        row=dict(runner=runner,exit=result.returncode,seconds=round(time.monotonic()-start,3),log=str(log.relative_to(root)))
        if history:row['previous_attempts']=history
        if old:entries[entries.index(old)]=row
        else:entries.append(row)
        print(('PASS ' if not result.returncode else 'FAIL ')+runner,flush=True)
        if result.returncode:print(result.stdout.decode('utf-8',errors='replace')[-4500:],flush=True)
        ledger.write_text(json.dumps(entries,ensure_ascii=False,indent=2),encoding='utf-8')
    failures=sum(row['exit']!=0 for row in entries)
    print('SUITES',len(entries),'FAILED',failures,flush=True)
    return int(failures!=0)
if __name__=='__main__':raise SystemExit(main())
