param(
    [string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $env:TEMP "rail_hmi_modbus_tests.exe"
Push-Location $root
try {
    & $Compiler -std=c99 -Wall -Wextra -Werror `
        -IAPP -IProtocol/HMI `
        Tests/test_hmi_modbus.c Protocol/HMI/hmi_modbus_slave.c -o $binary
    if ($LASTEXITCODE -ne 0) { throw "HMI Modbus测试编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "HMI Modbus测试失败" }
}
finally { Pop-Location }
