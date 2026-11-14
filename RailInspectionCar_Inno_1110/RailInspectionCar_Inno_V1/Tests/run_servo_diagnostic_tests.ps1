param([string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe", [int]$WireDelayMs = 0)
$ErrorActionPreference = "Stop"
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $irq = Join-Path $env:TEMP "rail_diagnostic_irq.o"
    & $Compiler -DAPP_DEBUG_CLI_ENABLED=1 -DAPP_SERVO_CLI_DEBUG_ENABLED=1 -DAPP_SERVO_PULSE_ENABLED=1 -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 -std=c99 -Wall -Wextra -Werror -ffunction-sections `
        -DAPP_CAN_ENABLED=0 -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
        -ITests/rs485_mocks -ITests/mocks -IAPP -IDevice -IBSP/RS485 -IBSP/DEBUG_UART `
        -IBSP/NRF24L01 -IBSP/HMI_UART -ICore/Inc -c Core/Src/stm32f4xx_it.c -o $irq
    if ($LASTEXITCODE -ne 0) { throw "诊断矩阵中断入口编译失败" }
    $tools = Split-Path $Compiler -Parent
    $sections = & (Join-Path $tools "objdump.exe") -h $irq
    if ($LASTEXITCODE -ne 0) { throw "中断目标文件读取失败" }
    $strip = @()
    foreach ($line in $sections) {
        if ($line -match '^\s*\d+\s+(\.rdata\$\.refptr\.\S+)') {
            $strip += "--remove-section=$($Matches[1])"
        }
        if ($line -match '^\s*\d+\s+(\.text\$\S+)') {
            if ($Matches[1] -notin @('.text$TIM1_BRK_TIM9_IRQHandler', '.text$TIM1_TRG_COM_TIM11_IRQHandler')) {
                $strip += "--remove-section=$($Matches[1])"
            }
        }
    }
    & (Join-Path $tools "objcopy.exe") @strip $irq
    if ($LASTEXITCODE -ne 0) { throw "中断入口提取失败" }
    foreach ($axis in @(1, 2, 3)) {
        foreach ($readonly in @(0, 1)) {
            $binary = Join-Path $env:TEMP "rail_servo_diagnostic_${axis}_${readonly}.exe"
            & $Compiler -DAPP_DEBUG_CLI_ENABLED=1 -DAPP_SERVO_CLI_DEBUG_ENABLED=1 -DAPP_SERVO_PULSE_ENABLED=1 -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 -std=c99 -Wall -Wextra -Werror -DTEST_DIAGNOSTIC_ONLY `
                "-DTEST_POLL_WIRE_DELAY_MS=$WireDelayMs" `
                -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
                "-DAPP_SERVO_QUERY_MODE=$axis" "-DAPP_SERVO_CONTROL_AXIS=$axis" `
                "-DAPP_SERVO_READONLY_TEST=$readonly" `
                -ITests/mocks -IAPP -IDevice -IProtocol -IBSP/RS485 -IBSP/SERVO_PULSE -ICore/Inc `
                $irq Tests/test_core.c Tests/aimtor_test_unit.c APP/app_config.c APP/motion_control.c `
                APP/servo_debug_control.c BSP/SERVO_PULSE/servo_pulse.c Protocol/modbus_rtu.c `
                -lm -o $binary
            if ($LASTEXITCODE -ne 0) { throw "诊断采集编译失败：轴=$axis 只读=$readonly" }
            & $binary
            if ($LASTEXITCODE -ne 0) { throw "诊断采集测试失败：轴=$axis 只读=$readonly" }
        }
    }
    Write-Output "PASS 伺服诊断矩阵：LEFT/RIGHT/BOTH，正常/只读，生产Modbus读请求与响应。"
} finally { Pop-Location }
