. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'));$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string](Join-Path $PSScriptRoot '17_轨道巡检车.pix'))
$proj.GetType().GetProperties()|ForEach-Object {Write-Output ($_.Name+' : '+$_.PropertyType.FullName)}
$vp.GetTypes()|Where-Object {$_.Name -match 'Function|UserFunc'}|ForEach-Object {Write-Output ($_.FullName)}

Write-Output ("UserMethodChars="+$proj.UserMethodCode.Length);Write-Output $proj.UserMethodCode.Substring(0,200)
$proj.GetType().GetFields($flags)|ForEach-Object {Write-Output ($_.Name+" : "+$_.FieldType.FullName)}

$proj.GetType().GetMethods($flags)|Where-Object {$_.Name -match "Generat|Compil|Method|Debug"}|ForEach-Object {$_.ToString()}
$g=$vp.GetType("WgsHpMfTlFFpaufXgwhdxAjqGiFJA");$g.GetMethods($flags)|Where-Object {$_.DeclaringType -eq $g}|ForEach-Object {$_.ToString()}

$vp.GetTypes()|Where-Object {$_.GetMethods($flags).Name -contains "tFNvAGptaLAuZbdwzCoplXHmwwbO"}|ForEach-Object {$_.FullName}
$vp.GetTypes()|Where-Object {$_.GetMethods($flags).Name -contains "OpBAoMsXgkEuYHScBqPNhOHapilh"}|ForEach-Object {$_.FullName}
