param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
 $taskBin=Join-Path $env:TEMP 'rail_flash_gate.exe'
 & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_SYNC_STORAGE_TEST -DAPP_SYNC_GATE_PRODUCTION_TEST -ITests/rs485_mocks -ITests/mocks -IAPP -IAPP/HMI -IDevice -ICore/Inc -IProtocol -IProtocol/CAN Tests/test_flash_gate.c APP/app_sync_params.c -o $taskBin
 if($LASTEXITCODE){throw 'Production Flash gate compile failed'}
 & $taskBin
 if($LASTEXITCODE){throw 'Production Flash gate tests failed'}
} finally {Pop-Location}
