$ErrorActionPreference='Stop'
$helper=Join-Path $PSScriptRoot 'pix-ui.ps1'
$evidence=Join-Path $PSScriptRoot '运行核验\28号实施_20261031'
$records=@()
function ReadEvent($page,$control,$event,$source){
 & $helper -Action SelectPage -Name $page
 . $helper -Action Select -Name $control
 $tab=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::TabItem -and $_.Current.Name -match [regex]::Escape($event)}|Select-Object -First 1
 if(!$tab){throw "缺少事件 $control/$event"}
 $tab.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
 $doc=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Document -and !$_.Current.BoundingRectangle.IsEmpty -and $_.GetSupportedPatterns().ProgrammaticName -contains 'ValuePatternIdentifiers.Pattern'}|Sort-Object {$_.Current.BoundingRectangle.X}|Select-Object -Last 1
 $actual=$doc.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value
 $expected=Get-Content (Join-Path $PSScriptRoot $source) -Raw -Encoding UTF8
 if($actual.Replace("`r",'').TrimEnd() -cne $expected.Replace("`r",'').TrimEnd()){throw "事件全文不一致 $page/$control/$event"}
 $script:records += [ordered]@{Page=$page;Control=$control;Event=$event;Source=$source;Matches=$true;SHA256=(Get-FileHash (Join-Path $PSScriptRoot $source)).Hash;Script=$actual}
 Write-Output "重开全文核验 $page/$control/$event"
}
ReadEvent P8_ServoControl recoveryButton '按下事件' '事件\故障恢复按钮_按下.txt'
ReadEvent P8_ServoControl recoveryButton '弹起事件' '事件\空事件.txt'
ReadEvent P8_ServoControl h13Service '定时事件' 'HMI13事务服务.txt'
ReadEvent P8_ServoControl authorize '按下事件' '事件\授权_按下.txt'
ReadEvent P8_ServoControl authorize '弹起事件' '事件\授权_弹起.txt'
ReadEvent P8_ServoControl estop '按下事件' '事件\急停_按下.txt'
ReadEvent P8_ServoControl backHome '弹起事件' '事件\返回_弹起.txt'
ReadEvent P8_ServoControl rpmSlider '值改变事件' '事件\滑块_滑动.txt'
ReadEvent P8_ServoControl P8_ServoControl '加载页面' '事件\P8_加载.txt'
ReadEvent P8_ServoControl P8_ServoControl '离开页面' '事件\P8_离开.txt'
ReadEvent P1_Main servoEntry '按下事件' '事件\主页入口_按下.txt'
ReadEvent P1_Main servoEntry '弹起事件' '事件\主页入口_弹起.txt'
ReadEvent P0_Boot refreshTick '定时事件' '全局计龄刷新.txt'
ReadEvent P0_Boot mod '数据接收' 'Default模式中断.txt'
$records|ConvertTo-Json -Depth 6|Set-Content (Join-Path $evidence '28号重开事件绑定.json') -Encoding UTF8
$properties=@()
. $helper -Action SelectPage -Name P8_ServoControl
foreach($name in @('h13','h13Status','h13Result','h13Tx','h13Service','recoveryButton','recoveryStatus','recoveryDetail','controlTitle','authorize','estop','rpmSlider','stopStatus')){
 SelectControl $name
 $v=@(Props|ForEach-Object {$_.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value})
 if($v -notcontains $name){throw "控件读回错误 $name"}
 if($name -eq 'h13' -and $v[5] -ne '179'){throw 'h13最大索引应179，共180字'}
 if($name -eq 'h13Status' -and $v[5] -ne '95'){throw '状态最大索引应95，共96字'}
 if($name -eq 'recoveryButton' -and $v[11] -ne 'False'){throw '恢复按钮启动默认必须不可用'}
 $properties += [ordered]@{Name=$name;Properties=$v}
}
$properties|ConvertTo-Json -Depth 6|Set-Content (Join-Path $evidence '28号重开控件属性.json') -Encoding UTF8
. $helper -Action SelectPage -Name P0_Boot
SelectControl mod
$mod=@(Props|ForEach-Object {$_.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value})
if($mod[4] -ne 'Default' -or $mod[5] -ne '255'){throw 'Modbus模式或缓冲区错误'}
[ordered]@{Project=$PSScriptRoot;Reopened=$true;StateWords=180;StatusWords=96;RecoveryStartEnabled=$false;Mode=$mod[4];ReceiveBytes=$mod[5];ReadAddress='0x0460';ReadCount=96;WriteAddress='0x0100';WriteCount=24;ResultAddress='0x0120';ResultCount=20;EventCount=$records.Count;PhysicalCommunicationVerified=$false;MotionVerified=$false}|ConvertTo-Json|Set-Content (Join-Path $evidence '28号配置重开核验.json') -Encoding UTF8
. $helper -Action SelectPage -Name P8_ServoControl
Write-Output '28号工程重开读回完成'
