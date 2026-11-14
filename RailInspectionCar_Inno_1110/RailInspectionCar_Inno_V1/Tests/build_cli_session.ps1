param(
    [string]$Keil = "D:\NewMDK5.36\UV4\UV4.exe"
)
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
}
try {
    # 只临时覆盖编译宏，结束时恢复原工程字节及现场固件。
    foreach ($axis in @(1, 2, 3)) {
        foreach ($enabled in @(0, 1)) {
            $xml = New-Object System.Xml.XmlDocument
            $xml.PreserveWhitespace = $true
            $xml.LoadXml([System.Text.Encoding]::UTF8.GetString($original))
            $node = $xml.SelectSingleNode("//TargetArmAds/Cads/VariousControls/Define")
            if ($null -eq $node) { throw "未找到Keil编译宏节点" }
            $node.InnerText += ",APP_DEBUG_CLI_ENABLED=1,APP_SERVO_PULSE_ENABLED=1,APP_SERVO_QUERY_MODE=$axis,APP_SERVO_CONTROL_AXIS=$axis,APP_SERVO_CLI_DEBUG_ENABLED=$enabled"
            $settings = New-Object System.Xml.XmlWriterSettings
            $settings.Encoding = New-Object System.Text.UTF8Encoding($false)
            $writer = [System.Xml.XmlWriter]::Create($project, $settings)
            try { $xml.Save($writer) } finally { $writer.Dispose() }
            Invoke-Rebuild "cli_matrix_${axis}_${enabled}"
        }
    }
    [System.IO.File]::WriteAllBytes($project, $original)
    Invoke-Rebuild "cli_final_current"
}
finally {
    [System.IO.File]::WriteAllBytes($project, $original)
}
