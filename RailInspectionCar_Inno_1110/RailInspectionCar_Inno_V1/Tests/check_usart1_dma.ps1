$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$ioc = Get-Content (Join-Path $root "RailInspectionCar_V1.ioc") -Raw
$uart = Get-Content (Join-Path $root "Core/Src/usart.c") -Raw
$dma = Get-Content (Join-Path $root "Core/Src/dma.c") -Raw
$irq = Get-Content (Join-Path $root "Core/Src/stm32f4xx_it.c") -Raw
$debug = Get-Content (Join-Path $root "BSP/DEBUG_UART/debug_uart.c") -Raw
$cli = Get-Content (Join-Path $root "APP/app_debug_cli.c") -Raw
foreach ($pattern in @(
 '(?m)^Dma.Request2=USART1_TX\r?$', '(?m)^Dma.Request8=USART1_RX\r?$', '(?m)^Dma.RequestsNb=9\r?$',
 '(?m)^Dma.USART1_TX.2.Instance=DMA2_Stream7\r?$', '(?m)^Dma.USART1_TX.2.Mode=DMA_NORMAL\r?$',
 '(?m)^Dma.USART1_RX.8.Instance=DMA2_Stream2\r?$', '(?m)^Dma.USART1_RX.8.Mode=DMA_CIRCULAR\r?$',
 'NVIC.DMA2_Stream2_IRQn=true\\:6\\:0\\:', 'NVIC.DMA2_Stream7_IRQn=true\\:6\\:0\\:',
 'NVIC.USART1_IRQn=true\\:6\\:0\\:',
 'Dma.USART2_RX.0.Instance=DMA1_Stream5', 'Dma.USART2_TX.1.Instance=DMA1_Stream6'
)) { if($ioc -notmatch $pattern){throw "USART1 isolated DMA config missing: $pattern"} }
foreach ($request in @('USART1_RX.8','USART1_TX.2','USART2_RX.0','USART2_TX.1')) {
 $prefix=[regex]::Escape("Dma.$request.")
 foreach($entry in @{MemInc='DMA_MINC_ENABLE';PeriphInc='DMA_PINC_DISABLE';MemDataAlignment='DMA_MDATAALIGN_BYTE';PeriphDataAlignment='DMA_PDATAALIGN_BYTE';Priority='DMA_PRIORITY_LOW';FIFOMode='DMA_FIFOMODE_DISABLE'}.GetEnumerator()) {
  if($ioc -notmatch "(?m)^$prefix$($entry.Key)=$($entry.Value)\r?$"){throw "DMA config changed: $request $($entry.Key)"}
 }
}
foreach($direction in @('rx','tx')) {
 $stream=if($direction -eq 'rx'){2}else{7}
 foreach($pattern in @("hdma_usart1_$direction.Instance = DMA2_Stream$stream", "hdma_usart1_$direction.Init.Channel = DMA_CHANNEL_4", "__HAL_LINKDMA\(uartHandle,\s*hdma$direction,\s*hdma_usart1_$direction\)")) {
  if($uart -notmatch $pattern){throw "USART1 DMA init missing: $pattern"}
 }
 if($dma -notmatch "HAL_NVIC_SetPriority\(DMA2_Stream$($stream)_IRQn,\s*6,\s*0\)" -or $irq -notmatch "HAL_DMA_IRQHandler\(&hdma_usart1_$direction\)"){throw "USART1 DMA IRQ route missing: $direction"}
}
if($uart -notmatch 'HAL_NVIC_SetPriority\(USART1_IRQn,\s*6,\s*0\)' -or $uart -notmatch 'HAL_NVIC_SetPriority\(USART2_IRQn,\s*5,\s*0\)'){throw 'USART2 must preempt USART1'}
if($debug -match '__disable_irq|HAL_UART_Receive_IT|HAL_UART_AbortReceive\(|HAL_UART_AbortTransmit\('){throw 'USART1 must not globally mask interrupts or use blocking recovery'}
if($debug -notmatch 'HAL_UARTEx_ReceiveToIdle_DMA' -or $debug -notmatch '__set_BASEPRI_MAX' -or $debug -notmatch 'HAL_UART_AbortReceive_IT'){throw 'USART1 RX DMA/isolated publication/async recovery missing'}
if($cli -match 'xQueueSendFromISR\(' -or $cli -notmatch 'app_cli_rx_service\(\);'){throw 'CLI assembly must run in task context'}
foreach($pattern in @('debug_uart_rx_event_isr\(', 'debug_uart_rx_error_isr\(', 'debug_uart_rx_abort_isr\(', 'debug_uart_tx_abort_isr\(')) {
 if($irq -notmatch $pattern){throw "USART1 callback missing: $pattern"}
}
Write-Output 'PASS USART1 RX DMA2S2 circular / TX DMA2S7 normal, priority 6; USART2 priority 5, bounded task parser and asynchronous recovery'
