param([string]$Compiler='D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $binary=Join-Path $env:TEMP 'rail_rs485_diag.exe'
    & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 -DAPP_LINK_DIAG_TEST=1 -DAPP_SERVO_RS485_DIAG_ENABLED=1 `
        -ITests/mocks -IAPP -IDevice -IProtocol -IBSP/RS485 Tests/test_rs485_diag.c Protocol/modbus_rtu.c -o $binary
    if($LASTEXITCODE){throw 'RS485 diagnostic compile failed'}
    & $binary
    if($LASTEXITCODE){throw 'RS485 diagnostic failed'}
    & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 -DAPP_LINK_DIAG_TEST=1 -DAPP_SERVO_RS485_DIAG_ENABLED=1 -DAPP_SERVO_PULSE_ENABLED=1 `
        -DAPP_CAN_ENABLED=0 -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 -DHAL_UART_RXEVENT_HT=1 -DHAL_UART_RXEVENT_TC=0 `
        -ITests/rs485_mocks -ITests/mocks -IAPP -IDevice -IBSP/RS485 -IBSP/DEBUG_UART -IBSP/NRF24L01 -IBSP/HMI_UART -ICore/Inc `
        Tests/test_rs485_diag_wire.c BSP/RS485/bsp_rs485.c Core/Src/stm32f4xx_it.c -o $binary
    if($LASTEXITCODE){throw 'RS485 diagnostic wire compile failed'}
    & $binary
    if($LASTEXITCODE){throw 'RS485 diagnostic wire failed'}
    $irq=Join-Path $env:TEMP 'rail_rs485_diag_irq.o'
    & $Compiler -DAPP_DEBUG_CLI_ENABLED=1 -DAPP_SERVO_CLI_DEBUG_ENABLED=1 -DAPP_SERVO_PULSE_ENABLED=1 -std=c99 -Wall -Wextra -Werror -ffunction-sections -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 `
        -DAPP_CAN_ENABLED=0 -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 -DHAL_UART_RXEVENT_HT=1 -DHAL_UART_RXEVENT_TC=0 `
        -ITests/rs485_mocks -ITests/mocks -IAPP -IDevice -IBSP/RS485 -IBSP/DEBUG_UART -IBSP/NRF24L01 -IBSP/HMI_UART -ICore/Inc `
        -c Core/Src/stm32f4xx_it.c -o $irq
    if($LASTEXITCODE){throw 'RS485 driver fixture IRQ compile failed'}
    $compilerTools=Split-Path $Compiler -Parent
    $sections=& (Join-Path $compilerTools 'objdump.exe') -h $irq
    if($LASTEXITCODE){throw 'RS485 driver fixture IRQ inspection failed'}
    $strip=@()
    foreach($line in $sections){
        if($line -match '^\s*\d+\s+(\.rdata\$\.refptr\.\S+)'){$strip+="--remove-section=$($Matches[1])"}
        if($line -match '^\s*\d+\s+(\.text\$\S+)'){
            if($Matches[1] -notin @('.text$TIM1_BRK_TIM9_IRQHandler','.text$TIM1_TRG_COM_TIM11_IRQHandler')){
                $strip+="--remove-section=$($Matches[1])"
            }
        }
    }
    & (Join-Path $compilerTools 'objcopy.exe') @strip $irq
    if($LASTEXITCODE){throw 'RS485 driver fixture IRQ extraction failed'}
    & $Compiler -DAPP_DEBUG_CLI_ENABLED=1 -DAPP_SERVO_CLI_DEBUG_ENABLED=1 -DAPP_SERVO_PULSE_ENABLED=1 -std=c99 -Wall -Wextra -Werror -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 -DAPP_LINK_DIAG_TEST=1 -DAPP_SERVO_RS485_DIAG_ENABLED=1 `
        -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 -DAPP_SERVO_QUERY_MODE=3 -DAPP_SERVO_CONTROL_AXIS=3 -DAPP_SERVO_READONLY_TEST=0 `
        -ITests/mocks -IAPP -IDevice -IProtocol -IBSP/RS485 -IBSP/SERVO_PULSE -ICore/Inc `
        $irq Tests/test_rs485_diag_driver.c Tests/aimtor_rs485_diag_test_unit.c APP/app_config.c APP/motion_control.c APP/servo_debug_control.c `
        BSP/SERVO_PULSE/servo_pulse.c Protocol/modbus_rtu.c -lm -o $binary
    if($LASTEXITCODE){throw 'RS485 production driver diagnostics compile failed'}
    & $binary
    if($LASTEXITCODE){throw 'RS485 production driver diagnostics failed'}
    & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_LINK_DIAG_TEST=1 -DAPP_SERVO_RS485_DIAG_ENABLED=1 `
        -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 -DAPP_WDG_TEST_K6_SHORT_ENABLED=0 `
        -ITests/mocks -IAPP -IDevice -IBSP/SYSTEM -IBSP/LED -IBSP/SERVO_PULSE -IProtocol -IBSP/RS485 `
        Tests/test_rs485_diag_health.c APP/app_health.c APP/app_config.c Protocol/modbus_rtu.c -o $binary
    if($LASTEXITCODE){throw 'RS485 safety diagnostics compile failed'}
    & $binary
    if($LASTEXITCODE){throw 'RS485 safety diagnostics failed'}
} finally {Pop-Location}
