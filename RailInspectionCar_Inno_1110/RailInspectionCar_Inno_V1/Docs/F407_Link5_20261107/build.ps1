param([string]$Keil='D:/NewMDK5.36/UV4/UV4.exe', [string[]]$Variants=@('hmi','remote','canservo','canarm','rs485','all','legacy','alloff'))
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$mdk=Join-Path $root 'MDK-ARM'
$project=Join-Path $mdk 'RailInspectionCar_V1.uvprojx'
$rte=Join-Path $mdk 'RTE/_RailInspectionCar_V1/RTE_Components.h'
$artifact=Join-Path $PSScriptRoot '验证'
New-Item -ItemType Directory -Path $artifact -Force | Out-Null
$original=[IO.File]::ReadAllBytes($project)
$rteOriginal=[IO.File]::ReadAllBytes($rte)
$text=[IO.File]::ReadAllText($project)
$expected=(Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash
$switches=@('APP_HMI_LINK_DIAG_ENABLED','APP_NRF_DIAGNOSTIC_ENABLED','APP_CAN_SERVO_DIAG_ENABLED','APP_CAN_ARM_DIAG_ENABLED','APP_SERVO_RS485_DIAG_ENABLED','APP_CAN_DEBUG_ENABLED','APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED')
$names=@('hmi','remote','canservo','canarm','rs485','raw','sync')
$results=@()
function ProductionDigest {
    $entries=@(rg --files -g '*.c' -g '*.h' -g '*.s' -g '*.sct' APP BSP Core Device Protocol | Sort-Object | ForEach-Object {
        [ordered]@{Path=$_;SHA256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash}
    })
    $json=ConvertTo-Json -InputObject $entries -Depth 3 -Compress
    $sha=[Security.Cryptography.SHA256]::Create()
    return [ordered]@{Files=$entries;Digest=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($json)))).Replace('-','')}
}
$source=ProductionDigest
$source | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $artifact 'production-source-sha256.json') -Encoding utf8
try {
    foreach($variant in $Variants) {
        if((Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash -ne $expected){throw 'Formal project changed concurrently'}
        if((ProductionDigest).Digest -ne $source.Digest){throw 'Production source changed before build'}
        $destination=Join-Path $artifact $variant
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        $defines=@('USE_HAL_DRIVER','STM32F407xx')
        for($i=0;$i -lt $switches.Count;$i++) {
            $value=[int](($variant -eq $names[$i]) -or ($variant -eq 'all' -and $i -lt 5) -or ($variant -eq 'legacy'))
            $defines+=($switches[$i]+'='+$value)
        }
        $replacement='<Define>'+($defines -join ',')+'</Define>'
        $modified=$text.Replace('<Define>USE_HAL_DRIVER,STM32F407xx</Define>',$replacement)
        if($modified -eq $text){throw 'Formal target defines not found'}
        [IO.File]::WriteAllText($project,$modified,[Text.UTF8Encoding]::new($false))
        $expected=(Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash
        Copy-Item -LiteralPath $project -Destination (Join-Path $destination 'project.uvprojx.snapshot') -Force
        $log=Join-Path $env:TEMP ('rail_link5_'+$variant+'_'+[guid]::NewGuid().ToString('N')+'.log')
        Write-Output "START full Keil $variant"
        $started=Get-Date
        $process=Start-Process -FilePath $Keil -WorkingDirectory $mdk -WindowStyle Hidden -Wait -PassThru -ArgumentList @('-r','RailInspectionCar_V1.uvprojx','-t','RailInspectionCar_V1','-o',('"'+$log+'"'))
        if((Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash -ne $expected){throw 'Formal project changed during build'}
        if(!(Test-Path -LiteralPath $log)){throw "No build log, exit=$($process.ExitCode)"}
        Copy-Item -LiteralPath $log -Destination (Join-Path $destination 'keil-rebuild.log') -Force
        $content=[IO.File]::ReadAllText($log)
        if($content -notmatch '0 Error\(s\), 0 Warning\(s\)' -or $process.ExitCode -gt 1){
            Get-Content -LiteralPath $log | Select-Object -Last 60
            throw "Keil failed $variant exit=$($process.ExitCode)"
        }
        foreach($extension in @('map','htm','hex','axf')) {
            Copy-Item -LiteralPath (Join-Path $mdk "RailInspectionCar_V1/RailInspectionCar_V1.$extension") -Destination $destination -Force
        }
        if((ProductionDigest).Digest -ne $source.Digest){throw 'Production source changed during build'}
        $memory=@(& (Join-Path $root 'Tests/check_tune_memory.ps1'))
        $memory | Set-Content -LiteralPath (Join-Path $destination 'memory-stack.log') -Encoding utf8
        $map=[IO.File]::ReadAllText((Join-Path $destination 'RailInspectionCar_V1.map'))
        $physical=0;$regions=[ordered]@{}
        foreach($region in @('RW_CCM','RW_IRAM1','RW_IRAM2','RETAINED_FAULT')) {
            $m=[regex]::Match($map,"Execution Region $region \([^\r\n]*Size: (0x[0-9a-f]+), Max: (0x[0-9a-f]+)")
            if(!$m.Success){throw "Missing $region"}
            $bytes=[Convert]::ToUInt32($m.Groups[1].Value,16);$physical+=$bytes
            $regions[$region]=[ordered]@{Bytes=$bytes;Free=([Convert]::ToUInt32($m.Groups[2].Value,16)-$bytes)}
        }
        if($physical-174876 -gt 4096){throw "Fixed RAM budget exceeded: $physical"}
        $results+=[ordered]@{Variant=$variant;Defines=$defines;SourceDigest=$source.Digest;Started=$started.ToString('o');Exit=$process.ExitCode;Errors=0;Warnings=0;PhysicalRam=$physical;Delta82d6a0c=($physical-174876);Regions=$regions;HexSha256=(Get-FileHash -LiteralPath (Join-Path $destination 'RailInspectionCar_V1.hex') -Algorithm SHA256).Hash;AxfSha256=(Get-FileHash -LiteralPath (Join-Path $destination 'RailInspectionCar_V1.axf') -Algorithm SHA256).Hash}
        $results | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $artifact 'Keil构建结果.json') -Encoding utf8
        Write-Output "PASS $variant 0 errors/warnings RAM=$physical delta=$($physical-174876)"
    }
} finally {
    if((Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash -ne $expected){throw 'Refusing to overwrite concurrent formal-project change'}
    [IO.File]::WriteAllBytes($project,$original)
    [IO.File]::WriteAllBytes($rte,$rteOriginal)
}
