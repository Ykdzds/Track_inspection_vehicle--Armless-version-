. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'));$fp31=Join-Path $PSScriptRoot '17_轨道巡检车.pix';$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string]$fp31)
foreach($pg31 in $proj.Layers){foreach($ob31 in @($pg31)+@($pg31.Elements)){foreach($evt31 in $ob31.Events){if($evt31.Code){$evt31.Code=$evt31.Code.Replace('sliderSpeedHint.txtHorAgn=1;','sliderSpeedHint.txtHorAgn=2;')}}}}
foreach($im31 in $proj.Images|Where-Object {$_.Name -like 'Day_*_disabled_*' -and $_.Name -match '_primary_|_danger_'}){$im31.Data=[IO.File]::ReadAllBytes((Join-Path $PSScriptRoot ('ui31/'+$im31.Name+'.png')))}
$io.wHAMcrUVnVnATbRNFqXJTFWDNjDo($proj,[string]$fp31)
Write-Output 'Final patch saved'