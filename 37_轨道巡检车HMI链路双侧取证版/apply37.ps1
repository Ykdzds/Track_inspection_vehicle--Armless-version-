$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$projectPath=Join-Path $PSScriptRoot '17_轨道巡检车.pix'
$evidence=Join-Path $PSScriptRoot '运行核验/37号实施_20261107'
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'))
$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string]$projectPath)
$p0=$proj.Layers[0]
function Arr37($name,$length,$byte=$false){
$a=$p0.Elements|Where-Object Name -eq $name
if(!$a){$a=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Model.Controls.ArrayCtrl'));$a.Id=$p0.GetNextElementId();$a.Name=$name;$p0.Elements.Add($a)}
$a.IsGlobal=$true;$a.NameCompile=$true
$a.ArrayType=[Enum]::ToObject($a.ArrayType.GetType(),0)
if($byte){$donor=$proj.Layers[8].Elements|Where-Object Name -eq h13Tx;$a.ArrayType=$donor.ArrayType}
$a.Length=$length
if(!$byte){for($i=0;$i -lt $length;$i++){$a.IntArray[$i]=0};if($a.IntArray.Length -ne $length){throw 'Array capacity mismatch'}}
}
Arr37 d37 48
Arr37 d37c 32
Arr37 d37raw 18
Arr37 d37Tx 48 $true
$map=Get-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/31号实施_20261101/资源索引绑定.json') -Raw -Encoding UTF8|ConvertFrom-Json
function Resolve37($src){[regex]::Replace($src,'user\.(image|font)\.[A-Za-z0-9_]+',{param($m) if(!$map.($m.Value)){throw 'Resource missing'};[string]$map.($m.Value)})}
$proj.UserMethodCode=Resolve37 ([IO.File]::ReadAllText((Join-Path $PSScriptRoot 'HMI37自定义函数.txt')))
$global=[IO.File]::ReadAllText((Join-Path $PSScriptRoot '全局计龄刷新.txt'))
$original=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'HMI34事务服务.txt'))
$replacement=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'HMI37事务服务.txt'))
if(!$global.Contains($original)){throw 'Original service missing in global event'}
$global=$global.Replace($original,$replacement)
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'HMI37全局刷新.txt'),$global,[Text.UTF8Encoding]::new($true))
$timer=$p0.Elements|Where-Object Name -eq refreshTick
($timer.Events|Where-Object Id -eq 21).Code=Resolve37 $global
if(!$timer.Enable -or !$timer.Global -or $timer.Interval -ne 5){throw 'Timer differs'}
($proj.Layers[8].Elements|Where-Object Name -eq controlTitle).Text='伺服控制 / 37'
($proj.Layers[4].Elements|Where-Object Name -eq screenVersion).Text='屏幕工程：37'
$io.wHAMcrUVnVnATbRNFqXJTFWDNjDo($proj,[string]$projectPath)
$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string]$projectPath)
if($proj.UserMethodCode.Replace("`r`n","`n").Trim() -ne (Resolve37 ([IO.File]::ReadAllText((Join-Path $PSScriptRoot 'HMI37自定义函数.txt')))).Replace("`r`n","`n").Trim()){throw 'Saved function mismatch'}
if((($proj.Layers[0].Elements|Where-Object Name -eq refreshTick).Events|Where-Object Id -eq 21).Code.Replace("`r`n","`n").Trim() -ne (Resolve37 $global).Replace("`r`n","`n").Trim()){throw 'Saved global mismatch'}
$pages=foreach($pg in $proj.Layers){
$objects=foreach($c in $pg.Elements){$props=[ordered]@{};foreach($prop in $c.GetType().GetProperties()){
if($prop.CanRead -and $prop.GetIndexParameters().Count -eq 0 -and ($prop.PropertyType.IsPrimitive -or $prop.PropertyType.IsEnum -or $prop.PropertyType -eq [string])){$props[$prop.Name]=$prop.GetValue($c,$null)}
};[ordered]@{Name=$c.Name;Id=$c.Id;Type=$c.GetType().FullName;Properties=$props;Events=@($c.Events|ForEach-Object {[ordered]@{Id=$_.Id;Type=[string]$_.Type;Name=$_.Name;Code=$_.Code;Enable=$_.Enable}})}}
[ordered]@{Name=$pg.Name;Id=$pg.Id;BackgroundType=[string]$pg.BackgroundType;ImageId=$pg.ImageId;Controls=@($objects);Events=@($pg.Events|ForEach-Object {[ordered]@{Id=$_.Id;Type=[string]$_.Type;Name=$_.Name;Code=$_.Code;Enable=$_.Enable}})}
}
[ordered]@{Pages=@($pages);Fonts=@($proj.Fonts|ForEach-Object {[ordered]@{Id=$_.Id;Name=$_.Name;Height=$_.Font.PixelHeight;Characters=(-join ($_.Font.Chars|ForEach-Object {$_.Char}))}});Images=@($proj.Images|ForEach-Object {[ordered]@{Id=$_.Id;Name=$_.Name;Bytes=$_.Data.Length}})}|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $evidence '37号实际工程读回.json') -Encoding UTF8
$summary=@($proj.Layers[0].Elements|Where-Object Name -in @('d37','d37c','d37raw','d37Tx')|ForEach-Object {[ordered]@{Name=$_.Name;Length=$_.Length;Type=[string]$_.ArrayType;ActualIntLength=$_.IntArray.Length}})
[ordered]@{SavedReopened=$true;FunctionsMatch=$true;GlobalMatches=$true;TimerInterval=5;TimerUnitMs=10;Arrays=$summary;HardwareVerified=$false}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $evidence '37号保存核验.json') -Encoding UTF8
Write-Output 'PASS HMI37 saved, reopened, functions/global exact match; timer unchanged'
$summary|Format-Table