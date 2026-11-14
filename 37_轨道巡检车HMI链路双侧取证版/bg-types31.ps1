. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
[Enum]::GetValues($vp.GetType('Visual_PIX.Model.Controls.BackgroundType'))|ForEach-Object {Write-Output ($_.'value__'.ToString()+' '+$_.ToString())}