. (Join-Path $PSScriptRoot 'pix-ui.ps1') -Action Inspect
Nodes | Where-Object { $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Document } | ForEach-Object {
 $_.GetSupportedPatterns().ProgrammaticName
 if($_.GetSupportedPatterns().ProgrammaticName -contains 'TextPatternIdentifiers.Pattern'){$_.GetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern).DocumentRange.GetText(-1)}
}
Nodes | Where-Object { $_.Current.BoundingRectangle.Y -lt 190 } | ForEach-Object {
 '{0}|{1}|{2}|{3}|{4}' -f $_.Current.ControlType.ProgrammaticName,$_.Current.Name,$_.Current.AutomationId,($_.GetSupportedPatterns().ProgrammaticName -join ','),$_.Current.HelpText
}
