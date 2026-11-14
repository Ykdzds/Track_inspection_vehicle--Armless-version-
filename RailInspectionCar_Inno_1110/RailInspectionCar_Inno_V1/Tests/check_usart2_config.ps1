$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent

# 对比应用配置、实际初始化和CubeMX配置，防止重新生成后波特率不一致。
$config = Get-Content -LiteralPath (Join-Path $root "APP/app_config.h") -Raw -Encoding UTF8
$usart = Get-Content -LiteralPath (Join-Path $root "Core/Src/usart.c") -Raw -Encoding UTF8
$ioc = Get-Content -LiteralPath (Join-Path $root "RailInspectionCar_V1.ioc") -Raw -Encoding UTF8
$appMatch = [regex]::Match($config, '(?m)^\s*#define\s+APP_RS485_BAUD_RATE\s*\\\s*\r?\n\s*\((\d+)U\)')
$initMatch = [regex]::Match($usart, '(?s)void\s+MX_USART2_UART_Init\s*\(\s*void\s*\)\s*\{.*?huart2\.Init\.BaudRate\s*=\s*(\d+)\s*;')
$iocMatch = [regex]::Match($ioc, '(?m)^USART2\.BaudRate=(\d+)\s*$')
if (!$appMatch.Success -or !$initMatch.Success -or !$iocMatch.Success) {
    throw "无法读取USART2波特率配置，请检查宏、初始化和CubeMX配置格式。"
}
$appBaud = [uint32]$appMatch.Groups[1].Value
$initBaud = [uint32]$initMatch.Groups[1].Value
$iocBaud = [uint32]$iocMatch.Groups[1].Value
if (($appBaud -ne 115200) -or ($appBaud -ne $initBaud) -or ($appBaud -ne $iocBaud)) {
    throw "USART2波特率不一致或不是115200：APP=$appBaud USART2=$initBaud CubeMX=$iocBaud"
}

# 同时确保升级没有改变8N1格式。
foreach ($setting in @(
    'huart2\.Init\.WordLength\s*=\s*UART_WORDLENGTH_8B\s*;',
    'huart2\.Init\.StopBits\s*=\s*UART_STOPBITS_1\s*;',
    'huart2\.Init\.Parity\s*=\s*UART_PARITY_NONE\s*;'
)) {
    if (![regex]::IsMatch($usart, $setting)) {
        throw "USART2实际初始化不是预期的8N1格式。"
    }
}
Write-Output "USART2配置检查通过：APP=$appBaud USART2=$initBaud CubeMX=$iocBaud，8N1。"
