param(
    [string]$Map='', [string]$CallGraph='', [switch]$RequireHmiLinkDiag,
    [switch]$NoLcd, [switch]$NoUartRx=$true, [switch]$MinimalWorker=$true,
    [switch]$DebugCli, [switch]$TuneUart, [switch]$ServoCli, [switch]$Pulse,
    [int]$BaselinePhysicalBytes=175136, [string]$BaselineLabel='a2c49d3/alloff',
    [int]$MaximumGrowthBytes=4096
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!$Map){$Map=Join-Path $root 'MDK-ARM/RailInspectionCar_V1/RailInspectionCar_V1.map'}
if(!$CallGraph){$CallGraph=Join-Path $root 'MDK-ARM/RailInspectionCar_V1/RailInspectionCar_V1.htm'}
$text=Get-Content -LiteralPath $Map -Raw
if (!$PSBoundParameters.ContainsKey('NoUartRx')) { $NoUartRx=!($DebugCli -or $TuneUart) }
if (!$PSBoundParameters.ContainsKey('MinimalWorker')) { $MinimalWorker=$NoUartRx }
if ([bool]$NoUartRx -eq ([bool]$DebugCli -or [bool]$TuneUart)) {
    throw 'NoUartRx must equal !(DebugCli || TuneUart)'
}
if ([bool]$MinimalWorker -ne [bool]$NoUartRx) { throw 'MinimalWorker must equal NoUartRx' }
if ($ServoCli -and !$DebugCli) { throw 'ServoCli requires DebugCli' }
function Find-Data([string]$Object,[string]$Name) {
    $pattern='(?m)^\s*'+[regex]::Escape($Name)+'\s+(0x[0-9a-fA-F]+)\s+Data\s+(\d+)\s+'+[regex]::Escape($Object)+'\('
    return [regex]::Match($text,$pattern)
}
function Assert-AbsentData([string]$Object,[string]$Name) {
    if ((Find-Data $Object $Name).Success) { throw "Disabled RAM symbol remains: $Object/$Name" }
    Write-Output "PASS absent RAM $Object/$Name"
}
function Assert-NoObjectRam([string]$Object) {
    $pattern='(?m)^\s*\S+\s+(0x[0-9a-fA-F]+)\s+Data\s+(\d+)\s+'+[regex]::Escape($Object)+'\('
    foreach ($symbol in [regex]::Matches($text,$pattern)) {
        $at=[Convert]::ToUInt32($symbol.Groups[1].Value,16)
        if (($at -ge 0x10000000 -and $at -lt 0x10010000) -or
            ($at -ge 0x20000000 -and $at -lt 0x20020000)) {
            throw "Disabled module retains RAM: $Object : $($symbol.Value.Trim())"
        }
    }
    Write-Output "PASS disabled module RAM absent $Object"
}
function Assert-AbsentCode([string]$Name) {
    if ($text -match ('(?m)^\s*'+[regex]::Escape($Name)+'\s+0x[0-9a-fA-F]+\s+Thumb Code\s+')) {
        throw "Disabled function remains: $Name"
    }
    Write-Output "PASS absent code $Name"
}
if ($NoLcd) {
    foreach ($object in @('app_lcd_show.o','bsp_lcd.o','lcd_port.o','lcd_core.o')) { Assert-NoObjectRam $object }
    Assert-AbsentData 'spi.o' 'hspi1'; Assert-AbsentData 'spi.o' 'hdma_spi1_tx'
    foreach ($name in @('lcd_task','MX_SPI1_Init','lcd_dma_transmit')) { Assert-AbsentCode $name }
    $lcdHandle=Find-Data 'app_rtos_create.o' 'lcd_task_handle'
    if (!$lcdHandle.Success -or [int]$lcdHandle.Groups[2].Value -ne 4) { throw 'LCD NULL-compatible snapshot handle/slot must remain4 B' }
}
if ($NoUartRx) {
    foreach ($name in @('s_rx_buffer','s_rx_turns','s_rx_epoch','s_rx_consumed','s_rx_last_poll','s_rx_last_wraps',
        's_rx_running','s_rx_recovery','s_rx_aborting','s_rx_aborted','s_rx_abort_at','s_rx_next_retry',
        's_rx_seen_sr','s_rx_seen_hal','s_rx_total_base')) { Assert-AbsentData 'debug_uart.o' $name }
    foreach ($name in @('s_build_line','s_build_length','s_line_overflow','s_line_queue','s_rx_epoch')) {
        Assert-AbsentData 'app_debug_cli.o' $name
    }
    Assert-AbsentCode 'app_cli_rx_service'
}
if (!$DebugCli) {
    Assert-AbsentCode 'app_cli_execute'
    Assert-AbsentData 'app_debug_cli.o' 's_sync_debug_skipped'
    Assert-AbsentData 'app_debug_cli.o' 's_position_summary_skipped'
}
if (!$TuneUart) {
    Assert-NoObjectRam 'app_tune_cli.o'; Assert-NoObjectRam 'app_tune_diagnostics.o'
}
if (!$ServoCli) {
    Assert-NoObjectRam 'servo_debug_control.o'; Assert-NoObjectRam 'app_cli_servo_session.o'
}
if (!$Pulse) {
    Assert-AbsentData 'servo_pulse.o' 's_axes'
    Assert-AbsentData 'tim.o' 'htim9'; Assert-AbsentData 'tim.o' 'htim11'
    Assert-AbsentCode 'MX_TIM9_Init'; Assert-AbsentCode 'MX_TIM11_Init'
}
$window=Find-Data 'app_hmi_snapshot.o' 's_registers'
if (!$window.Success -or [int]$window.Groups[2].Value -ne 250) { throw 'HMI request workspace must be exactly250 B' }
Write-Output 'PASS HMI relative request workspace=250 B (max125 registers)'

$regions=@{}
foreach($region in @('ER_IROM1','RW_CCM','RW_IRAM1','RW_IRAM2','RETAINED_FAULT')) {
    $match=[regex]::Match($text,"Execution Region $region \(Exec base: (0x[0-9a-f]+), Load base: (0x[0-9a-f]+), Size: (0x[0-9a-f]+), Max: (0x[0-9a-f]+), ([^\r\n]+)")
    if(!$match.Success){throw "Missing memory region $region"}
    $address=[Convert]::ToUInt32($match.Groups[1].Value,16)
    $size=[Convert]::ToUInt32($match.Groups[3].Value,16)
    $capacity=[Convert]::ToUInt32($match.Groups[4].Value,16)
    $regions[$region]=@{Address=$address;Size=$size;Capacity=$capacity}
    if($size -gt $capacity){throw "Overflow $region"}
    if($region -eq 'ER_IROM1' -and ($address -ne 0x08000000 -or $address+$size -gt 0x080A0000)){throw 'Image overlaps parameter Flash'}
    if($region -eq 'RW_CCM' -and ($address -ne 0x10000000 -or $address+$size -gt 0x10010000)){throw 'CCM bounds'}
    if($region -eq 'RW_IRAM1' -and ($address -ne 0x20000000 -or $address+$size -gt 0x2001C000)){throw 'IRAM1 bounds'}
    if($region -eq 'RW_IRAM2' -and ($address -ne 0x2001C000 -or $address+$size -gt 0x2001FE00)){throw 'IRAM2 overlaps fault record'}
    if($region -eq 'RETAINED_FAULT' -and ($address -ne 0x2001FE00 -or $size -ne 512 -or $match.Groups[5].Value -notmatch 'UNINIT')){throw 'Invalid retained fault placement'}
    Write-Output "PASS region $region base=$($match.Groups[1].Value) size=$size capacity=$capacity free=$($capacity-$size)"
}
# Match both symbol and owning object: USART1 and UART5 use the same local
# s_tx_buffer name. CCM is CPU-only and cannot serve these DMA transfers.
# RS485 TX uses a modbus task stack buffer allocated from ucHeap, so the
# complete FreeRTOS heap must also remain in DMA-accessible ordinary SRAM.
$dmaBuffers=@(
    @('debug_uart.o','s_tx_buffer'),
    @('bsp_hmi_uart.o','s_dma_rx'),@('bsp_hmi_uart.o','s_tx_buffer'),
    @('bsp_rs485.o','s_dma_rx_buffer'),
    @('nrf24l01.o','s_tx'),@('nrf24l01.o','s_rx'),
    @('heap_4.o','ucHeap')
)
if (!$NoUartRx) { $dmaBuffers+=,@('debug_uart.o','s_rx_buffer') }
if (!$NoLcd) { $dmaBuffers+=,@('lcd_port.o','s_dma_buffer') }
foreach($buffer in $dmaBuffers) {
    $pattern='(?m)^\s*'+[regex]::Escape($buffer[1])+'\s+(0x[0-9a-fA-F]+)\s+Data\s+(\d+)\s+'+[regex]::Escape($buffer[0])+'\('
    $symbol=[regex]::Match($text,$pattern)
    if(!$symbol.Success){throw "Missing DMA buffer $($buffer[0])/$($buffer[1])"}
    $address=[Convert]::ToUInt32($symbol.Groups[1].Value,16)
    $size=[uint32]$symbol.Groups[2].Value
    $contained=$false
    foreach($regionName in @('RW_IRAM1','RW_IRAM2')) {
        $region=$regions[$regionName]
        if($address -ge $region.Address -and [uint64]$address+$size -le [uint64]$region.Address+$region.Size){$contained=$true}
    }
    if(!$contained -or $size -eq 0){throw "DMA buffer outside ordinary SRAM: $($buffer[0])/$($buffer[1])"}
    if($buffer[0] -eq 'lcd_port.o' -and $size -ne 640){throw "LCD DMA buffer budget changed: $size, expected 640 B"}
    if($buffer[0] -eq 'heap_4.o' -and $size -ne 49152){throw "FreeRTOS heap budget changed: $size, expected 49152 B"}
    Write-Output "PASS DMA $($buffer[0])/$($buffer[1]) address=$($symbol.Groups[1].Value) size=$size"
}
$ordinaryBytes=[uint32]$regions.RW_IRAM1.Size+[uint32]$regions.RW_IRAM2.Size+[uint32]$regions.RETAINED_FAULT.Size
$physicalBytes=$ordinaryBytes+[uint32]$regions.RW_CCM.Size
$physicalBaseline=$BaselinePhysicalBytes # Region sizes include RAM code and the fixed48 KiB heap.
$diagnosticBytes=[regex]::Match($text,'(?m)^\s*\d+\s+\d+\s+\d+\s+(\d+)\s+(\d+)\s+\d+\s+app_hmi_link_diag\.o\s*$')
if($diagnosticBytes.Success) {
    $moduleRam=[uint32]$diagnosticBytes.Groups[1].Value+[uint32]$diagnosticBytes.Groups[2].Value
    if($RequireHmiLinkDiag -and $moduleRam -eq 0){throw 'Link diagnostic enabled build has no fixed diagnostic state'}
    if(!$RequireHmiLinkDiag -and $moduleRam -ne 0){throw 'All diagnostic switches disabled, but fixed logger RAM remains'}
    if($moduleRam -gt 4096){throw "HMI link diagnostic module exceeds 4096 B: $moduleRam"}
    Write-Output "PASS HMI link diagnostic module fixed RAM=$moduleRam bytes <=4096"
} elseif($RequireHmiLinkDiag) { throw 'Missing HMI link diagnostic module in enabled build' }
else { Write-Output 'INFO HMI link diagnostic module absent (baseline or diagnostics disabled build)' }
$growth=[int64]$physicalBytes-$physicalBaseline
if($growth -gt $MaximumGrowthBytes){throw "Total physical RAM growth relative to $BaselineLabel exceeds $MaximumGrowthBytes B: $growth"}
Write-Output "RAM physical=$physicalBytes baseline=$BaselineLabel/$physicalBaseline delta=$growth bytes; ordinary SRAM free=$([uint32]131072-$ordinaryBytes), CCM free=$([uint32]65536-[uint32]$regions.RW_CCM.Size)"
if($text -notmatch 'bsp_system_exception_stack\s+0x[0-9a-f]+\s+Data\s+512\s+bsp_system.o'){throw 'Missing independent 512 B fault stack'}
$startup=Get-Content -LiteralPath (Join-Path $root 'MDK-ARM/startup_stm32f407xx.s') -Raw
if($startup -notmatch 'Stack_Size\s+EQU\s+0x800'){throw 'MSP budget must remain 2048 bytes'}
$html=Get-Content -LiteralPath $CallGraph -Raw
$fault=[regex]::Match($html,'(?s)>bsp_fault_entry</STRONG>.*?Max Depth =\s*(\d+)')
if(!$fault.Success -or [int]$fault.Groups[1].Value+104 -gt 512){throw 'Fault stack depth unknown or exceeds conservative frame budget'}
Write-Output "PASS independent fault stack: call depth=$($fault.Groups[1].Value), plus 104 B extended hardware frame <=512 B"
Write-Output 'MSP=2048 B; callback/nested IRQ and actual high-water measurement still required on target.'
& (Join-Path $PSScriptRoot 'check_task_stack.ps1') -Report $CallGraph -NoLcd:$NoLcd -NoUartRx:$NoUartRx -MinimalWorker:$MinimalWorker
