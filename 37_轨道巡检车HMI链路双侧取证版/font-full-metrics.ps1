. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$type=$vp.GetType('Visual_PIX.Modules.FontConverter.ViewModels.FontConverterViewModel')
$obj=[Runtime.Serialization.FormatterServices]::GetUninitializedObject($type)
$chars=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'ui30\characters.txt'))
foreach($n in @(14,15,16,17,18,19,22,23,24,25,26,27)){
 $opt=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.FontConverter.PixelFontOption'))
 $opt.FontName='微软雅黑';$opt.PixelHeight=$n;$opt.Chars=$chars;$opt.FontType='AntiAliasing4';$opt.CharRange=[Enum]::Parse($opt.CharRange.GetType(),'指定字符')
 $font=$type.GetMethod('iRQrnlxjjRtUcaagoMIHqaUZEWgW',$flags).Invoke($obj,[object[]]@($opt,$false))
 Write-Output ('input '+$n+' actual '+$font.PixelHeight+' maxwidth '+$font.MaxPixelWidth)
}
