param(
    [string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $env:TEMP "rail_motion_ramp_tests.exe"
Push-Location $root
try {
    & $Compiler -std=c99 -Wall -Wextra -Werror `
        -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol `
        Tests/test_motion_ramp.c -lm -o $binary
    if ($LASTEXITCODE -ne 0) { throw "共享斜坡主机测试编译失败" }
    foreach ($invalid in @('0', '65536')) {
        $result = & $Compiler -E "-DAPP_SERVO_DRIVER_ACCEL_GUARD_MS=$invalid" APP/app_config.h 2>&1
        if ($LASTEXITCODE -eq 0) { throw "Invalid acceleration guard accepted: $invalid" }
    }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "共享斜坡主机测试失败" }
} finally {
    Pop-Location
}
