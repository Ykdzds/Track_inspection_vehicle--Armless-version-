param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $profile=@('-DAPP_CHASSIS_LCD_ENABLED=0','-DAPP_SERVO_PULSE_ENABLED=0','-DAPP_DEBUG_CLI_ENABLED=0','-DAPP_SERVO_CLI_DEBUG_ENABLED=0',
        '-DAPP_UNPROTECTED_MOTION_TEST_ENABLED=0','-std=c99','-Wall','-Wextra','-Werror','-ITests/mocks','-IAPP','-IDevice','-IBSP/SERVO_PULSE','-IBSP/SPI_LCD','-ICore/Inc')
    $binary=Join-Path $env:TEMP 'rail_disabled_local_features.exe'
    & $Compiler @profile Tests/test_disabled_local_features.c APP/motion_control.c APP/servo_debug_control.c BSP/SERVO_PULSE/servo_pulse.c `
        APP/app_lcd_show.c BSP/SPI_LCD/bsp_lcd.c BSP/SPI_LCD/lcd_port.c BSP/SPI_LCD/lcd_core.c BSP/SPI_LCD/lcd_font.c -o $binary
    if($LASTEXITCODE){throw 'Disabled production paths compile failed'}
    & $binary
    if($LASTEXITCODE){throw 'Disabled production paths regression failed'}
    $nm=Join-Path (Split-Path $Compiler -Parent) 'nm.exe'
    foreach($source in @('APP/servo_debug_control.c','BSP/SERVO_PULSE/servo_pulse.c','APP/app_lcd_show.c','BSP/SPI_LCD/bsp_lcd.c','BSP/SPI_LCD/lcd_port.c','BSP/SPI_LCD/lcd_core.c','BSP/SPI_LCD/lcd_font.c')) {
        $object=Join-Path $env:TEMP ('rail_disabled_'+[IO.Path]::GetFileNameWithoutExtension($source)+'.o')
        & $Compiler @profile -c $source -o $object
        if($LASTEXITCODE){throw "Disabled object compile failed: $source"}
        $symbols=& $nm $object
        if($LASTEXITCODE){throw "Disabled object inspection failed: $source"}
        if($symbols -match '\s[BbDd]\s+[^.\s]') {throw "Disabled path retains writable state: $source"}
        if($symbols -match '\b(htim9|htim11|hspi1|hdma_spi1_tx)\b') {throw "Disabled path retains removed peripheral: $source"}
    }
    foreach($pulse in @(0,1)) {
        $object=Join-Path $env:TEMP "rail_debug_enabled_p${pulse}.o"
        & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_DEBUG_CLI_ENABLED=1 -DAPP_SERVO_CLI_DEBUG_ENABLED=1 "-DAPP_SERVO_PULSE_ENABLED=$pulse" `
            -ITests/mocks -IAPP -IDevice -IBSP/SERVO_PULSE -ICore/Inc -c APP/servo_debug_control.c -o $object
        if($LASTEXITCODE){throw "Enabled CLI/pulse=$pulse compilation failed"}
        if(!$pulse -and ((& $nm $object) -match '\bs_pulse_')) {throw 'Pulse-disabled CLI retains pulse state'}
    }
    Write-Output 'PASS disabled objects: no writable LCD/debug/pulse state or peripheral handles; CLI enabled P0/P1 both compile'
} finally {Pop-Location}
