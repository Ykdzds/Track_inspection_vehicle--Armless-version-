. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'));$fp31=Join-Path $PSScriptRoot '17_轨道巡检车.pix';$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string]$fp31)
$im31=$proj.Images|Where-Object Name -eq 'sliderTouchTransparent'
if(!$im31){$im31=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.ImageData'));$im31.Id=[guid]::NewGuid().ToString();$im31.Name='sliderTouchTransparent';$im31.Extension='.png';$im31.Index=$proj.Images.Count;$im31.IsPng=$true;$proj.Images.Add($im31)}
$im31.Data=[IO.File]::ReadAllBytes((Join-Path $PSScriptRoot 'ui31/sliderTouchTransparent.png'));$im31.Locked=$true
$sl31=$proj.Layers[8].Elements|Where-Object Name -eq 'rpmSlider';$sl31.Id=36;$sl31.BackgroundType='Image';$sl31.ImageId=$im31.Id
foreach($imx31 in $proj.Images|Where-Object {$_.Name -in @('Day_P8_ServoControl','Night_P8_ServoControl')}){$imx31.Data=[IO.File]::ReadAllBytes((Join-Path $PSScriptRoot ('ui31/'+$imx31.Name+'.jpg')))}
$io.wHAMcrUVnVnATbRNFqXJTFWDNjDo($proj,[string]$fp31)
$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string]$fp31);$sl31=$proj.Layers[8].Elements|Where-Object Name -eq 'rpmSlider';Write-Output ('READBACK '+$sl31.Id+' '+$sl31.BackgroundType+' '+$sl31.ImageId+' IMAGE '+($proj.Images|Where-Object Name -eq 'sliderTouchTransparent').Id)
$map31=@{};foreach($im in $proj.Images){$map31['user.image.'+$im.Name]=[int]$im.Index+1};foreach($f in $proj.Fonts){$map31['user.font.'+$f.Name]=[int]$f.Index};$map31|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/31号实施_20261101/资源索引绑定.json') -Encoding UTF8