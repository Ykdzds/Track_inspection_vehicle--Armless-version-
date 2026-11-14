param([switch]$AuditOnly)
. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$projectPath=Join-Path $PSScriptRoot '17_轨道巡检车.pix'
$evidence=Join-Path $PSScriptRoot '运行核验\30号实施_20261101'
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'))
$sourcePath=if($AuditOnly){$projectPath}else{Join-Path (Split-Path $PSScriptRoot -Parent) '29_轨道巡检车跨页运行与伺服快捷导航版\17_轨道巡检车.pix'}
$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string]$sourcePath)
function ExportModel($file){
 $pages=foreach($page in $proj.Layers){
  $objects=foreach($c in $page.Elements){
   $props=[ordered]@{}
   foreach($p in $c.GetType().GetProperties()){
    if($p.CanRead -and $p.GetIndexParameters().Count -eq 0 -and ($p.PropertyType.IsPrimitive -or $p.PropertyType.IsEnum -or $p.PropertyType -eq [string])){ $props[$p.Name]=$p.GetValue($c,$null) }
   }
   [ordered]@{Name=$c.Name;Id=$c.Id;Type=$c.GetType().FullName;Properties=$props;Events=@($c.Events|ForEach-Object {[ordered]@{Id=$_.Id;Type=[string]$_.Type;Name=$_.Name;Code=$_.Code;Enable=$_.Enable}})}
  }
  [ordered]@{Name=$page.Name;Id=$page.Id;BackgroundType=[string]$page.BackgroundType;ImageId=$page.ImageId;Controls=@($objects);Events=@($page.Events|ForEach-Object {[ordered]@{Id=$_.Id;Type=[string]$_.Type;Name=$_.Name;Code=$_.Code;Enable=$_.Enable}})}
 }
 $config=[ordered]@{};foreach($p in $proj.GetType().GetProperties()){if($p.CanRead -and ($p.PropertyType.IsPrimitive -or $p.PropertyType.IsEnum -or $p.PropertyType -eq [string])){$config[$p.Name]=$p.GetValue($proj,$null)}}
 [ordered]@{Config=$config;Pages=@($pages);Fonts=@($proj.Fonts|ForEach-Object {[ordered]@{Id=$_.Id;Name=$_.Name;Source=$_.Font.SourceFontName;Height=$_.Font.PixelHeight;GlyphCount=$_.Font.Chars.Count;Characters=(-join ($_.Font.Chars|ForEach-Object {$_.Char}))}});Images=@($proj.Images|ForEach-Object {[ordered]@{Id=$_.Id;Name=$_.Name;Bytes=$_.Data.Length}})}|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $file -Encoding UTF8
}
if($AuditOnly){ExportModel (Join-Path $evidence '30号实际工程读回.json');exit}
ExportModel (Join-Path $evidence '29号完整模型基线.json')
$layout=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'ui30\layout.json') -Raw -Encoding UTF8|ConvertFrom-Json
function Color($hex){[Drawing.ColorTranslator]::FromHtml($hex).ToArgb()}
$fontIds=@{}
$ft=$vp.GetType('Visual_PIX.Modules.FontConverter.ViewModels.FontConverterViewModel')
$fc=[Runtime.Serialization.FormatterServices]::GetUninitializedObject($ft)
$chars=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'ui30\characters.txt'))
foreach($size in @(20,24,32,36)){
 $opt=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.FontConverter.PixelFontOption'))
 $opt.FontName='微软雅黑';$opt.PixelHeight=@{20=14;24=18;32=22;36=26}[$size];$opt.Chars=$chars;$opt.FontType='AntiAliasing4';$opt.CharRange=[Enum]::Parse($opt.CharRange.GetType(),'指定字符')
 $font=$ft.GetMethod('iRQrnlxjjRtUcaagoMIHqaUZEWgW',$flags).Invoke($fc,[object[]]@($opt,$false))
 $data=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.FontData'))
 $data.Id=[guid]::NewGuid().ToString();$data.Font=$font;$data.Name='UI30_微软雅黑'+$size;$data.Index=$proj.Fonts.Count+1
 $proj.Fonts.Add($data);$fontIds[$size]=$data.Id
 Write-Output ('字体 '+$size+' 高度 '+$font.PixelHeight+' 字符 '+$font.Chars.Count)
}
$imageIds=@{}
foreach($file in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'ui30') -File|Where-Object {$_.Extension -in @('.jpg','.png') -and $_.Name -notmatch '^(nav|primary|danger)_(normal|pressed|disabled)\.png$'}){
 $im=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.ImageData'));$im.Data=[IO.File]::ReadAllBytes($file.FullName);$im.Id=[guid]::NewGuid().ToString();$im.Name=$file.BaseName;$im.Extension=$file.Extension;$im.Index=$proj.Images.Count;$im.IsPng=$file.Extension -eq '.png';$proj.Images.Add($im);$imageIds[$file.BaseName]=$im.Id
}
function SetDisplay($c,$spec){
 $c.X=[int]$spec.x;$c.Y=[int]$spec.y;$c.Width=[int]$spec.w;$c.Height=[int]$spec.h
 if($c.PSObject.Properties['FontId']){$c.FontId=$fontIds[[int]$spec.font];$c.FontColor=Color '#EDF5FF'}
 if($c.PSObject.Properties['Text']){
  if($spec.PSObject.Properties['text']){$c.Text=$spec.text}
  if($c.PSObject.Properties['MaxLength']){$c.MaxLength=512}
  if($c.PSObject.Properties['MaxTextLength']){$c.MaxTextLength=512}
 }
 if($c.GetType().Name -eq 'TextCtrl'){
  $c.BackgroundType='Color';$c.BackgroundColor=Color '#14243B'
  if($spec.background){$c.BackgroundColor=Color $spec.background}
  if($spec.color){$c.FontColor=Color $spec.color}
  $c.HorizontalAlignment='LEFT';$c.VerticalAlignment='TOP';$c.WordWrap=$true
 }
 if($c.GetType().Name -in @('ButtonCtrl','ToggleButtonCtrl')){
  $kind=if($c.Name -eq 'estop'){'danger'}elseif($c.Name -in @('servoEntry','authorize')){'primary'}else{'nav'}
  $stem=$spec.w.ToString()+'x'+$spec.h.ToString()
  $c.BackgroundType='ImageToggle';$c.ImageId1=$imageIds[$kind+'_normal_'+$stem];$c.ImageId2=$imageIds[$kind+'_pressed_'+$stem];$c.DisableBackgroundImageId=$imageIds[$kind+'_disabled_'+$stem]
  if(!$c.ImageId1){throw ('缺少按钮素材 '+$kind+' '+$stem)}
 }
}
foreach($page in $proj.Layers){
 $spec=$layout.pages.($page.Name);$page.BackgroundType='Image';$page.ImageId=$imageIds[$page.Name]
 foreach($c in $page.Elements){$s=$spec.controls.($c.Name);if($s){SetDisplay $c $s}}
 foreach($s in $spec.add){
  if($page.Elements.Name -contains $s.name){throw ('重复控件 '+$s.name)}
  $c=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.Controls.TextCtrl'));$c.Id=$page.GetNextElementId();$c.Name=$s.name;$c.Text=$s.text;$c.IsGlobal=[bool]$s.global_;$c.Visbile=$true;SetDisplay $c $s;$page.Elements.Add($c)
 }
 Write-Output ('完成 '+$page.Name+' 控件 '+$page.Elements.Count)
}
$slider=$proj.Layers[8].Elements|Where-Object {$_.Name -eq 'rpmSlider'}
$slider.BackgroundColor=Color '#253D56';$slider.ThumbColor=Color '#22D3EE';$slider.ThumbWidth=32
$progress=$proj.Layers[0].Elements|Where-Object {$_.Name -eq 'bootProgress'}
$progress.BackgroundColor=Color '#253D56';$progress.ForegroundColor=Color '#22D3EE'
foreach($p in $progress.GetType().GetProperties()){if($p.Name -match 'Color'){Write-Output ('progress '+$p.Name)}}
# Replace the existing refresh event only. All motion and navigation events remain untouched.
$tick=$proj.Layers[0].Elements|Where-Object {$_.Name -eq 'refreshTick'}
$tick.Events[0].Code=[IO.File]::ReadAllText((Join-Path $PSScriptRoot '全局计龄刷新.txt'))
$io.wHAMcrUVnVnATbRNFqXJTFWDNjDo($proj,[string]$projectPath)
Write-Output '30号正式PIX已保存'
$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string]$projectPath)
ExportModel (Join-Path $evidence '30号实际工程读回.json')
