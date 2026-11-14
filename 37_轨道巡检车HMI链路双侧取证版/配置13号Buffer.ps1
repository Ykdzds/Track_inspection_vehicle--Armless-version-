throw '29号工程请使用安装29号事件.ps1或实施29号导航.ps1；本历史脚本禁止执行。'
param([switch]$AuditOnly)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient
$automation=[System.Windows.Automation.AutomationElement]
$condition=[System.Windows.Automation.Condition]::TrueCondition
$windows=$automation::RootElement.FindAll('Children',$condition)
$main=@($windows | Where-Object {$_.Current.Name -match '\[.*13_轨道巡检车UART5共同目标转速控制版\\13_轨道巡检车\.pix\]'})
if($main.Count -ne 1){throw '必须仅打开一个13号目标工程'}
$dialog=$windows | Where-Object {$_.Current.ProcessId -eq $main[0].Current.ProcessId -and $_.Current.Name -eq '集合编辑器'} | Select-Object -First 1
if(!$dialog){throw '请先打开13号工程的Buffer集合编辑器'}
$grid=$dialog.FindFirst('Descendants',[System.Windows.Automation.PropertyCondition]::new($automation::ControlTypeProperty,[System.Windows.Automation.ControlType]::DataGrid))
$gridPattern=$grid.GetCurrentPattern([System.Windows.Automation.GridPattern]::Pattern)
$headers=$grid.GetCurrentPattern([System.Windows.Automation.TablePattern]::Pattern).Current.GetColumnHeaders()
$columns=@{}
for($column=0;$column -lt $headers.Count;$column++){
    $title=$headers[$column].Current.Name
    if($title){$columns[$title]=$column}
}
foreach($title in @('序号','从机地址','数据类型','起始地址','数量','读取次数','循环读取间隔时间（ms）','读取超时时间（ms）','备注')){
    if(!$columns.ContainsKey($title) -or $headers[$columns[$title]].Current.BoundingRectangle.IsEmpty){throw "缺少主站列：$title；先确认项目设置勾选主机，禁止按旧列号写入"}
}
if($gridPattern.Current.RowCount -ne 8){throw '必须有且仅有8个Buffer，禁止隐式添加或删除'}
$areas=@(@(0,64,'系统与双轴'),@(80,32,'通信统计'),@(112,32,'位置与同步'),@(144,32,'告警与外围'),@(176,32,'能力与诊断'),@(512,24,'R2元数据'),@(288,20,'R2命令结果'),@(1120,64,'R2控制状态'))
function GetCell($row,$title){$gridPattern.GetItem($row,$columns[$title])}
function ReadCell($row,$title){(GetCell $row $title).GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value}
function WriteCell($row,$title,$value){
    $cell=GetCell $row $title
    if((ReadCell $row $title) -eq [string]$value){return}
    $cell.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    $edit=$cell.FindFirst('Descendants',[System.Windows.Automation.PropertyCondition]::new($automation::ControlTypeProperty,[System.Windows.Automation.ControlType]::Edit))
    if(!$edit){throw "Buffer$row $title 没有真实编辑器"}
    $edit.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).SetValue([string]$value)
    $edit.SetFocus()
    [System.Windows.Forms.SendKeys]::SendWait('{TAB}')
    if((ReadCell $row $title) -ne [string]$value){throw "Buffer$row $title 编辑提交失败"}
}
for($region=0;$region -lt 8;$region++){
    $typeNames=@((GetCell $region '数据类型').FindAll('Descendants',$condition) | ForEach-Object {$_.Current.Name})
    if($typeNames -notcontains '保持寄存器'){throw "Buffer$region 不是03H保持寄存器，停止提交"}
}
$readback=@()
for($region=0;$region -lt 8;$region++){
    $expected=[ordered]@{'序号'=$region;'从机地址'=1;'起始地址'=$areas[$region][0];'数量'=$areas[$region][1];'读取次数'=0;'循环读取间隔时间（ms）'=100;'读取超时时间（ms）'=50;'备注'=$areas[$region][2]}
    if(!$AuditOnly){foreach($title in $expected.Keys){if($title -ne '序号'){WriteCell $region $title $expected[$title]}}}
    $record=[ordered]@{Buffer=$region;Type='保持寄存器'}
    foreach($title in $expected.Keys){
        $actual=ReadCell $region $title
        if($actual -ne [string]$expected[$title]){throw "Buffer$region $title 不匹配：实际=$actual 预期=$($expected[$title])；不得点击确定"}
        $record[$title]=$actual
    }
    $readback+=$record
}
$evidence=[ordered]@{CapturedAt=(Get-Date).ToString('o');Source=$main[0].Current.Name;AuditOnly=[bool]$AuditOnly;GridColumns=$gridPattern.Current.ColumnCount;Headers=@($headers | ForEach-Object {$_.Current.Name});Rows=$readback}
$fileName=if($AuditOnly){'运行核验/Buffer重开核验_20261016.json'}else{'Buffer配置读回.json'}
$evidence | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $PSScriptRoot $fileName) -Encoding utf8
$buttonName=if($AuditOnly){'取消'}else{'确定'}
$button=$dialog.FindFirst('Descendants',[System.Windows.Automation.PropertyCondition]::new($automation::NameProperty,$buttonName))
$button.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
Write-Output '八区主站列、03H类型、地址、长度及默认停止读取已核验。'
