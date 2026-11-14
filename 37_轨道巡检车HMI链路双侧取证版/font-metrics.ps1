. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$type=$vp.GetType('Visual_PIX.Modules.FontConverter.ViewModels.FontConverterViewModel')
$obj=[Runtime.Serialization.FormatterServices]::GetUninitializedObject($type)
foreach($n in @(15,18,20,24,27,32)){
 $opt=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.FontConverter.PixelFontOption'))
 $opt.FontName='微软雅黑';$opt.PixelHeight=$n;$opt.Chars='0-实际目标';$opt.FontType='AntiAliasing4';$opt.CharRange=[Enum]::Parse($opt.CharRange.GetType(),'指定字符')
 $font=$type.GetMethod('iRQrnlxjjRtUcaagoMIHqaUZEWgW',$flags).Invoke($obj,[object[]]@($opt,$false))
 Write-Output ('input '+$n+' actual '+$font.PixelHeight+' maxwidth '+$font.MaxPixelWidth+' data '+$font.Chars[1].Data.Length+' chars '+$font.Chars.Count)
}
Write-Output 'Image enums';foreach($n in @('Visual_PIX.Model.Controls.BackgroundType','Visual_PIX.Model.Controls.ButtonBackgroundType','Visual_PIX.Model.Controls.TextCtrl')){$t=$vp.GetType($n);if($t.IsEnum){[Enum]::GetNames($t)}}
