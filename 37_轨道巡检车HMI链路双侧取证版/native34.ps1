$ErrorActionPreference='Stop'
# Native VM exercises presentation only; all serial calls are excluded from
# this in-memory test project and the production PIX is never overwritten.
$runner=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'native-run31.ps1'))
$marker='$screen=[Activator]'
$at=$runner.IndexOf($marker)
if($at -lt 0){throw 'Native harness entry missing'}
$inject=@'
$nativeDisplay=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'HMI34显示刷新.txt'))
$resourceMap=Get-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/31号实施_20261101/资源索引绑定.json') -Raw -Encoding UTF8|ConvertFrom-Json
$nativeDisplay=[regex]::Replace($nativeDisplay,'user\.(image|font)\.[A-Za-z0-9_]+',{param($m) [string]$resourceMap.($m.Value)})
($proj.Layers[0].Elements|Where-Object Name -eq refreshTick).Events[0].Code=$nativeDisplay
# The debug entry consumes GeneratedData; prepare the same custom functions used
# by the production compiler before entering its event engine.
$methodCompiler33=$vp.GetType('YfWokryEEyXYShnUWReANAvuxqev')
$methodParser33=[Activator]::CreateInstance($methodCompiler33)
$methodCompiler33.GetMethod('ywkirFwRbWKwnxeZkimVLuCLWwFD',$flags).Invoke($methodParser33,[object[]]@($proj.UserMethodCode,$proj))|Out-Null
$compiler33=$vp.GetType('QAbCrxbbHRemtjVisQnLmnreObaX')


'@
$runner=$runner.Insert($at,$inject+"`n")
$nativeRoot33=$PSScriptRoot
$runner=$runner.Replace('$PSScriptRoot','$nativeRoot33')
Invoke-Expression $runner
$copy=$t.GetField('EgErvQLbNxVTVEjeyfsODMZZZGgcA',$flags).GetValue($d)
$controls=@{};$events=@{}
function Method33($o,$name){for($tp=$o.GetType();$tp;$tp=$tp.BaseType){$m=$tp.GetMethod($name,$flags);if($m){return $m}};throw "Missing $name"}
foreach($pg in $copy.Layers){$controls[$pg.Name]=$pg;foreach($c in $pg.Elements){$controls[$pg.Name+'.'+$c.Name]=$c}}
foreach($fn in @('FYPgzghjrjdfafFbeoXyZnjdoDpXb','iCwfvIbOGIlaphtxvrbKIAKaOJKt','rwjNaJQpoUjcsdiYgWBdFWXxIrlB')){
 foreach($wrapper in $t.GetField($fn,$flags).GetValue($d)){
  $el=(Method33 $wrapper 'TNUFAMhprWkVMKRQzJArkCVEjflw').Invoke($wrapper,@());$len=(Method33 $wrapper 'rTtQWPurdhwjGdtdamsLMKneIOShA').Invoke($wrapper,@()).Length
  if(!$events.ContainsKey($el.Name) -or $len -gt (Method33 $events[$el.Name] 'rTtQWPurdhwjGdtdamsLMKneIOShA').Invoke($events[$el.Name],@()).Length){$events[$el.Name]=$wrapper}
 }
}
function Run33($name){if(!$events[$name]){throw "No native event $name"};try{(Method33 $events[$name] 'VQHJIGmODxYDIDwmGPtbcmCSENze').Invoke($events[$name],@())|Out-Null}catch{$ex=$_.Exception;while($ex){Write-Output $ex.ToString();$ex=$ex.InnerException};throw}}
$evidence=Join-Path $PSScriptRoot '运行核验/34号实施_20261103'
$fixture=Get-Content (Join-Path $evidence '34号原生输入.json') -Raw -Encoding UTF8|ConvertFrom-Json
foreach($pp in $fixture.PSObject.Properties){$a=$controls[$pp.Name];if($pp.Value.Count -ne $a.IntArray.Length){throw "Fixture length mismatch $($pp.Name): $($pp.Value.Count)/$($a.IntArray.Length)"};for($j=0;$j -lt $pp.Value.Count;$j++){$a.IntArray[$j]=[int]$pp.Value[$j]}}
$q=$controls['P0_Boot.s33'];$h=$controls['P8_ServoControl.h13'];$s=$controls['P8_ServoControl.h13Status'];$mode=$controls['P0_Boot.themeMode']
$states=@();$perf=@()
foreach($theme in @(0,1)){
 $mode.Int=$theme
 $palette=if($theme -eq 0){@(-15719366,-12429715,-15438781,-7714048,-4971752)}else{@(-1182209,-5653552,-10624850,-14490,-39558)}
 $i=0;foreach($role in @('Ink','Muted','Good','Warn','Fault')){$controls['P0_Boot.theme'+$role].Int=$palette[$i];$i++}
 foreach($pg in $copy.Layers){
  $t.GetField('sptlnwNEeWINytssvYStiACLjpKx',$flags).SetValue($d,$pg)
  Run33 $pg.Name
  $times=@();$cold33=@()
  for($step=0;$step -lt 65;$step++){$s.IntArray[5]++;$s.IntArray[23]=100+$step;$q.IntArray[27]=0;$watch=[Diagnostics.Stopwatch]::StartNew();Run33 refreshTick;$watch.Stop();$times+=$watch.Elapsed.TotalMilliseconds;if($step -lt 10){$cold33+=$watch.Elapsed.TotalMilliseconds};if($q.IntArray[40] -gt 6){throw 'Display batch exceeds budget'}}
  $perf+=[ordered]@{Theme=$theme;Page=$pg.Name;MinMs=($times|Measure-Object -Minimum).Minimum;MaxMs=($times|Measure-Object -Maximum).Maximum;AverageMs=($times|Measure-Object -Average).Average;SteadyMaxMs=($times[40..64]|Measure-Object -Maximum).Maximum;SteadyAverageMs=($times[40..64]|Measure-Object -Average).Average;EntryMaxMs=($cold33|Measure-Object -Maximum).Maximum}
  $states+=[ordered]@{Theme=$theme;Page=$pg.Name;ImageId=$pg.ImageId;Controls=@($pg.Elements|Where-Object {$_.PSObject.Properties['FontId']}|ForEach-Object {[ordered]@{Name=$_.Name;Text=$_.Text;X=$_.X;Y=$_.Y;Width=$_.Width;Height=$_.Height;FontId=$_.FontId;FontColor=$_.FontColor;Visible=$_.Visbile;BackgroundColor=$_.BackgroundColor;BackgroundType=[string]$_.BackgroundType;Enable=$_.Enable;ImageId=$_.ImageId;ImageId1=$_.ImageId1;ImageId2=$_.ImageId2;DisableBackgroundImageId=$_.DisableBackgroundImageId;Value=$_.Value;ThumbColor=$_.ThumbColor;HorizontalAlignment=[string]$_.HorizontalAlignment;VerticalAlignment=[string]$_.VerticalAlignment}})}
 }
}
$errors=$t.GetField('BBLYIVcgmApjnlSElGZdacQwrRhf',$flags).GetValue($d);if($errors.Count){throw ($errors -join '|')}
$perf|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $evidence '34号原生显示性能.json') -Encoding UTF8
$states|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $evidence '34号VP原生显示状态.json') -Encoding UTF8
$fonts=foreach($f in $copy.Fonts){[ordered]@{Id=$f.Id;Name=$f.Name;Height=$f.Font.PixelHeight;Glyphs=@($f.Font.Chars|ForEach-Object {[ordered]@{Char=[string]$_.Char;Width=$_.PixelWidth;Data=[Convert]::ToBase64String($_.Data)}})}}
$fonts|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $evidence '34号实际字模.json') -Encoding UTF8
$perf|ForEach-Object {Write-Output ($_.Page+' theme='+$_.Theme+' max='+$_.MaxMs+' avg='+$_.AverageMs+' steadyMax='+$_.SteadyMaxMs)}
[Environment]::Exit(0)
