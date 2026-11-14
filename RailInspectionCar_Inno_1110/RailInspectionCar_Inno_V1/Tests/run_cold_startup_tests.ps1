param([string]$Compiler = 'D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $env:TEMP 'rail_cold_startup_tests.exe'
Push-Location $root
try {
    & $Compiler -std=c99 -Wall -Wextra -Werror -ffunction-sections -fdata-sections '-Wl,--gc-sections' `
        -DAPP_CAN_ENABLED=0 -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
        -ITests/mocks -IAPP -IDevice -IProtocol -IBSP/RS485 -IBSP/SERVO_PULSE -IBSP/SYSTEM -IBSP/LED -ICore/Inc `
        Tests/test_cold_startup.c APP/app_health.c APP/app_config.c APP/motion_control.c `
        BSP/SERVO_PULSE/servo_pulse.c Protocol/modbus_rtu.c -lm -o $binary
    if ($LASTEXITCODE -ne 0) { throw '冷启动联合测试编译失败' }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw '冷启动联合测试失败' }
} finally { Pop-Location }
