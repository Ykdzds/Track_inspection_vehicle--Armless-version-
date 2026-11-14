param([string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $env:TEMP "rail_hmi_state_window_tests.exe"
Push-Location $root
try {
    & $Compiler -std=c99 -O1 -Wall -Wextra -Werror -finstrument-functions -DportTICK_PERIOD_MS=1U `
        -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IProtocol/CAN -IBSP/CAN `
        -IBSP/HMI_UART -IBSP/DEBUG_UART Tests/test_hmi_state_windows.c -o $binary
    if ($LASTEXITCODE -ne 0) { throw "HMI window tests compile failed" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "HMI window tests failed" }
}
finally { Pop-Location }
