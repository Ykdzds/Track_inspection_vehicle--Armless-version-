param([string]$Compiler = 'D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe')
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $binary = Join-Path $env:TEMP 'rail_nrf_irq_race.exe'
    & $Compiler -std=c99 -Wall -Wextra -Werror -ITests/nrf_mocks -ITests/mocks -IAPP -IBSP/NRF24L01 Tests/test_nrf_irq_race.c -o $binary
    if ($LASTEXITCODE) { throw 'NRF IRQ regression compile failed' }
    & $binary
    if ($LASTEXITCODE) { throw 'NRF IRQ regression failed' }
} finally { Pop-Location }
