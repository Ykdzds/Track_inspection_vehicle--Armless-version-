param([string]$Keil='D:\NewMDK5.36\UV4\UV4.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$project=Join-Path $root 'MDK-ARM\DigitalElectronicseCode.uvprojx'
$output=Join-Path $root 'Verification'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$log=Join-Path $output 'keil_build.log'
if(Test-Path -LiteralPath $log){Remove-Item -LiteralPath $log}
$process=Start-Process -FilePath $Keil -ArgumentList @('-r',('"'+$project+'"'),'-t','DigitalElectronicseCode','-j0','-o',('"'+$log+'"')) -WindowStyle Hidden -PassThru
$process.WaitForExit()
$content=Get-Content -LiteralPath $log -Raw
Write-Output $content
if($content -notmatch '0 Error\(s\), 0 Warning\(s\)') { throw 'Keil构建未达到0错误0警告' }
$hex=Join-Path $root 'MDK-ARM\DigitalElectronicseCode\DigitalElectronicseCode.hex'
if(!(Test-Path -LiteralPath $hex)) { throw '未生成HEX' }
$map=Join-Path $root 'MDK-ARM\DigitalElectronicseCode\DigitalElectronicseCode.map'
$memory=[regex]::Match((Get-Content -LiteralPath $map -Raw),'Total RW\s+Size\s+.*?\s(\d+)\s+\(')
if(!$memory.Success) { throw '无法读取SRAM占用' }
$used=[int]$memory.Groups[1].Value
$configuration=Get-Content -LiteralPath (Join-Path $root 'APP\can_upper_config.h') -Raw
$debugMatch=[regex]::Match($configuration,'#define UPPER_USART1_DEBUG_ENABLE\s+([01])U')
if(!$debugMatch.Success){throw '无法读取USART1总开关配置'}
$debug=[int]$debugMatch.Groups[1].Value
$projectText=Get-Content -LiteralPath $project -Raw
$override=[regex]::Match($projectText,'<Define>[^<]*UPPER_USART1_DEBUG_ENABLE=([01])(?:U)?(?:,|<)')
if($override.Success){$debug=[int]$override.Groups[1].Value}
$minimum=5120
if($debug){$minimum=2048}
if(20480-$used -lt $minimum){throw "SRAM余量不足: $used/20480; minimum free=$minimum"}
& (Join-Path $PSScriptRoot 'check_resources.ps1') -BuildRoot (Split-Path $map -Parent) -DebugEnabled $debug
Write-Output "SRAM=$used/20480 free=$(20480-$used) bytes"
Get-FileHash -LiteralPath $hex -Algorithm SHA256 | Format-List
