param(
    [string]$Keil = "D:\NewMDK5.36\UV4\UV4.exe",
    [ValidateSet(1, 2, 3)][int[]]$Axes = @(1, 2, 3)
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$mdk = Join-Path $root "MDK-ARM"
$project = Join-Path $mdk "RailInspectionCar_V1.uvprojx"
$original = [System.IO.File]::ReadAllBytes($project)
function Invoke-NrfBuild([string]$name) {
    $log = Join-Path $env:TEMP "nrf_${name}.log"
    $process = Start-Process -FilePath $Keil -WindowStyle Hidden -Wait -PassThru `
        -WorkingDirectory $mdk `
        -ArgumentList "-r RailInspectionCar_V1.uvprojx -t RailInspectionCar_V1 -o $log"
    $text = Get-Content -LiteralPath $log -Raw
    if ($process.ExitCode -ne 0 -or $text -notmatch '0 Error\(s\), 0 Warning\(s\)') {
        Write-Output $text
        throw "Keil编译失败：$name"
    }
    Write-Output "PASS Keil $name : 0 errors, 0 warnings; log=$log"
}
try {
    # 只临时修改工程编译宏，不修改现场参数；结束时恢复原工程和正式固件。
    foreach ($axis in $Axes) {
        foreach ($config in @(@(0, 0), @(0, 1), @(1, 1))) {
            $enabled = $config[0]
            $ack = $config[1]
            $xml = New-Object System.Xml.XmlDocument
            $xml.PreserveWhitespace = $true
            $xml.LoadXml([System.Text.Encoding]::UTF8.GetString($original))
            $node = $xml.SelectSingleNode("//TargetArmAds/Cads/VariousControls/Define")
            if ($null -eq $node) { throw "无法定位工程编译宏" }
            $node.InnerText += ",APP_SERVO_QUERY_MODE=$axis,APP_SERVO_CONTROL_AXIS=$axis,APP_NRF_TELEMETRY_ENABLED=$enabled,APP_NRF_ACK_PAYLOAD_ENABLED=$ack"
            $settings = New-Object System.Xml.XmlWriterSettings
            $settings.Encoding = New-Object System.Text.UTF8Encoding($false)
            $writer = [System.Xml.XmlWriter]::Create($project, $settings)
            try { $xml.Save($writer) } finally { $writer.Dispose() }
            Invoke-NrfBuild "matrix_${axis}_${enabled}_${ack}"
        }
    }
    [System.IO.File]::WriteAllBytes($project, $original)
    Invoke-NrfBuild "final_current"
}
finally {
    [System.IO.File]::WriteAllBytes($project, $original)
}
