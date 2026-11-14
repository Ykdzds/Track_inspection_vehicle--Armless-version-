param(
    [string]$Keil = "D:\NewMDK5.36\UV4\UV4.exe"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$mdk = Join-Path $root "MDK-ARM"
$project = Join-Path $mdk "RailInspectionCar_V1.uvprojx"
$original = [System.IO.File]::ReadAllBytes($project)
try {
    # 临时覆盖编译宏，结束或异常时恢复正式工程的原始字节。
    foreach ($mode in @(1, 2, 3)) {
        foreach ($sync in @(1, 0)) {
            # 最后恢复用户当前BOTH及同步开关配置，避免源码与固件不一致。
            $last = $false
            $name = "sync_matrix_${mode}_${sync}.log"
            $log = Join-Path $env:TEMP $name
            $inputProject = $project
            if (!$last) {
                $xml = New-Object System.Xml.XmlDocument
                $xml.PreserveWhitespace = $true
                $xml.LoadXml([System.Text.Encoding]::UTF8.GetString($original))
                $node = $xml.SelectSingleNode("//TargetArmAds/Cads/VariousControls/Define")
                if ($null -eq $node) { throw "无法定位Keil编译宏配置" }
                $node.InnerText += ",APP_SERVO_QUERY_MODE=$mode,APP_SERVO_CONTROL_AXIS=$mode,APP_REMOTE_SERVO_SYNC_ENABLED=$sync"
                $settings = New-Object System.Xml.XmlWriterSettings
                $settings.Encoding = New-Object System.Text.UTF8Encoding($false)
                $writer = [System.Xml.XmlWriter]::Create($project, $settings)
                try { $xml.Save($writer) } finally { $writer.Dispose() }
            }
            else { [System.IO.File]::WriteAllBytes($project, $original) }
            $projectName = Split-Path $inputProject -Leaf
            $arguments = "-r $projectName -t RailInspectionCar_V1 -o $log"
            $p = Start-Process -FilePath $Keil -ArgumentList $arguments `
                -WorkingDirectory $mdk -WindowStyle Hidden -PassThru -Wait
            $text = Get-Content -LiteralPath $log -Raw
            if ($p.ExitCode -ne 0 -or $text -notmatch '0 Error\(s\), 0 Warning\(s\)') {
                Write-Output $text
                throw "Keil全量编译失败：轴=$mode 同步=$sync"
            }
            Write-Output "PASS Keil axis=$mode sync=$sync : 0 errors, 0 warnings; log=$log"
        }
    }
    [System.IO.File]::WriteAllBytes($project, $original)
    $finalLog = Join-Path $env:TEMP "sync_final_current.log"
    $p = Start-Process -FilePath $Keil `
        -ArgumentList "-r RailInspectionCar_V1.uvprojx -t RailInspectionCar_V1 -o $finalLog" `
        -WorkingDirectory $mdk -WindowStyle Hidden -PassThru -Wait
    $text = Get-Content -LiteralPath $finalLog -Raw
    if ($p.ExitCode -ne 0 -or $text -notmatch '0 Error\(s\), 0 Warning\(s\)') {
        Write-Output $text
        throw "当前配置最终Keil编译失败"
    }
    Write-Output "PASS Keil final current configuration: 0 errors, 0 warnings; log=$finalLog"
}
finally {
    [System.IO.File]::WriteAllBytes($project, $original)
}
