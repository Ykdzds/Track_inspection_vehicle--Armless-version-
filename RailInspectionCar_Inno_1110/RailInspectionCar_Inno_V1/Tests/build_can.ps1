param([string]$Keil = "D:\NewMDK5.36\UV4\UV4.exe", [switch]$CompatibilityOnly)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$mdk = Join-Path $root "MDK-ARM"
$project = Join-Path $mdk "RailInspectionCar_V1.uvprojx"
$original = [System.IO.File]::ReadAllBytes($project)
function Invoke-Rebuild([string]$Name) {
    $log = Join-Path $env:TEMP "$Name.log"
    $process = Start-Process -FilePath $Keil -WindowStyle Hidden -Wait -PassThru `
        -WorkingDirectory $mdk `
        -ArgumentList "-r RailInspectionCar_V1.uvprojx -t RailInspectionCar_V1 -o $log"
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
    if (!$CompatibilityOnly) {
        foreach ($axis in @(1, 2, 3)) {
            foreach ($debug in @(0, 1)) {
                $variants += @{
                    Name = "can_matrix_${axis}_${debug}"
                    Define = ",APP_SERVO_QUERY_MODE=$axis,APP_SERVO_CONTROL_AXIS=$axis,APP_CAN_DEBUG_ENABLED=$debug"
                }
            }
        }
    }
    $variants += @{Name = 'can_disabled'; Define = ',APP_CAN_ENABLED=0,APP_CAN_CONTROL_ENABLED=0,APP_CAN_STOP_PARAMETERS_APPROVED=0'}
    $variants += @{Name = 'can_readonly'; Define = ',APP_SERVO_READONLY_TEST=1,APP_CAN_CONTROL_ENABLED=0,APP_CAN_STOP_PARAMETERS_APPROVED=0'}
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
    Invoke-Rebuild 'can_final_current'
}
finally { [System.IO.File]::WriteAllBytes($project, $original) }
