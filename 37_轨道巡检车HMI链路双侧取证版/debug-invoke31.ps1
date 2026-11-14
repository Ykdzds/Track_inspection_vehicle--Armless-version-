. (Join-Path $PSScriptRoot 'pix-ui.ps1') -Action Tree|Out-Null
$d=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Window -and $_.Current.Name -eq '调试'}|Select-Object -First 1
$buttons=$d.FindAll('Descendants',[System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ControlTypeProperty,[System.Windows.Automation.ControlType]::Button))
foreach($b in $buttons){if($b.Current.BoundingRectangle.Y -lt 200){Write-Output ($b.Current.BoundingRectangle.ToString()+' '+($b.GetSupportedPatterns().ProgrammaticName -join ','))}}
$b=$buttons|Where-Object {$_.Current.BoundingRectangle.X -eq 1099}|Select-Object -First 1
$b.SetFocus();[System.Windows.Forms.SendKeys]::SendWait(' ');Start-Sleep -Milliseconds 300;$b.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
Start-Sleep -Milliseconds 300