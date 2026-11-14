param(
    [string]$Keil = "D:\NewMDK5.36\UV4\UV4.exe",
    [string]$OutputRoot = "D:\CodexBuild\RailInspectionCar"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$mdk = Join-Path $root "MDK-ARM"
$project = Join-Path $mdk "RailInspectionCar_V1.uvprojx"
$original = [System.IO.File]::ReadAllBytes($project)
$projectHash = (Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash
$temporaryProject = Join-Path $mdk ("stop_matrix_" + [guid]::NewGuid().ToString('N') + '.uvprojx')
$rte = Join-Path $mdk 'RTE/_RailInspectionCar_V1/RTE_Components.h'
$rteOriginal = [System.IO.File]::ReadAllBytes($rte)
$folder = Join-Path $OutputRoot ('controlled_stop_' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
$null = New-Item -ItemType Directory -Path $folder -Force
$sourceHashes = @(Get-ChildItem -LiteralPath (Join-Path $root 'APP'),
    (Join-Path $root 'BSP'), (Join-Path $root 'Core'), (Join-Path $root 'Device'),
    (Join-Path $root 'Protocol') -File -Recurse |
    Where-Object { $_.Extension -in @('.c', '.h') } | Get-FileHash -Algorithm SHA256)
$sourceHashes | Select-Object Path,Hash | Export-Csv -LiteralPath (Join-Path $folder 'source_sha256.csv') -NoTypeInformation -Encoding UTF8
function Assert-SourceUnchanged {
    foreach ($entry in $sourceHashes) {
        if ((Get-FileHash -LiteralPath $entry.Path -Algorithm SHA256).Hash -ne $entry.Hash) {
            throw ('构建期间源码发生变化：' + $entry.Path)
        }
    }
}
function Invoke-Rebuild([string]$Name, [string]$Defines) {
    Assert-SourceUnchanged
    $xml = New-Object System.Xml.XmlDocument
    $xml.PreserveWhitespace = $true
    $xml.LoadXml([System.Text.Encoding]::UTF8.GetString($original))
    $node = $xml.SelectSingleNode('//TargetArmAds/Cads/VariousControls/Define')
    if ($null -eq $node) { throw '未找到Keil编译宏节点' }
    if ($Defines) { $node.InnerText += ',' + $Defines }
    $output = Join-Path $folder ($Name + '\')
    $xml.SelectSingleNode('//TargetCommonOption/OutputDirectory').InnerText = $output
    $xml.SelectSingleNode('//TargetCommonOption/OutputName').InnerText = $Name
    $settings = New-Object System.Xml.XmlWriterSettings
    $settings.Encoding = New-Object System.Text.UTF8Encoding($false)
    $writer = [System.Xml.XmlWriter]::Create($temporaryProject, $settings)
    try { $xml.Save($writer) } finally { $writer.Dispose() }
    $log = Join-Path $folder "$Name.log"
    $process = Start-Process -FilePath $Keil -WindowStyle Hidden -Wait -PassThru `
        -WorkingDirectory $mdk -ArgumentList @('-r', (Split-Path $temporaryProject -Leaf), '-t', 'RailInspectionCar_V1', '-o', $log)
    $text = Get-Content -LiteralPath $log -Raw
    if ($process.ExitCode -ne 0 -or $text -notmatch '0 Error\(s\), 0 Warning\(s\)') {
        Write-Output $text
        throw "Keil全量编译失败：$Name"
    }
    Write-Output "PASS $Name : 0 errors, 0 warnings; log=$log"
    $map = Get-Content -LiteralPath (Join-Path $output "$Name.map") -Raw
    $region = [regex]::Match($map, 'Load Region LR_IROM1 \(Base: (0x[0-9a-fA-F]+), Size: (0x[0-9a-fA-F]+)')
    if (!$region.Success) { throw '链接映射中未找到Flash程序区' }
    $base = [Convert]::ToUInt64($region.Groups[1].Value.Substring(2), 16)
    $size = [Convert]::ToUInt64($region.Groups[2].Value.Substring(2), 16)
    if ($base -ne 0x08000000 -or ($base + $size) -gt 0x080A0000) { throw 'Flash程序区越界' }
    Write-Output ('PASS Flash region: end=0x{0:X8}' -f ($base + $size))
    & (Join-Path $PSScriptRoot 'check_task_stack.ps1') -Report (Join-Path $output "$Name.htm")
    Get-ChildItem -LiteralPath $output -File |
        Where-Object { $_.Extension -in @('.hex', '.axf', '.map', '.htm') } |
        Get-FileHash -Algorithm SHA256 | Select-Object Path,Hash |
        Export-Csv -LiteralPath (Join-Path $folder "$Name.sha256.csv") -NoTypeInformation -Encoding UTF8
}
try {
    # 使用独立工程和输出目录，正式工程及现场宏始终不变。
    foreach ($axis in @(1, 2, 3)) {
        foreach ($hold in @(0, 1)) {
            foreach ($readonly in @(0, 1)) {
                $canControl = if ($readonly -eq 1) { 0 } else { 1 }
                Invoke-Rebuild "stop_axis_${axis}_hold_${hold}_readonly_${readonly}" `
                    "APP_SERVO_QUERY_MODE=$axis,APP_SERVO_CONTROL_AXIS=$axis,APP_SERVO_STARTUP_HOLD_ENABLED=$hold,APP_SERVO_READONLY_TEST=$readonly,APP_CAN_CONTROL_ENABLED=$canControl"
            }
        }
    }
    Invoke-Rebuild 'stop_both_sync_off' 'APP_SERVO_QUERY_MODE=3,APP_SERVO_CONTROL_AXIS=3,APP_REMOTE_SERVO_SYNC_ENABLED=0'
    Invoke-Rebuild 'stop_final_current' ''
    Assert-SourceUnchanged
}
finally {
    [System.IO.File]::WriteAllBytes($rte, $rteOriginal)
    foreach ($path in @($temporaryProject, [System.IO.Path]::ChangeExtension($temporaryProject, '.uvoptx'), [System.IO.Path]::ChangeExtension($temporaryProject, '.uvguix.15056'))) {
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
    if ((Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash -ne $projectHash) { throw '正式工程配置被意外修改，请检查外部编辑' }
    Write-Output "构建证据保存在 $folder；未执行烧录。"
}
