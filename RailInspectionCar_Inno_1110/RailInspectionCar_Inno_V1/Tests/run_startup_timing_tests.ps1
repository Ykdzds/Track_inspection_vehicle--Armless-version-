param([string]$Compiler = 'D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    foreach ($wire in @(0,6,8,10)) {
        $binary = Join-Path $env:TEMP "rail_startup_timing_${wire}.exe"
        & $Compiler -std=c99 -Wall -Wextra -Werror -ffunction-sections -fdata-sections '-Wl,--gc-sections' `
            -DAPP_HMI_LINK_DIAG_ENABLED=0 -DAPP_SERVO_RS485_DIAG_ENABLED=0 `
            -DAPP_CAN_ENABLED=0 -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
            "-DTEST_POLL_WIRE_DELAY_MS=$wire" `
            -ITests/mocks -IAPP -IDevice -IProtocol -IBSP/RS485 -IBSP/SERVO_PULSE -IBSP/SYSTEM -IBSP/LED -ICore/Inc `
            Tests/test_startup_timing.c APP/app_health.c APP/app_config.c APP/motion_control.c `
            BSP/SERVO_PULSE/servo_pulse.c Protocol/modbus_rtu.c -lm -o $binary
        if ($LASTEXITCODE -ne 0) { throw "Startup timing build failed: $wire ms" }
        & $binary
        if ($LASTEXITCODE -ne 0) { throw "Startup timing regression failed: $wire ms" }
    }
} finally { Pop-Location }





