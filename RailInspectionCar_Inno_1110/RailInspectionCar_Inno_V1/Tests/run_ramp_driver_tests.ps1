param(
    [string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $env:TEMP "rail_ramp_driver_tests.exe"
$irqObject = Join-Path $env:TEMP "rail_ramp_driver_irq.o"
Push-Location $root
try {
    # 单独生成生产脉冲中断，专项测试不依赖此前运行过完整回归。
    & $Compiler -DAPP_DEBUG_CLI_ENABLED=1 -DAPP_SERVO_CLI_DEBUG_ENABLED=1 -DAPP_SERVO_PULSE_ENABLED=1 -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 -std=c99 -Wall -Wextra -Werror -ffunction-sections `
        -DHAL_UART_RXEVENT_HT=1 -DHAL_UART_RXEVENT_TC=0 `
        -ITests/rs485_mocks -ITests/mocks -IAPP -IDevice -IBSP/RS485 `
        -IBSP/DEBUG_UART -IBSP/NRF24L01 -IBSP/CAN -IBSP/HMI_UART -ICore/Inc `
        -c Core/Src/stm32f4xx_it.c -o $irqObject
    if ($LASTEXITCODE -ne 0) { throw "CLI脉冲中断编译失败" }
    $objdump = Join-Path (Split-Path $Compiler -Parent) "objdump.exe"
    $objcopy = Join-Path (Split-Path $Compiler -Parent) "objcopy.exe"
    $sections = & $objdump -h $irqObject
    if ($LASTEXITCODE -ne 0) { throw "读取CLI中断目标文件失败" }
    $stripArgs = @()
    foreach ($line in $sections) {
        if ($line -match '^\s*\d+\s+(\.rdata\$\.refptr\.\S+)') {
            $stripArgs += "--remove-section=$($Matches[1])"
        }
        if ($line -match '^\s*\d+\s+(\.text\$\S+)') {
            $section = $Matches[1]
            if ($section -notin @('.text$TIM1_BRK_TIM9_IRQHandler', '.text$TIM1_TRG_COM_TIM11_IRQHandler')) {
                $stripArgs += "--remove-section=$section"
            }
        }
    }
    & $objcopy @stripArgs $irqObject
    if ($LASTEXITCODE -ne 0) { throw "提取CLI脉冲中断入口失败" }
    foreach ($axis in @(1,2,3)) {
        foreach ($hold in @(0,1)) {
            & $Compiler -DAPP_DEBUG_CLI_ENABLED=1 -DAPP_SERVO_CLI_DEBUG_ENABLED=1 -DAPP_SERVO_PULSE_ENABLED=1 -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 -std=c99 -Wall -Wextra -Werror -DTEST_CLI_SESSION_ONLY=1 -DTEST_HMI_PRODUCTION=1 `
                "-DAPP_SERVO_QUERY_MODE=$axis" "-DAPP_SERVO_CONTROL_AXIS=$axis" "-DAPP_SERVO_STARTUP_HOLD_ENABLED=$hold" `
                -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IBSP/RS485 -IBSP/SERVO_PULSE -ICore/Inc `
                "-Wl,--gc-sections" $irqObject Tests/test_ramp_driver.c Tests/aimtor_test_unit.c APP/app_config.c `
                APP/motion_control.c APP/servo_debug_control.c APP/app_cli_servo_session.c `
                BSP/SERVO_PULSE/servo_pulse.c Protocol/modbus_rtu.c APP/HMI/app_control_arbiter.c `
                APP/HMI/app_hmi_control.c APP/app_remote_servo_sync.c APP/app_dual_axis_motion.c APP/app_sync_params.c -lm -o $binary
            if ($LASTEXITCODE -ne 0) { throw "HMI真实驱动联动编译失败：轴=$axis 待机=$hold" }
            & $binary
            if ($LASTEXITCODE -ne 0) { throw "HMI真实驱动联动测试失败：轴=$axis 待机=$hold" }
        }
    }
}
finally {
    Pop-Location
}
