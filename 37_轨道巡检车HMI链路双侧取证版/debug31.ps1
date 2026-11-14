. (Join-Path $PSScriptRoot 'pix-ui.ps1') -Action SelectPage -Name P1_Main
$n=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::MenuItem -and $_.Current.BoundingRectangle.X -eq 162}|Select-Object -First 1
$n.GetSupportedPatterns().ProgrammaticName
$n.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
Start-Sleep -Milliseconds 500
$n=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::MenuItem -and $_.FindFirst('Descendants',[System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty,'调试'))}|Select-Object -Last 1
$n.GetSupportedPatterns().ProgrammaticName
$n.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
Start-Sleep -Seconds 2