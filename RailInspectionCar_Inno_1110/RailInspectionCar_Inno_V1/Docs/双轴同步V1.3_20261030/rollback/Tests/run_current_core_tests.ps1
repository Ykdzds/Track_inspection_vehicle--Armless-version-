param([string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe")
$ErrorActionPreference = "Stop"
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $irq = Join-Path $env:TEMP "rail_safety_irq.o"
    & $Compiler -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 -std=c99 -Wall -Wextra -Werror -ffunction-sections `
        -DAPP_CAN_ENABLED=0 -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
        -DHAL_UART_RXEVENT_HT=1 -DHAL_UART_RXEVENT_TC=0 `
        -ITests/rs485_mocks -ITests/mocks -IAPP -IDevice -IBSP/RS485 -IBSP/DEBUG_UART `
        -IBSP/NRF24L01 -IBSP/HMI_UART -ICore/Inc -c Core/Src/stm32f4xx_it.c -o $irq
    if ($LASTEXITCODE -ne 0) { throw "安全停车中断入口编译失败" }
    $tools = Split-Path $Compiler -Parent
    $sections = & (Join-Path $tools "objdump.exe") -h $irq
    $strip = @()
    foreach ($line in $sections) {
        if ($line -match '^\s*\d+\s+(\.rdata\$\.refptr\.\S+)') { $strip += "--remove-section=$($Matches[1])" }
        if ($line -match '^\s*\d+\s+(\.text\$\S+)') {
            if ($Matches[1] -notin @('.text$TIM1_BRK_TIM9_IRQHandler', '.text$TIM1_TRG_COM_TIM11_IRQHandler')) {
                $strip += "--remove-section=$($Matches[1])"
            }
        }
    }
    & (Join-Path $tools "objcopy.exe") @strip $irq
    if ($LASTEXITCODE -ne 0) { throw "安全停车中断入口提取失败" }
    $binary = Join-Path $env:TEMP 'rail_current_core.exe'
    $common = @('-DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1','-std=c99','-Wall','-Wextra','-Werror',
        '-DAPP_CAN_CONTROL_ENABLED=0','-DAPP_CAN_STOP_PARAMETERS_APPROVED=0',
        '-ITests/mocks','-IAPP','-IDevice','-IProtocol','-IBSP/RS485','-IBSP/SERVO_PULSE','-ICore/Inc',
        $irq,'Tests/test_core.c','Tests/aimtor_test_unit.c','APP/app_config.c','APP/motion_control.c',
        'APP/servo_debug_control.c','BSP/SERVO_PULSE/servo_pulse.c','Protocol/modbus_rtu.c','-lm','-o',$binary)
    foreach($hold in @(0,1)) {
        & $Compiler @common -DAPP_SERVO_QUERY_MODE=3 -DAPP_SERVO_CONTROL_AXIS=3 -DAPP_SERVO_READONLY_TEST=0 "-DAPP_SERVO_STARTUP_HOLD_ENABLED=$hold"
        if($LASTEXITCODE){throw 'Current core compile failed'}
        & $binary
        if($LASTEXITCODE){throw "Current core regression failed hold=$hold"}
    }
    foreach($axis in @(1,2,3)) {
        foreach($hold in @(0,1)) {
            foreach($kind in @('BOOT_ZERO','READONLY')) {
                $readonly=if($kind -eq 'READONLY'){1}else{0}
                & $Compiler @common "-DTEST_${kind}_ONLY=1" "-DAPP_SERVO_QUERY_MODE=$axis" "-DAPP_SERVO_CONTROL_AXIS=$axis" "-DAPP_SERVO_STARTUP_HOLD_ENABLED=$hold" "-DAPP_SERVO_READONLY_TEST=$readonly"
                if($LASTEXITCODE){throw "Current compile failed $kind/$axis/$hold"}
                & $binary
                if($LASTEXITCODE){throw "Current regression failed $kind/$axis/$hold"}
            }
        }
    }
    & $Compiler -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 -std=c99 -Wall -Wextra -Werror -DAPP_CAN_ENABLED=0 -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 -DHAL_UART_RXEVENT_HT=1 -DHAL_UART_RXEVENT_TC=0 `
        -ITests/rs485_mocks -ITests/mocks -IAPP -IDevice -IBSP/RS485 -IBSP/DEBUG_UART -IBSP/NRF24L01 -IBSP/HMI_UART -ICore/Inc `
        Tests/test_rs485.c BSP/RS485/bsp_rs485.c Core/Src/stm32f4xx_it.c -o $binary
    if($LASTEXITCODE){throw 'RS485 DMA compile failed'}
    & $binary
    if($LASTEXITCODE){throw 'RS485 DMA regression failed'}
    foreach($trip in @(0,1)) {
        & $Compiler -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 -std=c99 -Wall -Wextra -Werror -DAPP_REMOTE_SERVO_SYNC_ENABLED=1 "-DAPP_REMOTE_SERVO_SYNC_TRIP_ENABLED=$trip" `
            -ITests/mocks -IAPP -IDevice Tests/test_remote_sync.c APP/app_remote_servo_sync.c -o $binary
        if($LASTEXITCODE){throw 'Synchronization compile failed'}
        & $binary
        if($LASTEXITCODE){throw 'Synchronization regression failed'}
    }
} finally { Pop-Location }
