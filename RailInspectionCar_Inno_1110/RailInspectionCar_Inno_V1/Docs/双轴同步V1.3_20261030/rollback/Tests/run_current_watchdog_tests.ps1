param(
    [string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $env:TEMP ("rail_watchdog_test_" + [guid]::NewGuid().ToString("N") + ".exe")
Push-Location $root
try {
    foreach ($value in @("2", "-1")) {
        $savedErrorAction = $ErrorActionPreference
        try {
            $ErrorActionPreference = "Continue"
            & $Compiler -E "-DAPP_WDG_TEST_K6_SHORT_ENABLED=$value" APP/app_config.h 2>&1 | Out-Null
        }
        finally { $ErrorActionPreference = $savedErrorAction }
        if ($LASTEXITCODE -eq 0) { throw "非法K6看门狗开关未被拒绝：$value" }
    }
    foreach ($enabled in @(0, 1)) {
        # 编译真实健康监控模块，验证喂狗入口而非复制的测试逻辑。
        & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
            "-DAPP_WDG_TEST_K6_SHORT_ENABLED=$enabled" `
            -ITests/mocks -IAPP -IDevice -IBSP/SYSTEM -IBSP/LED -IBSP/SERVO_PULSE -IProtocol -IBSP/RS485 `
            Tests/test_watchdog_health.c APP/app_health.c APP/app_config.c Protocol/modbus_rtu.c -o $binary
        if ($LASTEXITCODE -ne 0) { throw "看门狗健康模块编译失败：$enabled" }
        & $binary
        if ($LASTEXITCODE -ne 0) { throw "看门狗健康模块测试失败：$enabled" }
        foreach ($axis in @(1, 2, 3)) {
            foreach ($readonly in @(0, 1)) {
                    & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
                        "-DTEST_K6_WATCHDOG_ONLY=1" "-DAPP_WDG_TEST_K6_SHORT_ENABLED=$enabled" `
                        "-DAPP_SERVO_QUERY_MODE=$axis" "-DAPP_SERVO_CONTROL_AXIS=$axis" `
                        "-DAPP_SERVO_READONLY_TEST=$readonly" `
                        -ITests/mocks -IAPP -IAPP/HMI -IDevice -IBSP/SERVO_PULSE -IProtocol `
                        Tests/test_remote_servo.c APP/app_remote_servo.c `
                        APP/app_remote_servo_sync.c APP/app_config.c APP/HMI/app_control_arbiter.c -o $binary
                    if ($LASTEXITCODE -ne 0) { throw "K6事件编译失败：$enabled/$axis/$readonly" }
                    & $binary
                    if ($LASTEXITCODE -ne 0) { throw "K6事件测试失败：$enabled/$axis/$readonly" }
            }
            # K6开关与只读配置下完整遥控回归也必须不受影响。
            foreach ($readonly in @(0, 1)) {
            & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
                "-DAPP_WDG_TEST_K6_SHORT_ENABLED=$enabled" `
                "-DAPP_SERVO_QUERY_MODE=$axis" "-DAPP_SERVO_CONTROL_AXIS=$axis" `
                "-DAPP_SERVO_READONLY_TEST=$readonly" `
                -ITests/mocks -IAPP -IAPP/HMI -IDevice -IBSP/SERVO_PULSE -IProtocol `
                Tests/test_remote_servo.c APP/app_remote_servo.c `
                APP/app_remote_servo_sync.c APP/app_config.c APP/HMI/app_control_arbiter.c -o $binary
            if ($LASTEXITCODE -ne 0) { throw "K6配置遥控回归编译失败" }
            & $binary
            if ($LASTEXITCODE -ne 0) { throw "K6配置遥控回归失败" }
            }
        }
    }
}
finally { Pop-Location }
