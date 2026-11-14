param([Parameter(Mandatory=$true)][string]$BuildRoot,[string]$Name='DigitalElectronicseCode',[int]$DebugEnabled=0)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$config=Get-Content -LiteralPath (Join-Path $root 'APP\can_upper_config.h') -Raw
$map=Get-Content -LiteralPath (Join-Path $BuildRoot ($Name+'.map')) -Raw
$match=[regex]::Match($map,'Total RW\s+Size.*?\s(\d+)\s+\(')
if(!$match.Success){throw '无法读取SRAM占用'}
$used=[int]$match.Groups[1].Value
$minimum=5120
if($DebugEnabled){$minimum=2048}
if(20480-$used -lt $minimum){throw "SRAM余量不足:$used/20480; required free=$minimum"}
Write-Output "SRAM=$used/20480 free=$(20480-$used) bytes; includes static stacks, heap and MSP"
$base=0; $end=0
foreach($line in Get-Content -LiteralPath (Join-Path $BuildRoot ($Name+'.hex'))){
    $count=[Convert]::ToInt32($line.Substring(1,2),16)
    $address=[Convert]::ToInt32($line.Substring(3,4),16)
    $type=[Convert]::ToInt32($line.Substring(7,2),16)
    if($type -eq 4){$base=[Convert]::ToInt32($line.Substring(9,4),16)*65536}
    if($type -eq 0){$end=[Math]::Max($end,$base+$address+$count)}
}
if($end -gt 0x0800F800){throw 'HEX侵入启动身份保留区'}
Write-Output ('Flash end=0x{0:X8} <= 0x0800F800' -f $end)
& python (Join-Path $PSScriptRoot 'check_stack.py') (Join-Path $BuildRoot ($Name+'.htm')) (Join-Path $root 'APP\can_upper_config.h') $DebugEnabled
if($LASTEXITCODE -ne 0){throw '静态栈与间接回调预算核验失败'}
if(!$DebugEnabled){
    $symbols=[regex]::Matches($map,'(?m)^\s+(\S+)\s+0x[0-9a-f]+\s+(?:Thumb Code|Data|Number)') | ForEach-Object {$_.Groups[1].Value}
    foreach($symbol in @('LogTask','log_stack','log_storage','log_queue','log_control','log_errors','log_record_at','log_gate_at','huart1','HAL_UART_Transmit','HAL_UART_Init','HAL_UART_IRQHandler','MX_USART1_UART_Init')){
        if($symbols -contains $symbol){throw "关闭版本仍有日志/UART符号:$symbol"}
    }
    Write-Output 'PASS no linked log task/stack/queue/UART init/transmit/handle'
}
Write-Output 'Runtime historical stack minimum >=256 bytes remains a hardware release requirement'
