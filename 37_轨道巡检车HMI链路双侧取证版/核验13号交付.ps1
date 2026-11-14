$ErrorActionPreference='Stop'
$helper=Join-Path $PSScriptRoot 'pix-ui.ps1'
$records=@()
function CheckEvent($page,$control,$tab,$file){
    & $helper -Action SelectPage -Name $page
    if($control){. $helper -Action Select -Name $control}else{. $helper -Action SelectPage -Name $page;SelectControl $page}
    $eventTab=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::TabItem -and $_.Current.Name -match [regex]::Escape($tab)}|Select-Object -First 1
    if(!$eventTab){throw "缺少事件 $page/$control/$tab"}
    $eventTab.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    $document=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Document -and !$_.Current.BoundingRectangle.IsEmpty -and $_.GetSupportedPatterns().ProgrammaticName -contains 'ValuePatternIdentifiers.Pattern'}|Select-Object -Last 1
    $actual=$document.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value.Replace("
","
").TrimEnd()
    $expected=(Get-Content -LiteralPath (Join-Path $PSScriptRoot $file) -Raw -Encoding UTF8).Replace("
","
").TrimEnd()
    if($actual -cne $expected){throw "重开后事件不一致 $page/$control/$tab"}
    $script:records+=[ordered]@{Page=$page;Control=$control;Event=$tab;Source=$file;Matches=$true;SHA256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $file)).Hash}
}
CheckEvent 'P8_ServoControl' 'h13Service' '定时事件' 'HMI13事务服务.txt'
CheckEvent 'P8_ServoControl' 'authorize' '按下事件' '事件/授权_按下.txt'
CheckEvent 'P8_ServoControl' 'authorize' '弹起事件' '事件/授权_弹起.txt'
CheckEvent 'P8_ServoControl' 'estop' '按下事件' '事件/急停_按下.txt'
CheckEvent 'P8_ServoControl' 'backHome' '弹起事件' '事件/返回_弹起.txt'
CheckEvent 'P8_ServoControl' 'rpmSlider' '值改变事件' '事件/滑块_滑动.txt'
CheckEvent 'P8_ServoControl' '' '加载页面' '事件/P8_加载.txt'
CheckEvent 'P8_ServoControl' '' '离开页面' '事件/P8_离开.txt'
CheckEvent 'P1_Main' 'servoEntry' '按下事件' '事件/主页入口_按下.txt'
CheckEvent 'P1_Main' 'servoEntry' '弹起事件' '事件/主页入口_弹起.txt'
CheckEvent 'P0_Boot' 'refreshTick' '定时事件' '全局计龄刷新.txt'
CheckEvent 'P0_Boot' 'mod' '数据接收' 'Default模式中断.txt'
$records | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/全量修复重开事件绑定核验_20261016.json') -Encoding utf8
& $helper -Action Keys -Value '^b'
Start-Sleep -Seconds 2
. $helper -Action Inspect | Out-Null
$output=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Document -and $_.GetSupportedPatterns().ProgrammaticName -contains 'TextPatternIdentifiers.Pattern'}|ForEach-Object {$_.GetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern).DocumentRange.GetText(-1)}|Select-Object -First 1
$output | Set-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/全量修复VisualPIX编译日志_20261016.txt') -Encoding utf8
if($output -notmatch '编译成功' -or $output -match '编译失败'){throw '重开编译失败'}
Write-Output $output
