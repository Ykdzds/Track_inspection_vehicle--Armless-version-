param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $binary=Join-Path $env:TEMP 'rail_tune_tests.exe'
    & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_TUNE_UART_ENABLED=1 -ITests/rs485_mocks -ITests/mocks -IAPP -IDevice -IProtocol -IProtocol/CAN -IBSP/DEBUG_UART -ICore/Inc Tests/test_tune.c APP/app_tune_cli.c APP/app_sync_params.c APP/app_dual_axis_motion.c APP/app_remote_servo_sync.c -lm -o $binary
    if($LASTEXITCODE){throw 'tune compile failed'}
    & $binary
    if($LASTEXITCODE){throw 'tune tests failed'}
} finally { Pop-Location }
