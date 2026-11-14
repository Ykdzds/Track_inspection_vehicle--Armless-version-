param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $binary=Join-Path $env:TEMP 'rail_runtime_metrics_tests.exe'
    & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_RUNTIME_METRICS_TEST -ITests/can_mocks -IAPP Tests/test_runtime_metrics.c APP/app_runtime_metrics.c -o $binary
    if($LASTEXITCODE){throw 'runtime metrics compile failed'}
    & $binary
    if($LASTEXITCODE){throw 'runtime metrics tests failed'}
} finally {Pop-Location}
