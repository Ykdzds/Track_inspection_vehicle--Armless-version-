$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'native-run31.ps1')
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
$before32=@{};foreach($k32 in $watch32){$before32[$k32]=$controls32[$k32].Text;Check32 ($before32[$k32] -notmatch '未知|--') ('Missing initial '+$k32+' '+$before32[$k32]);$checks32++}
$stopwatch32=[Diagnostics.Stopwatch]::StartNew()
for($step32=0;$step32 -lt 200;$step32++){
 $h32.IntArray[1]+=50;$t.GetField('sptlnwNEeWINytssvYStiACLjpKx',$flags).SetValue($d,$copy.Layers[$step32%9]);Run32 refreshTick
 foreach($k32 in $watch32){Check32 ($controls32[$k32].Text -eq $before32[$k32]) ('History changed '+$step32+' '+$k32+' '+$controls32[$k32].Text);$checks32++}
}
$stopwatch32.Stop();Check32 ($controls32['P8_ServoControl.linkStatus'].Text -match '超时') 'Link failed to expire';$checks32++
# Restore valid transport and confirm immediate data changes including zero speed.
$h32.IntArray[1]+=50;$s32.IntArray[5]++;$s32.IntArray[23]=0;$h32.IntArray[21]=$h32.IntArray[73]=$h32.IntArray[1];Run32 refreshTick
Check32 ($controls32['P8_ServoControl.leftActual'].Text -match '0rpm\|0[.0]*m/s\|0[.0]*km/h') ('Zero speed invalid: '+$controls32['P8_ServoControl.leftActual'].Text);$checks32++
Check32 ($controls32['P8_ServoControl.linkStatus'].Text -match '正常') 'Link did not recover';$checks32++
$states32=@();$maxText32=0
foreach($theme32 in @(0,1)){
 $mode32.Int=$theme32
 foreach($pg32 in $copy.Layers){
  $t.GetField('sptlnwNEeWINytssvYStiACLjpKx',$flags).SetValue($d,$pg32);Run32 $pg32.Name;Run32 refreshTick
  $maxText32=[Math]::Max($maxText32,(@($controls32.Values|Where-Object {$_.PSObject.Properties['Text']}|ForEach-Object {[Text.Encoding]::GetEncoding(936).GetByteCount([string]$_.Text)}|Measure-Object -Maximum).Maximum));$states32+=[ordered]@{Theme=$theme32;Page=$pg32.Name;ImageId=$pg32.ImageId;Controls=@($pg32.Elements|Where-Object {$_.PSObject.Properties['FontId']}|ForEach-Object {[ordered]@{Name=$_.Name;Text=$_.Text;X=$_.X;Y=$_.Y;Width=$_.Width;Height=$_.Height;FontId=$_.FontId;FontColor=$_.FontColor;Visible=$_.Visbile;BackgroundColor=$_.BackgroundColor;BackgroundType=[string]$_.BackgroundType;Enable=$_.Enable;ImageId=$_.ImageId;ImageId1=$_.ImageId1;ImageId2=$_.ImageId2;DisableBackgroundImageId=$_.DisableBackgroundImageId;Value=$_.Value;ThumbColor=$_.ThumbColor;HorizontalAlignment=[string]$_.HorizontalAlignment;VerticalAlignment=[string]$_.VerticalAlignment}})}
 }
}
$report32=[ordered]@{Engine='Visual-PIX native compiled event engine, final saved PIX';Assertions=$checks32;GapMs=10000;Pages=9;Themes=2;Native200RendersMs=$stopwatch32.ElapsedMilliseconds;MaxGBKTextBytes=$maxText32;ArrayStringBytes=192;PhysicalCommunicationVerified=$false;MotionVerified=$false}
$report32|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $e32 '32号VP原生执行核验.json') -Encoding UTF8
$states32|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $e32 '32号VP原生显示状态.json') -Encoding UTF8
Write-Output ($report32|ConvertTo-Json -Compress)
[Environment]::Exit(0)
