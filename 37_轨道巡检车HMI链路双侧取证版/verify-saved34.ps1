$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'))
$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string](Join-Path $PSScriptRoot '17_轨道巡检车.pix'))
$map=Get-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/31号实施_20261101/资源索引绑定.json') -Raw -Encoding UTF8|ConvertFrom-Json
function Resolve33($src){[regex]::Replace($src,'user\.(image|font)\.[A-Za-z0-9_]+',{param($m) if(!$map.($m.Value)){throw 'Resource missing'};[string]$map.($m.Value)})}
function Same33($actual,$expected,$name){if($actual.Replace("`r`n","`n").Trim() -ne $expected.Replace("`r`n","`n").Trim()){throw ('Saved PIX mismatch: '+$name)}}
Same33 $proj.UserMethodCode (Resolve33 ([IO.File]::ReadAllText((Join-Path $PSScriptRoot 'HMI34自定义函数.txt')))) 'UserMethodCode'
$p8=$proj.Layers[8]
$timer=$proj.Layers[0].Elements|Where-Object Name -eq refreshTick
Same33 ($timer.Events|Where-Object Id -eq 21).Code (Resolve33 ([IO.File]::ReadAllText((Join-Path $PSScriptRoot '全局计龄刷新.txt')))) 'global timer'
foreach($pg in $proj.Layers){
 Same33 ($pg.Events|Where-Object Id -eq 21).Code (Resolve33 ([IO.File]::ReadAllText((Join-Path $PSScriptRoot ('事件34/'+$pg.Name+'_加载.txt'))))) ($pg.Name+'/load')
 Same33 ($pg.Events|Where-Object Id -eq 22).Code (Resolve33 ([IO.File]::ReadAllText((Join-Path $PSScriptRoot ('事件34/'+$pg.Name+'_离开.txt'))))) ($pg.Name+'/leave')
}
Same33 (($proj.Layers[1].Elements|Where-Object Name -eq themeToggle).Events|Where-Object Id -eq 2).Code (Resolve33 ([IO.File]::ReadAllText((Join-Path $PSScriptRoot '事件34/主题切换.txt')))) 'theme toggle'
if(!$timer.Enable -or !$timer.Global -or $timer.Interval -ne 5){throw 'Global timer configuration mismatch'}
$schema=Get-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/34号实施_20261103/34号输入索引.json') -Raw -Encoding UTF8|ConvertFrom-Json
if(($proj.Layers[0].Elements|Where-Object Name -eq u34).Length -ne $schema.length){throw 'Input schema capacity mismatch'}
foreach($key in @('t34','b34')){$a=$proj.Layers[0].Elements|Where-Object Name -eq $key;if($a.Length -ne 9){throw 'Page theme cache capacity mismatch'}}
$hint=$p8.Elements|Where-Object Name -eq sliderSpeedHint
if($hint.Enable -or $hint.KeyboardId){throw 'Hint can intercept touch or open keyboard'}
foreach($b in @(@('authorize',1,'授权_按下'),@('authorize',2,'授权_弹起'),@('estop',1,'急停_按下'),@('estop',2,'急停_弹起'),@('rpmSlider',5,'滑块_滑动'),@('recoveryButton',1,'故障恢复按钮_按下'))){Same33 (($p8.Elements|Where-Object Name -eq $b[0]).Events|Where-Object Id -eq $b[1]).Code ([IO.File]::ReadAllText((Join-Path $PSScriptRoot ('事件/'+$b[2]+'.txt')))) ($b[0]+'/'+$b[1])}
$arrays=foreach($pg in $proj.Layers){foreach($a in $pg.Elements|Where-Object {$_.GetType().Name -eq 'ArrayCtrl'}){if($a.Length -ne $a.IntArray.Length){throw 'Array real length mismatch'};[ordered]@{Name=$pg.Name+'.'+$a.Name;Length=$a.Length;RealLength=$a.IntArray.Length}}}
[ordered]@{SavedPIXMatchesText=$true;CustomFunctions=([regex]::Matches($proj.UserMethodCode,'void HMI34\w+\(\)')).Count;Arrays=@($arrays);HardwareVerified=$false}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/34号实施_20261103/34号保存源码一致性.json') -Encoding UTF8
Write-Output 'PASS saved PIX: functions, timer, page bindings, input events and real array lengths'
