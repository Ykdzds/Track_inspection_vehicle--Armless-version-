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
    & (Join-Path $PSScriptRoot "check_task_stack.ps1") `
        -Report (Join-Path $mdk "RailInspectionCar_V1/RailInspectionCar_V1.htm")
}
try {
    # 仅临时覆盖轴与上电待机宏，不改变K2开关和现场控制参数。
    foreach ($axis in @(1, 2, 3)) {
        foreach ($hold in @(0, 1)) {
            $xml = New-Object System.Xml.XmlDocument
            $xml.PreserveWhitespace = $true
            $xml.LoadXml([System.Text.Encoding]::UTF8.GetString($original))
            $node = $xml.SelectSingleNode("//TargetArmAds/Cads/VariousControls/Define")
            if ($null -eq $node) { throw "未找到Keil编译宏节点" }
            $node.InnerText += ",APP_SERVO_QUERY_MODE=$axis,APP_SERVO_CONTROL_AXIS=$axis,APP_SERVO_STARTUP_HOLD_ENABLED=$hold"
            $settings = New-Object System.Xml.XmlWriterSettings
            $settings.Encoding = New-Object System.Text.UTF8Encoding($false)
            $writer = [System.Xml.XmlWriter]::Create($project, $settings)
            try { $xml.Save($writer) } finally { $writer.Dispose() }
            Invoke-Rebuild "k1_hold_matrix_${axis}_${hold}"
        }
    }
    [System.IO.File]::WriteAllBytes($project, $original)
    Invoke-Rebuild "k1_hold_final_current"
}
finally { [System.IO.File]::WriteAllBytes($project, $original) }
