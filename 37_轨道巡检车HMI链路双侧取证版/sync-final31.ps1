. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'));$fp31=Join-Path $PSScriptRoot '17_轨道巡检车.pix';$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string]$fp31)
$map31=Get-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/31号实施_20261101/资源索引绑定.json') -Raw|ConvertFrom-Json
function Resolve31($s31){[regex]::Replace($s31,'user\.(image|font)\.[A-Za-z0-9_]+',{param($m31);$key31=$m31.Value;if(-not $map31.PSObject.Properties[$key31]){throw ('Missing '+$key31)};[string]$map31.$key31})}
($proj.Layers[0].Elements|Where-Object Name -eq 'refreshTick').Events[0].Code=Resolve31 ([IO.File]::ReadAllText((Join-Path $PSScriptRoot '全局计龄刷新.txt')))
($proj.Layers[8].Elements|Where-Object Name -eq 'rpmSlider').Events[0].Code=Resolve31 ([IO.File]::ReadAllText((Join-Path $PSScriptRoot '事件/滑块_滑动.txt')))
$proj.Layers[8].Events[0].Code=Resolve31 ([IO.File]::ReadAllText((Join-Path $PSScriptRoot '事件/P8_加载.txt')))
$io.wHAMcrUVnVnATbRNFqXJTFWDNjDo($proj,[string]$fp31)
Write-Output 'Display source and bound events synchronized'