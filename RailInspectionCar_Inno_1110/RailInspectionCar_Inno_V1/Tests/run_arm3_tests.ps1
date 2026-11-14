param([string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe")
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $env:TEMP 'rail_arm3_tests.exe'
Push-Location $root
try {
    & $Compiler -std=c99 -Wall -Wextra -Werror -IAPP -IProtocol/CAN `
        Tests/test_arm3.c APP/app_arm3.c Protocol/CAN/can_arm3.c Protocol/CAN/can_protocol.c -o $binary
    if ($LASTEXITCODE -ne 0) { throw 'ARM3核心编译失败' }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw 'ARM3核心测试失败' }
} finally { Pop-Location }
