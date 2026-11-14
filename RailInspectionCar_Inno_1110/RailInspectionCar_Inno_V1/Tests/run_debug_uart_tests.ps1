param([string]$Compiler="D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe")
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
 $binary=Join-Path $env:TEMP 'rail_debug_uart_tests.exe'
 & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_DEBUG_CLI_ENABLED=1 -IAPP -ITests/debug_uart_mocks -IBSP/DEBUG_UART Tests/test_debug_uart.c BSP/DEBUG_UART/debug_uart.c -o $binary
 if($LASTEXITCODE){throw 'USART1 DMA test compile failed'}
 & $binary
 if($LASTEXITCODE){throw 'USART1 DMA tests failed'}
 $cli=Get-Content 'APP/app_debug_cli.c' -Raw
 $parser=[regex]::Match($cli,'(?s)static void app_cli_rx_service\(void\).*?(?=#endif /\* APP_UART_RX_ACTIVE \*/)').Value
 if(!$parser){throw 'Production CLI parser not found'}
 $source=(Get-Content 'Tests/test_cli_dma_rx.c' -Raw).Replace('/* PRODUCTION_PARSER */',$parser)
 $temporary=Join-Path $env:TEMP 'rail_cli_dma_rx_test.c'
 [IO.File]::WriteAllText($temporary,$source)
 $binary=Join-Path $env:TEMP 'rail_cli_dma_rx_tests.exe'
 & $Compiler -std=c99 -Wall -Wextra -Werror -IBSP/DEBUG_UART $temporary -o $binary
 if($LASTEXITCODE){throw 'CLI RX parser test compile failed'}
 & $binary
 if($LASTEXITCODE){throw 'CLI RX parser tests failed'}
} finally {Pop-Location}
