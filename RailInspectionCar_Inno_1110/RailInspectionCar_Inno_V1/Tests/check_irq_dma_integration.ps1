$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$irq = Get-Content (Join-Path $root "Core/Src/stm32f4xx_it.c") -Raw
$ioc = Get-Content (Join-Path $root "RailInspectionCar_V1.ioc") -Raw
$uart = Get-Content (Join-Path $root "Core/Src/usart.c") -Raw
$dma = Get-Content (Join-Path $root "Core/Src/dma.c") -Raw
$can = Get-Content (Join-Path $root "Core/Src/can.c") -Raw
# 专用驱动在用户区处理事件，屏蔽生成的HAL调用，防止重复消费。
$routes = @{
    CAN1_TX = '#if APP_CAN_ENABLED\s+bsp_can_tx_isr\(\);\s*#endif'
    CAN1_RX0 = '#if APP_CAN_ENABLED\s+bsp_can_rx_isr\(0U\);\s*#endif'
    CAN1_RX1 = '#if APP_CAN_ENABLED\s+bsp_can_rx_isr\(1U\);\s*#endif'
    CAN1_SCE = '#if APP_CAN_ENABLED\s+bsp_can_error_isr\(\);\s*#endif'
    DMA1_Stream3 = 'NRF24L01_DmaRxIsr\(\);'
    DMA1_Stream4 = 'NRF24L01_DmaTxIsr\(\);'
    TIM1_BRK_TIM9 = 'servo_pulse_update_tim9_isr\(\);'
    TIM1_TRG_COM_TIM11 = 'servo_pulse_update_tim11_isr\(\);'
}
foreach ($name in $routes.Keys) {
    if ([regex]::Matches($irq, "void\s+$($name)_IRQHandler\(void\)").Count -ne 1) {
        throw "中断入口必须唯一：$name"
    }
    $pattern = '(?s)void ' + $name + '_IRQHandler\(void\)\s*\{\s*' +
        '/\* USER CODE BEGIN ' + $name + '_IRQn 0 \*/\s*' + $routes[$name] +
        '\s*/\*[^*]*\*/\s*#if 0\s*/\* USER CODE END ' + $name + '_IRQn 0 \*/\s*' +
        'HAL_(?:CAN|DMA|TIM)_IRQHandler\(&\w+\);\s*' +
        '/\* USER CODE BEGIN ' + $name + '_IRQn 1 \*/\s*#endif\s*' +
        '/\* USER CODE END ' + $name + '_IRQn 1 \*/\s*\}'
    if ($name -in @('TIM1_BRK_TIM9','TIM1_TRG_COM_TIM11')) {
        $pattern = '#if APP_SERVO_PULSE_ENABLED\s*' + $pattern + '\s*#endif'
    }
    if ($irq -notmatch $pattern) { throw "专用中断分派、功能开关或HAL隔离失效：$name" }
}
foreach ($name in @('TX', 'RX0', 'RX1', 'SCE')) {
    if ($can -notmatch "USER CODE BEGIN CAN1_MspInit 1 \*/(?s:.*?)HAL_NVIC_DisableIRQ\(CAN1_$($name)_IRQn\)") {
        throw "CAN必须等待BSP运行阶段再启用中断：$name"
    }
}
foreach ($entry in @('Dma.RequestsNb=9', 'Dma.Request5=UART5_RX', 'Dma.Request6=UART5_TX', 'Dma.Request7=SPI1_TX')) {
    if ($ioc -notmatch ('(?m)^' + [regex]::Escape($entry) + '\r?$')) { throw "DMA请求配置错误：$entry" }
}
foreach ($spec in @(@('rx', 'RX', 5, 0, 'DMA_PERIPH_TO_MEMORY', 'hdmarx', 'DMA_CIRCULAR'),
                     @('tx', 'TX', 6, 7, 'DMA_MEMORY_TO_PERIPH', 'hdmatx', 'DMA_NORMAL'))) {
    $direction, $label, $request, $stream, $flow, $link, $mode = $spec
    $prefix = 'Dma.UART5_' + $label + '.' + $request + '.'
    foreach ($entry in @("Instance=DMA1_Stream$stream", "Direction=$flow", "Mode=$mode",
        'MemInc=DMA_MINC_ENABLE', 'PeriphInc=DMA_PINC_DISABLE',
        'MemDataAlignment=DMA_MDATAALIGN_BYTE', 'PeriphDataAlignment=DMA_PDATAALIGN_BYTE',
        'Priority=DMA_PRIORITY_LOW', 'FIFOMode=DMA_FIFOMODE_DISABLE')) {
        if ($ioc -notmatch ('(?m)^' + [regex]::Escape($prefix + $entry) + '\r?$')) {
            throw "UART5 DMA CubeMX配置不匹配：$entry"
        }
    }
    foreach ($entry in @("Instance = DMA1_Stream$stream", 'Init.Channel = DMA_CHANNEL_4',
        "Init.Direction = $flow", "Init.Mode = $mode", 'Init.MemInc = DMA_MINC_ENABLE',
        'Init.PeriphInc = DMA_PINC_DISABLE', 'Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE',
        'Init.MemDataAlignment = DMA_MDATAALIGN_BYTE', 'Init.Priority = DMA_PRIORITY_LOW',
        'Init.FIFOMode = DMA_FIFOMODE_DISABLE')) {
        if (!$uart.Contains("hdma_uart5_$direction.$entry;")) { throw "UART5 DMA初始化缺失：$entry" }
    }
    if ($uart -notmatch "__HAL_LINKDMA\(uartHandle,\s*$link,\s*hdma_uart5_$direction\)" -or
        $dma -notmatch "HAL_NVIC_SetPriority\(DMA1_Stream$($stream)_IRQn, 5, 0\)" -or
        $dma -notmatch "HAL_NVIC_EnableIRQ\(DMA1_Stream$($stream)_IRQn\)" -or
        $ioc -notmatch "NVIC.DMA1_Stream$($stream)_IRQn=true\\:5\\:0" -or
        $irq -notmatch "HAL_DMA_IRQHandler\(&hdma_uart5_$direction\)") {
        throw "UART5 DMA链接或中断缺失：$direction"
    }
}
$spi=Get-Content (Join-Path $root 'Core/Src/spi.c') -Raw
foreach($entry in @('Instance = DMA2_Stream3','Init.Channel = DMA_CHANNEL_3','Init.Priority = DMA_PRIORITY_LOW')) {
    if(!$spi.Contains("hdma_spi1_tx.$entry;")){throw "LCD DMA configuration missing: $entry"}
}
if($dma -notmatch 'HAL_NVIC_SetPriority\(DMA2_Stream3_IRQn, 6, 0\)' -or
   $irq -notmatch '#if APP_CHASSIS_LCD_ENABLED\s*void DMA2_Stream3_IRQHandler\(void\)\s*\{\s*lcd_port_dma_tx_isr\(\);\s*\}\s*#endif' -or
   $ioc -notmatch 'Dma.SPI1_TX.7.Instance=DMA2_Stream3') {throw 'LCD DMA IRQ/config mismatch'}
if([regex]::Matches($irq, 'void DMA2_Stream3_IRQHandler\(void\)').Count -ne 1) {
    throw 'LCD DMA handler must be unique'
}
if($dma -notmatch '#if APP_CHASSIS_LCD_ENABLED\s*/\*[^*]*\*/\s*HAL_NVIC_SetPriority\(DMA2_Stream3_IRQn, 6, 0\);\s*HAL_NVIC_EnableIRQ\(DMA2_Stream3_IRQn\);\s*#endif') {
    throw 'LCD DMA IRQ enable must be compiled out when LCD is disabled'
}
# TIM2 is the UART5 RTU silence clock and survives PULSE0; SPI2/USART1 keep DMA2.
$tim=Get-Content (Join-Path $root 'Core/Src/tim.c') -Raw
$main=Get-Content (Join-Path $root 'Core/Src/main.c') -Raw
if($irq -notmatch '(?s)void TIM2_IRQHandler\(void\)\s*\{[^}]*HAL_TIM_IRQHandler\(&htim2\);[^}]*\}' -or
   $tim -notmatch 'TIM_HandleTypeDef htim2;\s*#if APP_SERVO_PULSE_ENABLED' -or
   $main -notmatch 'MX_IWDG_Init\(\);\s*MX_TIM2_Init\(\);' -or
   $dma -notmatch '__HAL_RCC_DMA2_CLK_ENABLE\(\)' -or
   $spi -notmatch 'SPI_HandleTypeDef hspi2;' -or
   $irq -notmatch 'HAL_DMA_IRQHandler\(&hdma_usart1_tx\)') {throw 'Shared UART5 TIM2, SPI2 or USART1 DMA resource was removed'}
Write-Output "PASS IRQ/DMA integration: unique BSP handlers; UART5 DMA retained; LCD DMA2 Stream3 Channel3 IRQ6; nine DMA requests."
