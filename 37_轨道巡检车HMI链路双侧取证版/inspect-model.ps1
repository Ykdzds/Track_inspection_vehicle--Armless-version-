. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
foreach($name in @('blFUmdHRIKgcqLnrHkXULnrUGQNn','Visual_PIX.Model.PixelFont','Visual_PIX.Model.PixLayer','Visual_PIX.Model.FontConverter.PixelFontOption')){
 $t=$vp.GetType($name); Write-Output "TYPE $name"
 $t.GetConstructors($flags)|ForEach-Object {$_.ToString()}
 $t.GetMethods($flags)|Where-Object {$_.DeclaringType -eq $t -and $_.Name -notmatch '^[\p{Cf}]'}|ForEach-Object {$_.ToString()}
}
$t=$vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn')
$obj=[Activator]::CreateInstance($t)
try{
 $proj=$t.GetMethod('rqkGbDgDKWruWOpLfoxBcumhVTMG',$flags).Invoke($obj,[object[]]@([string](Join-Path $PSScriptRoot '17_轨道巡检车.pix')))
 Write-Output ('LOADED '+$proj.Name)
 $proj|Format-List
 $proj.Layers[0]|Format-List
 $proj.Fonts[0]|Format-List
}catch{Write-Output $_.Exception.ToString(); if($_.Exception.InnerException){Write-Output $_.Exception.InnerException.ToString()}}
