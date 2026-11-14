param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $binary=Join-Path $env:TEMP 'rail_tune_diagnostics_tests.exe'
    & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_TUNE_UART_ENABLED=1 -ITests/can_mocks -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IProtocol/CAN -IBSP/SYSTEM -IBSP/RS485 -IBSP/DEBUG_UART Tests/test_tune_diagnostics.c APP/app_tune_diagnostics.c -o $binary
    if($LASTEXITCODE){throw 'diagnostic mapping compile failed'}
    & $binary
    if($LASTEXITCODE){throw 'diagnostic mapping tests failed'}
} finally {Pop-Location}
