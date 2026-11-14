param(
    [string]$Report = "",
    [int]$MinimumMarginBytes = 256,
    [switch]$NoLcd,
    [switch]$NoUartRx = $true,
    [switch]$MinimalWorker = $true
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$create = Get-Content -LiteralPath (Join-Path $root "APP/app_rtos_create.c") -Raw
$tasks = Get-Content -LiteralPath (Join-Path $root "APP/app_rtos_task.c") -Raw
$can = Get-Content -LiteralPath (Join-Path $root "APP/app_can.c") -Raw
$status = [regex]::Match($tasks, '(?s)void status_task\(.*?(?=\r?\nvoid \w+\()')
if (!$status.Success -or $status.Value -match 'remote_servo_status_t|app_remote_servo_get_status' -or
    $status.Value -notmatch 'app_remote_servo_k1_latched') {
    throw "状态灯任务必须使用轻量K1锁存接口，不得生成完整遥控快照"
}
if ($status.Value -notmatch 'aimtor_remote_control_active\(\)' -or
    $status.Value -notmatch 'aimtor_hmi_control_active\(\)' -or
    $status.Value -notmatch 'aimtor_can_control_active\(\)' -or
    $status.Value -notmatch 'set_led\(RGB_GREEN,\s*motion_authorized\s*\?\s*LED_ON\s*:\s*LED_OFF\)') {
    throw "状态灯任务必须使用统一运动授权状态驱动绿色指示灯"
}
$definitions = [regex]::Matches($create,
    'app_create_task_checked\((\w+),\s*"[^"]+",\s*(\d+)U')
# The source contains both preprocessor branches for the same worker. Select
# the exact branch represented by this build; never count both as two tasks.
if ($definitions.Count -ne 12) { throw "任务注册数变化，需更新栈预算检查" }
$worker = @($definitions | Where-Object { $_.Groups[1].Value -eq 'cli_task' })
if ($worker.Count -ne 2 -or
    @($worker | Where-Object { [int]$_.Groups[2].Value -eq 832 }).Count -ne 1 -or
    @($worker | Where-Object { [int]$_.Groups[2].Value -eq 256 }).Count -ne 1 -or
    $create -notmatch '(?s)#if APP_UART_RX_ACTIVE\s+app_create_task_checked\(cli_task,\s*"cli",\s*832U,\s*2U,\s*#else\s+app_create_task_checked\(cli_task,\s*"diag",\s*256U,\s*2U,\s*#endif') {
    throw 'UART worker branches must remain literal832/256words at priority2'
}
if (!$PSBoundParameters.ContainsKey('MinimalWorker')) { $MinimalWorker=$NoUartRx }
if ([bool]$NoUartRx -ne [bool]$MinimalWorker) { throw 'MinimalWorker must exactly match NoUartRx for the current resource profiles' }
$workerWords = if ($MinimalWorker) { 256 } else { 832 }
$definitions = @($definitions | Where-Object {
    ($_.Groups[1].Value -ne 'cli_task' -or [int]$_.Groups[2].Value -eq $workerWords) -and
    (!$NoLcd -or $_.Groups[1].Value -ne 'lcd_task')
})
$expectedTasks = if ($NoLcd) { 10 } else { 11 }
if ($definitions.Count -ne $expectedTasks) { throw "Selected task count mismatch: $($definitions.Count), expected $expectedTasks" }
$snapshotSource = Get-Content -LiteralPath (Join-Path $root 'APP/app_system_snapshot.c') -Raw
if ($snapshotSource -notmatch 'lcd_task_handle') { throw 'LCD task must retain its NULL-compatible snapshot slot' }
if ($NoLcd -and $create -notmatch '(?s)#if APP_CHASSIS_LCD_ENABLED\s+(?:/\*.*?\*/\s*)?app_create_task_checked\(lcd_task,') {
    throw 'LCD task registration is not guarded by its resource switch'
}
$lcdDefinition = [regex]::Matches($create,
    'app_create_task_checked\(lcd_task,\s*"lcd",\s*(\d+)U,\s*1U,')
if ($lcdDefinition.Count -ne 1 -or [int]$lcdDefinition[0].Groups[1].Value -ne 960) {
    throw "LCD任务栈必须为960 words，优先级保持1"
}
$canDefinition = [regex]::Match($can,
    'xTaskCreate\(can_task,\s*"can",\s*(\d+)U,\s*NULL,\s*3U,\s*&can_task_handle\)')
if (!$canDefinition.Success) { throw "可选CAN任务栈或优先级定义缺失" }
if ($MinimumMarginBytes -lt 256) { throw "任务最小静态栈余量不得低于256字节" }
if (!$Report) {
    Write-Output "PASS task stack structure: selected=$expectedTasks workerWords=$workerWords workerAllocated=$($workerWords*4) NoLcd=$([bool]$NoLcd) NoUartRx=$([bool]$NoUartRx); compile-time budgets require Keil report"
    return
}
$html = Get-Content -LiteralPath $Report -Raw
if ($NoLcd -and $html -match '<P><STRONG><a name="\[[^]]+\]"></a>lcd_task</STRONG>') { throw 'Disabled LCD task still exists in call graph' }
function Get-Depth([string]$Name) {
    $pattern = '(?s)<P><STRONG><a name="\[[^]]+\]"></a>' +
        [regex]::Escape($Name) + '</STRONG>.*?(?=<P><STRONG>|\z)'
    $section = [regex]::Match($html, $pattern)
    if (!$section.Success) { throw "Keil调用图未找到函数：$Name" }
    $depth = [regex]::Match($section.Value, 'Max Depth =\s*(\d+)')
    if ($depth.Success) { return [int]$depth.Groups[1].Value }
    if ($section.Value -match 'Stack size 0 bytes') { return 0 }
    throw "函数调用深度未知：$Name，不能视为栈检查通过"
}
foreach ($definition in $definitions) {
    $name = $definition.Groups[1].Value
    # Cortex-M4的StackType_t为32位；保留上下文保存及未追踪调用所需余量。
    $allocated = [int]$definition.Groups[2].Value * 4
    $pattern = '(?s)<P><STRONG><a name="\[[^]]+\]"></a>' +
        [regex]::Escape($name) + '</STRONG>.*?(?=<P><STRONG>|\z)'
    $section = [regex]::Match($html, $pattern)
    if (!$section.Success) { throw "Keil调用图未找到任务：$name" }
    $depth = [regex]::Match($section.Value, 'Max Depth =\s*(\d+)')
    if (!$depth.Success) { throw "任务调用深度未知：$name，不能视为栈检查通过" }
    $used = [int]$depth.Groups[1].Value
    $margin = $allocated - $used
    if ($margin -lt $MinimumMarginBytes) {
        throw "任务栈预算不足：$name allocated=$allocated depth=$used margin=$margin"
    }
    Write-Output "PASS stack $name : allocated=$allocated depth=$used margin=$margin bytes"
}
# ARMCC does not follow function pointers. A display progress callback can in
# turn enter the link callbacks: add both depths, rather than taking one max.
if ($html -match '>can_task</STRONG>') {
    $callbacks = @('send_frame', 'cancel_frame', 'build_record',
                   'publish_upper', 'invalidate_upper', 'describe',
                   'record_failed', 'arm_record_complete', 'arm_prepare')
    foreach ($name in @('submit_control', 'submit_speed', 'submit_stop')) {
        if ($html -match ('>' + $name + '</STRONG>')) { $callbacks += $name }
    }
    $callbackDepth = ($callbacks | ForEach-Object { Get-Depth $_ } |
        Measure-Object -Maximum).Maximum
    $allocated = [int]$canDefinition.Groups[1].Value * 4
    $progressDepth = Get-Depth 'display_progress'
    $used = (Get-Depth 'can_task') + $progressDepth + $callbackDepth
    $margin = $allocated - $used
    if ($margin -lt $MinimumMarginBytes) {
        throw "CAN任务回调保守栈预算不足：allocated=$allocated depth=$used margin=$margin"
    }
    Write-Output "PASS stack can_task with nested callbacks : allocated=$allocated depth=$used margin=$margin bytes (includes display_progress=$progressDepth and link_callback=$callbackDepth)"
}
Write-Output "静态调用图不能替代实物栈高水位测试；函数指针、递归及中断现场仍需实测。"
