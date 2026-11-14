param(
    [string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $env:TEMP "rail_can_protocol_tests.exe"
Push-Location $root
try {
    & $Compiler -std=c99 -Wall -Wextra -Werror -IProtocol/CAN `
        Tests/test_can_protocol.c Protocol/CAN/can_protocol.c -o $binary
    if ($LASTEXITCODE -ne 0) { throw "CAN协议层编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "CAN协议层测试失败" }
    & $Compiler -std=c99 -Wall -Wextra -Werror -IProtocol/CAN `
        Tests/test_can_link.c Protocol/CAN/can_protocol.c Protocol/CAN/can_link.c -o $binary
    if ($LASTEXITCODE -ne 0) { throw "CAN会话层编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "CAN会话层测试失败" }
    & $Compiler -std=c99 -Wall -Wextra -Werror -IProtocol/CAN `
        Tests/test_can_identity.c Protocol/CAN/can_protocol.c Protocol/CAN/can_identity.c -o $binary
    if ($LASTEXITCODE -ne 0) { throw "CAN身份日志编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "CAN身份日志测试失败" }
    & $Compiler -std=c99 -Wall -Wextra -Werror -IProtocol/CAN `
        Tests/test_can_upper.c Protocol/CAN/can_upper.c Protocol/CAN/can_protocol.c -o $binary
    if ($LASTEXITCODE -ne 0) { throw "CAN上层缓存编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "CAN上层缓存测试失败" }
    & $Compiler -std=c99 -Wall -Wextra -Werror -IProtocol/CAN `
        Tests/test_can_arm.c Protocol/CAN/can_arm.c Protocol/CAN/can_protocol.c -o $binary
    if ($LASTEXITCODE -ne 0) { throw "CAN机械臂缓存编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "CAN机械臂缓存测试失败" }
    & $Compiler -std=c99 -Wall -Wextra -Werror -DportTICK_PERIOD_MS=1U `
        -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
        -ITests/mocks -IAPP -IDevice -IProtocol -IProtocol/CAN -IBSP/CAN -IBSP/DEBUG_UART `
        Tests/test_can_status.c APP/app_can_status.c APP/app_can_config.c `
        Protocol/CAN/can_protocol.c -lm -o $binary
    if ($LASTEXITCODE -ne 0) { throw "CAN状态组包编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "CAN状态组包测试失败" }
    foreach ($enabled in @(0, 1)) {
    foreach ($debug in @(0, 1)) {
        & $Compiler -std=c99 -Wall -Wextra -Werror -ffunction-sections -fdata-sections `
            -DportTICK_PERIOD_MS=1U "-DAPP_CAN_ENABLED=$enabled" "-DAPP_CAN_DEBUG_ENABLED=$debug" `
            -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
            -ITests/can_mocks -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IProtocol/CAN `
            -IBSP/CAN -IBSP/DEBUG_UART -IBSP/NRF24L01 `
            Tests/test_can_app.c APP/app_can_status.c APP/app_can_config.c APP/app_can_control.c `
            Protocol/CAN/can_protocol.c Protocol/CAN/can_link.c Protocol/CAN/can_upper.c Protocol/CAN/can_arm.c `
            "-Wl,--gc-sections" -lm -o $binary
        if ($LASTEXITCODE -ne 0) { throw "CAN APP调度编译失败，开关=$enabled 调试=$debug" }
        & $binary
        if ($LASTEXITCODE -ne 0) { throw "CAN APP调度测试失败，开关=$enabled 调试=$debug" }
    }
    }
    & $Compiler -std=c99 -Wall -Wextra -Werror -ffunction-sections -fdata-sections `
        -DportTICK_PERIOD_MS=1U -DAPP_CAN_CONTROL_ENABLED=1 -DAPP_CAN_STOP_PARAMETERS_APPROVED=1 `
        -ITests/can_mocks -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IProtocol/CAN `
        -IBSP/CAN -IBSP/DEBUG_UART -IBSP/NRF24L01 `
        Tests/test_can_app.c APP/app_can_status.c APP/app_can_config.c APP/app_can_control.c `
        APP/app_remote_servo_sync.c APP/HMI/app_control_arbiter.c `
        Protocol/CAN/can_protocol.c Protocol/CAN/can_link.c Protocol/CAN/can_upper.c Protocol/CAN/can_arm.c `
        "-Wl,--gc-sections" -lm -o $binary
    if ($LASTEXITCODE -ne 0) { throw "CAN宏开启APP编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "CAN宏开启APP调度测试失败" }
    # 厂商ARM头文件按系统头处理，生产驱动和测试代码仍执行严格告警检查。
    & $Compiler -std=c99 -Wall -Wextra -Werror `
        -ITests/can_mocks -ITests/mocks -IBSP/CAN -IProtocol/CAN `
        -isystem Drivers/CMSIS/Device/ST/STM32F4xx/Include -isystem Drivers/CMSIS/Include `
        Tests/test_can_bsp.c BSP/CAN/bsp_can.c Protocol/CAN/can_protocol.c -o $binary
    if ($LASTEXITCODE -ne 0) { throw "CAN BSP生产驱动编译失败" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "CAN BSP生产驱动测试失败" }
    & "$PSScriptRoot/generate_can_vectors.ps1" -Compiler $Compiler `
        -Output (Join-Path $env:TEMP "rail_can_vectors_checked.md")
}
finally {
    Pop-Location
}
