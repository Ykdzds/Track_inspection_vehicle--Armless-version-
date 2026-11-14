param([string]$Compiler='D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$binary=Join-Path $env:TEMP 'rail_can_k5_recovery.exe'
Push-Location $root
try {
    foreach($axis in @(1,2,3)) {
        & $Compiler -std=c99 -Wall -Wextra -Werror -DportTICK_PERIOD_MS=1U `
            -DAPP_CAN_CONTROL_ENABLED=1 -DAPP_CAN_STOP_PARAMETERS_APPROVED=1 `
            -DAPP_SERVO_READONLY_TEST=0 "-DAPP_SERVO_QUERY_MODE=$axis" "-DAPP_SERVO_CONTROL_AXIS=$axis" `
            -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IProtocol/CAN `
            -IBSP/SERVO_PULSE -IBSP/CAN -IBSP/DEBUG_UART `
            Tests/test_can_k5_recovery.c APP/app_remote_servo.c APP/HMI/app_hmi_control.c `
            APP/HMI/app_control_arbiter.c APP/app_remote_servo_sync.c APP/app_dual_axis_motion.c APP/app_sync_params.c APP/app_can_control.c `
            APP/app_can_status.c APP/app_can_config.c Protocol/CAN/can_protocol.c -lm -o $binary
        if($LASTEXITCODE){throw "K5恢复编译失败 axis=$axis"}
        & $binary
        if($LASTEXITCODE){throw "K5恢复测试失败 axis=$axis"}
        Write-Output "PASS K5 recovery axis=$axis"
        $upper=Join-Path $root '..\F103_TEST\06_Upper_level_CAN_com_v1.1_low_speed_test\APP'
        & $Compiler -std=c99 -Wall -Wextra -Werror -DportTICK_PERIOD_MS=1U `
            -DAPP_CAN_CONTROL_ENABLED=1 -DAPP_CAN_STOP_PARAMETERS_APPROVED=1 `
            -DAPP_SERVO_READONLY_TEST=0 "-DAPP_SERVO_QUERY_MODE=$axis" "-DAPP_SERVO_CONTROL_AXIS=$axis" `
            -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IProtocol/CAN "-I$upper" `
            -IBSP/SERVO_PULSE -IBSP/CAN -IBSP/DEBUG_UART `
            Tests/test_can_k5_f103_joint.c APP/app_remote_servo.c APP/HMI/app_hmi_control.c `
            APP/HMI/app_control_arbiter.c APP/app_remote_servo_sync.c APP/app_dual_axis_motion.c APP/app_sync_params.c APP/app_can_control.c `
            APP/app_can_status.c APP/app_can_config.c Protocol/CAN/can_protocol.c `
            "$upper\can_upper_protocol.c" "$upper\can_upper_control.c" "$upper\can_upper_ui.c" -lm -o $binary
        if($LASTEXITCODE){throw "双端K5编译失败 axis=$axis"}
        & $binary
        if($LASTEXITCODE){throw "双端K5测试失败 axis=$axis"}
    }
} finally {Pop-Location}
