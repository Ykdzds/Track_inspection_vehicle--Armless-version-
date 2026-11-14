param([switch]$AuditOnly)
$ErrorActionPreference='Stop'
if((Split-Path $PSScriptRoot -Leaf) -ne '33_轨道巡检车调度与会话稳定修复版'){throw 'Only HMI33'}
. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$projectPath=Join-Path $PSScriptRoot '17_轨道巡检车.pix'
$evidence=Join-Path $PSScriptRoot '运行核验/33号实施_20261102'
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'))
$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string]$projectPath)
function Export33 {
 $pages=foreach($pg in $proj.Layers){
  $objects=foreach($c in $pg.Elements){$props=[ordered]@{};foreach($prop in $c.GetType().GetProperties()){
   if($prop.CanRead -and $prop.GetIndexParameters().Count -eq 0 -and ($prop.PropertyType.IsPrimitive -or $prop.PropertyType.IsEnum -or $prop.PropertyType -eq [string])){$props[$prop.Name]=$prop.GetValue($c,$null)}
  };[ordered]@{Name=$c.Name;Id=$c.Id;Type=$c.GetType().FullName;Properties=$props;Events=@($c.Events|ForEach-Object {[ordered]@{Id=$_.Id;Type=[string]$_.Type;Name=$_.Name;Code=$_.Code;Enable=$_.Enable}})}}
  [ordered]@{Name=$pg.Name;Id=$pg.Id;BackgroundType=[string]$pg.BackgroundType;ImageId=$pg.ImageId;Controls=@($objects);Events=@($pg.Events|ForEach-Object {[ordered]@{Id=$_.Id;Type=[string]$_.Type;Name=$_.Name;Code=$_.Code;Enable=$_.Enable}})}
 }
 [ordered]@{Pages=@($pages);Fonts=@($proj.Fonts|ForEach-Object {[ordered]@{Id=$_.Id;Name=$_.Name;Height=$_.Font.PixelHeight;Characters=(-join ($_.Font.Chars|ForEach-Object {$_.Char}))}});Images=@($proj.Images|ForEach-Object {[ordered]@{Id=$_.Id;Name=$_.Name;Bytes=$_.Data.Length}})}|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $evidence '33号实际工程读回.json') -Encoding UTF8
}
if($AuditOnly){Export33;exit}
$p0=$proj.Layers[0];$p8=$proj.Layers[8]
foreach($pg in $proj.Layers){foreach($c in $pg.Elements){$c.IsGlobal=$true;$c.NameCompile=$true}}
foreach($c in @($p0.Elements|Where-Object {$_.Name -like 'ui32*'})){$p0.Elements.Remove($c)|Out-Null}
function Arr33($name,$length,$fill=0){
 $a=$p0.Elements|Where-Object Name -eq $name
 if(!$a){$a=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.Controls.ArrayCtrl'));$a.Id=$p0.GetNextElementId();$a.Name=$name;$a.IsGlobal=$true;$p0.Elements.Add($a)}
 $a.ArrayType=[Enum]::ToObject($a.ArrayType.GetType(),0);$a.Length=$length
 if($a.IntArray.Length -ne $length){throw "Array length mismatch $name"}
 for($j=0;$j -lt $length;$j++){$a.IntArray[$j]=$fill}
}
Arr33 s33 64
Arr33 dRaw 208 65535
Arr33 dStatus 96 65535
$ds33=$p0.Elements|Where-Object Name -eq dStatus;foreach($j in 18..24){$ds33.IntArray[$j]=32767}
$fieldCount=(Get-Content -LiteralPath (Join-Path $evidence '33号显示字段.json') -Raw -Encoding UTF8|ConvertFrom-Json).fields.Count
Arr33 dField $fieldCount
Arr33 dRev 9
Arr33 dStamp ($fieldCount*9) (0-1)
Arr33 dLocal 8 (0-1)
($p8.Elements|Where-Object Name -eq h13).Length=180
($p8.Elements|Where-Object Name -eq h13Status).Length=96
$scratch=$p0.Elements|Where-Object Name -eq displayScratch
if(!$scratch){$scratch=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.Controls.TextCtrl'));$scratch.Id=$p0.GetNextElementId();$scratch.Name='displayScratch';$scratch.IsGlobal=$true;$scratch.NameCompile=$true;$scratch.Visbile=$false;$scratch.MaxLength=192;$p0.Elements.Add($scratch)}
$scratch.FontId=($proj.Fonts|Where-Object Name -eq UI31_20|Select-Object -First 1).Id
# Replace toggle with the same native ordinary button type as the emergency key.
$old=$p8.Elements|Where-Object Name -eq authorize
if($old.GetType().Name -eq 'ToggleButtonCtrl'){
 $btn=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.Controls.ButtonCtrl'))
 foreach($prop in $btn.GetType().GetProperties()){
  $oldProp=$old.GetType().GetProperty($prop.Name)
  if($prop.CanWrite -and $oldProp -and $oldProp.CanRead -and $prop.PropertyType -eq $oldProp.PropertyType -and $prop.GetIndexParameters().Count -eq 0 -and $prop.Name -notin @('Events')){try{$prop.SetValue($btn,$oldProp.GetValue($old,$null),$null)}catch{}}
 }
 $donor=$p8.Elements|Where-Object Name -eq backHome
 $btn.Events.Clear()
 foreach($donorEvent in $donor.Events){$newEvent=[Activator]::CreateInstance($donorEvent.GetType());foreach($ep in $donorEvent.GetType().GetProperties()){if($ep.CanWrite -and $ep.CanRead -and $ep.GetIndexParameters().Count -eq 0){try{$ep.SetValue($newEvent,$ep.GetValue($donorEvent,$null),$null)}catch{}}};$newEvent.Code='';$btn.Events.Add($newEvent)}
 $p8.Elements.Remove($old)|Out-Null;$p8.Elements.Add($btn)
}
$map=Get-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/31号实施_20261101/资源索引绑定.json') -Raw -Encoding UTF8|ConvertFrom-Json
function Resolve33($src){[regex]::Replace($src,'user\.(image|font)\.[A-Za-z0-9_]+',{param($m) if(!$map.($m.Value)){throw ('Resource missing '+$m.Value)};[string]$map.($m.Value)})}
function Bind33($obj,$id,$src){$ev=$obj.Events|Where-Object Id -eq $id;if(!$ev){throw "Missing event $($obj.Name)/$id"};$ev.Enable=$true;$ev.Code=Resolve33 $src}
$theme='if(P0_Boot.themeMode.val==0){P0_Boot.themeInk.val=-15719366;P0_Boot.themeMuted.val=-12429715;P0_Boot.themeGood.val=-15438781;P0_Boot.themeWarn.val=-7714048;P0_Boot.themeFault.val=-4971752;}else{P0_Boot.themeInk.val=-1182209;P0_Boot.themeMuted.val=-5653552;P0_Boot.themeGood.val=-10624850;P0_Boot.themeWarn.val=-14490;P0_Boot.themeFault.val=-39558;}'
$service=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'HMI33事务服务.txt'))
$display=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'HMI33显示刷新.txt'))
$proj.UserMethodCode=Resolve33 ([IO.File]::ReadAllText((Join-Path $PSScriptRoot 'HMI33自定义函数.txt')))
$boot='if(sys_pid==P0_Boot.id){P0_Boot.bootState.vals[0]=P0_Boot.bootState.vals[0]+1;if((P0_Boot.bootState.vals[0]>=10 && P0_Boot.regionSeen.vals[0]==1 && P0_Boot.rawRegs.vals[1]==6 && P0_Boot.rawRegs.vals[2]==100 && P0_Boot.rawRegs.vals[15]==1) || P0_Boot.bootState.vals[0]>=50){P0_Boot.bootState.vals[1]=1;showPage(P1_Main.id);}}'

$controlUi='P8_ServoControl.recoveryButton.enable=0;if(P0_Boot.s33.vals[50]==1 && P8_ServoControl.h13Status.vals[89]==2 && P8_ServoControl.h13Status.vals[90]==1){P8_ServoControl.recoveryButton.enable=1;}'
$global=$theme+"`n"+$service+"`n"+$controlUi+"`n"+$display+"`n"+$boot
[IO.File]::WriteAllText((Join-Path $PSScriptRoot '全局计龄刷新.txt'),$global,[Text.UTF8Encoding]::new($true))
Bind33 ($p0.Elements|Where-Object Name -eq refreshTick) 21 $global
$timer=$p0.Elements|Where-Object Name -eq refreshTick;$timer.Enable=$true;$timer.Global=$true;$timer.Interval=5
$oldTimer=$p8.Elements|Where-Object Name -eq h13Service;$oldTimer.Enable=$false;$oldTimer.Global=$false;foreach($ev in $oldTimer.Events){$ev.Code='// HMI33: dispatch owned by P0_Boot.refreshTick';$ev.Enable=$false}
foreach($pg in $proj.Layers){Bind33 $pg 21 ([IO.File]::ReadAllText((Join-Path $PSScriptRoot ('事件33/'+$pg.Name+'_加载.txt'))))}
Bind33 ($proj.Layers[1].Elements|Where-Object Name -eq themeToggle) 2 ([IO.File]::ReadAllText((Join-Path $PSScriptRoot '事件33/主题切换.txt')))
foreach($binding in @(@('authorize',1,'授权_按下'),@('authorize',2,'授权_弹起'),@('estop',1,'急停_按下'),@('rpmSlider',5,'滑块_滑动'),@('recoveryButton',1,'故障恢复按钮_按下'))){Bind33 ($p8.Elements|Where-Object Name -eq $binding[0]) $binding[1] ([IO.File]::ReadAllText((Join-Path $PSScriptRoot ('事件/'+$binding[2]+'.txt'))))}
($p8.Elements|Where-Object Name -eq controlTitle).Text='伺服控制 / 33'
($proj.Layers[4].Elements|Where-Object Name -eq screenVersion).Text='屏幕工程：33'
# Regenerate existing theme fonts with the original character set plus new text.
$chars=New-Object 'System.Collections.Generic.HashSet[char]'
foreach($f in $proj.Fonts){foreach($g in $f.Font.Chars){if([int][char]$g.Char -ge 32){[void]$chars.Add([char]$g.Char)}}}
foreach($c in ($global+$proj.UserMethodCode).ToCharArray()){if([int]$c -ge 32){[void]$chars.Add($c)}}
$fontType=$vp.GetType('Visual_PIX.Modules.FontConverter.ViewModels.FontConverterViewModel')
$fontConverter=[Runtime.Serialization.FormatterServices]::GetUninitializedObject($fontType)
foreach($f in $proj.Fonts|Where-Object Name -like 'UI31_*'){
 $size=[int]($f.Name.Split('_')[-1]);$opt=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.FontConverter.PixelFontOption'))
 $opt.FontName='微软雅黑';$opt.PixelHeight=@{20=14;24=18;32=22;36=26}[$size];$opt.Chars=(-join ($chars|Sort-Object));$opt.FontType='AntiAliasing4';$opt.CharRange=[Enum]::Parse($opt.CharRange.GetType(),'指定字符')
 $font=$fontType.GetMethod('iRQrnlxjjRtUcaagoMIHqaUZEWgW',$flags).Invoke($fontConverter,[object[]]@($opt,$false));if($font.PixelHeight -ne $size){throw 'Font height changed'};$f.Font=$font
}
try{$io.wHAMcrUVnVnATbRNFqXJTFWDNjDo($proj,[string]$projectPath)}catch{$ex=$_.Exception;while($ex){Write-Output $ex.ToString();$ex=$ex.InnerException};throw}
$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string]$projectPath)
Export33
Write-Output 'HMI33 saved, reopened and bindings exported'
