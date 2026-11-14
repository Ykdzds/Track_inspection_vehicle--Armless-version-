param([string]$Compiler = 'D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe')
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $binary = Join-Path $env:TEMP 'rail_snapshot_transport.exe'
    & $Compiler -std=c99 -Wall -Wextra -Werror -ffunction-sections -fdata-sections `
        -DportTICK_PERIOD_MS=1U -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
        -ITests/can_mocks -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IProtocol/CAN `
        -IBSP/CAN -IBSP/DEBUG_UART -IBSP/NRF24L01 Tests/test_snapshot_transport.c `
        -I../F103_TEST/06_Upper_level_CAN_com_v1.1_low_speed_test/APP `
        ../F103_TEST/06_Upper_level_CAN_com_v1.1_low_speed_test/APP/can_upper_protocol.c `
        APP/app_can_status.c APP/app_can_config.c APP/app_can_control.c `
        Protocol/CAN/can_protocol.c Protocol/CAN/can_link.c Protocol/CAN/can_upper.c Protocol/CAN/can_arm.c `
        '-Wl,--gc-sections' -lm -o $binary
    if ($LASTEXITCODE -ne 0) { throw 'CAN快照传输预算测试编译失败' }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw 'CAN快照传输预算测试失败' }
} finally { Pop-Location }
