$ErrorActionPreference='Stop'
$evidence=Join-Path $PSScriptRoot '运行核验/31号实施_20261101'
. (Join-Path $PSScriptRoot 'pix-ui.ps1') -Action SelectPage -Name P1_Main
& (Join-Path $PSScriptRoot 'pix-ui.ps1') -Action Keys -Value '^s'
$compiledAfter=Get-Date
& (Join-Path $PSScriptRoot 'pix-ui.ps1') -Action Keys -Value '^b'
Start-Sleep -Seconds 4
$bin=Get-Item -LiteralPath (Join-Path $PSScriptRoot '17_轨道巡检车.pix.bin')
if($bin.LastWriteTime -lt $compiledAfter){throw '本次未生成新BIN'}
$output=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Document -and $_.GetSupportedPatterns().ProgrammaticName -contains 'TextPatternIdentifiers.Pattern'}|ForEach-Object {$_.GetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern).DocumentRange.GetText(-1)}|Where-Object {$_.Length -lt 12000 -and $_ -match '编译成功|编译失败'}|Select-Object -First 1
if(!$output -or $output -notmatch '编译成功' -or $output -match '编译失败'){throw '编译失败或状态未知'}
$output|Set-Content -LiteralPath (Join-Path $evidence '31号VisualPIX编译.log') -Encoding UTF8
[ordered]@{CompiledAt=(Get-Date -Format o);PIX='17_轨道巡检车.pix';PIX_SHA256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot '17_轨道巡检车.pix')).Hash;BIN='17_轨道巡检车.pix.bin';BIN_SHA256=(Get-FileHash -LiteralPath $bin.FullName).Hash;BIN_Length=$bin.Length;NewBinary=$true;PhysicalCommunicationVerified=$false;MotionVerified=$false}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $evidence '31号编译产物.json') -Encoding UTF8
Write-Output $output
