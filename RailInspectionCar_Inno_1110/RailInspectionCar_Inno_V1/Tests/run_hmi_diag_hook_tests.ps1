param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $binary=Join-Path $env:TEMP 'rail_hmi_diag_hook_tests.exe'
    & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_HMI_LINK_DIAG_TEST=1 -DAPP_HMI_LINK_DIAG_ENABLED=1 -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 -DAPP_SERVO_QUERY_MODE=3 -DAPP_SERVO_CONTROL_AXIS=3 -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol Tests/test_hmi_diag_hooks.c APP/HMI/app_hmi_control.c APP/HMI/app_control_arbiter.c APP/app_remote_servo_sync.c APP/app_dual_axis_motion.c APP/app_sync_params.c -o $binary
    if($LASTEXITCODE){throw 'HMI diagnostic hook compile failed'}
    & $binary
    if($LASTEXITCODE){throw 'HMI diagnostic hook tests failed'}
} finally {Pop-Location}
