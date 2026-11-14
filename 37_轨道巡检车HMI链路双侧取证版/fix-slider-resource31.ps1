. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'));$fp31=Join-Path $PSScriptRoot '17_轨道巡检车.pix';$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string]$fp31)
foreach($pg31 in $proj.Layers){foreach($ob31 in @($pg31)+@($pg31.Elements)){foreach($evt31 in $ob31.Events){if($evt31.Code){$evt31.Code=[regex]::Replace($evt31.Code,'P8_ServoControl.rpmSlider.bgImg=[^;]+;','')}}}}
$io.wHAMcrUVnVnATbRNFqXJTFWDNjDo($proj,[string]$fp31)