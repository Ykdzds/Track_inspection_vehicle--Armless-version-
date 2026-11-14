param([string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe")
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $env:TEMP 'rail_can_control_tests.exe'
Push-Location $root
try {
    foreach ($axis in @(1,2,3)) {
        foreach ($control in @(0,1)) {
        & $Compiler -std=c99 -Wall -Wextra -Werror "-DAPP_CAN_CONTROL_ENABLED=$control" "-DAPP_CAN_STOP_PARAMETERS_APPROVED=$control" "-DAPP_SERVO_QUERY_MODE=$axis" "-DAPP_SERVO_CONTROL_AXIS=$axis" -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IProtocol/CAN Tests/test_can_control.c APP/app_can_control.c APP/app_remote_servo_sync.c APP/app_dual_axis_motion.c APP/app_sync_params.c APP/HMI/app_control_arbiter.c Protocol/CAN/can_protocol.c -lm -o $binary
        if ($LASTEXITCODE -ne 0) { throw "CAN控制主机编译失败：$axis" }
        & $binary
        if ($LASTEXITCODE -ne 0) { throw "CAN控制主机测试失败：$axis" }
        }
    }
    foreach ($axis in @(1,2,3)) {
        & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_CAN_CONTROL_ENABLED=1 -DAPP_CAN_STOP_PARAMETERS_APPROVED=1 -DTEST_CAN_MECHANICS_VARIANT=1 "-DAPP_SERVO_QUERY_MODE=$axis" "-DAPP_SERVO_CONTROL_AXIS=$axis" -include Tests/mocks/can_mechanics_override.h -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IProtocol/CAN Tests/test_can_control.c APP/app_can_control.c APP/app_remote_servo_sync.c APP/app_dual_axis_motion.c APP/app_sync_params.c APP/HMI/app_control_arbiter.c Protocol/CAN/can_protocol.c -lm -o $binary
        if ($LASTEXITCODE -ne 0) { throw "异机械参数CAN编译失败：$axis" }
        & $binary
        if ($LASTEXITCODE -ne 0) { throw "异机械参数CAN测试失败：$axis" }
    }
    foreach ($defines in @(
        @('-DAPP_CAN_CONTROL_ENABLED=1','-DAPP_CAN_STOP_PARAMETERS_APPROVED=1','-DAPP_CAN_ENABLED=0'),
        @('-DAPP_CAN_CONTROL_ENABLED=1','-DAPP_CAN_STOP_PARAMETERS_APPROVED=0'),
        @('-DAPP_CAN_CONTROL_ENABLED=1','-DAPP_CAN_STOP_PARAMETERS_APPROVED=1','-DAPP_SERVO_READONLY_TEST=1'),
        @('-DAPP_CAN_CONTROL_ENABLED=2'), @('-DAPP_CAN_SPEED_TIMEOUT_MS=301'),
        @('-DAPP_CAN_STOP_HOLD_ENABLED=0'), @('-DAPP_CAN_QUICK_STOP_ENABLED=2'))) {
        $saved = $ErrorActionPreference
        try { $ErrorActionPreference = 'Continue'; & $Compiler -E @defines APP/app_config.h 2>&1 | Out-Null }
        finally { $ErrorActionPreference = $saved }
        if ($LASTEXITCODE -eq 0) { throw ('非法CAN运动配置未拒绝：' + ($defines -join ' ')) }
    }
    Write-Output 'PASS CAN illegal configurations rejected'
    $global:LASTEXITCODE = 0 # Expected negative-preprocessor probes must not mark a passing run failed.
} finally { Pop-Location }
