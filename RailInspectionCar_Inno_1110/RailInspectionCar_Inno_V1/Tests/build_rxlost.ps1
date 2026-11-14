param([string]$Keil = 'D:\NewMDK5.36\UV4\UV4.exe')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$projects = @(
    @{ Path = (Join-Path $root 'MDK-ARM/RailInspectionCar_V1.uvprojx'); Target = 'RailInspectionCar_V1'; Macro = 'APP_NRF_DIAGNOSTIC_ENABLED' },
    @{ Path = (Join-Path $root '../Remote_control/MDK-ARM/Remote_control.uvprojx'); Target = 'Remote_control'; Macro = 'REMOTE_DIAGNOSTIC_ENABLED' }
)
foreach ($item in $projects) {
    $project = [IO.Path]::GetFullPath($item.Path)
    $original = [IO.File]::ReadAllBytes($project)
    try {
        foreach ($mode in @('off','current')) {
            [IO.File]::WriteAllBytes($project, $original)
            if ($mode -eq 'off') {
                $xml = New-Object System.Xml.XmlDocument
                $xml.PreserveWhitespace = $true
                $xml.LoadXml([Text.Encoding]::UTF8.GetString($original))
                $node = $xml.SelectSingleNode('//TargetArmAds/Cads/VariousControls/Define')
                if ($null -eq $node) { throw '未找到编译宏定义' }
                $node.InnerText += ',' + $item.Macro + '=0'
                $settings = New-Object System.Xml.XmlWriterSettings
                $settings.Encoding = New-Object System.Text.UTF8Encoding($false)
                $writer = [Xml.XmlWriter]::Create($project, $settings)
                try { $xml.Save($writer) } finally { $writer.Dispose() }
            }
            $log = Join-Path $env:TEMP ('rxlost_build_' + $item.Target + '_' + $mode + '.log')
            $process = Start-Process -FilePath $Keil -WindowStyle Hidden -Wait -PassThru `
                -WorkingDirectory (Split-Path $project -Parent) `
                -ArgumentList ('-r "' + $project + '" -t "' + $item.Target + '" -o "' + $log + '"')
            $text = Get-Content -LiteralPath $log -Raw
            if ($process.ExitCode -ne 0 -or $text -notmatch '0 Error\(s\), 0 Warning\(s\)') {
                Write-Output $text
                throw ('Keil构建失败: ' + $item.Target + '/' + $mode)
            }
            Write-Output ('PASS Keil ' + $item.Target + '/' + $mode + ': 0 errors, 0 warnings; ' + $log)
        }
    } finally { [IO.File]::WriteAllBytes($project, $original) }
}
