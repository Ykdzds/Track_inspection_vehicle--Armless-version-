param(
    [string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe",
    [switch]$ExcludePositionMonitorTests
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $env:TEMP "rail_core_tests.exe"
$irqObject = Join-Path $env:TEMP "rail_pulse_irq.o"
$coreArgs = @(
    "-DAPP_DEBUG_CLI_ENABLED=1", "-DAPP_SERVO_CLI_DEBUG_ENABLED=1", "-DAPP_SERVO_PULSE_ENABLED=1", "-DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1", "-std=c99", "-Wall", "-Wextra", "-Werror",
    "-DAPP_SERVO_QUERY_MODE=3", "-DAPP_SERVO_CONTROL_AXIS=3",
    "-DAPP_SERVO_READONLY_TEST=0",
    "-ITests/mocks", "-IAPP", "-IDevice", "-IProtocol",
    "-IBSP/RS485", "-IBSP/SERVO_PULSE", "-ICore/Inc",
    "-Wl,--gc-sections", $irqObject,
    "Tests/test_core.c", "Tests/aimtor_test_unit.c", "APP/app_config.c",
    "APP/motion_control.c", "APP/servo_debug_control.c",
    "BSP/SERVO_PULSE/servo_pulse.c",
    "Protocol/modbus_rtu.c", "-lm", "-o", $binary
)
Push-Location $root
try {
    & (Join-Path $PSScriptRoot "run_cold_startup_tests.ps1") -Compiler $Compiler
    & (Join-Path $PSScriptRoot "check_task_stack.ps1")
    & (Join-Path $PSScriptRoot "run_status_led_tests.ps1") -Compiler $Compiler
    & (Join-Path $PSScriptRoot "check_can_config.ps1")
    & (Join-Path $PSScriptRoot "run_servo_diagnostic_tests.ps1") -Compiler $Compiler
    & (Join-Path $PSScriptRoot "run_hmi_modbus_tests.ps1") -Compiler $Compiler
    & (Join-Path $PSScriptRoot "run_hmi_state_tests.ps1") -Compiler $Compiler
    & (Join-Path $PSScriptRoot "run_hmi_control_tests.ps1") -Compiler $Compiler
    & (Join-Path $PSScriptRoot "run_motion_ramp_tests.ps1") -Compiler $Compiler
    & (Join-Path $PSScriptRoot "run_remote_key_config_tests.ps1") -Compiler $Compiler
    & (Join-Path $PSScriptRoot "run_can_protocol_tests.ps1") -Compiler $Compiler
    & (Join-Path $PSScriptRoot "run_can_control_tests.ps1") -Compiler $Compiler
    & (Join-Path $PSScriptRoot "run_can_hmi_joint_tests.ps1") -Compiler $Compiler
    & (Join-Path $PSScriptRoot "run_can_k5_recovery_tests.ps1") -Compiler $Compiler
    foreach ($define in @("APP_CAN_ENABLED=2", "APP_CAN_DEBUG_ENABLED=2",
                          "APP_CAN_CONTROL_ENABLED=2", "APP_CAN_STOP_PARAMETERS_APPROVED=2",
                          "APP_CAN_HEARTBEAT_PERIOD_MS=0", "APP_CAN_LINK_TIMEOUT_MS=100")) {
        $savedErrorAction = $ErrorActionPreference
        try {
            $ErrorActionPreference = "Continue"
            & $Compiler -E "-D$define" APP/app_config.h 2>&1 | Out-Null
        }
        finally {
            $ErrorActionPreference = $savedErrorAction
        }
        if ($LASTEXITCODE -eq 0) { throw "非法CAN配置未被编译期拒绝：$define" }
    }
    # 运动控制不能在CAN传输关闭或停车参数未批准时编译启用。
    $savedErrorAction = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        & $Compiler -E -DAPP_CAN_ENABLED=0 -DAPP_CAN_CONTROL_ENABLED=1 `
            -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 APP/app_config.h 2>&1 | Out-Null
    }
    finally {
        $ErrorActionPreference = $savedErrorAction
    }
    if ($LASTEXITCODE -eq 0) { throw "CAN传输关闭时仍允许运动控制配置" }
    & (Join-Path $PSScriptRoot "run_watchdog_test.ps1") -Compiler $Compiler
    & (Join-Path $PSScriptRoot "run_nrf_tests.ps1") -Compiler $Compiler
    & (Join-Path $PSScriptRoot "check_position_monitor.ps1")
    # 非法配置预期编译失败，兼容Windows PowerShell对原生标准错误的处理。
    $savedErrorAction = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        & $Compiler -E "-DAPP_POSITION_MONITOR_ENABLED=2" APP/app_config.h 2>&1 | Out-Null
    }
    finally {
        $ErrorActionPreference = $savedErrorAction
    }
    if ($LASTEXITCODE -eq 0) { throw "非法位置监控开关未被编译期拒绝" }
    foreach ($define in @("APP_WDG_RECOVERY_DECEL_MS=0",
                          "APP_WDG_RECOVERY_DECEL_MS=65536",
                          "APP_WDG_RECOVERY_STOP_MARGIN_MS=0")) {
        $savedErrorAction = $ErrorActionPreference
        try {
            $ErrorActionPreference = "Continue"
            & $Compiler -E "-D$define" APP/app_config.h 2>&1 | Out-Null
        }
        finally {
            $ErrorActionPreference = $savedErrorAction
        }
        if ($LASTEXITCODE -eq 0) { throw "非法看门狗恢复参数未被拒绝：$define" }
    }
    if ($ExcludePositionMonitorTests) { Write-Output "SKIP 已知位置监控基线断言；本轮不声明完整回归通过" }
    else {
    foreach ($mode in @(1, 2, 3)) {
        foreach ($monitor in @(0, 1)) {
            foreach ($readonly in @(0, 1)) {
                $positionBinary = Join-Path $env:TEMP "rail_position_tests.exe"
                $canMotion = if ($readonly -eq 1) { 0 } else { 1 }
                & $Compiler -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 -std=c99 -Wall -Wextra -Werror `
                    "-DAPP_SERVO_QUERY_MODE=$mode" "-DAPP_SERVO_CONTROL_AXIS=$mode" `
                    "-DAPP_POSITION_MONITOR_ENABLED=$monitor" "-DAPP_SERVO_READONLY_TEST=$readonly" `
                    "-DAPP_CAN_ENABLED=1" "-DAPP_CAN_CONTROL_ENABLED=$canMotion" "-DAPP_CAN_STOP_PARAMETERS_APPROVED=$canMotion" `
                    -ITests/mocks -IAPP -IDevice -IBSP/SERVO_PULSE `
                    Tests/test_position_monitor.c APP/app_config.c -lm -o $positionBinary
                if ($LASTEXITCODE -ne 0) { throw "位置监控测试编译失败" }
                & $positionBinary
                if ($LASTEXITCODE -ne 0) { throw "位置监控测试失败：轴=$mode 开关=$monitor 只读=$readonly" }
            }
        }
    }
    }
    & (Join-Path $PSScriptRoot "check_usart2_config.ps1")
    & (Join-Path $PSScriptRoot "check_usart1_dma.ps1")
    $txBinary = Join-Path $env:TEMP "rail_debug_uart_tests.exe"
    & $Compiler -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 -std=c99 -Wall -Wextra -Werror `
        -ITests/debug_uart_mocks -IBSP/DEBUG_UART `
        Tests/test_debug_uart.c BSP/DEBUG_UART/debug_uart.c -o $txBinary
    if ($LASTEXITCODE -ne 0) { throw "USART1发送DMA测试编译失败" }
    & $txBinary
    if ($LASTEXITCODE -ne 0) { throw "USART1发送DMA测试失败" }
    & (Join-Path $PSScriptRoot "check_pulse_integration.ps1")
    foreach ($trip in @(0, 1)) {
        $syncBinary = Join-Path $env:TEMP "rail_remote_sync_tests.exe"
        & $Compiler -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 -std=c99 -Wall -Wextra -Werror `
            "-DAPP_REMOTE_SERVO_SYNC_ENABLED=1" "-DAPP_REMOTE_SERVO_SYNC_TRIP_ENABLED=$trip" `
            -ITests/mocks -IAPP -IDevice Tests/test_remote_sync.c APP/app_remote_servo_sync.c APP/app_dual_axis_motion.c APP/app_sync_params.c `
            -o $syncBinary
        if ($LASTEXITCODE -ne 0) { throw "同步算法编译失败" }
        & $syncBinary
        if ($LASTEXITCODE -ne 0) { throw "同步算法测试失败" }
    }
    # 直接编译生产复位模块，备份寄存器桩验证跨复位安全标记及校验。
    $systemBinary = Join-Path $env:TEMP "rail_system_tests.exe"
    & $Compiler -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 -std=c99 -Wall -Wextra -Werror `
        -ITests/system_mocks -IBSP/SYSTEM `
        Tests/test_system.c BSP/SYSTEM/bsp_system.c -o $systemBinary
    if ($LASTEXITCODE -ne 0) { throw "复位诊断模块编译失败" }
    foreach ($reason in @("0x0E000003", "0x04000003", "0x10000000",
                         "0x20000000", "0x24000003", "0x40000000",
                         "0x02000000", "0x2E000003")) {
        & $systemBinary $reason
        if ($LASTEXITCODE -ne 0) { throw "复位标志测试失败：$reason" }
    }
    # 单独编译真实中断文件；核心测试链接其中两个脉冲入口，其余硬件入口丢弃。
    & $Compiler -DAPP_DEBUG_CLI_ENABLED=1 -DAPP_SERVO_CLI_DEBUG_ENABLED=1 -DAPP_SERVO_PULSE_ENABLED=1 -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 -std=c99 -Wall -Wextra -Werror -ffunction-sections `
        -DAPP_CAN_ENABLED=0 -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
        -ITests/rs485_mocks -ITests/mocks -IAPP -IDevice -IBSP/RS485 -IBSP/DEBUG_UART -IBSP/NRF24L01 -IBSP/HMI_UART -ICore/Inc `
        -c Core/Src/stm32f4xx_it.c -o $irqObject
    if ($LASTEXITCODE -ne 0) { throw "脉冲真实中断入口编译失败" }
    # MinGW不一定丢弃带外部依赖的未用节，显式仅保留两个生产脉冲中断代码节。
    $objdump = Join-Path (Split-Path $Compiler -Parent) "objdump.exe"
    $objcopy = Join-Path (Split-Path $Compiler -Parent) "objcopy.exe"
    $sections = & $objdump -h $irqObject
    if ($LASTEXITCODE -ne 0) { throw "读取中断目标文件失败" }
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
    if ($LASTEXITCODE -ne 0) { throw "提取生产脉冲中断入口失败" }
    foreach ($mode in @(1, 2, 3)) {
        $bootBinary = Join-Path $env:TEMP ("rail_boot_zero_" + $mode + "_tests.exe")
        $bootArgs = @(
            "-DAPP_DEBUG_CLI_ENABLED=1", "-DAPP_SERVO_CLI_DEBUG_ENABLED=1", "-DAPP_SERVO_PULSE_ENABLED=1", "-DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1", "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-DTEST_BOOT_ZERO_ONLY=1",
            ("-DAPP_SERVO_QUERY_MODE=" + $mode),
            ("-DAPP_SERVO_CONTROL_AXIS=" + $mode),
            "-ITests/mocks", "-IAPP", "-IDevice", "-IProtocol",
            "-IBSP/RS485", "-IBSP/SERVO_PULSE", "-ICore/Inc",
            "-Wl,--gc-sections", $irqObject,
            "Tests/test_core.c", "Tests/aimtor_test_unit.c", "APP/app_config.c",
            "APP/motion_control.c", "APP/servo_debug_control.c",
            "BSP/SERVO_PULSE/servo_pulse.c", "Protocol/modbus_rtu.c",
            "-lm", "-o", $bootBinary
        )
        foreach ($hold in @(0, 1)) {
            & $Compiler @bootArgs "-DAPP_SERVO_STARTUP_HOLD_ENABLED=$hold"
            if ($LASTEXITCODE -ne 0) { throw "恢复及设零专项编译失败：$mode/$hold" }
            & $bootBinary
            if ($LASTEXITCODE -ne 0) { throw "恢复及设零专项失败：$mode/$hold" }
        }
        $remoteBinary = Join-Path $env:TEMP ("rail_remote_servo_" + $mode + "_tests.exe")
        $remoteArgs = @(
            "-DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1", "-std=c99", "-Wall", "-Wextra", "-Werror",
            ("-DAPP_SERVO_QUERY_MODE=" + $mode),
            ("-DAPP_SERVO_CONTROL_AXIS=" + $mode),
            "-ITests/mocks", "-IAPP", "-IAPP/HMI", "-IDevice", "-IBSP/SERVO_PULSE",
            "-IProtocol", "Tests/test_remote_servo.c",
            "APP/app_remote_servo.c", "APP/app_remote_servo_sync.c", "APP/app_dual_axis_motion.c", "APP/app_sync_params.c", "APP/app_config.c", "APP/HMI/app_control_arbiter.c", "-o", $remoteBinary
        )
        foreach ($sync in @(0, 1)) {
        foreach ($hold in @(0, 1)) {
            & $Compiler @remoteArgs "-DAPP_REMOTE_SERVO_SYNC_ENABLED=$sync" "-DAPP_SERVO_STARTUP_HOLD_ENABLED=$hold"
            if ($LASTEXITCODE -ne 0) { throw "遥控伺服测试编译失败" }
            & $remoteBinary
            if ($LASTEXITCODE -ne 0) { throw "遥控伺服测试失败：轴=$mode 同步=$sync 待机=$hold" }
        }
        # 三种只读配置均禁止通过K5长按授权实际运动。
        $readonlyRemoteArgs = @(
            "-DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1", "-std=c99", "-Wall", "-Wextra", "-Werror",
            ("-DAPP_SERVO_QUERY_MODE=" + $mode),
            ("-DAPP_SERVO_CONTROL_AXIS=" + $mode),
            "-DAPP_SERVO_READONLY_TEST=1",
            "-DAPP_CAN_ENABLED=0", "-DAPP_CAN_CONTROL_ENABLED=0", "-DAPP_CAN_STOP_PARAMETERS_APPROVED=0",
            "-ITests/mocks", "-IAPP", "-IAPP/HMI", "-IDevice", "-IBSP/SERVO_PULSE",
            "-IProtocol", "Tests/test_remote_servo.c",
            "APP/app_remote_servo.c", "APP/app_remote_servo_sync.c", "APP/app_dual_axis_motion.c", "APP/app_sync_params.c", "APP/app_config.c", "APP/HMI/app_control_arbiter.c", "-o", $remoteBinary
        )
        & $Compiler @readonlyRemoteArgs "-DAPP_REMOTE_SERVO_SYNC_ENABLED=$sync"
        if ($LASTEXITCODE -ne 0) { throw "只读遥控测试编译失败" }
        & $remoteBinary
        if ($LASTEXITCODE -ne 0) { throw "只读遥控授权测试失败" }
        }
    }
    $nrfBinary = Join-Path $env:TEMP "rail_nrf_tests.exe"
    & $Compiler -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 -std=c99 -Wall -Wextra -Werror `
        -ITests/mocks -IAPP -IBSP/NRF24L01 `
        Tests/test_nrf_remote.c APP/nrf_remote_protocol.c APP/app_nrf_remote.c `
        -o $nrfBinary
    if ($LASTEXITCODE -ne 0) { throw "NRF测试编译失败" }
    & $nrfBinary
    if ($LASTEXITCODE -ne 0) { throw "NRF测试失败" }
    # 独立参数必须分别生效；K5升级不得降低失联停车减速度。
    foreach ($pair in @(@("0.5f", "1.0f"), @("1.0f", "0.5f"))) {
        $stopBinary = Join-Path $env:TEMP "rail_stop_independent_tests.exe"
        & $Compiler -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 -std=c99 -Wall -Wextra -Werror `
            "-DAPP_SERVO_QUERY_MODE=3" "-DAPP_SERVO_CONTROL_AXIS=3" `
            "-DAPP_REMOTE_SERVO_LINK_STOP_DECEL_MPS2=$($pair[0])" `
            "-DAPP_REMOTE_SERVO_K5_STOP_DECEL_MPS2=$($pair[1])" `
            -ITests/mocks -IAPP -IAPP/HMI -IDevice -IBSP/SERVO_PULSE -IProtocol `
            Tests/test_remote_servo.c APP/app_remote_servo.c APP/app_remote_servo_sync.c APP/app_dual_axis_motion.c APP/app_sync_params.c APP/HMI/app_control_arbiter.c `
            APP/app_config.c -o $stopBinary
        if ($LASTEXITCODE -ne 0) { throw "独立停车参数测试编译失败" }
        & $stopBinary
        if ($LASTEXITCODE -ne 0) { throw "独立停车参数测试失败" }
    }
    # 直接编译生产核心代码，只替换硬件及RTOS依赖。
    $compileOutput = & $Compiler @coreArgs 2>&1
    if ($LASTEXITCODE -ne 0) {
        $compileOutput | Write-Output
        throw "主机测试编译失败"
    }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "主机测试失败" }
    & $Compiler @coreArgs "-DAPP_CAN_CONTROL_ENABLED=1" "-DAPP_CAN_STOP_PARAMETERS_APPROVED=1"
    if ($LASTEXITCODE -ne 0) { throw "CAN生产伺服事务编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "CAN生产伺服事务测试失败" }
    # 关闭监控时生产后台查询恢复原调度，其他控制逻辑仍通过回归。
    & $Compiler @coreArgs "-DAPP_POSITION_MONITOR_ENABLED=0"
    if ($LASTEXITCODE -ne 0) { throw "关闭位置监控的主机编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "关闭位置监控的主机回归失败" }
    # 关闭上电待机时，仍须通过原有通信、运动及启动安全回归。
    & $Compiler @coreArgs "-DAPP_SERVO_STARTUP_HOLD_ENABLED=0"
    if ($LASTEXITCODE -ne 0) { throw "关闭上电待机的主机编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "关闭上电待机的主机回归失败" }
    # 分别验证左轴、右轴和双轴只读配置的查询隔离及写操作门禁。
    $queryModes = @("left=1:1", "right=2:2", "both=3:3")
    foreach ($querySpec in $queryModes) {
        $queryParts = $querySpec -split "[:=]"
        $queryName = $queryParts[0]
        $queryValue = $queryParts[1]
        $controlValue = $queryParts[2]
        $readonlyBinary = Join-Path $env:TEMP ("rail_readonly_" + $queryName + "_tests.exe")
        $readonlyArgs = @(
            "-DAPP_DEBUG_CLI_ENABLED=1", "-DAPP_SERVO_CLI_DEBUG_ENABLED=1", "-DAPP_SERVO_PULSE_ENABLED=1", "-DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1", "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-DTEST_READONLY_ONLY=1",
            ("-DAPP_SERVO_QUERY_MODE=" + $queryValue),
            ("-DAPP_SERVO_CONTROL_AXIS=" + $controlValue),
            "-DAPP_SERVO_READONLY_TEST=1",
            "-DAPP_CAN_ENABLED=0", "-DAPP_CAN_CONTROL_ENABLED=0", "-DAPP_CAN_STOP_PARAMETERS_APPROVED=0",
            "-ITests/mocks", "-IAPP", "-IDevice", "-IProtocol",
            "-IBSP/RS485", "-IBSP/SERVO_PULSE", "-ICore/Inc",
            "-Wl,--gc-sections", $irqObject,
            "Tests/test_core.c", "Tests/aimtor_test_unit.c", "APP/app_config.c",
            "APP/motion_control.c", "BSP/SERVO_PULSE/servo_pulse.c",
            "Protocol/modbus_rtu.c", "-lm", "-o", $readonlyBinary
        )
        foreach ($hold in @(0, 1)) {
            $compileOutput = & $Compiler @readonlyArgs "-DAPP_SERVO_STARTUP_HOLD_ENABLED=$hold" 2>&1
            if ($LASTEXITCODE -ne 0) {
                $compileOutput | Write-Output
                throw ($queryName + "只读配置主机测试编译失败，待机=$hold")
            }
            & $readonlyBinary
            if ($LASTEXITCODE -ne 0) {
                throw ($queryName + "只读配置主机测试失败，待机=$hold")
            }
        }
    }
    & (Join-Path $PSScriptRoot "run_cli_session_tests.ps1") -Compiler $Compiler
    # 接收驱动单独链接HAL桩，验证Circular DMA搬运和错误恢复。
    $rxBinary = Join-Path $env:TEMP "rail_rs485_tests.exe"
    $compileOutput = & $Compiler -DAPP_SERVO_PULSE_ENABLED=1 -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 -std=c99 -Wall -Wextra -Werror `
        -DAPP_CAN_ENABLED=0 -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
        -ITests/rs485_mocks -ITests/mocks -IAPP -IDevice -IBSP/RS485 -IBSP/DEBUG_UART -IBSP/NRF24L01 -IBSP/HMI_UART -ICore/Inc `
        Tests/test_rs485.c BSP/RS485/bsp_rs485.c Core/Src/stm32f4xx_it.c `
        -o $rxBinary 2>&1
    if ($LASTEXITCODE -ne 0) {
        $compileOutput | Write-Output
        throw "RS485接收测试编译失败"
    }
    & $rxBinary
    if ($LASTEXITCODE -ne 0) { throw "RS485接收测试失败" }
    & (Join-Path $PSScriptRoot "run_ramp_driver_tests.ps1") -Compiler $Compiler
    & (Join-Path $PSScriptRoot "run_ramp_source_tests.ps1") -Compiler $Compiler
    & (Join-Path $PSScriptRoot "run_normal_timing_tests.ps1") -Compiler $Compiler
    & (Join-Path $PSScriptRoot "run_ramp_diagnostic_tests.ps1") -Compiler $Compiler
    & (Join-Path $PSScriptRoot "run_dual_axis_tests.ps1") -Compiler $Compiler
    & (Join-Path $PSScriptRoot "run_dual_source_random_tests.ps1") -Compiler $Compiler
}
finally {
    Pop-Location
}
