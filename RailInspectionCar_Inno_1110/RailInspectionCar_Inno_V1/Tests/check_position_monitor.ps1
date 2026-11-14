$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$project = New-Object System.Xml.XmlDocument
$project.Load((Join-Path $root "MDK-ARM/RailInspectionCar_V1.uvprojx"))
if ($null -eq $project.SelectSingleNode("//File[FileName='app_position_monitor.c']")) {
    throw "Keil工程未注册位置监控模块"
}
$device = Get-Content (Join-Path $root "Device/aimtor_servo.c") -Raw
$task = Get-Content (Join-Path $root "APP/app_rtos_task.c") -Raw
$create = Get-Content (Join-Path $root "APP/app_rtos_create.c") -Raw
$monitor = Get-Content (Join-Path $root "APP/app_position_monitor.c") -Raw
$cli = Get-Content (Join-Path $root "APP/app_debug_cli.c") -Raw
$lcd = Get-Content (Join-Path $root "APP/app_lcd_show.c") -Raw
if ($create -notmatch 'app_position_monitor_init\(\)' -or
    $task -notmatch 'app_remote_servo_motion_service\(\);\s*app_position_monitor_service\(\)' -or
    $device -notmatch 'sample->sample_tick\s*=\s*state->position_tick' -or
    $device -notmatch 'snapshot.position_generation\+\+' -or
    $device -notmatch 's_position_pair_pending') {
    throw "位置采集、设零代次、后台调度或任务接入缺失"
}
# 监控模块只读既有门禁和反馈，不得增加写指令或停止动作。
if ($monitor -match '\b(modbus_submit\w*|HAL_UART_\w+|servo_pulse_stop\w*|aimtor_(set|enable|disable|prepare|emergency)\w*|motion_(stop|emergency)\w*|app_health_set_diagnostic_fault)\s*\(') {
    throw "位置监控不可参与运动控制、参数写入或故障联停"
}
foreach ($text in @('position status', 'position trip reset', 'POS_GEN', 'POS_VALID')) {
    if (!$cli.Contains($text)) { throw "位置CLI或TAB字段缺失：$text" }
}
if ($lcd -notmatch 'LCD_PAGE_POSITION' -or $lcd -notmatch 'DIFF:--' -or
    $cli -notmatch 'tx.buffer_free < \(uint16_t\)length') {
    throw "位置LCD无效显示或USART1发送容量保护缺失"
}
Write-Output "位置监控集成检查通过：任务、反馈代次、工程注册、只读边界和显示接入。"
