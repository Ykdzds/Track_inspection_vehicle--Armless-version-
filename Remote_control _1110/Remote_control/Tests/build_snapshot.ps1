param([string]$Keil = 'D:\NewMDK5.36\UV4\UV4.exe',
      [string]$OutputRoot = 'D:\CodexBuild\Remote_control')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$mdk = Join-Path $root 'MDK-ARM'
$source = Join-Path $mdk 'Remote_control.uvprojx'
$original = [System.IO.File]::ReadAllBytes($source)
$rte = Join-Path $mdk 'RTE/_Remote_control/RTE_Components.h'
$rteOriginal = [System.IO.File]::ReadAllBytes($rte)
$project = Join-Path $mdk ('snapshot_' + [guid]::NewGuid().ToString('N') + '.uvprojx')
$folder = Join-Path $OutputRoot ('snapshot_' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
New-Item -ItemType Directory -Force -Path $folder | Out-Null
$inputs = @(Get-ChildItem -LiteralPath (Join-Path $root 'APP'), (Join-Path $root 'BSP'), (Join-Path $root 'Core') -File -Recurse |
    Where-Object { $_.Extension -in @('.c', '.h') })
$hashes = @($inputs | Get-FileHash -Algorithm SHA256)
$hashes | Select-Object Path,Hash | Export-Csv -LiteralPath (Join-Path $folder 'source_sha256.csv') -NoTypeInformation -Encoding UTF8
try {
    $xml = New-Object System.Xml.XmlDocument
    $xml.PreserveWhitespace = $true
    $xml.LoadXml([System.Text.Encoding]::UTF8.GetString($original))
    $xml.SelectSingleNode('//TargetCommonOption/OutputDirectory').InnerText = $folder + '\'
    $xml.SelectSingleNode('//TargetCommonOption/CreateHexFile').InnerText = '1'
    $settings = New-Object System.Xml.XmlWriterSettings
    $settings.Encoding = New-Object System.Text.UTF8Encoding($false)
    $writer = [System.Xml.XmlWriter]::Create($project, $settings)
    try { $xml.Save($writer) } finally { $writer.Dispose() }
    $log = Join-Path $folder 'build.log'
    $process = Start-Process -FilePath $Keil -WindowStyle Hidden -Wait -PassThru -WorkingDirectory $mdk -ArgumentList @('-r',(Split-Path $project -Leaf),'-t','Remote_control','-o',$log)
    $text = Get-Content -LiteralPath $log -Raw
    Write-Output $text
    if ($process.ExitCode -ne 0 -or !$text.Contains('0 Error(s), 0 Warning(s)')) { throw '遥控工程构建失败' }
    foreach ($entry in $hashes) {
        if ((Get-FileHash -LiteralPath $entry.Path -Algorithm SHA256).Hash -ne $entry.Hash) { throw ('构建期间源码改变：' + $entry.Path) }
    }
    Get-FileHash -LiteralPath (Join-Path $folder 'Remote_control.hex') -Algorithm SHA256 | Format-List
} finally {
    # UV4 regenerates this file with the temporary project name. Restore the
    # exact pre-build bytes without relying on an external patch executable.
    $rteNow = [System.IO.File]::ReadAllBytes($rte)
    if ([Convert]::ToBase64String($rteNow) -ne [Convert]::ToBase64String($rteOriginal)) {
        [System.IO.File]::WriteAllBytes($rte, $rteOriginal)
    }
    if ([Convert]::ToBase64String([System.IO.File]::ReadAllBytes($source)) -ne [Convert]::ToBase64String($original)) { throw '正式工程配置被意外修改' }
    foreach ($path in @($project,[System.IO.Path]::ChangeExtension($project,'.uvoptx'),[System.IO.Path]::ChangeExtension($project,'.uvguix.15056'))) {
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
    Write-Output "构建证据保存于 $folder；未烧录。"
}
