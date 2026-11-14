param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$upper=Join-Path $root '../F103_TEST/06_Upper_level_CAN_com_v1.1_low_speed_test/APP'
$binary=Join-Path $env:TEMP 'rail_can_transport_timing_tests.exe'
Push-Location $root
try {
    & $Compiler -std=c99 -Wall -Wextra -Werror -DUPPER_USART1_DEBUG_ENABLE=0 -IProtocol/CAN "-I$upper" `
        Tests/test_can_transport_timing.c Protocol/CAN/can_protocol.c Protocol/CAN/can_link.c `
        "$upper/can_upper_protocol.c" "$upper/can_upper_control.c" -o $binary
    if ($LASTEXITCODE) { throw 'CAN asynchronous timing compile failed' }
    & $binary
    if ($LASTEXITCODE) { throw 'CAN asynchronous timing test failed' }
} finally { Pop-Location }
