. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'));$fp31=Join-Path $PSScriptRoot '17_轨道巡检车.pix';$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string]$fp31)
$im31=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.ImageData'));$im31.Data=[IO.File]::ReadAllBytes((Join-Path $PSScriptRoot 'ui31/sliderTouchTransparent.png'));$im31.Id=[guid]::NewGuid().ToString();$im31.Name='sliderTouchTransparent';$im31.Extension='.png';$im31.Index=$proj.Images.Count;$im31.IsPng=$true;$im31.Locked=$true;$proj.Images.Add($im31)
$mapfile31=Join-Path $PSScriptRoot '运行核验/31号实施_20261101/资源索引绑定.json';$map31=Get-Content -LiteralPath $mapfile31 -Raw|ConvertFrom-Json;$map31|Add-Member -NotePropertyName 'user.image.sliderTouchTransparent' -NotePropertyValue ($im31.Index+1);$map31|ConvertTo-Json|Set-Content -LiteralPath $mapfile31 -Encoding UTF8
foreach($imx31 in $proj.Images|Where-Object {$_.Name -in @('Day_P8_ServoControl','Night_P8_ServoControl')}){$imx31.Data=[IO.File]::ReadAllBytes((Join-Path $PSScriptRoot ('ui31/'+$imx31.Name+'.jpg')))}
$p831=$proj.Layers[8];$sl31=$p831.Elements|Where-Object Name -eq 'rpmSlider';$hi31=$p831.Elements|Where-Object Name -eq 'sliderSpeedHint';$sl31.BackgroundType='Image';$sl31.ImageId=$im31.Id
$p831.Elements.Remove($hi31)|Out-Null;$p831.Elements.Insert($p831.Elements.IndexOf($sl31),$hi31)
foreach($pg31 in $proj.Layers){foreach($ob31 in @($pg31)+@($pg31.Elements)){foreach($evt31 in $ob31.Events){if($evt31.Code){$evt31.Code=[regex]::Replace($evt31.Code,'P8_ServoControl.rpmSlider.bgColor=-?\d+;',('P8_ServoControl.rpmSlider.bgImg='+($im31.Index+1)+';'))}}}}
$io.wHAMcrUVnVnATbRNFqXJTFWDNjDo($proj,[string]$fp31)
Write-Output 'Transparent slider background and label below original slider saved'