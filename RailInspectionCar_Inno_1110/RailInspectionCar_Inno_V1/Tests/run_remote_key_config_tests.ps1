param(
    [string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $env:TEMP "rail_remote_key_config.exe"
Push-Location $root
try {
    $validCases = @(
        @(),
        @("-DAPP_REMOTE_SERVO_K5_STOP_DECEL_MPS2=0.8f", "-DTEST_EXPECT_K5_DECEL=0.8f"),
        @("-DAPP_REMOTE_SERVO_K1_STOP_DECEL_MPS2=0.8f", "-DTEST_EXPECT_K5_DECEL=0.8f"),
        @("-DAPP_WDG_TEST_K6_SHORT_ENABLED=1", "-DTEST_EXPECT_K6_ENABLED=1"),
        @("-DAPP_WDG_TEST_K2_SHORT_ENABLED=1", "-DTEST_EXPECT_K6_ENABLED=1"),
        @("-DAPP_REMOTE_SERVO_K5_STOP_DECEL_MPS2=0.0f", "-DTEST_EXPECT_K5_DECEL=0.0f", "-DTEST_EXPECT_RAMP_VALID=0"),
        @("-DAPP_REMOTE_SERVO_K5_STOP_DECEL_MPS2=-0.6f", "-DTEST_EXPECT_K5_DECEL=-0.6f", "-DTEST_EXPECT_RAMP_VALID=0"),
        @("-DAPP_REMOTE_SERVO_K1_STOP_DECEL_MPS2=0.0f", "-DTEST_EXPECT_K5_DECEL=0.0f", "-DTEST_EXPECT_RAMP_VALID=0"),
        @("-DAPP_REMOTE_SERVO_K5_STOP_DECEL_MPS2=1000000.0f", "-DTEST_EXPECT_K5_DECEL=1000000.0f", "-DTEST_EXPECT_RAMP_VALID=0"),
        @("-DAPP_SERVO_SAFETY_STOP_DECEL_MPS2=0.0f", "-DTEST_EXPECT_RAMP_VALID=0"),
        @("-DAPP_SERVO_SAFETY_STOP_DECEL_MPS2=-0.6f", "-DTEST_EXPECT_RAMP_VALID=0"),
        @("-DAPP_SERVO_SAFETY_STOP_DECEL_MPS2=1000000.0f", "-DTEST_EXPECT_RAMP_VALID=0"),
        @("-DAPP_CAN_QUICK_STOP_DECEL_MPS2=0.0f", "-DTEST_EXPECT_RAMP_VALID=0"),
        @("-DAPP_CAN_QUICK_STOP_DECEL_MPS2=65.535f", "-DTEST_EXPECT_RAMP_VALID=0")
    )
    foreach ($definitions in $validCases) {
        & $Compiler -std=c99 -Wall -Wextra -Werror -IAPP @definitions `
            Tests/test_remote_key_config.c -lm -o $binary
        if ($LASTEXITCODE -ne 0) { throw "按键配置兼容测试编译失败" }
        & $binary
        if ($LASTEXITCODE -ne 0) { throw "按键配置兼容或非法减速度校验失败" }
    }
    $invalidCases = @(
        @("-DAPP_REMOTE_SERVO_K5_STOP_DECEL_MPS2=0.6f", "-DAPP_REMOTE_SERVO_K1_STOP_DECEL_MPS2=0.6f"),
        @("-DAPP_WDG_TEST_K6_SHORT_ENABLED=0", "-DAPP_WDG_TEST_K2_SHORT_ENABLED=0"),
        @("-DAPP_WDG_TEST_K6_SHORT_ENABLED=2"),
        @("-DAPP_WDG_TEST_K6_SHORT_ENABLED=-1"),
        @("-DAPP_WDG_TEST_K2_SHORT_ENABLED=2")
    )
    foreach ($definitions in $invalidCases) {
        & $Compiler -E -IAPP @definitions Tests/test_remote_key_config.c 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) { throw "新旧宏冲突或非法看门狗开关未被拒绝" }
    }
    Write-Output "PASS 按键迁移配置矩阵：新旧宏兼容、冲突拒绝和非法值拒绝。"
} finally {
    Pop-Location
}
