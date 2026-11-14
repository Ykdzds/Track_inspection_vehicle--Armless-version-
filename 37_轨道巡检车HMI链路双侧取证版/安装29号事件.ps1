$ErrorActionPreference='Stop'
if((Split-Path $PSScriptRoot -Leaf) -ne '29_轨道巡检车跨页运行与伺服快捷导航版'){throw '只能操作29号副本'}
$helper=Join-Path $PSScriptRoot 'pix-ui.ps1'
. $helper -Action SelectPage -Name P8_ServoControl
function Bind29($page,$control,$event,$source){
 SelectPage $page
 SelectControl $control
 $tab=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::TabItem -and $_.Current.Name -match [regex]::Escape($event)}|Select-Object -First 1
 if(!$tab){throw "缺少事件 $page/$control/$event"}
 $tab.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
 & $helper -Action Event -Value (Get-Content -LiteralPath (Join-Path $PSScriptRoot $source) -Raw -Encoding UTF8)
 Write-Output "写入并全文核对 $page/$control/$event"
}
Bind29 P8_ServoControl backHome '按下事件' '事件/返回_按下.txt'
Bind29 P8_ServoControl backHome '弹起事件' '事件/返回_弹起.txt'
Bind29 P8_ServoControl P8_ServoControl '加载页面' '事件/P8_加载.txt'
Bind29 P8_ServoControl P8_ServoControl '离开页面' '事件/P8_离开.txt'
Bind29 P8_ServoControl h13Service '定时事件' 'HMI13事务服务.txt'
Bind29 P0_Boot refreshTick '定时事件' '全局计龄刷新.txt'
foreach($page in @('P1_Main','P2_Diag','P3_Position','P4_Info','P5_Alarm','P6_Sync','P7_System_Ext')){
 Bind29 $page servoEntry '按下事件' '事件/主页入口_按下.txt'
 Bind29 $page servoEntry '弹起事件' '事件/主页入口_弹起.txt'
}
SelectPage P8_ServoControl
& $helper -Action SetGlobal -Name rpmSlider
SelectControl controlTitle
SetRow 14 '伺服控制 / 29'
SelectPage P4_Info
SelectControl screenVersion
SetRow 14 '屏幕工程：29'
& $helper -Action Keys -Value '^s'
