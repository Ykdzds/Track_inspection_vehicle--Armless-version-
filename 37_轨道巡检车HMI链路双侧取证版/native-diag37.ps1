$ErrorActionPreference='Stop'
$runner=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'native-run31.ps1'))
$inject=@'
($proj.Layers[0].Elements|Where-Object Name -eq refreshTick).Events[0].Code='HMI37Capture();'
$proj.UserMethodCode=$proj.UserMethodCode.Replace('sys_run_time','P0_Boot.d37c.vals[31]').Replace('sys_pid','P0_Boot.d37c.vals[30]')
$methodCompiler37=$vp.GetType('YfWokryEEyXYShnUWReANAvuxqev')
$methodParser37=[Activator]::CreateInstance($methodCompiler37)
$methodCompiler37.GetMethod('ywkirFwRbWKwnxeZkimVLuCLWwFD',$flags).Invoke($methodParser37,[object[]]@($proj.UserMethodCode,$proj))|Out-Null
'@
$runner=$runner.Insert($runner.IndexOf('$screen=[Activator]'),$inject+"`n");$nativeRoot37=$PSScriptRoot;$runner=$runner.Replace('$PSScriptRoot','$nativeRoot37');Invoke-Expression $runner
$copy=$t.GetField('EgErvQLbNxVTVEjeyfsODMZZZGgcA',$flags).GetValue($d)
function Method37($o,$name){for($tp=$o.GetType();$tp;$tp=$tp.BaseType){$m=$tp.GetMethod($name,$flags);if($m){return $m}};throw 'Missing method'}
$event37=$null
foreach($fn in @('FYPgzghjrjdfafFbeoXyZnjdoDpXb','iCwfvIbOGIlaphtxvrbKIAKaOJKt','rwjNaJQpoUjcsdiYgWBdFWXxIrlB')){foreach($w37 in $t.GetField($fn,$flags).GetValue($d)){$el=(Method37 $w37 'TNUFAMhprWkVMKRQzJArkCVEjflw').Invoke($w37,@());if($el.Name -eq 'refreshTick'){$event37=$w37}}}
if(!$event37){throw 'Timer wrapper missing'}
$h=($copy.Layers[8].Elements|Where-Object Name -eq h13).IntArray
$q=($copy.Layers[0].Elements|Where-Object Name -eq s33).IntArray
$k=($copy.Layers[0].Elements|Where-Object Name -eq d37c).IntArray
$f=($copy.Layers[0].Elements|Where-Object Name -eq d37).IntArray
$h[2]=3;$h[4]=0x1234;$h[5]=0x5678;$h[6]=0;$h[7]=9;$q[2]=0x12345678;$k[17]=8;$k[18]=188;$k[24]=-123
try{(Method37 $event37 'VQHJIGmODxYDIDwmGPtbcmCSENze').Invoke($event37,@())|Out-Null}catch{$ex=$_.Exception;while($ex){Write-Output $ex.ToString();$ex=$ex.InnerException};throw}
$errors=$t.GetField('BBLYIVcgmApjnlSElGZdacQwrRhf',$flags).GetValue($d)
if($errors.Count){throw ($errors -join '|')}
if($f[0] -ne 0x4844 -or $f[1] -ne 0x100 -or $f[4] -ne 8 -or $f[14] -ne 0x1234 -or $f[15] -ne 0x5678 -or $f[46] -ne 65535 -or $f[47] -ne 65413){throw ('Native freeze mismatch '+($f -join ','))}
[ordered]@{Engine='Visual-PIX native compiled events';CaptureExecuted=$true;EventId=$f[3];Reason=$f[4];ScheduleHigh=$f[14];ScheduleLow=$f[15];NegativeGapHigh=$f[46];NegativeGapLow=$f[47];Errors=$errors.Count;PhysicalVerified=$false}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/37号实施_20261107/37号VP原生首因核验.json') -Encoding UTF8
Write-Output 'PASS Visual-PIX native capture event, unsigned words, int32 negative gap'
[Environment]::Exit(0)