param(
    [string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $env:TEMP "rail_hmi_control_tests.exe"
Push-Location $root
try {
    foreach ($axis in @(1, 2, 3)) {
    foreach ($enabled in @(0,1)) {
    foreach ($readonly in @(0,1)) {
        $args = @(
            "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-DAPP_HMI_CONTROL_ENABLED=$enabled", "-DAPP_SERVO_READONLY_TEST=$readonly",
            "-DAPP_CAN_CONTROL_ENABLED=0", "-DAPP_CAN_STOP_PARAMETERS_APPROVED=0",
            "-DAPP_SERVO_QUERY_MODE=$axis", "-DAPP_SERVO_CONTROL_AXIS=$axis",
            "-ITests/mocks", "-IAPP", "-IAPP/HMI", "-IDevice", "-IProtocol",
            "Tests/test_hmi_control.c",
            "APP/HMI/app_hmi_control.c",
            "APP/HMI/app_control_arbiter.c",
            "APP/app_remote_servo_sync.c",
            "-o", $binary
        )
        & $Compiler @args
        if ($LASTEXITCODE -ne 0) { throw "HMI控制状态机主机测试编译失败：轴配置=$axis" }
        & $binary
        if ($LASTEXITCODE -ne 0) { throw "HMI控制状态机主机测试失败：轴配置=$axis" }
        Write-Output "PASS HMI control host test: axis=$axis enabled=$enabled readonly=$readonly"
    }
    }
    }
}
finally { Pop-Location }
