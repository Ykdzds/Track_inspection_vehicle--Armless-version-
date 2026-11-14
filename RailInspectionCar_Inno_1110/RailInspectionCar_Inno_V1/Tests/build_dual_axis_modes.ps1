param([string]$Keil='D:/NewMDK5.36/UV4/UV4.exe', [string]$LogDirectory=$env:TEMP)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$mdk=Join-Path $root 'MDK-ARM'
$project=Join-Path $mdk 'RailInspectionCar_V1.uvprojx'
$original=[IO.File]::ReadAllBytes($project)
try {
    foreach($mode in @('s_p','s_pi','current')) {
        [IO.File]::WriteAllBytes($project,$original)
        if($mode -ne 'current') {
            $xml=New-Object System.Xml.XmlDocument
            $xml.PreserveWhitespace=$true
            $xml.Load($project)
            $node=$xml.SelectSingleNode('//TargetArmAds/Cads/VariousControls/Define')
            # Algorithm verification parameters only; never a field calibration.
            $node.InnerText+=',APP_DUAL_S_ENABLED=1,APP_DUAL_JERK_MPS3=0.4f'
            if($mode -eq 's_pi'){$node.InnerText+=',APP_DUAL_PI_ENABLED=1,APP_DUAL_KI=0.02f,APP_DUAL_KAW=1.0f'}
            $settings=New-Object System.Xml.XmlWriterSettings
            $settings.Encoding=New-Object System.Text.UTF8Encoding($false)
            $writer=[System.Xml.XmlWriter]::Create($project,$settings)
            try {$xml.Save($writer)} finally {$writer.Dispose()}
        }
        $log=Join-Path $LogDirectory "keil_dual_$mode.log"
        $p=Start-Process -FilePath $Keil -ArgumentList "-r RailInspectionCar_V1.uvprojx -t RailInspectionCar_V1 -o $log" -WorkingDirectory $mdk -WindowStyle Hidden -PassThru -Wait
        $content=Get-Content -LiteralPath $log -Raw
        if($p.ExitCode -ne 0 -or $content -notmatch '0 Error\(s\), 0 Warning\(s\)'){throw "Keil mode=$mode failed; see $log"}
        & "$PSScriptRoot/check_task_stack.ps1" -Report (Join-Path $mdk 'RailInspectionCar_V1/RailInspectionCar_V1.htm')
        Write-Output "PASS Keil mode=$mode 0 errors 0 warnings"
    }
} finally { [IO.File]::WriteAllBytes($project,$original) }
