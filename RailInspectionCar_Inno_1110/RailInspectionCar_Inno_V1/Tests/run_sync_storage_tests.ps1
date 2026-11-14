param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
 $binary=Join-Path $env:TEMP 'rail_sync_storage.exe'
 & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_SYNC_STORAGE_TEST -ITests/mocks -IAPP Tests/test_sync_storage.c APP/app_sync_params.c -o $binary
 if($LASTEXITCODE){throw 'storage compile failed'}
 & $binary
 if($LASTEXITCODE){throw 'storage tests failed'}
 & $Compiler -std=c99 -Wall -Wextra -Werror -IAPP -ITests/mocks -IAPP/HMI Tests/test_parameter_lock.c APP/HMI/app_control_arbiter.c -o $binary
 if($LASTEXITCODE){throw 'parameter lock compile failed'}
 & $binary
 if($LASTEXITCODE){throw 'parameter lock test failed'}
 & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_TUNE_UART_ENABLED=1 -DAPP_SYNC_STORAGE_TEST -ITests/rs485_mocks -ITests/mocks -IAPP -IDevice -IProtocol -IProtocol/CAN -IBSP/DEBUG_UART -ICore/Inc Tests/test_tune.c APP/app_tune_cli.c APP/app_sync_params.c APP/app_dual_axis_motion.c APP/app_remote_servo_sync.c -lm -o $binary
 if($LASTEXITCODE){throw 'Flash wire compile failed'}
 & $binary
 if($LASTEXITCODE){throw 'Flash wire test failed'}
} finally {Pop-Location}
