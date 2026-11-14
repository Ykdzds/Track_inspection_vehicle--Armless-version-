. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'));$fp31=Join-Path $PSScriptRoot '17_轨道巡检车.pix';$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string]$fp31)
foreach($im31 in $proj.Images|Where-Object Name -like 'Night_primary_pressed_*'){$im31.Data=[IO.File]::ReadAllBytes((Join-Path $PSScriptRoot ('ui31/'+$im31.Name+'.png')))}
$io.wHAMcrUVnVnATbRNFqXJTFWDNjDo($proj,[string]$fp31)