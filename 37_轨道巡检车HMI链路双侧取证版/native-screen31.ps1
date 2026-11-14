. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'));$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string](Join-Path $PSScriptRoot '17_轨道巡检车.pix'))
$t=$proj.GetType();$t.GetProperties($flags)|Where-Object {$_.Name -match 'Screen|Layer'}|ForEach-Object {$_.Name+' '+$_.CanWrite+' '+$_.GetGetMethod($true)}
$t.GetFields($flags)|Where-Object {$_.FieldType.Name -eq 'RpgRniqBlWjCFiSagdbuqoKvDXsaA'}|ForEach-Object {$_.ToString()}
$st=$vp.GetType('RpgRniqBlWjCFiSagdbuqoKvDXsaA');$screen=[Activator]::CreateInstance($st);$screen.Width=1024;$screen.Height=600
try{$proj.Screen=$screen;Write-Output ('screen set '+$proj.GetActualScreenWidth())}catch{Write-Output $_.Exception.Message}