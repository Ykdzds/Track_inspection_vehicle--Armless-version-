param(
    [string]$Keil = "D:\NewMDK5.36\UV4\UV4.exe",
    [string]$Artifacts = "",
    [switch]$FullMatrix
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$mdk = Join-Path $root "MDK-ARM"
$sourceProject = Join-Path $mdk "RailInspectionCar_V1.uvprojx"
$originalProject = [System.IO.File]::ReadAllBytes($sourceProject)
$originalProjectHash = [Convert]::ToBase64String($originalProject)
$temporaryProject = Join-Path $mdk ("hmi_control_matrix_" + [guid]::NewGuid().ToString("N") + ".uvprojx")
$temporaryProjectName = Split-Path $temporaryProject -Leaf
$temporaryOutputs = @()
$rtePath = Join-Path $mdk "RTE/_RailInspectionCar_V1/RTE_Components.h"
$rteOriginal = [System.IO.File]::ReadAllBytes($rtePath)
$hashes = @()
if ($Artifacts) { $Artifacts = [System.IO.Path]::GetFullPath($Artifacts); New-Item -ItemType Directory -Path $Artifacts -Force | Out-Null }
function Get-MapRegion([string]$MapPath) {
    if (!(Test-Path -LiteralPath $MapPath)) { throw "未找到链接Map文件：$MapPath" }
    $text = Get-Content -LiteralPath $MapPath -Raw
    $match = [regex]::Match($text, 'Load Region LR_IROM1 \(Base: (0x[0-9a-fA-F]+), Size: (0x[0-9a-fA-F]+)')
    if (!$match.Success) { throw "未找到IROM链接区域：$MapPath" }
    $base = [Convert]::ToUInt64($match.Groups[1].Value.Substring(2), 16)
    $size = [Convert]::ToUInt64($match.Groups[2].Value.Substring(2), 16)
    if ($base -ne 0x08000000 -or ($base + $size) -gt 0x080A0000) { throw ("程序区越界：base=0x{0:X8}, end=0x{1:X8}" -f $base, ($base + $size)) }
    Write-Output ("PASS Flash region: end=0x{0:X8} < 0x080A0000" -f ($base + $size))
}
function Invoke-VariantBuild([int]$Axis, [int]$Hold, [int]$Enabled = 1, [int]$Readonly = 0) {
    $name = "hmi_control_axis_${Axis}_hold_${Hold}_hmi_${Enabled}_readonly_${Readonly}"
    $outputDirectoryName = ".codex-hmi-control-build\\$name\\"
    $outputDirectory = Join-Path $mdk $outputDirectoryName
    $script:temporaryOutputs += $outputDirectory
    $xml = New-Object System.Xml.XmlDocument
    $xml.PreserveWhitespace = $true
    $xml.LoadXml([System.Text.Encoding]::UTF8.GetString($originalProject))
    $define = $xml.SelectSingleNode('//Target/TargetOption/TargetArmAds/Cads/VariousControls/Define')
    if ($null -eq $define) { throw "未找到正式工程编译宏节点" }
    $define.InnerText = "USE_HAL_DRIVER,STM32F407xx,APP_SERVO_QUERY_MODE=$Axis,APP_SERVO_CONTROL_AXIS=$Axis,APP_SERVO_STARTUP_HOLD_ENABLED=$Hold,APP_HMI_CONTROL_ENABLED=$Enabled,APP_SERVO_READONLY_TEST=$Readonly"
    $output = $xml.SelectSingleNode('//Target/TargetOption/TargetCommonOption/OutputDirectory')
    $outputName = $xml.SelectSingleNode('//Target/TargetOption/TargetCommonOption/OutputName')
    if ($null -eq $output -or $null -eq $outputName) { throw "未找到Keil输出配置节点" }
    $output.InnerText = $outputDirectoryName
    $outputName.InnerText = $name
    $settings = New-Object System.Xml.XmlWriterSettings
    $settings.Encoding = New-Object System.Text.UTF8Encoding($false)
    $writer = [System.Xml.XmlWriter]::Create($temporaryProject, $settings)
    try { $xml.Save($writer) } finally { $writer.Dispose() }
    $log = Join-Path $env:TEMP ("$name.log")
    $arguments = @("-r", $temporaryProjectName, "-t", "RailInspectionCar_V1", "-o", $log)
    $process = Start-Process -FilePath $Keil -WindowStyle Hidden -Wait -PassThru -WorkingDirectory $mdk -ArgumentList $arguments
    $text = Get-Content -LiteralPath $log -Raw
    if ($process.ExitCode -ne 0 -or $text -notmatch '0 Error\(s\), 0 Warning\(s\)') { Write-Output $text; throw "HMI宏开启全量编译失败：$name" }
    Write-Output "PASS $name : 0 errors, 0 warnings; log=$log"
    $map = Join-Path $mdk (Join-Path $outputDirectoryName "$name.map")
    $htm = Join-Path $mdk (Join-Path $outputDirectoryName "$name.htm")
    Get-MapRegion $map
    if (Test-Path -LiteralPath $htm) { & (Join-Path $PSScriptRoot 'check_task_stack.ps1') -Report $htm }
    else { Write-Output "INFO 未生成HTM报告，跳过静态栈调用图检查：$htm" }
    if ($Artifacts) {
        $destination = Join-Path $Artifacts $name
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        Copy-Item -LiteralPath $log -Destination $destination
        foreach ($extension in @("hex", "axf", "map", "htm")) {
            $artifact = Join-Path $outputDirectory "$name.$extension"
            if (Test-Path -LiteralPath $artifact) {
                Copy-Item -LiteralPath $artifact -Destination $destination
                $hash = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash
                $script:hashes += "$hash  $name/$name.$extension"
            }
        }
    }
}
try {
    foreach ($axis in @(1, 2, 3)) {
        foreach ($hold in @(0, 1)) {
            Invoke-VariantBuild $axis $hold
            if ($FullMatrix) { Invoke-VariantBuild $axis $hold 0 0 }
        }
        if ($FullMatrix) { Invoke-VariantBuild $axis 1 1 1 }
    }
    if ($Artifacts) { $hashes | Set-Content -LiteralPath (Join-Path $Artifacts "SHA256SUMS.txt") -Encoding UTF8 }
}
finally {
    [System.IO.File]::WriteAllBytes($rtePath, $rteOriginal)
    [System.IO.File]::WriteAllBytes($sourceProject, $originalProject)
    if ([Convert]::ToBase64String([System.IO.File]::ReadAllBytes($sourceProject)) -ne $originalProjectHash) { throw "正式Keil工程恢复校验失败" }
    if (Test-Path -LiteralPath $temporaryProject) { Remove-Item -LiteralPath $temporaryProject -Force }
    $allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $mdk ".codex-hmi-control-build")) + [System.IO.Path]::DirectorySeparatorChar
    foreach ($output in $temporaryOutputs) {
        $resolved = [System.IO.Path]::GetFullPath($output)
        if (!$resolved.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) { throw "拒绝清理越界构建目录：$resolved" }
        if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
    }
    foreach ($extension in @('.uvoptx', '.uvguix.15056')) { $temporarySidecar = [System.IO.Path]::ChangeExtension($temporaryProject, $extension); if (Test-Path -LiteralPath $temporarySidecar) { Remove-Item -LiteralPath $temporarySidecar -Force } }
}
