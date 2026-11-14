. (Join-Path $PSScriptRoot 'pix-ui.ps1') -Action Inspect
$debug33=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::MenuItem -and $_.FindFirst('Descendants',[System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty,'调试 (_D)'))}|Select-Object -First 1
$debug33.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
Start-Sleep -Milliseconds 200
Nodes | Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::MenuItem -or $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Text -and $_.Current.BoundingRectangle.Y -lt 400} | ForEach-Object {'{0}|{1}|{2}|{3}' -f $_.Current.ControlType.ProgrammaticName,$_.Current.Name,($_.GetSupportedPatterns().ProgrammaticName -join ','),$_.Current.BoundingRectangle}
