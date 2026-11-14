$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'native-run-base31.ps1')
$e32=Join-Path $PSScriptRoot '运行核验/32号实施_20261102'
$copy=$t.GetField('EgErvQLbNxVTVEjeyfsODMZZZGgcA',$flags).GetValue($d)
function M32($o32,$name32){for($tp32=$o32.GetType();$tp32;$tp32=$tp32.BaseType){$m32=$tp32.GetMethod($name32,$flags);if($m32){return $m32}};throw ('Missing '+$name32)}
$controls32=@{};$events32=@{}
foreach($pg32 in $copy.Layers){$controls32[$pg32.Name]=$pg32;foreach($c32 in $pg32.Elements){$controls32[$pg32.Name+'.'+$c32.Name]=$c32}}
foreach($fn32 in @('FYPgzghjrjdfafFbeoXyZnjdoDpXb','iCwfvIbOGIlaphtxvrbKIAKaOJKt','rwjNaJQpoUjcsdiYgWBdFWXxIrlB')){
 foreach($w32 in $t.GetField($fn32,$flags).GetValue($d)){
  $el32=(M32 $w32 'TNUFAMhprWkVMKRQzJArkCVEjflw').Invoke($w32,@());$len32=(M32 $w32 'rTtQWPurdhwjGdtdamsLMKneIOShA').Invoke($w32,@()).Length
  if(!$events32.ContainsKey($el32.Name) -or $len32 -gt (M32 $events32[$el32.Name] 'rTtQWPurdhwjGdtdamsLMKneIOShA').Invoke($events32[$el32.Name],@()).Length){$events32[$el32.Name]=$w32}
 }
}
function Run32($name32){$w32=$events32[$name32];if(!$w32){throw ('No event '+$name32)};try{(M32 $w32 'VQHJIGmODxYDIDwmGPtbcmCSENze').Invoke($w32,@())|Out-Null}catch{Write-Output ('Event failed '+$name32);$ex32=$_.Exception;while($ex32){Write-Output $ex32.ToString();$ex32=$ex32.InnerException};throw}}
function Check32($condition32,$message32){if(!$condition32){throw $message32}}
$fixture32=Get-Content -LiteralPath (Join-Path $e32 '32号原生输入.json') -Raw -Encoding UTF8|ConvertFrom-Json
foreach($pp32 in $fixture32.PSObject.Properties){$ctrl32=$controls32[$pp32.Name];for($j32=0;$j32 -lt [Math]::Min($pp32.Value.Count,$ctrl32.IntArray.Length);$j32++){$ctrl32.IntArray[$j32]=[int]$pp32.Value[$j32]}}
$h32=$controls32['P8_ServoControl.h13'];$s32=$controls32['P8_ServoControl.h13Status'];$r32=$controls32['P0_Boot.rawRegs'];$mode32=$controls32['P0_Boot.themeMode']
$checks32=0;$watch32=@('P8_ServoControl.leftActual','P8_ServoControl.rightActual','P8_ServoControl.leftTarget','P8_ServoControl.rightOutput','P1_Main.targetSpeed','P1_Main.leftSpeed','P1_Main.controlSource','P3_Position.startupTravel','P3_Position.axis1Position','P4_Info.ctrlVersion','P5_Alarm.alarmLevel','P6_Sync.syncActualDiff','P7_System_Ext.batteryState')
$t.GetField('sptlnwNEeWINytssvYStiACLjpKx',$flags).SetValue($d,$copy.Layers[1]);Run32 P1_Main;Run32 refreshTick
$h32.IntArray[1]+=50;foreach($j32 in 109..113){$h32.IntArray[$j32]++};$s32.IntArray[5]++;$h32.IntArray[21]=$h32.IntArray[73]=$h32.IntArray[1];Run32 refreshTick
$t.GetField('sptlnwNEeWINytssvYStiACLjpKx',$flags).SetValue($d,$copy.Layers[8]);
$perf32=@();foreach($case32 in @('NoNewFrame','StatusEach150ms','AllRegionsEach50ms')){
 $timer32=[Diagnostics.Stopwatch]::StartNew();for($k32=0;$k32 -lt 100;$k32++){
  $h32.IntArray[1]+=50
  if($case32 -eq 'AllRegionsEach50ms'){foreach($j32 in 109..113){$h32.IntArray[$j32]++};foreach($j32 in 0..4){$controls32['P0_Boot.regionValid'].IntArray[$j32]=1;$controls32['P0_Boot.regionAge'].IntArray[$j32]=0}}
  if($case32 -ne 'NoNewFrame' -and ($k32%3 -eq 0 -or $case32 -eq 'AllRegionsEach50ms')){$s32.IntArray[5]++;$h32.IntArray[21]=$h32.IntArray[73]=$h32.IntArray[1]}
  Run32 refreshTick
 };$timer32.Stop();$perf32+=@{Case=$case32;Renders=100;Ms=$timer32.ElapsedMilliseconds;MsPerRender=$timer32.ElapsedMilliseconds/100.0}
};$perf32|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $e32 '31号对照原生刷新性能.json') -Encoding UTF8
$perf32|ConvertTo-Json
[Environment]::Exit(0)
