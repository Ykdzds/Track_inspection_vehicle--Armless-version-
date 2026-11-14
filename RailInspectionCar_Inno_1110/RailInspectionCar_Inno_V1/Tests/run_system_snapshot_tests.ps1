param([string]$Compiler = 'D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $env:TEMP 'rail_system_snapshot_tests.exe'
Push-Location $root
try {
    & $Compiler -std=c99 -Wall -Wextra -Werror -DportTICK_PERIOD_MS=1U `
        -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
        -ITests/hmi_mocks -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol `
        -IProtocol/CAN -IProtocol/HMI -IBSP/CAN -IBSP/HMI_UART -IBSP/DEBUG_UART -IBSP/SYSTEM `
        Tests/test_system_snapshot.c APP/HMI/app_hmi_state.c Protocol/HMI/hmi_modbus_slave.c -o $binary
    if ($LASTEXITCODE -ne 0) { throw '统一快照测试编译失败' }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw '统一快照测试失败' }
} finally { Pop-Location }
