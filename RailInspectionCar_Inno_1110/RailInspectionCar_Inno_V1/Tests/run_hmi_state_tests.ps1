param([string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $env:TEMP "rail_hmi_state_tests.exe"
Push-Location $root
try {
    & $Compiler -std=c99 -Wall -Wextra -Werror -DportTICK_PERIOD_MS=1U         -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IProtocol/CAN -IBSP/CAN -IBSP/HMI_UART -IBSP/DEBUG_UART         Tests/test_hmi_state.c APP/HMI/app_hmi_state.c -o $binary
    if ($LASTEXITCODE -ne 0) { throw "HMI状态测试编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "HMI状态测试失败" }
    foreach ($can in @(0,1)) {
        & $Compiler -std=c99 -Wall -Wextra -Werror "-DAPP_CAN_ENABLED=$can" -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0             -IAPP -IProtocol/CAN -IBSP/CAN Tests/test_boot_identity.c             Protocol/CAN/can_identity.c Protocol/CAN/can_protocol.c -o $binary
        if ($LASTEXITCODE -ne 0) { throw "统一启动身份编译失败" }
        & $binary
        if ($LASTEXITCODE -ne 0) { throw "统一启动身份测试失败" }
    }
    foreach ($enabled in @(0,1)) {
    foreach ($axis in @(1,2,3)) {
    foreach ($readonly in @(0,1)) {
        & $Compiler -std=c99 -Wall -Wextra -Werror -DportTICK_PERIOD_MS=1U -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0             "-DAPP_HMI_CONTROL_ENABLED=$enabled" "-DAPP_SERVO_CONTROL_AXIS=$axis" "-DAPP_SERVO_QUERY_MODE=$axis" "-DAPP_SERVO_READONLY_TEST=$readonly"             -ITests/hmi_mocks -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol             -IProtocol/CAN -IProtocol/HMI -IBSP/CAN -IBSP/HMI_UART -IBSP/DEBUG_UART -IBSP/SYSTEM             Tests/test_hmi_integration.c APP/HMI/app_hmi_state.c             Protocol/HMI/hmi_modbus_slave.c -o $binary
        if ($LASTEXITCODE -ne 0) { throw "HMI完整链路编译失败" }
        & $binary
        if ($LASTEXITCODE -ne 0) { throw "HMI完整链路测试失败" }
    }
    }
    }
}
finally { Pop-Location }
