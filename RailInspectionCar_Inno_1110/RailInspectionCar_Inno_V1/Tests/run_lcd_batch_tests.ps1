param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
 $exe=Join-Path $env:TEMP 'rail_lcd_batch.exe'
 & $Compiler -std=c99 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -IAPP -IBSP/SPI_LCD Tests/test_lcd_batch.c BSP/SPI_LCD/lcd_core.c BSP/SPI_LCD/lcd_font.c '-Wl,--gc-sections' -o $exe
 if($LASTEXITCODE){throw 'LCD batch compile failed'}
 & $exe
 if($LASTEXITCODE){throw 'LCD batch test failed'}
 $source=Get-Content -LiteralPath 'APP/app_lcd_show.c' -Raw
 $cacheFields=[regex]::Match($source,'static char s_line_cache[^;]+;\s*static uint8_t s_line_valid[^;]+;').Value
 $formatter=[regex]::Match($source,'(?s)static void app_lcd_line\(.*?(?=\r?\n#define lcd_print)').Value
 if(!$cacheFields -or !$formatter){throw 'Cannot extract production LCD formatter'}
 Set-Content -LiteralPath (Join-Path $env:TEMP 'rail_lcd_cache_under_test.inc') -Value ($cacheFields+[Environment]::NewLine+$formatter) -Encoding UTF8
 $cache=Join-Path $env:TEMP 'rail_lcd_cache.exe'
 & $Compiler -std=c99 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IBSP/SPI_LCD -IBSP/RS485 -IBSP/SYSTEM -IBSP/NRF24L01 -IBSP/SERVO_PULSE ('-I'+$env:TEMP) Tests/test_lcd_cache.c '-Wl,--gc-sections' -o $cache
 if($LASTEXITCODE){throw 'LCD cache compile failed'}
 & $cache
 if($LASTEXITCODE){throw 'LCD cache test failed'}
}finally{Pop-Location}
