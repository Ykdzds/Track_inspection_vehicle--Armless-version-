$ErrorActionPreference='Stop'
$helper=Join-Path $PSScriptRoot 'pix-ui.ps1'
$records=@()
function ReadScript($page,$control,$event,$source){
    & $helper -Action SelectPage -Name $page
    if($control){. $helper -Action Select -Name $control}else{. $helper -Action Select -Name $page}
    $tab=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::TabItem -and $_.Current.Name -match [regex]::Escape($event)}|Select-Object -First 1
    if(!$tab){throw "缺少事件 $page/$control/$event"}
    $tab.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    $doc=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Document -and !$_.Current.BoundingRectangle.IsEmpty -and $_.GetSupportedPatterns().ProgrammaticName -contains 'ValuePatternIdentifiers.Pattern'}|Sort-Object {$_.Current.BoundingRectangle.X}|Select-Object -Last 1
    if(!$doc){throw "缺少事件正文 $page/$control/$event"}
    $actual=$doc.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value
    $expected=Get-Content -LiteralPath (Join-Path $PSScriptRoot $source) -Raw -Encoding utf8
    if($actual.Replace([string][char]13,'').TrimEnd() -cne $expected.Replace([string][char]13,'').TrimEnd()){throw "事件全文不一致 $page/$control/$event"}
    $script:records += [ordered]@{Page=$page;Control=$control;Event=$event;Source=$source;Matches=$true;SHA256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $source)).Hash}
}
ReadScript 'P8_ServoControl' 'h13Service' '定时事件' 'HMI13事务服务.txt'
ReadScript 'P8_ServoControl' 'authorize' '按下事件' '事件/授权_按下.txt'
ReadScript 'P8_ServoControl' 'authorize' '弹起事件' '事件/授权_弹起.txt'
ReadScript 'P8_ServoControl' 'estop' '按下事件' '事件/急停_按下.txt'
ReadScript 'P8_ServoControl' 'backHome' '弹起事件' '事件/返回_弹起.txt'
ReadScript 'P8_ServoControl' 'rpmSlider' '值改变事件' '事件/滑块_滑动.txt'
ReadScript 'P8_ServoControl' '' '加载页面' '事件/P8_加载.txt'
ReadScript 'P8_ServoControl' '' '离开页面' '事件/P8_离开.txt'
ReadScript 'P1_Main' 'servoEntry' '按下事件' '事件/主页入口_按下.txt'
ReadScript 'P1_Main' 'servoEntry' '弹起事件' '事件/主页入口_弹起.txt'
ReadScript 'P0_Boot' 'refreshTick' '定时事件' '全局计龄刷新.txt'
ReadScript 'P0_Boot' 'mod' '数据接收' 'Default模式中断.txt'
$records|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/20号重开事件绑定_20261019.json') -Encoding utf8
$lifecycles=@()
foreach($page in @('P0_Boot','P1_Main','P2_Diag','P3_Position','P4_Info','P5_Alarm','P6_Sync','P7_System_Ext','P8_ServoControl')){
    foreach($event in @('加载页面','离开页面')){
        . $helper -Action SelectPage -Name $page
        . $helper -Action Select -Name $page
        $tab=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::TabItem -and $_.Current.Name -match [regex]::Escape($event)}|Select-Object -First 1
        if(!$tab){throw "缺少页面生命周期 $page/$event"}
        $tab.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
        $doc=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Document -and !$_.Current.BoundingRectangle.IsEmpty -and $_.GetSupportedPatterns().ProgrammaticName -contains 'ValuePatternIdentifiers.Pattern'}|Sort-Object {$_.Current.BoundingRectangle.X}|Select-Object -Last 1
        if(!$doc){throw "缺少生命周期正文 $page/$event"}
        $script=$doc.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value
        $baseline=Get-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/全页面加载离开事件读回_20261016.json') -Raw -Encoding utf8|ConvertFrom-Json
        $expected=$baseline|Where-Object {$_.Page -eq $page -and $_.Event -match [regex]::Escape($event)}|Select-Object -First 1
        if($null -eq $expected -or $script.Replace([string][char]13,'').TrimEnd() -cne $expected.Script.Replace([string][char]13,'').TrimEnd()){throw "页面生命周期与基线不符 $page/$event"}
        $lifecycles += [ordered]@{Page=$page;Event=$event;Script=$script;Matches=$true}
    }
}
$lifecycles|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/20号全页生命周期_20261019.json') -Encoding utf8
. $helper -Action SelectPage -Name P8_ServoControl
. $helper -Action Select -Name h13
$stateProps=@(Props|ForEach-Object {$_.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value})
if($stateProps[2] -ne 'h13' -or $stateProps[5] -ne '160'){throw '状态数组容量读回不符'}
. $helper -Action Select -Name authorize
$tabs=@(Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::TabItem}|ForEach-Object {$_.Current.Name})
if(!($tabs -match '^TOGGLE_BUTTON.按下事件') -or !($tabs -match '^TOGGLE_BUTTON.弹起事件')){throw '授权按钮类型或事件读回不符'}
. $helper -Action SelectPage -Name P0_Boot
. $helper -Action Select -Name mod
$modProps=@(Props|ForEach-Object {$_.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value})
if($modProps[4] -ne 'Default' -or $modProps[5] -ne '255'){throw 'Default接收模式或容量读回不符'}
$tools=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Text -and $_.Current.Name -eq '工具' -and !$_.Current.BoundingRectangle.IsEmpty}|Select-Object -First 1
if(!$tools){throw '未找到工具菜单'}
$rect=$tools.Current.BoundingRectangle;Click ([int]($rect.X+$rect.Width/2)) ([int]($rect.Y+$rect.Height/2))
$item=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Text -and $_.Current.Name -eq '项目设置' -and !$_.Current.BoundingRectangle.IsEmpty}|Select-Object -First 1
if(!$item){throw '未找到项目设置'}
$rect=$item.Current.BoundingRectangle;Click ([int]($rect.X+$rect.Width/2)) ([int]($rect.Y+$rect.Height/2))
$dialog=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Window -and $_.Current.Name -eq '项目设置'}|Select-Object -First 1
if(!$dialog){throw '项目设置未打开'}
$children=$dialog.FindAll('Descendants',[System.Windows.Automation.Condition]::TrueCondition)
$master=$children|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::CheckBox -and $_.Current.Name -eq '主机'}|Select-Object -First 1
if(!$master){throw '未找到主机配置'}
$masterState=$master.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern).Current.ToggleState.ToString()
if($masterState -ne 'On'){throw '主机模式未启用'}
$cancel=$children|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Button -and $_.Current.Name -eq '取消'}|Select-Object -First 1
if(!$cancel){throw '缺少取消按钮'}
$cancel.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
. $helper -Action Keys -Value '^s'
$compiledAfter=Get-Date
. $helper -Action Keys -Value '^b'
Start-Sleep -Seconds 5
if((Get-Item -LiteralPath (Join-Path $PSScriptRoot '17_轨道巡检车.pix.bin')).LastWriteTime -lt $compiledAfter){throw '本次未生成新的BIN，禁止复用历史编译输出'}
. $helper -Action Inspect|Out-Null
$output=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Document -and $_.GetSupportedPatterns().ProgrammaticName -contains 'TextPatternIdentifiers.Pattern'}|ForEach-Object {$_.GetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern).DocumentRange.GetText(-1)}|Where-Object {$_ -match '编译成功|编译失败'}|Select-Object -First 1
if(!$output -or $output -notmatch '编译成功' -or $output -match '编译失败'){throw '20号编译失败或结果未知'}
$output|Set-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/20号VisualPIX编译_20261019.txt') -Encoding utf8
[ordered]@{Project=$PSScriptRoot;Reopened=$true;PIX=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot '17_轨道巡检车.pix')).Hash;StateArrayLength=161;StateArrayMaxIndex=160;AuthorizeControlType='TOGGLE_BUTTON';Mode='Default';RxBuffer='255';Master=$masterState;BufferPollEnabled=$false;ReadbackOnly=$true;PhysicalCommunicationVerified=$false;MotionVerified=$false}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/20号配置重开核验_20261019.json') -Encoding utf8
Write-Output $output
Write-Output '20号十二项事件及关键配置重开读回通过'
