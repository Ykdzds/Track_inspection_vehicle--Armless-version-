. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
foreach($n in @('Visual_PIX.Modules.FontConverter.ViewModels.FontConverterViewModel','pYrovclfpRcCikDswOBFioOyMKXV','Visual_PIX.Model.ImageData','Visual_PIX.Model.PixelChar','Visual_PIX.Model.Controls.CtrlEvent')){
 $t=$vp.GetType($n); if(!$t){continue}; Write-Output ('TYPE '+$n)
 $t.GetConstructors($flags)|ForEach-Object {$_.ToString()}
 $t.GetProperties()|ForEach-Object {$_.ToString()}
 $t.GetMethods($flags)|Where-Object {$_.DeclaringType -eq $t -and ($_.ReturnType.FullName -eq 'Visual_PIX.Model.PixelFont' -or $_.GetParameters().ParameterType.FullName -contains 'Visual_PIX.Model.FontConverter.PixelFontOption')}|ForEach-Object {('STATIC '+$_.IsStatic+' '+$_.ToString())}
}
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'))
$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string](Join-Path $PSScriptRoot '17_轨道巡检车.pix'))
Write-Output 'font enum'; [Enum]::GetNames($vp.GetType('Visual_PIX.Model.PixelFontType'))
Write-Output 'events'; $proj.Layers[8].Elements[5].Events[0].GetType().GetProperties()|ForEach-Object {$_.ToString()}
$summary=foreach($page in $proj.Layers){[ordered]@{Page=$page.Name;Id=$page.Id;Elements=@($page.Elements|ForEach-Object {[ordered]@{Name=$_.Name;Id=$_.Id;Type=$_.GetType().FullName;X=$_.X;Y=$_.Y;Width=$_.Width;Height=$_.Height;Text=$_.Text;Global=$_.IsGlobal;Events=@($_.Events|ForEach-Object {[ordered]@{Event=$_.ToString();Code=$_.Code}})}})}}
$summary|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验\30号实施_20261101\baseline-objects.json') -Encoding UTF8
