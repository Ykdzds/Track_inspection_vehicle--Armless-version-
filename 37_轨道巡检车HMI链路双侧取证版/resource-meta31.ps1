. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
foreach($n in @('Visual_PIX.Model.PixLayer','Visual_PIX.Model.Controls.TextCtrl','Visual_PIX.Model.Controls.ImageCtrl','Visual_PIX.Model.Controls.PropertyIdDefinition','Visual_PIX.Model.Controls.PropertyConst','Visual_PIX.Model.Controls.PropertyConsts','LHNDqqIicLDDptgwnLaDrksySGEp')){
$t=$vp.GetType($n);Write-Output $n
foreach($p in $t.GetProperties()|Where-Object {$_.Name -match 'ImageId|FontId|Index|Value|Id|Name'}){Write-Output $p.ToString();foreach($a in $p.GetCustomAttributes($true)){Write-Output ('attr '+$a.GetType().FullName);$a|Format-List *}}
$t.GetFields($flags)|Where-Object {$_.IsStatic}|ForEach-Object {try{Write-Output ($_.Name+'='+$_.GetValue($null))}catch{}}
if($n -eq 'LHNDqqIicLDDptgwnLaDrksySGEp'){$t.GetMethods($flags)|Where-Object {$_.DeclaringType -eq $t}|ForEach-Object {$_.ToString()}}
}
$vp.GetType('Visual_PIX.Model.FontData').BaseType.GetProperties()|ForEach-Object {$_.ToString()};$vp.GetType('Visual_PIX.Model.PixelFont').GetProperties()|ForEach-Object {$_.ToString()}