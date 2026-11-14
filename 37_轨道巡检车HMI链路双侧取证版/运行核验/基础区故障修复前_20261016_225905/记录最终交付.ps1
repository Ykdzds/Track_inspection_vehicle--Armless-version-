$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$records=Get-Content -LiteralPath (Join-Path $root '运行核验/全量修复重开事件绑定核验_20261016.json') -Raw|ConvertFrom-Json
if($records.Count -ne 11 -or @($records|Where-Object {!$_.Matches}).Count){throw '重开事件证据不完整'}
$records|Where-Object {$_.Control -in @('authorize','estop','backHome','rpmSlider','servoEntry') -and $_.Event -ne '按下事件' -or $_.Control -eq 'estop'}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $root '运行核验/全量修复操作控件事件核验_20261016.json') -Encoding utf8
$records|Where-Object {$_.Page -eq 'P8_ServoControl' -and !$_.Control}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $root '运行核验/全量修复P8页面事件核验_20261016.json') -Encoding utf8
$protected=Get-Content -LiteralPath (Join-Path $root '运行核验/全量修复基线_20261016_182412/保护工程哈希.json') -Raw|ConvertFrom-Json
$changed=@($protected|Where-Object {!(Test-Path -LiteralPath $_.Path) -or (Get-FileHash -LiteralPath $_.Path).Hash -ne $_.SHA256})
if($changed.Count){throw '08或F103保护文件发生变化'}
$compile=Get-Content -LiteralPath (Join-Path $root '运行核验/全量修复VisualPIX编译日志_20261016.txt') -Raw
if($compile -notmatch '编译成功' -or $compile -match '编译失败'){throw '本轮编译未通过'}
$role=Get-Content -LiteralPath (Join-Path $root '运行核验/全量修复角色重开核验_20261016.json') -Raw|ConvertFrom-Json
if($role.PIX -ne (Get-FileHash -LiteralPath (Join-Path $root '13_轨道巡检车.pix')).Hash){throw '当前PIX变动后必须重新核验主站角色'}
$files=@(Get-ChildItem -LiteralPath $root -File|Where-Object {$_.Extension -in @('.pix','.bin','.pixelFont','.txt','.ps1','.cjs','.json','.md','.py') -and $_.Name -notlike '*.bak'};Get-ChildItem -LiteralPath (Join-Path $root '事件') -File)
$manifest=@($files|ForEach-Object {[ordered]@{Name=$_.FullName.Substring($root.Length+1);Length=$_.Length;SHA256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
[ordered]@{CapturedAt=(Get-Date -Format o);Baseline08Unchanged=$true;ReferenceF103Unchanged=$true;ProtectedFiles=$protected.Count;PhysicalCommunicationVerified=$false;MotionVerified=$false;Files=$manifest}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $root '运行核验/全量修复交付哈希_20261016.json') -Encoding utf8
Write-Output ('已记录交付文件：'+$manifest.Count+'；保护文件：'+$protected.Count+'；实物未验证')
