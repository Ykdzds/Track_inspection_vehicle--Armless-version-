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

$report=[ordered]@{Engine='Visual-PIX native compiled events and native hit model';PhysicalVerified=$false;SliderChecks=0;HitChecks=0;LifecycleChecks=0;Failures=@()}
function Assert34($ok,$message){if(!$ok){throw $message}}
$sl=$controls['P8_ServoControl.rpmSlider'];$hint=$controls['P8_ServoControl.sliderSpeedHint'];$u=$controls['P0_Boot.u34']
$t.GetField('sptlnwNEeWINytssvYStiACLjpKx',$flags).SetValue($d,$copy.Layers[8])
$h.IntArray[2]=3;$h.IntArray[3]=1;$h.IntArray[32]=0;$h.IntArray[138]=0;$h.IntArray[116]=0;$q.IntArray[0]=$q.IntArray[1];$u.IntArray[26]=0;$u.IntArray[38]=1;$u.IntArray[16]=1
Run33 P8_ServoControl
foreach($theme in @(0,1)){
 $mode.Int=$theme;$controls['P0_Boot.themeInk'].Int=if($theme){-1182209}else{-15719366}
 foreach($allowed in @($false,$true)){
  $h.IntArray[2]=if($allowed){3}else{1};$u.IntArray[26]=0;$u.IntArray[39]=0;$sl.Value=1600;Run33 rpmSlider
  for($value=0;$value -le 3200;$value++){
   $sl.Value=$value;Run33 rpmSlider
   $thumb=24+[math]::Floor($value*944.0/3200);$expectedX=if($value -gt 1600){$thumb-204}else{$thumb+44}
   Assert34 ($hint.X -eq $expectedX) ('Hint coordinate '+$value+' actual='+$hint.X+' expected='+$expectedX)
   $actual=[double]::Parse($hint.Text.Substring(3).Replace('km/h',''),[Globalization.CultureInfo]::InvariantCulture)
   Assert34 ([math]::Abs($actual-($value-1600)*0.01307687942057) -le .000501) ('Hint conversion '+$value+' '+$hint.Text)
   Assert34 ($hint.X -ge 24 -and $hint.X+192 -le 1000) 'Hint outside track'
   if($allowed){Assert34 ($u.IntArray[1] -eq $value-1600) 'Candidate not captured'}
   $report.SliderChecks++
  }
 }
}
$t.GetMethod('BlonFGjajYudmSgKcPxpfCQUShxe',$flags).Invoke($d,@())|Out-Null
foreach($pos in @(@(280,550,'estop'),@(386,570,'estop'),@(500,590,'estop'),@(775,550,'backHome'),@(900,570,'backHome'),@(1000,590,'backHome'))){
 $hits=$t.GetMethod('tZLQyBPOWDpxXBBbzumxudRwnpLe',$flags).Invoke($d,[object[]]@([double]$pos[0],[double]$pos[1]));Assert34 ($hits.Count -gt 0 -and $hits[$hits.Count-1].Name -eq $pos[2]) ('Button occluded '+$pos);$report.HitChecks++
}
$h.IntArray[2]=3;$h.IntArray[3]=1;$h.IntArray[138]=0;$u.IntArray[16]=1
foreach($value in @(0,800,1600,2400,3200)){
 $sl.Value=$value;Run33 rpmSlider
 foreach($x in @(($hint.X+4),($hint.X+96),($hint.X+188))){
  $hits=$t.GetMethod('tZLQyBPOWDpxXBBbzumxudRwnpLe',$flags).Invoke($d,[object[]]@([double]$x,[double]320));Assert34 ($hits.Count -gt 0 -and $hits[$hits.Count-1].Name -eq 'rpmSlider') 'Hint steals slider touch';$report.HitChecks++
 }
}
$h.IntArray[10]=725;$sl.Value=2325;Run33 rpmSlider;$beforeTarget=$h.IntArray[10];$beforeHi=$h.IntArray[6];$beforeLo=$h.IntArray[7]
foreach($page in $copy.Layers){$t.GetField('sptlnwNEeWINytssvYStiACLjpKx',$flags).SetValue($d,$page);Run33 $page.Name;Assert34 ($h.IntArray[10] -eq $beforeTarget -and $h.IntArray[6] -eq $beforeHi -and $h.IntArray[7] -eq $beforeLo) 'Page load changed control';$report.LifecycleChecks++}
$errors=$t.GetField('BBLYIVcgmApjnlSElGZdacQwrRhf',$flags).GetValue($d);Assert34 ($errors.Count -eq 0) ($errors -join '|')
$report|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $evidence '34号原生触摸滑块核验.json') -Encoding UTF8
$report|ConvertTo-Json -Depth 7
[Environment]::Exit(0)
