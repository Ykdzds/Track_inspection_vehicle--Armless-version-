param([string]$Map = "")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$source = Get-Content (Join-Path $root "Core/Src/can.c") -Raw
$ioc = Get-Content (Join-Path $root "RailInspectionCar_V1.ioc") -Raw
$irq = Get-Content (Join-Path $root "Core/Src/stm32f4xx_it.c") -Raw
$clock = Get-Content (Join-Path $root "Core/Src/main.c") -Raw
$halConfig = Get-Content (Join-Path $root "Core/Inc/stm32f4xx_hal_conf.h") -Raw
foreach ($entry in @(
    @('hcan1.Init.Prescaler = 6;', 'CAN1.Prescaler=6'),
    @('hcan1.Init.TimeSeg1 = CAN_BS1_11TQ;', 'CAN1.BS1=CAN_BS1_11TQ'),
    @('hcan1.Init.TimeSeg2 = CAN_BS2_2TQ;', 'CAN1.BS2=CAN_BS2_2TQ'),
    @('hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;', 'CAN1.SJW=CAN_SJW_1TQ'),
    @('hcan1.Init.AutoBusOff = DISABLE;', 'CAN1.CalculateBaudRate=500000')
    @('hcan1.Init.AutoRetransmission = ENABLE;', 'CAN1.AutoRetransmission=ENABLE')
)) {
    if (!$source.Contains($entry[0]) -or !$ioc.Contains($entry[1])) {
        throw "CAN生成代码与CubeMX不一致：$($entry[0]) / $($entry[1])"
    }
}
if ($source -notmatch 'GPIO_PIN_11\|GPIO_PIN_12' -or
    $source -notmatch 'GPIO_AF9_CAN1' -or
    $ioc -notmatch 'PA11.Signal=CAN1_RX' -or $ioc -notmatch 'PA12.Signal=CAN1_TX') {
    throw "CAN引脚必须保持PA11/PA12 AF9"
}
if ($halConfig -notmatch '#define\s+HSE_VALUE\s+8000000U' -or
    $ioc -notmatch '(?m)^RCC.HSE_VALUE=8000000\r?$' -or
    $clock -notmatch 'PLL\.PLLSource\s*=\s*RCC_PLLSOURCE_HSE;' -or
    $clock -notmatch 'PLL\.PLLM\s*=\s*4;' -or $clock -notmatch 'PLL\.PLLN\s*=\s*168;' -or
    $clock -notmatch 'PLL\.PLLP\s*=\s*RCC_PLLP_DIV2;' -or
    $clock -notmatch 'AHBCLKDivider\s*=\s*RCC_SYSCLK_DIV1;' -or
    $clock -notmatch 'APB1CLKDivider\s*=\s*RCC_HCLK_DIV4;' -or
    $ioc -notmatch '(?m)^RCC.APB1Freq_Value=42000000\r?$') {
    throw "8MHz HSE / 4 * 168 / 2 / 4 的42MHz APB1配置不一致，需重新核验位时序"
}
foreach ($name in @('TX', 'RX0', 'RX1', 'SCE')) {
    if ($source -notmatch "HAL_NVIC_SetPriority\(CAN1_${name}_IRQn,\s*6U?,\s*0U?\)" -or
        $ioc -notmatch "NVIC.CAN1_${name}_IRQn=true\\:6\\:0") {
        throw "CAN中断优先级或CubeMX配置不一致：$name"
    }
    if ($irq -notmatch "void CAN1_${name}_IRQHandler\(void\)") { throw "CAN中断入口缺失：$name" }
}
[xml]$project = Get-Content (Join-Path $root "MDK-ARM/RailInspectionCar_V1.uvprojx")
[xml]$options = Get-Content (Join-Path $root "MDK-ARM/RailInspectionCar_V1.uvoptx")
$files = @($project.SelectNodes('//Group[starts-with(GroupName,"BSP/CAN") or starts-with(GroupName,"Protocol/CAN") or starts-with(GroupName,"APP/CAN")]/Files/File/FilePath') |
    ForEach-Object { $_.InnerText })
$optionFiles = @($options.SelectNodes('//Group[starts-with(GroupName,"BSP/CAN") or starts-with(GroupName,"Protocol/CAN") or starts-with(GroupName,"APP/CAN")]/File/PathWithFileName') |
    ForEach-Object { $_.InnerText })
if ($files.Count -ne 11 -or $optionFiles.Count -ne 11 -or
    (Compare-Object ($files | Sort-Object) ($optionFiles | Sort-Object))) {
    throw "Keil CAN模块登记数量或两工程文件路径不一致"
}
foreach ($file in $files) {
    if (!(Test-Path (Join-Path "$root/MDK-ARM" $file))) { throw "Keil文件不存在：$file" }
}
$romSizes = $project.SelectNodes('//OnChipMemories/IROM/Size | //OnChipMemories/OCR_RVCT4/Size')
if ($romSizes.Count -ne 2) { throw "未找到两处Keil程序区大小，不能跳过Flash保护检查" }
foreach ($size in $romSizes) {
    if ($size.InnerText -ne '0xa0000') { throw "程序区未限制在640KiB" }
}
if ($Map) {
    $text = Get-Content -LiteralPath $Map -Raw
    $region = [regex]::Match($text, 'Load Region LR_IROM1 \(Base: (0x[0-9a-fA-F]+), Size: (0x[0-9a-fA-F]+)')
    if (!$region.Success) { throw "未找到LR_IROM1链接信息" }
    $base = [Convert]::ToUInt64($region.Groups[1].Value.Substring(2), 16)
    $size = [Convert]::ToUInt64($region.Groups[2].Value.Substring(2), 16)
    if ($base -ne 0x08000000 -or $base + $size -gt 0x080A0000) {
        throw "程序侵入CAN身份日志扇区11"
    }
    Write-Output ("PASS CAN link region end=0x{0:X8} < 0x080A0000" -f ($base + $size))
}
Write-Output "PASS CAN configuration: 42MHz / (6 * 14) = 500000 bit/s; PA11/12; IRQ priority 6; eleven Keil sources"
