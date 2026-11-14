param(
    [string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $env:TEMP "rail_k5_repeat_tests.exe"
Push-Location $root
try {
    $common = @("-std=c99", "-Wall", "-Wextra", "-Werror", "-DTEST_COMMUNICATION_KEY_ONLY=1",
        "-DAPP_CAN_CONTROL_ENABLED=0", "-DAPP_CAN_STOP_PARAMETERS_APPROVED=0",
        "-ITests/mocks", "-IAPP", "-IAPP/HMI", "-IDevice", "-IBSP/SERVO_PULSE", "-IProtocol",
        "Tests/test_remote_servo.c", "APP/app_remote_servo.c", "APP/app_remote_servo_sync.c", "APP/app_dual_axis_motion.c", "APP/app_sync_params.c",
        "APP/app_config.c", "APP/HMI/app_control_arbiter.c", "-o", $binary)
    function Invoke-RemoteCase([string[]]$Defines, [string]$Name) {
        & $Compiler @common @Defines
        if ($LASTEXITCODE -ne 0) { throw "K5专项编译失败：$Name" }
        & $binary
        if ($LASTEXITCODE -ne 0) { throw "K5专项失败：$Name" }
        Write-Output "PASS K5 matrix: $Name"
    }
    foreach ($axis in @(1, 2, 3)) {
        foreach ($sync in @(0, 1)) {
            foreach ($hold in @(0, 1)) {
                Invoke-RemoteCase @("-DAPP_SERVO_QUERY_MODE=$axis", "-DAPP_SERVO_CONTROL_AXIS=$axis",
                    "-DAPP_REMOTE_SERVO_SYNC_ENABLED=$sync", "-DAPP_SERVO_STARTUP_HOLD_ENABLED=$hold") `
                    "axis=$axis sync=$sync hold=$hold"
            }
            Invoke-RemoteCase @("-DAPP_SERVO_QUERY_MODE=$axis", "-DAPP_SERVO_CONTROL_AXIS=$axis",
                "-DAPP_REMOTE_SERVO_SYNC_ENABLED=$sync", "-DAPP_SERVO_READONLY_TEST=1") `
                "axis=$axis sync=$sync readonly=1"
        }
    }
    foreach ($pair in @(@("0.5f", "1.0f"), @("1.0f", "0.5f"))) {
        Invoke-RemoteCase @("-DAPP_SERVO_QUERY_MODE=3", "-DAPP_SERVO_CONTROL_AXIS=3",
            "-DAPP_REMOTE_SERVO_LINK_STOP_DECEL_MPS2=$($pair[0])",
            "-DAPP_REMOTE_SERVO_K5_STOP_DECEL_MPS2=$($pair[1])") `
            "link=$($pair[0]) K5=$($pair[1])"
    }
}
finally { Pop-Location }
