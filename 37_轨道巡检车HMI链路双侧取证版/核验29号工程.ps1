$ErrorActionPreference='Stop'
if((Split-Path $PSScriptRoot -Leaf) -ne '29_轨道巡检车跨页运行与伺服快捷导航版'){throw '只能核验29号副本'}
$helper=Join-Path $PSScriptRoot 'pix-ui.ps1'
$evidence=Join-Path $PSScriptRoot '运行核验/29号实施_20261101'
. $helper -Action SelectPage -Name P8_ServoControl
$records=@();$properties=@();$lifecycles=@()
function Read29($page,$control,$event,$source){
 SelectControl $control
 $tab=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::TabItem -and $_.Current.Name -match [regex]::Escape($event)}|Select-Object -First 1
 if(!$tab){throw "缺少事件 $page/$control/$event"}
 $tab.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
 $doc=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Document -and !$_.Current.BoundingRectangle.IsEmpty -and $_.GetSupportedPatterns().ProgrammaticName -contains 'ValuePatternIdentifiers.Pattern'}|Sort-Object {$_.Current.BoundingRectangle.X}|Select-Object -Last 1
 $actual=$doc.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value
 if($source){
  $expected=Get-Content -LiteralPath (Join-Path $PSScriptRoot $source) -Raw -Encoding UTF8
  if($actual.Replace("`r",'').TrimEnd() -cne $expected.Replace("`r",'').TrimEnd()){throw "事件不同 $page/$control/$event"}
  $script:records += [ordered]@{Page=$page;Control=$control;Event=$event;Source=$source;Matches=$true;SHA256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $source)).Hash;Script=$actual}
 }else{
  $script:lifecycles += [ordered]@{Page=$page;Event=$event;Script=$actual}
 }
 Write-Output "重开读回 $page/$control/$event"
}
function ReadProps29($page,$control){
 SelectControl $control
 $v=@(Props|ForEach-Object {$_.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value})
 if($v -notcontains $control){throw "读回控件错误 $page/$control"}
 $script:properties += [ordered]@{Page=$page;Name=$control;Properties=$v}
 return ,$v
}
Read29 P8_ServoControl backHome '按下事件' '事件/返回_按下.txt'
Read29 P8_ServoControl backHome '弹起事件' '事件/返回_弹起.txt'
Read29 P8_ServoControl P8_ServoControl '加载页面' '事件/P8_加载.txt'
Read29 P8_ServoControl P8_ServoControl '离开页面' '事件/P8_离开.txt'
Read29 P8_ServoControl h13Service '定时事件' 'HMI13事务服务.txt'
Read29 P8_ServoControl authorize '按下事件' '事件/授权_按下.txt'
Read29 P8_ServoControl authorize '弹起事件' '事件/授权_弹起.txt'
Read29 P8_ServoControl estop '按下事件' '事件/急停_按下.txt'
Read29 P8_ServoControl rpmSlider '值改变事件' '事件/滑块_滑动.txt'
Read29 P8_ServoControl recoveryButton '按下事件' '事件/故障恢复按钮_按下.txt'
Read29 P8_ServoControl recoveryButton '弹起事件' '事件/空事件.txt'
foreach($name in @('h13','h13Status','h13Result','h13Tx','h13Service','rpmSlider','authorize','backHome','recoveryButton','controlTitle')){
 $v=ReadProps29 P8_ServoControl $name
 if($name -eq 'h13' -and $v[5] -ne '179'){throw '状态数组容量改变'}
 if($name -eq 'h13Status' -and $v[5] -ne '95'){throw '快照数组容量改变'}
 if($name -eq 'h13Service' -and ($v[1] -ne 'True' -or $v[5] -ne 'True' -or $v[6] -ne '5')){throw '全局50ms定时器配置错误'}
 if($name -eq 'rpmSlider' -and $v[1] -ne 'True'){throw '滑块不是全局控件'}
 if($name -eq 'recoveryButton' -and $v[11] -ne 'False'){throw '恢复按钮启动状态改变'}
}
foreach($page in @('P1_Main','P2_Diag','P3_Position','P4_Info','P5_Alarm','P6_Sync','P7_System_Ext')){
 SelectPage $page
 Read29 $page servoEntry '按下事件' '事件/主页入口_按下.txt'
 Read29 $page servoEntry '弹起事件' '事件/主页入口_弹起.txt'
 $v=ReadProps29 $page servoEntry
 if($v[14] -ne '伺服控制' -or $v[11] -ne 'True'){throw "快捷按钮文字/可用状态错误 $page"}
 if($page -ne 'P1_Main' -and (($v[4..7] -join ',') -ne '832,554,150,40')){throw "快捷按钮坐标错误 $page"}
 Read29 $page $page '加载页面' $null
 Read29 $page $page '离开页面' $null
}
SelectPage P0_Boot
Read29 P0_Boot refreshTick '定时事件' '全局计龄刷新.txt'
Read29 P0_Boot mod '数据接收' 'Default模式中断.txt'
$mod=ReadProps29 P0_Boot mod
if($mod[4] -ne 'Default' -or $mod[5] -ne '255'){throw 'Modbus基础配置改变'}
Read29 P0_Boot P0_Boot '加载页面' $null
Read29 P0_Boot P0_Boot '离开页面' $null
$records|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $evidence '29号重开事件绑定.json') -Encoding UTF8
$properties|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $evidence '29号重开控件属性.json') -Encoding UTF8
$lifecycles|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $evidence '29号查询页生命周期.json') -Encoding UTF8
[ordered]@{Project=$PSScriptRoot;Reopened=$true;StateWords=180;StatusWords=96;SliderGlobal=$true;ServiceGlobal=$true;ServicePeriodMs=50;Mode=$mod[4];ReceiveBytes=$mod[5];ReadAddress='0x0460';ReadCount=96;WriteAddress='0x0100';WriteCount=24;ResultAddress='0x0120';ResultCount=20;EventCount=$records.Count;QuickNavigationPages=6;PhysicalCommunicationVerified=$false;MotionVerified=$false}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $evidence '29号配置重开核验.json') -Encoding UTF8
SelectPage P1_Main
Write-Output '29号工程重开核验通过'
