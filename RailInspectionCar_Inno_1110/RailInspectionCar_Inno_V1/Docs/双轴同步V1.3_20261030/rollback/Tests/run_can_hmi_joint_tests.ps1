param([string]$Compiler="D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe")
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$binary=Join-Path $env:TEMP 'rail_can_hmi_joint.exe'
Push-Location $root
try {
    foreach($axis in @(1,2,3)) {
        & $Compiler -std=c99 -Wall -Wextra -Werror -DportTICK_PERIOD_MS=1U `
            -DAPP_CAN_CONTROL_ENABLED=1 -DAPP_CAN_STOP_PARAMETERS_APPROVED=1 `
            -DAPP_SERVO_READONLY_TEST=0 "-DAPP_SERVO_QUERY_MODE=$axis" "-DAPP_SERVO_CONTROL_AXIS=$axis" `
            -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IProtocol/CAN `
            -IBSP/SERVO_PULSE -IBSP/CAN -IBSP/DEBUG_UART `
            Tests/test_can_hmi_joint.c APP/app_remote_servo.c APP/HMI/app_hmi_control.c `
            APP/HMI/app_control_arbiter.c APP/app_remote_servo_sync.c APP/app_can_control.c `
            APP/app_can_status.c APP/app_can_config.c Protocol/CAN/can_protocol.c -lm -o $binary
        if($LASTEXITCODE){throw "CAN/HMI/遥控联合编译失败 axis=$axis"}
        & $binary
        if($LASTEXITCODE){throw "CAN/HMI/遥控联合测试失败 axis=$axis"}
        Write-Output "PASS CAN/HMI/remote joint axis=$axis"
    }
} finally {Pop-Location}
