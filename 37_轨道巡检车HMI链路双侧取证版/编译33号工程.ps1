$ErrorActionPreference='Stop'
$evidence=Join-Path $PSScriptRoot '运行核验/33号实施_20261102'
. (Join-Path $PSScriptRoot 'pix-ui.ps1') -Action SelectPage -Name P0_Boot
# Invoke the menu command: TogglePattern changes toolbar selection only.
$debug33=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::MenuItem -and $_.GetSupportedPatterns().ProgrammaticName -contains 'ExpandCollapsePatternIdentifiers.Pattern' -and $_.FindFirst('Descendants',[System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty,'调试 (_D)'))}|Select-Object -First 1
if(!$debug33){throw 'Debug menu missing'}
$debug33.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
$compile33=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::MenuItem -and $_.GetSupportedPatterns().ProgrammaticName -contains 'InvokePatternIdentifiers.Pattern' -and $_.FindFirst('Descendants',[System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty,'编译'))}|Select-Object -First 1
if(!$compile33){throw 'Compile command missing'}
$compiledAfter=Get-Date
$compile33.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
$deadline33=(Get-Date).AddSeconds(45)
do { Start-Sleep -Milliseconds 500; $candidate33=Get-Item -LiteralPath (Join-Path $PSScriptRoot '17_轨道巡检车.pix.bin') } while($candidate33.LastWriteTime -lt $compiledAfter -and (Get-Date) -lt $deadline33)
$bin=Get-Item -LiteralPath (Join-Path $PSScriptRoot '17_轨道巡检车.pix.bin')
if($bin.LastWriteTime -lt $compiledAfter){throw '本次未生成新BIN'}
$deadlineOutput33=(Get-Date).AddSeconds(30)
do {
$output=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Document -and $_.GetSupportedPatterns().ProgrammaticName -contains 'TextPatternIdentifiers.Pattern'}|ForEach-Object {$_.GetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern).DocumentRange.GetText(-1)}|Where-Object {$_.Length -lt 12000 -and $_ -match '编译成功|编译失败'}|Select-Object -First 1
if(!$output -or $output -notmatch '编译成功'){Start-Sleep -Milliseconds 500}
} while((!$output -or $output -notmatch '编译成功') -and (Get-Date) -lt $deadlineOutput33)
if(!$output -or $output -notmatch '编译成功' -or $output -match '编译失败'){throw '编译失败或状态未知'}
$output|Set-Content -LiteralPath (Join-Path $evidence '33号VisualPIX编译.log') -Encoding UTF8
[ordered]@{CompiledAt=(Get-Date -Format o);PIX='17_轨道巡检车.pix';PIX_SHA256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot '17_轨道巡检车.pix')).Hash;BIN='17_轨道巡检车.pix.bin';BIN_SHA256=(Get-FileHash -LiteralPath $bin.FullName).Hash;BIN_Length=$bin.Length;NewBinary=$true;PhysicalCommunicationVerified=$false;MotionVerified=$false}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $evidence '33号编译产物.json') -Encoding UTF8
Write-Output $output
