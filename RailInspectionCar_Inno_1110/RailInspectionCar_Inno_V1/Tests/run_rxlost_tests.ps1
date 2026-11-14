param([string]$Compiler = 'D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$remote = Join-Path $root '../Remote_control'
Push-Location $root
try {
    & (Join-Path $PSScriptRoot 'run_nrf_irq_tests.ps1') -Compiler $Compiler
    foreach ($config in @(@(0,0), @(0,1), @(1,1))) {
        $enabled = $config[0]; $ack = $config[1]
        $binary = Join-Path $env:TEMP "rxlost_dma_${enabled}_${ack}.exe"
        & $Compiler -std=c99 -Wall -Wextra -Werror "-DAPP_NRF_TELEMETRY_ENABLED=$enabled" `
            "-DAPP_NRF_ACK_PAYLOAD_ENABLED=$ack" -ITests/nrf_mocks -ITests/mocks `
            -IAPP -IBSP/NRF24L01 Tests/test_nrf_dma.c BSP/NRF24L01/NRF24L01.c -o $binary
        if ($LASTEXITCODE -ne 0) { throw 'NRF驱动编译失败' }
        & $binary
        if ($LASTEXITCODE -ne 0) { throw 'NRF驱动回归失败' }
        foreach ($axis in @(1,2,3)) {
            foreach ($readonly in @(0,1)) {
                $definitions = @("-DAPP_NRF_TELEMETRY_ENABLED=$enabled", "-DAPP_NRF_ACK_PAYLOAD_ENABLED=$ack",
                    "-DAPP_SERVO_QUERY_MODE=$axis", "-DAPP_SERVO_CONTROL_AXIS=$axis", "-DAPP_SERVO_READONLY_TEST=$readonly")
                if ($readonly) { $definitions += @('-DAPP_CAN_CONTROL_ENABLED=0','-DAPP_CAN_STOP_PARAMETERS_APPROVED=0') }
                $binary = Join-Path $env:TEMP "rxlost_telemetry_${enabled}_${ack}_${axis}_${readonly}.exe"
                & $Compiler -std=c99 -Wall -Wextra -Werror @definitions -ITests/mocks -IAPP -IDevice `
                    -IBSP/SYSTEM -IBSP/NRF24L01 Tests/test_nrf_telemetry.c APP/nrf_status_telemetry.c `
                    APP/nrf_remote_protocol.c APP/app_config.c -o $binary
                if ($LASTEXITCODE -ne 0) { throw "遥测编译失败 $enabled/$ack/$axis/$readonly" }
                & $binary
                if ($LASTEXITCODE -ne 0) { throw "遥测回归失败 $enabled/$ack/$axis/$readonly" }
            }
        }
    }
    foreach ($speed in @(0,1)) {
        $binary = Join-Path $env:TEMP "rxlost_schedule_model_${speed}.exe"
        & $Compiler -std=c99 -Wall -Wextra -Werror -DTEST_REMOTE_MODEL=1 `
            -DAPP_NRF_TELEMETRY_ENABLED=1 -DAPP_NRF_ACK_PAYLOAD_ENABLED=1 `
            "-DAPP_NRF_VEHICLE_SPEED_STATUS_ENABLED=$speed" -ITests/mocks -IAPP -IDevice `
            -IBSP/SYSTEM -IBSP/NRF24L01 "-I$remote/APP" Tests/test_nrf_telemetry.c `
            APP/nrf_status_telemetry.c APP/nrf_remote_protocol.c APP/app_config.c `
            "$remote/APP/remote_status_protocol.c" "$remote/APP/remote_status_model.c" -o $binary
        if ($LASTEXITCODE -ne 0) { throw "调度/遥控模型联合编译失败 speed=$speed" }
        & $binary
        if ($LASTEXITCODE -ne 0) { throw "调度/遥控模型联合回归失败 speed=$speed" }
    }
    $binary = Join-Path $env:TEMP 'rxlost_end_to_end.exe'
    & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_NRF_TELEMETRY_ENABLED=1 `
        -DAPP_NRF_ACK_PAYLOAD_ENABLED=1 -ITests/nrf_mocks -ITests/mocks -IAPP -IDevice `
        -IBSP/SYSTEM -IBSP/NRF24L01 "-I$remote/APP" Tests/test_rxlost_end_to_end.c `
        BSP/NRF24L01/NRF24L01.c APP/nrf_status_telemetry.c APP/nrf_remote_protocol.c `
        APP/app_config.c "$remote/APP/remote_status_protocol.c" "$remote/APP/remote_status_model.c" -o $binary
    if ($LASTEXITCODE -ne 0) { throw '真实驱动联合编译失败' }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw '真实驱动联合回归失败' }
    & (Join-Path $PSScriptRoot 'run_radio_diagnostic_tests.ps1') -Compiler $Compiler
    & (Join-Path $remote 'Tests/run_remote_tests.ps1') -Compiler $Compiler
    Write-Output 'PASS RX/LOST targeted regression matrix'
} finally { Pop-Location }
