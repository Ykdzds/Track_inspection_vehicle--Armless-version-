. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'));$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string](Join-Path $PSScriptRoot '17_轨道巡检车.pix'))
$out31=Join-Path $PSScriptRoot '运行核验/31号实施_20261101/资源读回';[IO.Directory]::CreateDirectory($out31)|Out-Null
foreach($im31 in $proj.Images|Where-Object {$_.Name -match '^(Day_|Night_|sliderTouch)'}){[IO.File]::WriteAllBytes((Join-Path $out31 ($im31.Name+$im31.Extension)),$im31.Data)}
Write-Output ('Exported '+(Get-ChildItem -LiteralPath $out31).Count+' embedded images')