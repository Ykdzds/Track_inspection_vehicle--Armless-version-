param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $binary=Join-Path $env:TEMP 'rail_hmi_link_wire_tests.exe'
    & $Compiler -std=c99 -Wall -Wextra -Werror -DportTICK_PERIOD_MS=1U -DAPP_HMI_LINK_DIAG_TEST=1 -DAPP_HMI_LINK_DIAG_ENABLED=1 -DAPP_SERVO_RS485_DIAG_ENABLED=0 -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 -ITests/hmi_mocks -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IProtocol/CAN -IProtocol/HMI -IBSP/CAN -IBSP/HMI_UART -IBSP/DEBUG_UART -IBSP/SPI_LCD -IBSP/SYSTEM Tests/test_hmi_link_wire.c APP/HMI/app_hmi_state.c Protocol/HMI/hmi_modbus_slave.c APP/HMI/app_hmi_link_diag.c -o $binary
    if($LASTEXITCODE){throw 'HMI diagnostic wire integration compile failed'}
    & $binary
    if($LASTEXITCODE){throw 'HMI diagnostic wire integration tests failed'}
} finally {Pop-Location}
