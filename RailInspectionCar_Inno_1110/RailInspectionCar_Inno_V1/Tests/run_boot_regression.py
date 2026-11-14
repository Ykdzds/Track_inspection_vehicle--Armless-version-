"""Run the startup fix and existing production regressions, saving build evidence."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import time


def main():
    root = Path(__file__).resolve().parents[1]
    spec = importlib.util.spec_from_file_location("link5_runners", root / "Tests/run_link5_regression.py")
    link5 = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(link5)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", nargs="+")
    parser.add_argument("--compiler", default="D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe")
    args = parser.parse_args()
    runners = list(dict.fromkeys(link5.RUNNERS + [
        "Tests/run_cold_startup_tests.ps1", "Tests/run_startup_timing_tests.ps1",
        "Tests/run_startup_first_tests.ps1",
        "Tests/run_board_key_startup_tests.ps1", "Tests/run_position_projection_tests.ps1",
        "Tests/run_disabled_local_feature_tests.ps1", "Tests/run_cli_profile_tests.ps1",
        "Tests/run_cli_session_tests.ps1", "Tests/run_debug_uart_tests.ps1",
        "Tests/run_tune_tests.ps1", "Tests/run_tune_diagnostics_tests.ps1",
        "Tests/run_sync_storage_tests.ps1", "Tests/run_current_diagnostic_tests.ps1",
        "Tests/run_safety_stop_tests.ps1", "Tests/run_ramp_driver_tests.ps1",
        "Tests/run_disabled_diag_tests.ps1", "Tests/run_communication_key_tests.ps1",
        "Tests/run_can_k5_recovery_tests.ps1", "Tests/test_link5_log_analyzer.py",
    ]))
    chosen = args.only or runners
    for runner in chosen:
        if runner not in runners:
            parser.error("unknown runner: " + runner)
    out = root / "Docs/F407_BOOT_20261108/验证/regression"
    out.mkdir(parents=True, exist_ok=True)
    summary = out / "summary.json"
    results = json.loads(summary.read_text(encoding="utf-8")) if summary.exists() else []
    for runner in chosen:
        path = root / runner
        if not path.is_file():
            parser.error("missing runner: " + runner)
        command = (["pwsh", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(path)]
                   if path.suffix == ".ps1" else ["python", "-B", str(path)])
        if path.suffix == ".ps1" and "$Compiler" in path.read_text(encoding="utf-8-sig"):
            command += ["-Compiler", args.compiler]
        start = time.monotonic()
        result = subprocess.run(command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        log = out / (path.stem + ".log")
        log.write_bytes(result.stdout)
        row = dict(runner=runner, exit=result.returncode,
                   seconds=round(time.monotonic() - start, 3), log=str(log.relative_to(root)))
        results = [item for item in results if item["runner"] != runner] + [row]
        summary.write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
        print(("PASS " if result.returncode == 0 else "FAIL ") + runner, flush=True)
        if result.returncode:
            print(result.stdout.decode("utf-8", errors="replace")[-5000:], flush=True)
    failed = [row for row in results if row["exit"]]
    print("SUITES", len(results), "FAILED", len(failed), flush=True)
    return int(bool(failed))


if __name__ == "__main__":
    raise SystemExit(main())
