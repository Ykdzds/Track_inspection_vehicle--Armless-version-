$ErrorActionPreference='Stop'
$helper=Join-Path $PSScriptRoot 'pix-ui.ps1'
function InstallEvent($control,$tab,$file){
    . $helper -Action Select -Name $control
    $eventTab=Nodes | Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::TabItem -and $_.Current.Name -match [regex]::Escape($tab)} | Select-Object -First 1
    if(!$eventTab){throw "缺少事件 $control/$tab"}
    $eventTab.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    & $helper -Action Event -Value (Get-Content -LiteralPath (Join-Path $PSScriptRoot $file) -Raw -Encoding UTF8)
    Write-Output "已安装并全文读回 $control/$tab"
}
& $helper -Action SelectPage -Name P8_ServoControl
InstallEvent 'authorize' '弹起事件' '事件/授权_弹起.txt'
InstallEvent 'estop' '按下事件' '事件/急停_按下.txt'
InstallEvent 'backHome' '弹起事件' '事件/返回_弹起.txt'
InstallEvent 'rpmSlider' '值改变事件' '事件/滑块_滑动.txt'
InstallEvent 'h13Service' '定时事件' 'HMI13事务服务.txt'
& $helper -Action SelectPage -Name P1_Main
InstallEvent 'servoEntry' '按下事件' '事件/主页入口_按下.txt'
InstallEvent 'servoEntry' '弹起事件' '事件/主页入口_弹起.txt'
& $helper -Action SelectPage -Name P0_Boot
InstallEvent 'mod' '中断' 'Modbus中断只读解析.txt'
InstallEvent 'refreshTick' '定时事件' '全局计龄刷新.txt'
& $helper -Action Keys -Value '^s'
