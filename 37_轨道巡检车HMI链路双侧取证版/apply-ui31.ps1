param([switch]$AuditOnly)
. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$projectPath=Join-Path $PSScriptRoot '17_轨道巡检车.pix'
$evidence=Join-Path $PSScriptRoot '运行核验\31号实施_20261101'
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'))
$sourcePath=if($AuditOnly){$projectPath}else{Join-Path (Split-Path $PSScriptRoot -Parent) '30_轨道巡检车全界面科技风优化版\17_轨道巡检车.pix'}
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
if($AuditOnly){ExportModel (Join-Path $evidence '31号实际工程读回.json');exit}
ExportModel (Join-Path $evidence '30号完整模型基线.json')
$manifest=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'ui31/manifest.json') -Raw -Encoding UTF8|ConvertFrom-Json
function Color($hex){[Drawing.ColorTranslator]::FromHtml($hex).ToArgb()}
$fontIds=@{}
$ft=$vp.GetType('Visual_PIX.Modules.FontConverter.ViewModels.FontConverterViewModel')
$fc=[Runtime.Serialization.FormatterServices]::GetUninitializedObject($ft)
$chars=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'ui31/characters.txt'))
foreach($size in @(20,24,32,36)){
 $opt=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.FontConverter.PixelFontOption'))
 $opt.FontName='微软雅黑';$opt.PixelHeight=@{20=14;24=18;32=22;36=26}[$size];$opt.Chars=$chars;$opt.FontType='AntiAliasing4';$opt.CharRange=[Enum]::Parse($opt.CharRange.GetType(),'指定字符')
 $font=$ft.GetMethod('iRQrnlxjjRtUcaagoMIHqaUZEWgW',$flags).Invoke($fc,[object[]]@($opt,$false))
 if($font.PixelHeight -ne $size){throw ('字库高度不匹配 '+$size+' actual '+$font.PixelHeight)}
 $data=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.FontData'));$data.Id=[guid]::NewGuid().ToString();$data.Font=$font;$data.Name='UI31_'+$size;$data.Index=$proj.Fonts.Count+1
 $proj.Fonts.Add($data);$fontIds[$size]=$data.Id
 Write-Output ('字体 '+$data.Name+' 高度 '+$font.PixelHeight+' 字符 '+$font.Chars.Count)
}
$imageIds=@{}
foreach($file in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'ui31') -File|Where-Object {$_.Extension -in @('.jpg','.png')}){
 $im=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.ImageData'));$im.Data=[IO.File]::ReadAllBytes($file.FullName);$im.Id=[guid]::NewGuid().ToString();$im.Name=$file.BaseName;$im.Extension=$file.Extension;$im.Index=$proj.Images.Count;$im.IsPng=$file.Extension -eq '.png';$proj.Images.Add($im);$imageIds[$file.BaseName]=$im.Id
}
$oldFontSizes=@{};foreach($f in $proj.Fonts){$oldFontSizes[$f.Id]=[int]$f.Font.PixelHeight}
foreach($page in $proj.Layers){
 foreach($c in $page.Elements){
  $spec=$manifest.updates.($page.Name).($c.Name)
  if($c.PSObject.Properties['FontId'] -and $oldFontSizes.ContainsKey($c.FontId)){$size=$oldFontSizes[$c.FontId];if($fontIds.ContainsKey($size)){$c.FontId=$fontIds[$size]}}
  if($spec){foreach($prop in $spec.PSObject.Properties){if($prop.Name -eq 'FontSize31'){$c.FontId=$fontIds[[int]$prop.Value]}else{$c.($prop.Name)=$prop.Value}}}
 }
 foreach($s in $manifest.add|Where-Object {$_.Page -eq $page.Name}){
  $c=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.Controls.'+$s.Type));$c.Id=$page.GetNextElementId()
  foreach($p in $s.PSObject.Properties){if($p.Name -in @('Page','Type')){continue};if($p.Name -eq 'FontSize31'){$c.FontId=$fontIds[[int]$p.Value]}else{$c.($p.Name)=$p.Value}}
  if($s.Type -eq 'VarCtrl'){$c.VarType='Int';if($s.Name -ne 'themeMode'){$role=$s.Name.Substring(5).ToLower();$c.Int=Color $manifest.palettes.Day.$role}}
  if($s.Type -eq 'TextCtrl'){$c.Visbile=$true;$c.HorizontalAlignment='LEFT';$c.VerticalAlignment='TOP';$c.MaxLength=512}
  if($s.Type -eq 'ButtonCtrl'){
   $c.Visbile=$true;$c.MaxTextLength=128
   $template=$proj.Layers[1].Elements|Where-Object Name -eq 'openDiag'
   foreach($old in $template.Events){$evt=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.Controls.CtrlEvent'));$evt.Id=$old.Id;$evt.Type=$old.Type;$evt.Enable=$true;$c.Events.Add($evt)}
  }
  $page.Elements.Add($c)
 }
 $page.BackgroundType='Image';$page.ImageId=$imageIds['Day_'+$page.Name]
 foreach($c in $page.Elements){
  if($c.GetType().Name -eq 'TextCtrl'){
   $c.FontColor=Color $manifest.palettes.Day.ink
   $old=$c.BackgroundColor
   $c.BackgroundType='Color'
   $c.BackgroundColor=if($c.Name -eq 'sliderSpeedHint'){Color $manifest.palettes.Day.track}elseif($old -eq (Color '#0B1426')){Color $manifest.palettes.Day.bg}else{Color $manifest.palettes.Day.card}
  }
  if($c.GetType().Name -in @('ButtonCtrl','ToggleButtonCtrl')){
   $kind=if($c.Name -eq 'estop'){'danger'}elseif($c.Name -in @('servoEntry','authorize','themeToggle')){'primary'}else{'nav'}
   $stem=$c.Width.ToString()+'x'+$c.Height.ToString();$c.BackgroundType='ImageToggle'
   $c.ImageId1=$imageIds['Day_'+$kind+'_normal_'+$stem];$c.ImageId2=$imageIds['Day_'+$kind+'_pressed_'+$stem];$c.DisableBackgroundImageId=$imageIds['Day_'+$kind+'_disabled_'+$stem]
   $c.FontColor=if($kind -eq 'nav'){Color $manifest.palettes.Day.ink}else{Color '#FFFFFF'}
   if(!$c.ImageId1){throw ('皮肤缺失 '+$page.Name+'.'+$c.Name)}
  }
  if($c.GetType().Name -eq 'SliderCtrl'){$c.BackgroundType='Image';$c.ImageId=$imageIds['sliderTouchTransparent'];$c.BackgroundColor=Color $manifest.palettes.Day.track;$c.ThumbColor=Color $manifest.palettes.Day.thumb}
  if($c.GetType().Name -eq 'ProgressBarCtrl'){$c.BackgroundColor=Color $manifest.palettes.Day.track;$c.ForegroundColor=Color $manifest.palettes.Day.accent}
 }
 $theme=[IO.File]::ReadAllText((Join-Path $PSScriptRoot ('事件31/'+$page.Name+'_主题.txt')))
 if($page.Name -eq 'P8_ServoControl'){$page.Events[0].Code=[IO.File]::ReadAllText((Join-Path $PSScriptRoot '事件/P8_加载.txt'))}
 else{$page.Events[0].Code=($page.Events[0].Code+[Environment]::NewLine+$theme)}
 $page.Events[0].Enable=$true
 Write-Output ('完成 '+$page.Name+' '+$page.Elements.Count)
}
($proj.Layers[1].Elements|Where-Object Name -eq 'themeToggle').Events[1].Code=[IO.File]::ReadAllText((Join-Path $PSScriptRoot '事件31/主题切换_弹起.txt'))
($proj.Layers[8].Elements|Where-Object Name -eq 'rpmSlider').Events[0].Code=[IO.File]::ReadAllText((Join-Path $PSScriptRoot '事件/滑块_滑动.txt'))
($proj.Layers[0].Elements|Where-Object Name -eq 'refreshTick').Events[0].Code=[IO.File]::ReadAllText((Join-Path $PSScriptRoot '全局计龄刷新.txt'))
# Dynamic image resources must be locked. VP runtime addresses the locked
# resource list using one-based indices; FontData.Index is already one-based.
$p831=$proj.Layers[8];$sl31=$p831.Elements|Where-Object Name -eq 'rpmSlider';$hi31=$p831.Elements|Where-Object Name -eq 'sliderSpeedHint';$p831.Elements.Remove($hi31)|Out-Null;$p831.Elements.Insert($p831.Elements.IndexOf($sl31),$hi31)
# VP paints and hits controls in numeric ID order, irrespective of collection order.
# Keep all original names/events; place the original slider above the read-only label.
$sl31.Id=36
$resourceMap=@{};foreach($im in $proj.Images){$im.Locked=$true;$resourceMap['user.image.'+$im.Name]=[int]$im.Index+1}
foreach($f in $proj.Fonts){$resourceMap['user.font.'+$f.Name]=[int]$f.Index}
foreach($page in $proj.Layers){foreach($obj in @($page)+@($page.Elements)){foreach($evt in $obj.Events){if($evt.Code){$evt.Code=[regex]::Replace($evt.Code,'user\.(image|font)\.[A-Za-z0-9_]+',{param($m) if(!$resourceMap.ContainsKey($m.Value)){throw ('resource missing '+$m.Value)};[string]$resourceMap[$m.Value]})}}}}
$resourceMap|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $evidence '资源索引绑定.json') -Encoding UTF8
$io.wHAMcrUVnVnATbRNFqXJTFWDNjDo($proj,[string]$projectPath)
$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string]$projectPath)
ExportModel (Join-Path $evidence '31号实际工程读回.json')
$fonts=foreach($f in $proj.Fonts|Where-Object {$_.Name -like 'UI31_*'}){[ordered]@{Id=$f.Id;Name=$f.Name;Height=$f.Font.PixelHeight;Glyphs=@($f.Font.Chars|ForEach-Object {[ordered]@{Char=[string]$_.Char;Width=$_.PixelWidth;Data=[Convert]::ToBase64String($_.Data)}})}}
$fonts|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $evidence '实际字模.json') -Encoding UTF8
Write-Output '31号正式PIX已保存并读回'
