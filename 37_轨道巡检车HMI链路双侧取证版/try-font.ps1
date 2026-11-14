. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$opt=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.FontConverter.PixelFontOption'))
$opt.FontName='微软雅黑';$opt.PixelHeight=24;$opt.Chars=' 0123456789-./rpm/skmh轨道巡检车实际目标转速等效';$opt.FontType='AntiAliasing4';$opt.CharRange=[Enum]::Parse($opt.CharRange.GetType(),'指定字符')
$type=$vp.GetType('Visual_PIX.Modules.FontConverter.ViewModels.FontConverterViewModel')
$obj=[Runtime.Serialization.FormatterServices]::GetUninitializedObject($type)
try{
 $font=$type.GetMethod('iRQrnlxjjRtUcaagoMIHqaUZEWgW',$flags).Invoke($obj,[object[]]@($opt,$false))
 Write-Output 'FONT RESULT';$font|Format-List
}catch{Write-Output $_.Exception.ToString();Write-Output $_.Exception.InnerException.ToString()}
$t=$vp.GetType('pYrovclfpRcCikDswOBFioOyMKXV');Write-Output 'WORKER';$t.GetFields($flags)|ForEach-Object {$_.ToString()};$t.GetProperties($flags)|ForEach-Object {$_.ToString()}
