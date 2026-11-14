param([switch]$AuditOnly)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$projectPath=Join-Path $PSScriptRoot '17_轨道巡检车.pix'
if((Split-Path $PSScriptRoot -Leaf) -ne '32_轨道巡检车状态显示稳定修复版'){throw 'Only HMI32 may be changed'}
$evidence=Join-Path $PSScriptRoot '运行核验/32号实施_20261102'
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'))
$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string]$projectPath)
$spec=Get-Content -LiteralPath (Join-Path $evidence '32号显示字段索引.json') -Raw -Encoding UTF8|ConvertFrom-Json
$n=[int]$spec.count
function Export32($name32){
 $pages32=foreach($pg32 in $proj.Layers){
  $objects32=foreach($c32 in $pg32.Elements){
   $props32=[ordered]@{}
   foreach($p32 in $c32.GetType().GetProperties()){
    if($p32.CanRead -and $p32.GetIndexParameters().Count -eq 0 -and ($p32.PropertyType.IsPrimitive -or $p32.PropertyType.IsEnum -or $p32.PropertyType -eq [string])){$props32[$p32.Name]=$p32.GetValue($c32,$null)}
   }
   [ordered]@{Name=$c32.Name;Id=$c32.Id;Type=$c32.GetType().FullName;Properties=$props32;Events=@($c32.Events|ForEach-Object {[ordered]@{Id=$_.Id;Type=[string]$_.Type;Name=$_.Name;Code=$_.Code;Enable=$_.Enable}})}
  }
  [ordered]@{Name=$pg32.Name;Id=$pg32.Id;BackgroundType=[string]$pg32.BackgroundType;ImageId=$pg32.ImageId;Controls=@($objects32);Events=@($pg32.Events|ForEach-Object {[ordered]@{Id=$_.Id;Type=[string]$_.Type;Name=$_.Name;Code=$_.Code;Enable=$_.Enable}})}
 }
 [ordered]@{Pages=@($pages32);Fonts=@($proj.Fonts|ForEach-Object {[ordered]@{Id=$_.Id;Name=$_.Name;Height=$_.Font.PixelHeight;Characters=(-join ($_.Font.Chars|ForEach-Object {$_.Char}))}});Images=@($proj.Images|ForEach-Object {[ordered]@{Id=$_.Id;Name=$_.Name;Bytes=$_.Data.Length}})}|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $evidence $name32) -Encoding UTF8
}
if($AuditOnly){Export32 '32号实际工程读回.json';exit}
Export32 '32号最近写入前模型.json'
$p032=$proj.Layers[0]
function Array32($name32,$length32,$type32){
 $ar32=$p032.Elements|Where-Object Name -eq $name32
 if(!$ar32){$ar32=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.Controls.ArrayCtrl'));$ar32.Id=$p032.GetNextElementId();$ar32.Name=$name32;$ar32.IsGlobal=$true;$ar32.ArrayType=[Enum]::ToObject($ar32.ArrayType.GetType(),$type32);$ar32.Length=$length32;$p032.Elements.Add($ar32)}
 $ar32.ArrayType=[Enum]::ToObject($ar32.ArrayType.GetType(),$type32)
 if($type32 -eq 1){$ar32.Size=192}
 $ar32.Length=$length32
 Write-Output ($name32+' type='+$ar32.ArrayType+' length='+$ar32.Length)
}
Array32 'ui32State' 96 0
Array32 'ui32Source' 9 0
Array32 'ui32SourceAt' 9 0
Array32 'ui32Field' ($n*5) 0
Array32 'ui32Text' ($n*3) 1
foreach($an32 in @('ui32Candidate','ui32CandidateSource','ui32Color','ui32Font','ui32WorkColor','ui32WorkFont','ui32Debounce','ui32DebounceAt')){Array32 $an32 $n 0}
$map32=Get-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/31号实施_20261101/资源索引绑定.json') -Raw -Encoding UTF8|ConvertFrom-Json
function Resolve32($text32){[regex]::Replace($text32,'user\.(image|font)\.[A-Za-z0-9_]+',{param($match32) [string]$map32.($match32.Value)})}
($p032.Elements|Where-Object Name -eq 'refreshTick').Events[0].Code=Resolve32 ([IO.File]::ReadAllText((Join-Path $PSScriptRoot '全局计龄刷新.txt')))
($proj.Layers[8].Elements|Where-Object Name -eq 'controlTitle').Text='伺服控制 / 32'
foreach($pidx32 in @(2,3,4,5,6,7)){
 $pg32=$proj.Layers[$pidx32];$q32=$pg32.Elements|Where-Object Name -eq 'dataQuality'
 if(!$q32){$q32=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.Controls.TextCtrl'));$q32.Id=$pg32.GetNextElementId();$q32.Name='dataQuality';$pg32.Elements.Add($q32)}
 $q32.IsGlobal=$true;$q32.NameCompile=$true;$q32.X=704;$q32.Y=20;$q32.Width=296;$q32.Height=28;$q32.Visbile=$true;$q32.Enable=$false;$q32.MaxLength=128;$q32.Text='等待有效数据';$q32.FontId=($proj.Fonts|Where-Object Name -eq 'UI31_20').Id;$q32.FontColor=-12429715;$q32.BackgroundColor=-1;$q32.BackgroundType='Color';$q32.HorizontalAlignment='RIGHT';$q32.VerticalAlignment='TOP'
 if($pidx32 -eq 2){($pg32.Elements|Where-Object Name -eq 'diagReadonly').Visbile=$false}
}
# Service, parser, navigation, input, recover/stop events remain byte-identical.
$io.wHAMcrUVnVnATbRNFqXJTFWDNjDo($proj,[string]$projectPath)
$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string]$projectPath)
Export32 '32号实际工程读回.json'
Write-Output 'HMI32 saved and reopened'
