param([string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe")
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $env:TEMP 'rail_status_led_tests.exe'
Push-Location $root
try {
    & $Compiler -std=c99 -Wall -Wextra -Werror -IAPP Tests/test_status_led.c -o $binary
    if ($LASTEXITCODE -ne 0) { throw '状态灯授权判定测试编译失败' }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw '状态灯授权判定测试失败' }
} finally {
    Pop-Location
}
