# Read-only native VP event execution and WPF slider verification; no physical port.
. (Join-Path $PSScriptRoot 'native-run31.ps1')
$evdir31=Join-Path $PSScriptRoot '运行核验/31号实施_20261101'
$copy=$t.GetField('EgErvQLbNxVTVEjeyfsODMZZZGgcA',$flags).GetValue($d)
function Method31($ob31,$method31){for($type31=$ob31.GetType();$type31;$type31=$type31.BaseType){$m31=$type31.GetMethod($method31,$flags);if($m31){return $m31}};throw ('Native method missing '+$method31)}
$controls31=@{};$events31=@{}
foreach($p31 in $copy.Layers){$controls31[$p31.Name]=$p31;foreach($c31 in $p31.Elements){$controls31[$p31.Name+'.'+$c31.Name]=$c31}}
foreach($fn31 in @('FYPgzghjrjdfafFbeoXyZnjdoDpXb','iCwfvIbOGIlaphtxvrbKIAKaOJKt','rwjNaJQpoUjcsdiYgWBdFWXxIrlB')){foreach($w31 in $t.GetField($fn31,$flags).GetValue($d)){$el31=(Method31 $w31 'TNUFAMhprWkVMKRQzJArkCVEjflw').Invoke($w31,@());$eventLength31=(Method31 $w31 'rTtQWPurdhwjGdtdamsLMKneIOShA').Invoke($w31,@()).Length;if(!$events31.ContainsKey($el31.Name) -or $eventLength31 -gt (Method31 $events31[$el31.Name] 'rTtQWPurdhwjGdtdamsLMKneIOShA').Invoke($events31[$el31.Name],@()).Length){$events31[$el31.Name]=$w31}}}
function Run31($name31){$w31=$events31[$name31];if(!$w31){throw ('No compiled event '+$name31)};try{(Method31 $w31 'VQHJIGmODxYDIDwmGPtbcmCSENze').Invoke($w31,@())|Out-Null}catch{Write-Output ('Event failed '+$name31);$ex31=$_.Exception;while($ex31){Write-Output $ex31.ToString();$ex31=$ex31.InnerException};throw}}
function Assert31($ok31,$msg31){if(!$ok31){throw $msg31}}
$mode31=$controls31['P0_Boot.themeMode'];$sl31=$controls31['P8_ServoControl.rpmSlider'];$hint31=$controls31['P8_ServoControl.sliderSpeedHint'];$h31=$controls31['P8_ServoControl.h13'];$s31=$controls31['P8_ServoControl.h13Status']
$report31=[ordered]@{Engine='Visual-PIX 1.7.2.179 native compiled event engine + WPF controls, loaded from final PIX; physical port unopened';Themes=@();SliderValues=0;SliderFailures=@();ThumbPositions=@();TouchLayer=$null;PhysicalVerified=$false}
# Native scripts use numeric resources; check the resolved object IDs in each page.
foreach($theme31 in @(0,1,0,1)){
 $mode31.Int=$theme31
 foreach($p31 in $copy.Layers){Run31 $p31.Name;$expected31=$copy.Images|Where-Object Name -eq ((@('Day','Night')[$theme31])+'_'+$p31.Name);Assert31 ($p31.ImageId -eq $expected31.Id) ('Wrong runtime page image '+$p31.Name+' theme='+$theme31+' mode='+$mode31.Int+' actual='+$p31.ImageId+' expected='+$expected31.Id);Assert31 ($mode31.Int -eq $theme31) 'Theme did not persist';$report31.Themes+=@{Theme=$theme31;Page=$p31.Name;ImageId=$p31.ImageId}}
}
$mode31.Int=0;Run31 P1_Main
foreach($expected31 in @(1,0,1,0)){Run31 themeToggle;Assert31 ($mode31.Int -eq $expected31) 'Release did not toggle exactly once'}
Run31 P8_ServoControl
foreach($allowed31 in @($false,$true)){
 $h31.IntArray[2]=if($allowed31){3}else{0};$h31.IntArray[3]=1;$h31.IntArray[32]=$h31.IntArray[116]=$h31.IntArray[138]=0
 for($value31=0;$value31 -le 3200;$value31++){
  $sl31.Value=$value31;Run31 rpmSlider;$rpm31=$value31-1600;$thumb31=24+[math]::Floor($value31*944.0/3200);$x31=if($rpm31 -gt 0){$thumb31-204}else{$thumb31+44};$align31=if($rpm31 -gt 0){'RIGHT'}else{'LEFT'}
  Assert31 ($hint31.X -eq $x31 -and $hint31.HorizontalAlignment.ToString() -eq $align31) ('Slider placement '+$value31)
  $prefix31=if($allowed31){'设定：'}else{'预览：'};Assert31 ($hint31.Text.StartsWith($prefix31)) 'Wrong accepted/preview state'
  $actual31=[double]::Parse($hint31.Text.Substring(3).Replace('km/h',''),[Globalization.CultureInfo]::InvariantCulture);$expected31=$rpm31*[math]::PI*.185/(60*(8.0/3))*3.6
  Assert31 ([math]::Abs($actual31-$expected31) -le .000501) ('Native conversion '+$value31+' actual='+$actual31+' expected='+$expected31+' text='+$hint31.Text)
  Assert31 ($hint31.X -ge 24 -and $hint31.X+192 -le 1000) 'Hint outside rail';$report31.SliderValues++
 }
}
# Font property scripts map one-based compiled resource numbers to the native font ID.
$h31.IntArray[86]=1;$h31.IntArray[1]=$h31.IntArray[21]=$h31.IntArray[73]=1000
$s31.IntArray[9]=$s31.IntArray[25]=$s31.IntArray[26]=$s31.IntArray[27]=$s31.IntArray[57]=3;$s31.IntArray[8]=3;$s31.IntArray[59]=1
$s31.IntArray[18]=$s31.IntArray[19]=$s31.IntArray[20]=$s31.IntArray[21]=$s31.IntArray[22]=$s31.IntArray[23]=$s31.IntArray[24]=32768
$s31.IntArray[58]=$s31.IntArray[60]=$s31.IntArray[61]=32768;$s31.IntArray[46]=$s31.IntArray[47]=0
$t.GetField('sptlnwNEeWINytssvYStiACLjpKx',$flags).SetValue($d,$copy.Layers[8]);Run31 refreshTick
$font2031=($copy.Fonts|Where-Object Name -eq 'UI31_20').Id
foreach($n31 in @('leftTarget','rightTarget','leftOutput','rightOutput','acceptedTarget')){Assert31 ($controls31['P8_ServoControl.'+$n31].FontId -eq $font2031) ('Dynamic font '+$n31)}
$report31.DynamicFont20=$true
[Windows.Application]::ResourceAssembly=$vp
foreach($file31 in @('resource/buttontheme.xaml','resource/textboxtheme.xaml','resource/checkboxtheme.xaml','resource/pixcontroldatatemplate.xaml','resource/pixcontroldatatemplatedebug.xaml','themes/generic.xaml')){$rd31=[Windows.ResourceDictionary]::new();$rd31.GetType().GetProperty('Source').SetValue($rd31,[Uri]('pack://application:,,,/Visual-PIX;component/'+$file31),$null);[Windows.Application]::Current.Resources.MergedDictionaries.Add($rd31)}
$key31=[Windows.DataTemplateKey]::new($vp.GetType('Visual_PIX.Model.Controls.SliderCtrl'));$dt31=[Windows.Application]::Current.FindResource($key31);$sliderView31=$dt31.LoadContent();$sliderView31.DataContext=$sl31
foreach($value31 in @(0,800,1600,2400,3200)){
 $sl31.Value=$value31;$sliderView31.Measure([Windows.Size]::new(976,48));$sliderView31.Arrange([Windows.Rect]::new(0,0,976,48));$sliderView31.UpdateLayout();$thumbView31=$sliderView31.Template.FindName('PART_Thumb',$sliderView31);$point31=$thumbView31.TransformToAncestor($sliderView31).Transform([Windows.Point]::new(0,0));Assert31 ($point31.X -eq $value31*944/3200 -and $thumbView31.ActualWidth -eq 32) 'Native thumb mapping';$report31.ThumbPositions+=@{Value=$value31;AbsoluteLeft=24+$point31.X;Width=$thumbView31.ActualWidth}
}
# The text must be below the original slider. Native hit order is reverse z-order.
$sl31.Value=1600;Run31 rpmSlider;$t.GetField('sptlnwNEeWINytssvYStiACLjpKx',$flags).SetValue($d,$copy.Layers[8]);$t.GetMethod('BlonFGjajYudmSgKcPxpfCQUShxe',$flags).Invoke($d,@())|Out-Null
$hit31=$t.GetMethod('tZLQyBPOWDpxXBBbzumxudRwnpLe',$flags).Invoke($d,[object[]]@([double]600,[double]320));Assert31 ($hit31[$hit31.Count-1].Name -eq 'rpmSlider') 'Hint blocks original slider';Assert31 ($sl31.BackgroundType.ToString() -eq 'Image') 'Slider background not transparent image';Assert31 (!$hint31.Enable -and !$hint31.KeyboardId -and @($hint31.Events|Where-Object Code).Count -eq 0) 'Hint accepts touch or keyboard';$report31.TouchLayer=@{OrderedHits=@($hit31.Name);TopHit=$hit31[$hit31.Count-1].Name;OriginalSliderId=$sl31.Id;HintId=$hint31.Id;TransparentBackgroundId=$sl31.ImageId}
$report31|ConvertTo-Json -Depth 9|Set-Content -LiteralPath (Join-Path $evdir31 '31号VP原生执行核验.json') -Encoding UTF8
# Native hit selection over the full range; disabled text never becomes the selected
# foreground object. This is event/hit-model evidence, not physical drag testing.
$hitChecks31=0
for($value31=0;$value31 -le 3200;$value31++){
 $sl31.Value=$value31;Run31 rpmSlider
 $hx31=[int]::Parse(([string]$hint31.X));$hxa31=$hx31+1;$hxb31=$hx31+96;$hxc31=$hx31+191;$hitXs31=@($hxa31,$hxb31,$hxc31,25,999)
 foreach($hitX31 in $hitXs31){
  $hits31=$t.GetMethod('tZLQyBPOWDpxXBBbzumxudRwnpLe',$flags).Invoke($d,[object[]]@([double]$hitX31,[double]320));Assert31 ($hits31[$hits31.Count-1].Name -eq 'rpmSlider') ('Slider hit blocked at '+$value31+'/'+$hitX31);$hitChecks31++
 }
}
$report31.TouchLayer.AllRangeHitChecks=$hitChecks31
$report31|ConvertTo-Json -Depth 9|Set-Content -LiteralPath (Join-Path $evdir31 '31号VP原生执行核验.json') -Encoding UTF8
# Produce native execution states for the font bitmap/layout audit.
$cases31=Get-Content -LiteralPath (Join-Path $evdir31 '31号显示工况输入.json') -Raw -Encoding UTF8|ConvertFrom-Json
$states31=foreach($case31 in $cases31){
 foreach($prop31 in $case31.arrays.PSObject.Properties){$ctrl31=if($controls31.ContainsKey('P8_ServoControl.'+$prop31.Name)){$controls31['P8_ServoControl.'+$prop31.Name]}else{$controls31['P0_Boot.'+$prop31.Name]};if($ctrl31){for($ix31=0;$ix31 -lt [math]::Min($ctrl31.IntArray.Length,$prop31.Value.Count);$ix31++){$ctrl31.IntArray[$ix31]=[int]$prop31.Value[$ix31]}}}
 $mode31.Int=[int]$case31.theme;$page31=$copy.Layers[[int]$case31.page];$t.GetField('sptlnwNEeWINytssvYStiACLjpKx',$flags).SetValue($d,$page31);Run31 $page31.Name;Run31 refreshTick
 $ctrlStates31=foreach($cc31 in $page31.Elements){if($cc31 -is [object] -and $cc31.PSObject.Properties['X']){[ordered]@{Name=$cc31.Name;X=$cc31.X;Y=$cc31.Y;Width=$cc31.Width;Height=$cc31.Height;Text=$cc31.Text;FontId=$cc31.FontId;FontColor=$cc31.FontColor;BackgroundColor=$cc31.BackgroundColor;BackgroundType=[string]$cc31.BackgroundType;Enable=$cc31.Enable;Visible=$cc31.Visbile;ImageId=$cc31.ImageId;ImageId1=$cc31.ImageId1;ImageId2=$cc31.ImageId2;DisableBackgroundImageId=$cc31.DisableBackgroundImageId;Value=$cc31.Value;ThumbColor=$cc31.ThumbColor;HorizontalAlignment=[string]$cc31.HorizontalAlignment;VerticalAlignment=[string]$cc31.VerticalAlignment}}}
 [ordered]@{Theme=$case31.theme;Scenario=$case31.scenario;Page=$page31.Name;ImageId=$page31.ImageId;Controls=@($ctrlStates31)}
}
$states31|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $evdir31 '31号VP原生显示状态.json') -Encoding UTF8
Write-Output ('Native verification passed: '+$report31.SliderValues+' slider events, '+$states31.Count+' page states; physical verification pending')
[Environment]::Exit(0)
