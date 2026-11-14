throw '29号工程请使用安装29号事件.ps1或实施29号导航.ps1；本历史脚本禁止执行。'
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot '安装13号事件.ps1')
$helper=Join-Path $PSScriptRoot 'pix-ui.ps1'
. $helper -Action SelectPage -Name P8_ServoControl
function Bind28($control,$event,$source){
 SelectControl $control
 $tab=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::TabItem -and $_.Current.Name -match [regex]::Escape($event)}|Select-Object -First 1
 if(!$tab){throw "找不到 $control/$event"}
 $tab.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
 & $helper -Action Event -Value (Get-Content (Join-Path $PSScriptRoot $source) -Raw -Encoding UTF8)
}
Bind28 recoveryButton '按下事件' '事件\故障恢复按钮_按下.txt'
Bind28 recoveryButton '弹起事件' '事件\空事件.txt'
Bind28 P8_ServoControl '加载页面' '事件\P8_加载.txt'
Bind28 P8_ServoControl '离开页面' '事件\P8_离开.txt'
& $helper -Action Keys -Value '^s'
