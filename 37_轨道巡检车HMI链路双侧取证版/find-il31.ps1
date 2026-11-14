param([string]$Method)
. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$ty=$vp.GetTypes()|Where-Object {$_.GetMethods($flags).Name -contains $Method}|Select-Object -First 1
& (Join-Path $PSScriptRoot 'dump-il31.ps1') -TypeName $ty.FullName -MethodName $Method