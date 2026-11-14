param(
    [string]$Compiler = "D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe",
    [string]$Output = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (!$Output) { $Output = Join-Path $root "Docs/CAN_V1_编码测试向量.md" }
$binary = Join-Path $env:TEMP "rail_can_vectors.exe"
Push-Location $root
try {
    & $Compiler -std=c99 -Wall -Wextra -Werror -DportTICK_PERIOD_MS=1U `
        -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
        -ITests/mocks -IAPP -IDevice -IProtocol -IProtocol/CAN -IBSP/CAN -IBSP/DEBUG_UART `
        Tests/generate_can_vectors.c APP/app_can_status.c APP/app_can_config.c `
        Protocol/CAN/can_protocol.c Protocol/CAN/can_link.c Protocol/CAN/can_upper.c `
        -lm -o $binary
    if ($LASTEXITCODE -ne 0) { throw "CAN测试向量生成器编译失败" }
    # 输出先保存在内存；断言失败时不覆盖已验证的交付文件。
    $text = & $binary
    if ($LASTEXITCODE -ne 0) { throw "CAN测试向量生成器自检失败" }
    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllLines($Output, [string[]]$text, $encoding)
    Write-Output "PASS CAN handshake/query/rejection/upper vectors: $Output"
}
finally { Pop-Location }
