param([string]$Keil = "D:\NewMDK5.36\UV4\UV4.exe", [switch]$Single, [switch]$SnapshotMatrix,
      [string]$OutputRoot = "D:\CodexBuild\RailInspectionCar")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$mdk = Join-Path $root 'MDK-ARM'
$source = Join-Path $mdk 'RailInspectionCar_V1.uvprojx'
$original = [System.IO.File]::ReadAllBytes($source)
$hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$rte = Join-Path $mdk 'RTE/_RailInspectionCar_V1/RTE_Components.h'
$rteBytes = [System.IO.File]::ReadAllBytes($rte)
$rteOriginal = Get-Content -LiteralPath $rte -Raw
$project = Join-Path $mdk ('can_control_' + [guid]::NewGuid().ToString('N') + '.uvprojx')
$folder = Join-Path $OutputRoot ('rail_can_control_' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
$null = New-Item -ItemType Directory -Path $folder
$sourceFiles = @(Get-ChildItem -LiteralPath (Join-Path $root 'APP'),
    (Join-Path $root 'BSP'), (Join-Path $root 'Core'), (Join-Path $root 'Device'),
    (Join-Path $root 'Protocol') -File -Recurse | Where-Object { $_.Extension -in @('.c','.h') })
$sourceHashes = @($sourceFiles | Get-FileHash -Algorithm SHA256)
$sourceHashes | Select-Object Path,Hash | Export-Csv -LiteralPath (Join-Path $folder 'source_sha256.csv') -NoTypeInformation -Encoding UTF8
function Assert-SourceUnchanged {
    foreach ($entry in $sourceHashes) {
        if ((Get-FileHash -LiteralPath $entry.Path -Algorithm SHA256).Hash -ne $entry.Hash) {
            throw ('构建期间源码改变，矩阵不能作为同版本证据：' + $entry.Path)
        }
    }
}
$oldTemp = $env:TEMP; $oldTmp = $env:TMP
$env:TEMP = $folder; $env:TMP = $folder
$variants = @()
if ($Single) { $variants += @{ Name='can_on'; Defines='APP_CAN_ENABLED=1,APP_CAN_CONTROL_ENABLED=1,APP_CAN_STOP_PARAMETERS_APPROVED=1,APP_SERVO_READONLY_TEST=0' } }
elseif ($SnapshotMatrix) {
    $variants += @{ Name='can_on'; Defines='APP_CAN_CONTROL_ENABLED=1,APP_CAN_STOP_PARAMETERS_APPROVED=1' }
    $variants += @{ Name='can_hmi_coexist'; Defines='APP_CAN_CONTROL_ENABLED=1,APP_CAN_STOP_PARAMETERS_APPROVED=1,APP_HMI_CONTROL_ENABLED=1' }
    $variants += @{ Name='can_disabled'; Defines='APP_CAN_ENABLED=0,APP_CAN_CONTROL_ENABLED=0,APP_CAN_STOP_PARAMETERS_APPROVED=0' }
    $variants += @{ Name='servo_readonly'; Defines='APP_SERVO_READONLY_TEST=1,APP_CAN_CONTROL_ENABLED=0,APP_CAN_STOP_PARAMETERS_APPROVED=0' }
    $variants += @{ Name='workspace_current'; Defines='' }
}
else {
    foreach ($axis in @(1,2,3)) { foreach ($hold in @(0,1)) { foreach ($control in @(0,1)) {
        $variants += @{ Name="can_axis_${axis}_hold_${hold}_control_${control}"; Defines="APP_SERVO_QUERY_MODE=$axis,APP_SERVO_CONTROL_AXIS=$axis,APP_SERVO_STARTUP_HOLD_ENABLED=$hold,APP_CAN_CONTROL_ENABLED=$control,APP_CAN_STOP_PARAMETERS_APPROVED=$control" }
    } } }
    $variants += @{ Name='can_hmi_coexist'; Defines='APP_CAN_CONTROL_ENABLED=1,APP_CAN_STOP_PARAMETERS_APPROVED=1,APP_HMI_CONTROL_ENABLED=1' }
    $variants += @{ Name='can_disabled'; Defines='APP_CAN_ENABLED=0,APP_CAN_CONTROL_ENABLED=0,APP_CAN_STOP_PARAMETERS_APPROVED=0' }
    $variants += @{ Name='servo_readonly'; Defines='APP_SERVO_READONLY_TEST=1,APP_CAN_CONTROL_ENABLED=0,APP_CAN_STOP_PARAMETERS_APPROVED=0' }
    $variants += @{ Name='formal_can_off'; Defines='APP_CAN_ENABLED=1,APP_CAN_CONTROL_ENABLED=0,APP_CAN_STOP_PARAMETERS_APPROVED=0' }
    $variants += @{ Name='workspace_current'; Defines='' }
}
try {
    foreach ($variant in $variants) {
        Assert-SourceUnchanged
        $xml = New-Object System.Xml.XmlDocument
        $xml.PreserveWhitespace = $true
        $xml.LoadXml([System.Text.Encoding]::UTF8.GetString($original))
        $xml.SelectSingleNode('//TargetOption/TargetArmAds/Cads/VariousControls/Define').InnerText = 'USE_HAL_DRIVER,STM32F407xx,' + $variant.Defines
        $output = Join-Path $folder ($variant.Name + '\')
        $xml.SelectSingleNode('//TargetCommonOption/OutputDirectory').InnerText = $output
        $xml.SelectSingleNode('//TargetCommonOption/OutputName').InnerText = $variant.Name
        $xml.SelectSingleNode('//TargetCommonOption/CreateHexFile').InnerText = $(if ($SnapshotMatrix -and $variant.Name -eq 'workspace_current') { '1' } else { '0' })
        $settings = New-Object System.Xml.XmlWriterSettings
        $settings.Encoding = New-Object System.Text.UTF8Encoding($false)
        $writer = [System.Xml.XmlWriter]::Create($project, $settings)
        try { $xml.Save($writer) } finally { $writer.Dispose() }
        $log = Join-Path $folder ($variant.Name + '.log')
        $process = Start-Process -FilePath $Keil -WindowStyle Hidden -Wait -PassThru -WorkingDirectory $mdk -ArgumentList @('-r',(Split-Path $project -Leaf),'-t','RailInspectionCar_V1','-o',$log)
        $text = Get-Content -LiteralPath $log -Raw
        if ($process.ExitCode -ne 0 -or !$text.Contains('0 Error(s), 0 Warning(s)')) { Write-Output $text; throw ('构建失败：' + $variant.Name) }
        Write-Output ('PASS ' + $variant.Name + ' 0 errors 0 warnings')
        & "$PSScriptRoot/check_can_config.ps1" -Map (Join-Path $output ($variant.Name + '.map'))
        & "$PSScriptRoot/check_task_stack.ps1" -Report (Join-Path $output ($variant.Name + '.htm'))
        Write-Output (($text -split "`r?`n" | Where-Object { $_ -match 'Program Size:' }) -join '')
        Assert-SourceUnchanged
    }
} finally {
    $env:TEMP = $oldTemp; $env:TMP = $oldTmp
    $rteNow = Get-Content -LiteralPath $rte -Raw
    if ($rteNow -ne $rteOriginal) {
        [System.IO.File]::WriteAllBytes($rte, $rteBytes)
    }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $hash) { throw '正式工程配置被意外修改' }
    foreach ($path in @($project,[System.IO.Path]::ChangeExtension($project,'.uvoptx'),[System.IO.Path]::ChangeExtension($project,'.uvguix.15056'))) {
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
    Write-Output "构建证据保存于 $folder；仅快照矩阵的workspace_current生成HEX，未烧录。"
}
