param([string]$Compiler = 'D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    foreach($enabled in @(0,1)) {
    $binary=Join-Path $env:TEMP "rail_startup_first_$enabled.exe"
    & $Compiler -std=c99 -Wall -Wextra -Werror -ffunction-sections -fdata-sections '-Wl,--gc-sections' `
        -DAPP_LINK_DIAG_TEST=1 "-DAPP_HMI_LINK_DIAG_ENABLED=$enabled" -DAPP_SERVO_RS485_DIAG_ENABLED=0 `
        -DAPP_CAN_ENABLED=0 -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
        -ITests/mocks -IAPP -IDevice -IProtocol -IBSP/RS485 -IBSP/SERVO_PULSE -IBSP/SYSTEM -IBSP/LED -ICore/Inc `
        Tests/test_startup_first.c APP/app_health.c APP/app_config.c APP/motion_control.c `
        BSP/SERVO_PULSE/servo_pulse.c Protocol/modbus_rtu.c -lm -o $binary
    if($LASTEXITCODE){throw 'Startup first-cause fixture compile failed'}
    & $binary
    if($LASTEXITCODE){throw 'Startup first-cause fields regression failed'}
    }
} finally {Pop-Location}
