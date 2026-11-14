param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $switches=@('APP_HMI_LINK_DIAG_ENABLED','APP_NRF_DIAGNOSTIC_ENABLED','APP_CAN_SERVO_DIAG_ENABLED','APP_CAN_ARM_DIAG_ENABLED','APP_SERVO_RS485_DIAG_ENABLED')
    $profiles=@(
        @{Name='hmi_only';Mask=1;Legacy=0},
        @{Name='rs485_only';Mask=16;Legacy=0},
        @{Name='five_and_legacy';Mask=31;Legacy=1}
    )
    foreach($profile in $profiles) {
        $binary=Join-Path $env:TEMP ('rail_no_lcd_diag_'+$profile.Name+'.exe')
        $arguments=@('-std=c99','-Wall','-Wextra','-Werror','-DAPP_LINK_DIAG_TEST=1',
            '-DAPP_CHASSIS_LCD_ENABLED=0','-DAPP_DEBUG_CLI_ENABLED=0','-DAPP_TUNE_UART_ENABLED=0',
            '-DAPP_SERVO_CLI_DEBUG_ENABLED=0','-DAPP_SERVO_PULSE_ENABLED=0',
            '-ITests/can_mocks','-ITests/mocks','-IAPP','-IAPP/HMI','-IDevice','-IProtocol',
            '-IProtocol/CAN','-IBSP/SYSTEM','-IBSP/HMI_UART','-IBSP/DEBUG_UART','-IBSP/SPI_LCD')
        for($i=0;$i -lt 5;$i++) {$arguments+='-D'+$switches[$i]+'='+[int](($profile.Mask -band (1 -shl $i)) -ne 0)}
        $arguments+='-DAPP_CAN_DEBUG_ENABLED='+$profile.Legacy
        $arguments+='-DAPP_REMOTE_SERVO_SYNC_DEBUG_ENABLED='+$profile.Legacy
        $arguments+=@('Tests/test_link_diag.c','APP/HMI/app_hmi_link_diag.c','-o',$binary)
        & $Compiler @arguments
        if($LASTEXITCODE){throw "No-LCD shared logger compile failed: $($profile.Name)"}
        & $binary
        if($LASTEXITCODE){throw "No-LCD shared logger test failed: $($profile.Name)"}
        Write-Output "PASS LCD=0 configuration $($profile.Name): BOOT/CONFIG/FIRST/MEM, no LCD getter calls, 6000B/s budget"
    }
} finally {Pop-Location}
