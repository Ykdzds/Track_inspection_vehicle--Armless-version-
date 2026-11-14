. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'))
$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string](Join-Path $PSScriptRoot '17_轨道巡检车.pix'))
$proj.Images[0]|Format-List *; $proj.Images[61]|Format-List *; $proj.GetType().GetProperties()|ForEach-Object {$_.ToString()}
$proj.Fonts[8]|Format-List *
$vp.GetTypes()|Where-Object {$_.Name -match 'Compil|Resourc|Debug'}|ForEach-Object {$_.FullName}
foreach($name in @('Visual_PIX.Modules.Home.ViewModels.DebugViewModel','Visual_PIX.Model.ImageData','Visual_PIX.Model.FontData')){$t=$vp.GetType($name);Write-Output $name;$t.GetMethods($flags)|Where-Object {$_.DeclaringType -eq $t}|ForEach-Object {$_.ToString()};$t.GetFields($flags)|ForEach-Object {$_.ToString()}}
$vp.GetTypes()|Where-Object {$_.Namespace -eq 'Visual_PIX.Model.Controls'}|ForEach-Object {$_.FullName};$vp.GetType('Visual_PIX.Model.Controls.ImageCtrl').GetProperties()|ForEach-Object {$_.ToString()}