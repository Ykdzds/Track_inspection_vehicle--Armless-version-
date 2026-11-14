param([string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe")
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    foreach ($axis in @(1,2,3)) {
        foreach ($sync in @(0,1)) {
            $binary = Join-Path $env:TEMP "rail_ramp_remote_${axis}_${sync}.exe"
            & $Compiler -std=c99 -Wall -Wextra -Werror "-DAPP_SERVO_QUERY_MODE=$axis" "-DAPP_SERVO_CONTROL_AXIS=$axis" `
                "-DAPP_REMOTE_SERVO_SYNC_ENABLED=$sync" -DAPP_SERVO_READONLY_TEST=0 -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
                -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IBSP/SERVO_PULSE `
                Tests/test_ramp_remote.c APP/app_remote_servo.c APP/app_remote_servo_sync.c APP/HMI/app_control_arbiter.c APP/app_config.c -lm -o $binary
            if ($LASTEXITCODE) {throw 'Remote timing compile failed'}
            & $binary
            if ($LASTEXITCODE) {throw 'Remote timing failed'}
        }
        $binary = Join-Path $env:TEMP "rail_ramp_can_${axis}.exe"
        & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_CAN_CONTROL_ENABLED=1 -DAPP_CAN_STOP_PARAMETERS_APPROVED=1 `
            "-DAPP_SERVO_QUERY_MODE=$axis" "-DAPP_SERVO_CONTROL_AXIS=$axis" -DAPP_SERVO_READONLY_TEST=0 `
            -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IProtocol/CAN `
            Tests/test_ramp_can.c APP/app_can_control.c APP/app_remote_servo_sync.c APP/HMI/app_control_arbiter.c Protocol/CAN/can_protocol.c -lm -o $binary
        if ($LASTEXITCODE) {throw 'CAN timing compile failed'}
        & $binary
        if ($LASTEXITCODE) {throw 'CAN timing failed'}
    }
} finally {Pop-Location}
