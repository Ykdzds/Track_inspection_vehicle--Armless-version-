param([string]$Keil = "D:\NewMDK5.36\UV4\UV4.exe")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$mdk = Join-Path $root "MDK-ARM"
$project = Join-Path $mdk "RailInspectionCar_V1.uvprojx"
$original = [System.IO.File]::ReadAllBytes($project)
try {
    # 仅临时覆盖编译宏，最终恢复原工程并重新生成正式固件。
    foreach ($mode in @(1, 2, 3)) {
        foreach ($monitor in @(0, 1)) {
            $xml = New-Object System.Xml.XmlDocument
            $xml.PreserveWhitespace = $true
            $xml.LoadXml([System.Text.Encoding]::UTF8.GetString($original))
            $node = $xml.SelectSingleNode("//TargetArmAds/Cads/VariousControls/Define")
            if ($null -eq $node) { throw "无法定位编译宏" }
            $node.InnerText += ",APP_SERVO_QUERY_MODE=$mode,APP_SERVO_CONTROL_AXIS=$mode,APP_POSITION_MONITOR_ENABLED=$monitor"
            $settings = New-Object System.Xml.XmlWriterSettings
            $settings.Encoding = New-Object System.Text.UTF8Encoding($false)
            $writer = [System.Xml.XmlWriter]::Create($project, $settings)
            try { $xml.Save($writer) } finally { $writer.Dispose() }
            $log = Join-Path $env:TEMP "position_matrix_${mode}_${monitor}.log"
            $process = Start-Process -FilePath $Keil -WindowStyle Hidden -Wait -PassThru `
                -WorkingDirectory $mdk `
                -ArgumentList "-r RailInspectionCar_V1.uvprojx -t RailInspectionCar_V1 -o $log"
            $text = Get-Content -LiteralPath $log -Raw
            if ($process.ExitCode -ne 0 -or $text -notmatch '0 Error\(s\), 0 Warning\(s\)') {
                Write-Output $text
                throw "Keil失败：轴=$mode 监控=$monitor"
            }
            Write-Output "PASS Keil axis=$mode position=$monitor : 0 errors, 0 warnings; log=$log"
        }
    }
    # 额外验证TAB调试扩展分支，正式配置仍恢复用户原设置。
    $node.InnerText += ",APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED=1"
    $writer = [System.Xml.XmlWriter]::Create($project, $settings)
    try { $xml.Save($writer) } finally { $writer.Dispose() }
    $debugLog = Join-Path $env:TEMP "position_debug_enabled.log"
    $process = Start-Process -FilePath $Keil -WindowStyle Hidden -Wait -PassThru `
        -WorkingDirectory $mdk `
        -ArgumentList "-r RailInspectionCar_V1.uvprojx -t RailInspectionCar_V1 -o $debugLog"
    $text = Get-Content -LiteralPath $debugLog -Raw
    if ($process.ExitCode -ne 0 -or $text -notmatch '0 Error\(s\), 0 Warning\(s\)') {
        Write-Output $text
        throw "调试开启编译失败"
    }
    Write-Output "PASS Keil debug enabled: 0 errors, 0 warnings; log=$debugLog"
    [System.IO.File]::WriteAllBytes($project, $original)
    $log = Join-Path $env:TEMP "position_final_current.log"
    $process = Start-Process -FilePath $Keil -WindowStyle Hidden -Wait -PassThru `
        -WorkingDirectory $mdk `
        -ArgumentList "-r RailInspectionCar_V1.uvprojx -t RailInspectionCar_V1 -o $log"
    $text = Get-Content -LiteralPath $log -Raw
    if ($process.ExitCode -ne 0 -or $text -notmatch '0 Error\(s\), 0 Warning\(s\)') {
        Write-Output $text
        throw "正式配置编译失败"
    }
    Write-Output "PASS Keil final BOTH position=1: 0 errors, 0 warnings; log=$log"
}
finally {
    [System.IO.File]::WriteAllBytes($project, $original)
}
