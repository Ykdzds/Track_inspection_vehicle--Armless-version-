param([string]$Compiler = "D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe")
$ErrorActionPreference = "Stop"
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $binary = Join-Path $env:TEMP ("rail_fault_retention_" + [guid]::NewGuid().ToString("N") + ".exe")
    & $Compiler -std=c99 -Wall -Wextra -Werror -DBSP_SYSTEM_RETENTION_TEST -ITests/system_mocks -IBSP/SYSTEM Tests/test_fault_retention.c BSP/SYSTEM/bsp_system.c -o $binary
    if ($LASTEXITCODE) { throw "Retained diagnostic compile failed" }
    & $binary
    if ($LASTEXITCODE) { throw "Retained diagnostic regression failed" }
    $legacy = Join-Path $env:TEMP ("rail_reset_" + [guid]::NewGuid().ToString("N") + ".exe")
    & $Compiler -std=c99 -Wall -Wextra -Werror -ITests/system_mocks -IBSP/SYSTEM Tests/test_system.c BSP/SYSTEM/bsp_system.c -o $legacy
    if ($LASTEXITCODE) { throw "Legacy reset compile failed" }
    foreach ($reason in @("0", "0x20000000", "0x28000000", "0x14000000")) {
        & $legacy $reason
        if ($LASTEXITCODE) { throw "Legacy reset regression failed" }
    }
} finally { Pop-Location }
