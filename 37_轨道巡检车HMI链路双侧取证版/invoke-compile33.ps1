$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'pix-ui.ps1') -Action Inspect
$debug33=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::MenuItem -and $_.FindFirst('Descendants',[System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty,'调试 (_D)'))}|Select-Object -First 1
$debug33.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
$command33=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::MenuItem -and $_.GetSupportedPatterns().ProgrammaticName -contains 'InvokePatternIdentifiers.Pattern' -and $_.FindFirst('Descendants',[System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty,'编译'))}|Select-Object -First 1
if(!$command33){throw 'Compile command missing'}
$command33.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
Start-Sleep -Seconds 2
Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Document -and $_.GetSupportedPatterns().ProgrammaticName -contains 'TextPatternIdentifiers.Pattern'}|ForEach-Object {$_.GetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern).DocumentRange.GetText(-1)}|Where-Object {$_.Length -lt 12000}
