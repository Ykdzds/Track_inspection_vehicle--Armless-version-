$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'))
$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string](Join-Path $PSScriptRoot '17_轨道巡检车.pix'))
$map=Get-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/31号实施_20261101/资源索引绑定.json') -Raw -Encoding UTF8|ConvertFrom-Json
function Resolve33($src){[regex]::Replace($src,'user\.(image|font)\.[A-Za-z0-9_]+',{param($m) if(!$map.($m.Value)){throw 'Resource missing'};[string]$map.($m.Value)})}
function Same33($actual,$expected,$name){if($actual.Replace("`r`n","`n").Trim() -ne $expected.Replace("`r`n","`n").Trim()){throw ('Saved PIX mismatch: '+$name)}}
Same33 $proj.UserMethodCode (Resolve33 ([IO.File]::ReadAllText((Join-Path $PSScriptRoot 'HMI33自定义函数.txt')))) 'UserMethodCode'
$p8=$proj.Layers[8]
$timer=$proj.Layers[0].Elements|Where-Object Name -eq refreshTick
Same33 ($timer.Events|Where-Object Id -eq 21).Code (Resolve33 ([IO.File]::ReadAllText((Join-Path $PSScriptRoot '全局计龄刷新.txt')))) 'global timer'
foreach($pg in $proj.Layers){Same33 ($pg.Events|Where-Object Id -eq 21).Code (Resolve33 ([IO.File]::ReadAllText((Join-Path $PSScriptRoot ('事件33/'+$pg.Name+'_加载.txt'))))) $pg.Name}
foreach($b in @(@('authorize',1,'授权_按下'),@('authorize',2,'授权_弹起'),@('estop',1,'急停_按下'),@('rpmSlider',5,'滑块_滑动'),@('recoveryButton',1,'故障恢复按钮_按下'))){Same33 (($p8.Elements|Where-Object Name -eq $b[0]).Events|Where-Object Id -eq $b[1]).Code ([IO.File]::ReadAllText((Join-Path $PSScriptRoot ('事件/'+$b[2]+'.txt')))) ($b[0]+'/'+$b[1])}
$arrays=foreach($pg in $proj.Layers){foreach($a in $pg.Elements|Where-Object {$_.GetType().Name -eq 'ArrayCtrl'}){if($a.Length -ne $a.IntArray.Length){throw 'Array real length mismatch'};[ordered]@{Name=$pg.Name+'.'+$a.Name;Length=$a.Length;RealLength=$a.IntArray.Length}}}
[ordered]@{SavedPIXMatchesText=$true;CustomFunctions=([regex]::Matches($proj.UserMethodCode,'void HMI33\w+\(\)')).Count;Arrays=@($arrays);HardwareVerified=$false}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/33号实施_20261102/33号保存源码一致性.json') -Encoding UTF8
Write-Output 'PASS saved PIX: functions, timer, page bindings, input events and real array lengths'
