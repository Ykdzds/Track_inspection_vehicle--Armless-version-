$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$ioc = Get-Content (Join-Path $root "RailInspectionCar_V1.ioc") -Raw
$spi = Get-Content (Join-Path $root "Core/Src/spi.c") -Raw
$dma = Get-Content (Join-Path $root "Core/Src/dma.c") -Raw
$irq = Get-Content (Join-Path $root "Core/Src/stm32f4xx_it.c") -Raw
$driver = Get-Content (Join-Path $root "BSP/NRF24L01/NRF24L01.c") -Raw
$telemetry = Get-Content (Join-Path $root "APP/nrf_status_telemetry.c") -Raw
$gpio = Get-Content (Join-Path $root "Core/Src/gpio.c") -Raw
$pins = Get-Content (Join-Path $root "Core/Inc/main.h") -Raw
if ($ioc -notmatch '(?m)^PE13.PinState=GPIO_PIN_RESET\r?$' -or
    $ioc -notmatch '(?m)^PE14.PinState=GPIO_PIN_SET\r?$' -or
    ($gpio -notmatch 'HAL_GPIO_WritePin\(GPIOE, CE_Pin, GPIO_PIN_RESET\)' -and
     ($gpio -notmatch 'HAL_GPIO_WritePin\(CE_GPIO_Port, CE_Pin, GPIO_PIN_RESET\)' -or
      $pins -notmatch '#define\s+CE_GPIO_Port\s+GPIOE')) -or
    $gpio -notmatch 'HAL_GPIO_WritePin\(GPIOE, CSN_Pin\|GPIO_PIN_15, GPIO_PIN_SET\)') {
    throw "无线热启动必须保持CE低、CSN高，代码与CubeMX配置必须一致"
}
foreach ($pattern in @(
    '(?m)^Dma.Request3=SPI2_RX\r?$', '(?m)^Dma.Request4=SPI2_TX\r?$',
    '(?m)^Dma.RequestsNb=9\r?$',
    '(?m)^Dma.SPI2_RX.3.Instance=DMA1_Stream3\r?$',
    '(?m)^Dma.SPI2_TX.4.Instance=DMA1_Stream4\r?$',
    'NVIC.DMA1_Stream3_IRQn=true\\:6\\:0\\:',
    'NVIC.DMA1_Stream4_IRQn=true\\:6\\:0\\:',
    'Dma.USART2_RX.0.Instance=DMA1_Stream5',
    'Dma.USART2_TX.1.Instance=DMA1_Stream6',
    'Dma.USART1_TX.2.Instance=DMA2_Stream7'
)) {
    if ($ioc -notmatch $pattern) { throw "NRF或原有DMA配置错误：$pattern" }
}
foreach ($spec in @("SPI2_RX.3", "SPI2_TX.4")) {
    $prefix = [regex]::Escape("Dma.$spec.")
    foreach ($entry in @("Mode=DMA_NORMAL", "MemInc=DMA_MINC_ENABLE",
        "PeriphInc=DMA_PINC_DISABLE", "MemDataAlignment=DMA_MDATAALIGN_BYTE",
        "PeriphDataAlignment=DMA_PDATAALIGN_BYTE", "Priority=DMA_PRIORITY_LOW",
        "FIFOMode=DMA_FIFOMODE_DISABLE")) {
        if ($ioc -notmatch "(?m)^$prefix$entry\r?$") { throw "SPI2参数错误：$spec $entry" }
    }
}
if ($spi -notmatch 'hdma_spi2_rx.Init.Channel = DMA_CHANNEL_0' -or
    $spi -notmatch 'hdma_spi2_tx.Init.Channel = DMA_CHANNEL_0' -or
    $spi -notmatch '__HAL_LINKDMA\(spiHandle,\s*hdmarx,\s*hdma_spi2_rx\)' -or
    $spi -notmatch '__HAL_LINKDMA\(spiHandle,\s*hdmatx,\s*hdma_spi2_tx\)') {
    throw "SPI2 DMA通道或句柄链接错误"
}
foreach ($stream in @(3, 4)) {
    if ($dma -notmatch "HAL_NVIC_SetPriority\(DMA1_Stream${stream}_IRQn, 6, 0\)" -or
        $dma -notmatch "HAL_NVIC_EnableIRQ\(DMA1_Stream${stream}_IRQn\)") {
        throw "SPI2 DMA中断未配置"
    }
}
& (Join-Path $PSScriptRoot "check_irq_dma_integration.ps1")
foreach ($direction in @("rx", "tx")) {
    foreach ($entry in @("PeriphInc = DMA_PINC_DISABLE", "MemInc = DMA_MINC_ENABLE",
        "PeriphDataAlignment = DMA_PDATAALIGN_BYTE", "MemDataAlignment = DMA_MDATAALIGN_BYTE",
        "Mode = DMA_NORMAL", "Priority = DMA_PRIORITY_LOW", "FIFOMode = DMA_FIFOMODE_DISABLE")) {
        if (!$spi.Contains("hdma_spi2_$direction.Init.$entry;")) {
            throw "SPI2生成DMA参数缺失：$direction $entry"
        }
    }
    if ([regex]::Matches($spi, "HAL_DMA_Init\(&hdma_spi2_$direction\)").Count -ne 1) {
        throw "SPI2 DMA不能重复初始化：$direction"
    }
}
# 禁止无线状态机中出现阻塞收发、等待循环和延时。
if ($driver -match 'HAL_Delay|vTaskDelay|HAL_SPI_TransmitReceive\s*\(|HAL_SPI_Abort|HAL_DMA_IRQHandler|while\s*\(') {
    throw "NRF生产驱动包含阻塞路径"
}
if ($telemetry -match 'aimtor_(enable|disable|set_velocity|emergency_stop)|motion_(stop|set_velocity)|modbus_') {
    throw "状态回传不得调用运动或Modbus写接口"
}
Write-Output "PASS NRF配置：SPI2 DMA1 Stream3/4 Channel0；保留USART DMA；无阻塞等待或运动写调用。"
