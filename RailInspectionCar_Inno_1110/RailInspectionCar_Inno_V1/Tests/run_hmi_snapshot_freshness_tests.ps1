param([string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $env:TEMP "rail_hmi_snapshot_freshness_tests.exe"
Push-Location $root
try {
    foreach ($diag in @(0,1)) {
        & $Compiler -std=c99 -Wall -Wextra -Werror -DportTICK_PERIOD_MS=1U `
            -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
            "-DAPP_HMI_LINK_DIAG_ENABLED=$diag" `
            -ITests -ITests/hmi_mocks -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol `
            -IProtocol/CAN -IProtocol/HMI -IBSP/CAN -IBSP/HMI_UART -IBSP/DEBUG_UART -IBSP/SYSTEM `
            Tests/test_hmi_snapshot_freshness.c APP/HMI/app_hmi_state.c `
            Protocol/HMI/hmi_modbus_slave.c -o $binary
        if ($LASTEXITCODE -ne 0) { throw "HMI快照期限测试编译失败" }
        & $binary
        if ($LASTEXITCODE -ne 0) { throw "HMI快照期限测试失败" }
    }
}
finally { Pop-Location }
