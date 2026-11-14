param([string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe")
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$binary=Join-Path $env:TEMP 'rail_link_can_diag_tests.exe'
Push-Location $root
try {
    & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_LINK_DIAG_TEST=1 -DAPP_CAN_SERVO_DIAG_ENABLED=1 `
        -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IProtocol/CAN `
        Tests/test_can_control.c APP/app_can_control.c APP/app_remote_servo_sync.c APP/app_dual_axis_motion.c `
        APP/app_sync_params.c APP/HMI/app_control_arbiter.c Protocol/CAN/can_protocol.c -lm -o $binary
    if($LASTEXITCODE -ne 0){throw 'CAN servo diagnostic production-path compile failed'}
    & $binary
    if($LASTEXITCODE -ne 0){throw 'CAN servo diagnostic production-path test failed'}
    & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_LINK_DIAG_TEST=1 -DAPP_CAN_ARM_DIAG_ENABLED=1 `
        -IAPP -IProtocol/CAN Tests/test_arm3.c APP/app_arm3.c Protocol/CAN/can_arm3.c Protocol/CAN/can_protocol.c -o $binary
    if($LASTEXITCODE -ne 0){throw 'CAN arm diagnostic production-path compile failed'}
    & $binary
    if($LASTEXITCODE -ne 0){throw 'CAN arm diagnostic production-path test failed'}
    & $Compiler -std=c99 -Wall -Wextra -Werror -ffunction-sections -fdata-sections `
        -DAPP_LINK_DIAG_TEST=1 -DAPP_CAN_SERVO_DIAG_ENABLED=1 -DAPP_CAN_ARM_DIAG_ENABLED=1 -DAPP_CAN_DEBUG_ENABLED=1 `
        -DportTICK_PERIOD_MS=1U -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
        -ITests/can_mocks -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IProtocol/CAN -IBSP/CAN -IBSP/DEBUG_UART -IBSP/NRF24L01 `
        Tests/test_can_app.c APP/app_can_status.c APP/app_can_config.c APP/app_can_control.c APP/app_dual_axis_motion.c `
        APP/app_sync_params.c APP/app_remote_servo_sync.c APP/app_arm3.c APP/app_arm3_inputs.c APP/HMI/app_hmi_arm3.c `
        Protocol/CAN/can_arm3.c Protocol/CAN/can_protocol.c Protocol/CAN/can_link.c Protocol/CAN/can_upper.c Protocol/CAN/can_arm.c `
        '-Wl,--gc-sections' -lm -o $binary
    if($LASTEXITCODE -ne 0){throw 'Shared CAN raw/servo/arm diagnostic compile failed'}
    & $binary
    if($LASTEXITCODE -ne 0){throw 'Shared CAN raw/servo/arm diagnostic test failed'}
} finally {Pop-Location}
