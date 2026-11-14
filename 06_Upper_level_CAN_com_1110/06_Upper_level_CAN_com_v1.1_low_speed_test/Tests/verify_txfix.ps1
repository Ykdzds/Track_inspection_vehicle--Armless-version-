param([string]$Baseline = 'C:\Users\15056\AppData\Local\Temp\F103_TXFIX_20261020_194833')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root 'Verification'
$baselineFiles = @(Get-ChildItem -LiteralPath $Baseline -Recurse -File | Where-Object {
    $_.FullName -match '[\\/](APP|Core|Tests)[\\/]' -or $_.Extension -in '.hex','.ioc','.uvprojx'
})
$baselineFiles | Sort-Object FullName | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash, $_.FullName.Substring($Baseline.Length + 1)
} | Set-Content -LiteralPath (Join-Path $output 'TXFIX_frozen_SPEED400_SHA256.txt') -Encoding utf8
$protected = Get-Content -LiteralPath (Join-Path $Baseline 'protected.json') -Raw | ConvertFrom-Json
$known = @{}
$changed = @()
foreach ($item in $protected) {
    $known[$item.Path] = $true
    if (!(Test-Path -LiteralPath $item.Path) -or (Get-FileHash -LiteralPath $item.Path -Algorithm SHA256).Hash -ne $item.Hash) { $changed += $item.Path }
}
$codeRoot = Split-Path (Split-Path $root -Parent) -Parent
$protectedRoots = @((Join-Path $codeRoot 'RailInspectionCar_Inno_V1'), (Join-Path $codeRoot 'F103_TEST\05_Upper_level_CAN_com_test'), (Join-Path $codeRoot 'F103_TEST\05_Upper_level_CAN_com_v1.1_readonly'))
$added = @()
foreach ($directory in $protectedRoots) {
    foreach ($file in Get-ChildItem -LiteralPath $directory -File -Recurse -Force | Where-Object { $_.FullName -notmatch '[\\/]\.git[\\/]' }) {
        if (!$known.ContainsKey($file.FullName)) { $added += $file.FullName }
    }
}
@("Protected baseline: $Baseline", "Protected files: $($protected.Count)", "Changed or missing: $($changed.Count)", "Added: $($added.Count)") + $changed + $added | Set-Content -LiteralPath (Join-Path $output 'TXFIX_protection.log') -Encoding utf8
if ($changed.Count -or $added.Count) { throw '受保护工程发生变化' }
$mapPath = Join-Path $root 'MDK-ARM\DigitalElectronicseCode\DigitalElectronicseCode.map'
$stackPath = Join-Path $root 'MDK-ARM\DigitalElectronicseCode\DigitalElectronicseCode.htm'
$map = Get-Content -LiteralPath $mapPath -Raw
$flash = [regex]::Match($map, 'Load Region LR_IROM1 \(Base: 0x08000000, Size: (0x[0-9a-fA-F]+)')
$ram = [regex]::Match($map, 'Execution Region RW_IRAM1 \(Exec base: 0x20000000,.*?Size: (0x[0-9a-fA-F]+)')
if (!$flash.Success -or !$ram.Success) { throw '无法解析链接资源' }
$flashBytes = [Convert]::ToInt32($flash.Groups[1].Value.Substring(2),16)
$ramBytes = [Convert]::ToInt32($ram.Groups[1].Value.Substring(2),16)
if ($flashBytes -gt 0xf800 -or $ramBytes -gt 0x5000) { throw 'Flash或RAM越界' }
$resources=@(& (Join-Path $PSScriptRoot 'check_resources.ps1') -BuildRoot (Split-Path $mapPath -Parent) -DebugEnabled 0)
$resources += 'Static estimates include unknown call paths; not a measured runtime stack high-water mark.'
$resources | Set-Content -LiteralPath (Join-Path $output 'TXFIX_resources.log') -Encoding utf8
Copy-Item -LiteralPath $mapPath -Destination (Join-Path $output 'TXFIX_link.map')
Copy-Item -LiteralPath $stackPath -Destination (Join-Path $output 'TXFIX_stack.htm')
$hashPaths = @('APP','Core','Tests') | ForEach-Object { Get-ChildItem -LiteralPath (Join-Path $root $_) -Recurse -File }
$hashPaths += Get-Item -LiteralPath (Join-Path $root 'DigitalElectronicseCode.ioc'), (Join-Path $root 'MDK-ARM\DigitalElectronicseCode\DigitalElectronicseCode.hex')
$hashPaths += Get-ChildItem -LiteralPath $output -File | Where-Object { $_.Name -like 'TXFIX*' -and $_.Name -ne 'TXFIX_SHA256.txt' }
$hashPaths += Get-Item -LiteralPath (Join-Path $output 'keil_build.log')
$hashPaths += Get-Item -LiteralPath (Join-Path $output 'unit.log'), (Join-Path $output 'pair.log'), (Join-Path $output 'logs.log'), (Join-Path $output 'auth_prepare.log'), (Join-Path $output 'auth_repro.log'), (Join-Path $output 'speed400.log'), (Join-Path $output 'txfix.log')
$hashPaths += Get-Item -LiteralPath (Join-Path $root 'ERR103_ERR110修复交付说明_20261020.md')
$hashPaths | Sort-Object FullName -Unique | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash, $_.FullName.Substring($root.Length + 1)
} | Set-Content -LiteralPath (Join-Path $output 'TXFIX_SHA256.txt') -Encoding utf8
$resources; Get-Content -LiteralPath (Join-Path $output 'TXFIX_protection.log')
