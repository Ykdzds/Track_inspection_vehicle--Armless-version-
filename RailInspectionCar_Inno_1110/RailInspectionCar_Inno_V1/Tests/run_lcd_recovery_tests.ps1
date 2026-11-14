param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
 $exe=Join-Path $env:TEMP 'rail_lcd_transport.exe'
 & $Compiler -std=c99 -Wall -Wextra -Werror -ITests/lcd_mocks -ITests/mocks -IAPP -IBSP/SPI_LCD Tests/test_lcd_transport.c -o $exe
 if($LASTEXITCODE){throw 'LCD transport compile failed'}
 & $exe
 if($LASTEXITCODE){throw 'LCD transport regression failed'}
 & "$PSScriptRoot/run_lcd_batch_tests.ps1" -Compiler $Compiler
 $source=Get-Content -LiteralPath 'APP/app_lcd_show.c' -Raw
 $fields=[regex]::Match($source,'(?s)static lcd \*s_lcd;.*?(?=\r?\n/\*)').Value
 $recovery=[regex]::Match($source,'(?s)static void app_lcd_publish_event\(.*?(?=\r?\nvoid app_lcd_refresh)').Value
 if(!$fields -or !$recovery){throw 'Cannot extract production LCD recovery'}
 Set-Content -LiteralPath (Join-Path $env:TEMP 'rail_lcd_recovery_under_test.inc') -Value ($fields+[Environment]::NewLine+$recovery) -Encoding UTF8
 $exe=Join-Path $env:TEMP 'rail_lcd_recovery.exe'
 & $Compiler -std=c99 -Wall -Wextra -Werror -Wno-unused-variable -ITests/mocks -IAPP -IBSP/SPI_LCD ('-I'+$env:TEMP) Tests/test_lcd_recovery.c -o $exe
 if($LASTEXITCODE){throw 'LCD recovery compile failed'}
 & $exe
 if($LASTEXITCODE){throw 'LCD recovery regression failed'}
} finally {Pop-Location}
