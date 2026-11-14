param([string]$Keil = "D:\NewMDK5.36\UV4\UV4.exe")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$mdk = Join-Path $root "MDK-ARM"
$project = Join-Path $mdk "RailInspectionCar_V1.uvprojx"
$original = [System.IO.File]::ReadAllBytes($project)
$rtePath = Join-Path $mdk "RTE/_RailInspectionCar_V1/RTE_Components.h"
$rteOriginal = [System.IO.File]::ReadAllBytes($rtePath)
$project = Join-Path $mdk ("hmi_matrix_" + [guid]::NewGuid().ToString("N") + ".uvprojx")
$projectName = Split-Path $project -Leaf
function Invoke-Rebuild([string]$Name) {
    $log = Join-Path $env:TEMP "$Name.log"
    $process = Start-Process -FilePath $Keil -WindowStyle Hidden -Wait -PassThru `
        -WorkingDirectory $mdk `
        -ArgumentList "-r $projectName -t RailInspectionCar_V1 -o $log"
    $text = Get-Content -LiteralPath $log -Raw
    if ($process.ExitCode -ne 0 -or $text -notmatch '0 Error\(s\), 0 Warning\(s\)') {
        Write-Output $text
        throw "Keil全量编译失败：$Name"
    }
    Write-Output "PASS $Name : 0 errors, 0 warnings; log=$log"
    & "$PSScriptRoot/check_can_config.ps1" -Map "$mdk/RailInspectionCar_V1/RailInspectionCar_V1.map"
    & "$PSScriptRoot/check_task_stack.ps1" -Report "$mdk/RailInspectionCar_V1/RailInspectionCar_V1.htm"
}
try {
    $variants = @()
    foreach ($axis in @(1, 2, 3)) {
        foreach ($hold in @(0, 1)) {
            $variants += @{
                Name = "hmi_readonly_${axis}_${hold}"
                Define = ",APP_SERVO_QUERY_MODE=$axis,APP_SERVO_CONTROL_AXIS=$axis,APP_SERVO_STARTUP_HOLD_ENABLED=$hold,APP_HMI_CONTROL_ENABLED=0"
            }
        }
    }
    $variants += @{Name = 'hmi_can_disabled'; Define = ',APP_CAN_ENABLED=0'}
    $variants += @{Name = 'hmi_servo_readonly'; Define = ',APP_SERVO_READONLY_TEST=1'}
    foreach ($variant in $variants) {
        $xml = New-Object System.Xml.XmlDocument
        $xml.PreserveWhitespace = $true
        $xml.LoadXml([System.Text.Encoding]::UTF8.GetString($original))
        $node = $xml.SelectSingleNode("//TargetArmAds/Cads/VariousControls/Define")
        if ($null -eq $node) { throw "未找到Keil编译宏节点" }
        $node.InnerText += $variant.Define
        $settings = New-Object System.Xml.XmlWriterSettings
        $settings.Encoding = New-Object System.Text.UTF8Encoding($false)
        $writer = [System.Xml.XmlWriter]::Create($project, $settings)
        try { $xml.Save($writer) } finally { $writer.Dispose() }
        Invoke-Rebuild $variant.Name
    }
    [System.IO.File]::WriteAllBytes($project, $original)
    $projectName = "RailInspectionCar_V1.uvprojx"
    Invoke-Rebuild 'hmi_final_current'
    $unchanged = [Convert]::ToBase64String([System.IO.File]::ReadAllBytes((Join-Path $mdk $projectName)))
    if ($unchanged -ne [Convert]::ToBase64String($original)) { throw "原Keil工程被外部修改，需人工核对，脚本不会回写覆盖" }
}
finally {
    [System.IO.File]::WriteAllBytes($rtePath, $rteOriginal)
    # 只删除本脚本生成的同目录临时工程，不触碰用户的原工程配置。
    foreach ($extension in @('.uvprojx','.uvoptx','.uvguix.15056')) {
        $temporary = [System.IO.Path]::ChangeExtension($project, $extension)
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
    }
}
