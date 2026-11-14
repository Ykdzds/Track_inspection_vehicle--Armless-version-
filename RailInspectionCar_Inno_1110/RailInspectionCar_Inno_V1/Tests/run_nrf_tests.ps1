param([string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    & (Join-Path $PSScriptRoot "run_nrf_irq_tests.ps1") -Compiler $Compiler
    & (Join-Path $PSScriptRoot "check_nrf_dma.ps1")
    foreach ($config in @(@(0, 0), @(0, 1), @(1, 1))) {
        $enabled = $config[0]
        $ack = $config[1]
        $binary = Join-Path $env:TEMP "rail_nrf_dma_${enabled}_${ack}.exe"
        & $Compiler -std=c99 -Wall -Wextra -Werror "-DAPP_NRF_ACK_PAYLOAD_ENABLED=$ack" `
            "-DAPP_NRF_TELEMETRY_ENABLED=$enabled" `
            -ITests/nrf_mocks -ITests/mocks -IAPP -IBSP/NRF24L01 `
            Tests/test_nrf_dma.c BSP/NRF24L01/NRF24L01.c -o $binary
        if ($LASTEXITCODE -ne 0) { throw "NRF DMA测试编译失败" }
        & $binary
        if ($LASTEXITCODE -ne 0) { throw "NRF DMA测试失败：状态=$enabled ACK=$ack" }
        foreach ($axis in @(1, 2, 3)) {
            foreach ($readonly in @(0, 1)) {
                $binary = Join-Path $env:TEMP "rail_nrf_telemetry.exe"
                & $Compiler -std=c99 -Wall -Wextra -Werror `
                    "-DAPP_NRF_TELEMETRY_ENABLED=$enabled" "-DAPP_NRF_ACK_PAYLOAD_ENABLED=$ack" `
                    -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
                    "-DAPP_SERVO_QUERY_MODE=$axis" "-DAPP_SERVO_CONTROL_AXIS=$axis" `
                    "-DAPP_SERVO_READONLY_TEST=$readonly" `
                    -ITests/mocks -IAPP -IDevice -IBSP/SYSTEM -IBSP/NRF24L01 `
                    Tests/test_nrf_telemetry.c APP/nrf_status_telemetry.c APP/nrf_remote_protocol.c `
                    APP/app_config.c -o $binary
                if ($LASTEXITCODE -ne 0) { throw "NRF协议测试编译失败" }
                & $binary
                if ($LASTEXITCODE -ne 0) { throw "NRF协议测试失败：开关=$enabled 轴=$axis 只读=$readonly" }
            }
        }
    }
    foreach ($definitions in @(
        @("-DAPP_NRF_TELEMETRY_ENABLED=2", "-DAPP_NRF_ACK_PAYLOAD_ENABLED=1"),
        @("-DAPP_NRF_TELEMETRY_ENABLED=1", "-DAPP_NRF_ACK_PAYLOAD_ENABLED=0"),
        @("-DAPP_NRF_ACK_PAYLOAD_ENABLED=2")
    )) {
        $saved = $ErrorActionPreference
        try {
            $ErrorActionPreference = "Continue"
            & $Compiler -E @definitions APP/app_config.h 2>&1 | Out-Null
        } finally { $ErrorActionPreference = $saved }
        if ($LASTEXITCODE -eq 0) { throw "NRF非法配置未被拒绝" }
    }
    Write-Output "PASS NRF主机矩阵：开关0/0、0/1、1/1；LEFT/RIGHT/BOTH、只读0/1及非法配置。"
} finally { Pop-Location }
