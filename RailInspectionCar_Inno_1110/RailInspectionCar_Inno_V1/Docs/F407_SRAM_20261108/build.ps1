param(
    [string]$Keil='D:/NewMDK5.36/UV4/UV4.exe',
    [string[]]$Variants=@('compat','lcd-off','hmi','remote','canservo','canarm','rs485','all','lcd-off-all','maintenance','tune-only','pulse-back','legacy','default'),
    [string]$BaselineMap='', [string]$BaselineCommit='a2c49d3',
    [switch]$PlanOnly
)
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$mdk=Join-Path $root 'MDK-ARM'
$project=Join-Path $mdk 'RailInspectionCar_V1.uvprojx'
$rte=Join-Path $mdk 'RTE/_RailInspectionCar_V1/RTE_Components.h'
$artifact=Join-Path $PSScriptRoot '验证'
if (!$BaselineMap) { $BaselineMap=Join-Path $root 'Docs/F407_Link5_20261107/验证/alloff/RailInspectionCar_V1.map' }
$profileSwitches=@('APP_CHASSIS_LCD_ENABLED','APP_DEBUG_CLI_ENABLED','APP_TUNE_UART_ENABLED','APP_SERVO_CLI_DEBUG_ENABLED','APP_SERVO_PULSE_ENABLED')
$diagnosticSwitches=@('APP_HMI_LINK_DIAG_ENABLED','APP_NRF_DIAGNOSTIC_ENABLED','APP_CAN_SERVO_DIAG_ENABLED','APP_CAN_ARM_DIAG_ENABLED','APP_SERVO_RS485_DIAG_ENABLED','APP_CAN_DEBUG_ENABLED','APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED')
$routes=@('hmi','remote','canservo','canarm','rs485','raw','sync')
$profiles=@{
    'compat'=@(1,1,1,0,1); 'lcd-off'=@(0,0,0,0,0);
    'hmi'=@(1,0,0,0,0); 'remote'=@(1,0,0,0,0); 'canservo'=@(1,0,0,0,0);
    'canarm'=@(1,0,0,0,0); 'rs485'=@(1,0,0,0,0); 'all'=@(1,0,0,0,0);
    'lcd-off-all'=@(0,0,0,0,0); 'maintenance'=@(1,1,1,1,0);
    'tune-only'=@(1,0,1,0,0); 'pulse-back'=@(1,1,1,1,1);
    'legacy'=@(1,0,0,0,0); 'default'=@(1,0,0,0,0)
}
foreach ($variant in $Variants) { if (!$profiles.ContainsKey($variant)) { throw "Unknown variant: $variant" } }
if (@($Variants | Group-Object | Where-Object Count -gt 1).Count) { throw 'Duplicate build variant' }
# A successful run always leaves the formal output directory on the source-default profile.
$Variants=@($Variants | Where-Object { $_ -ne 'default' })+@('default')
$configurations=@(foreach ($variant in $Variants) {
    $profile=$profiles[$variant]
    $defines=@('USE_HAL_DRIVER','STM32F407xx')
    for ($i=0;$i -lt $profileSwitches.Count;$i++) { $defines+=($profileSwitches[$i]+'='+$profile[$i]) }
    $diagnostics=@(for ($i=0;$i -lt $diagnosticSwitches.Count;$i++) {
        [int](($variant -eq $routes[$i]) -or (($variant -eq 'all' -or $variant -eq 'lcd-off-all') -and $i -lt 5) -or $variant -eq 'legacy')
    })
    for ($i=0;$i -lt $diagnosticSwitches.Count;$i++) { $defines+=($diagnosticSwitches[$i]+'='+$diagnostics[$i]) }
    [ordered]@{Variant=$variant;Profile=$profile;Defines=$defines;DiagnosticEnabled=(@($diagnostics | Where-Object { $_ -eq 1 }).Count -ne 0);
        NoLcd=($profile[0] -eq 0);NoUartRx=(!$profile[1] -and !$profile[2]);MinimalWorker=(!$profile[1] -and !$profile[2]);
        DebugCli=($profile[1] -eq 1);TuneUart=($profile[2] -eq 1);ServoCli=($profile[3] -eq 1);Pulse=($profile[4] -eq 1)}
})
if ($PlanOnly) { $configurations | ConvertTo-Json -Depth 6; return }
if (!(Test-Path -LiteralPath $Keil -PathType Leaf)) { throw "Keil executable missing: $Keil" }
function FileHash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function ProductionDigest {
    $paths=@(& rg --files -g '*.c' -g '*.h' -g '*.s' -g '*.sct' APP BSP Core Device Protocol Drivers Middlewares)
    if ($LASTEXITCODE -ne 0) { throw 'Failed to enumerate production sources' }
    $paths+=@('MDK-ARM/startup_stm32f407xx.s','MDK-ARM/RailInspectionCar_retained.sct')
    $entries=@($paths | Sort-Object -Unique | ForEach-Object { [ordered]@{Path=$_;SHA256=(FileHash (Join-Path $root $_))} })
    $json=ConvertTo-Json -InputObject $entries -Depth 3 -Compress
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $digest=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($json)))).Replace('-','') }
    finally { $sha.Dispose() }
    [ordered]@{Files=$entries;Digest=$digest}
}
function RamSummary([string]$Path) {
    $mapText=[IO.File]::ReadAllText($Path)
    $physical=0; $regions=[ordered]@{}
    foreach ($region in @('RW_CCM','RW_IRAM1','RW_IRAM2','RETAINED_FAULT')) {
        $m=[regex]::Match($mapText,"Execution Region $region \([^\r\n]*Size: (0x[0-9a-fA-F]+), Max: (0x[0-9a-fA-F]+)")
        if (!$m.Success) { throw "Missing memory region $region in $Path" }
        $bytes=[Convert]::ToUInt32($m.Groups[1].Value,16); $capacity=[Convert]::ToUInt32($m.Groups[2].Value,16)
        $physical+=$bytes; $regions[$region]=[ordered]@{Bytes=$bytes;Capacity=$capacity;Free=($capacity-$bytes)}
    }
    [ordered]@{PhysicalRam=$physical;CcmRam=$regions.RW_CCM.Bytes;OrdinaryRam=($physical-$regions.RW_CCM.Bytes);Regions=$regions}
}
Push-Location $root
$original=[IO.File]::ReadAllBytes($project); $rteOriginal=[IO.File]::ReadAllBytes($rte)
$originalHash=FileHash $project; $rteOriginalHash=FileHash $rte; $expected=$originalHash
$projectText=[IO.File]::ReadAllText($project)
$defineMarker='<Define>USE_HAL_DRIVER,STM32F407xx</Define>'
if ([regex]::Matches($projectText,[regex]::Escape($defineMarker)).Count -ne 1) { Pop-Location; throw 'Expected exactly one formal target C define marker' }
$baseline=RamSummary $BaselineMap
$baselineRef=(& git rev-parse --verify "$BaselineCommit^{commit}").Trim()
if ($LASTEXITCODE -ne 0) { Pop-Location; throw 'Baseline Git reference unavailable' }
$baselineInfo=[ordered]@{Commit=$baselineRef;Map=(Resolve-Path -LiteralPath $BaselineMap).Path;MapSha256=(FileHash $BaselineMap);Ram=$baseline;
    UserReferencePhysicalRam=175136;UserReferenceCcmRam=62024;HeapStaticBytes=49152;HeapSavingsAreNotAddedToLinkerSavings=$true}
$baselineResults=Join-Path $root 'Docs/F407_Link5_20261107/验证/Keil构建结果.json'
if (Test-Path -LiteralPath $baselineResults) {
    $entry=@((Get-Content -LiteralPath $baselineResults -Raw | ConvertFrom-Json) | Where-Object Variant -eq 'alloff')
    if ($entry.Count -eq 1) {
        if ([int]$entry[0].PhysicalRam -ne $baseline.PhysicalRam) { Pop-Location; throw 'Archived baseline metadata and actual map disagree' }
        $baselineInfo.ArchivedSourceDigest=$entry[0].SourceDigest
    }
}
New-Item -ItemType Directory -Path $artifact -Force | Out-Null
$source=ProductionDigest
$source | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $artifact 'production-source-sha256.json') -Encoding utf8
$baselineInfo | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $artifact 'baseline.json') -Encoding utf8
$head=(& git rev-parse HEAD).Trim(); $dirty=@(& git status --porcelain --untracked-files=no)
$results=@(); $completed=$false
try {
    foreach ($config in $configurations) {
        $variant=$config.Variant
        if ((FileHash $project) -ne $expected) { throw 'Formal project changed concurrently before build' }
        if ((ProductionDigest).Digest -ne $source.Digest) { throw 'Production source changed before build' }
        $destination=Join-Path $artifact $variant
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        $replacement='<Define>'+($config.Defines -join ',')+'</Define>'
        [IO.File]::WriteAllText($project,$projectText.Replace($defineMarker,$replacement),[Text.UTF8Encoding]::new($false))
        $expected=FileHash $project
        Copy-Item -LiteralPath $project -Destination (Join-Path $destination 'project.uvprojx.snapshot') -Force
        $config | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $destination 'configuration.json') -Encoding utf8
        $log=Join-Path $env:TEMP ('rail_sram_'+$variant+'_'+[guid]::NewGuid().ToString('N')+'.log')
        $started=Get-Date
        Write-Output "START full Keil $variant"
        $process=Start-Process -FilePath $Keil -WorkingDirectory $mdk -WindowStyle Hidden -Wait -PassThru -ArgumentList @('-r','RailInspectionCar_V1.uvprojx','-t','RailInspectionCar_V1','-o',('"'+$log+'"'))
        if (Test-Path -LiteralPath $log) { Copy-Item -LiteralPath $log -Destination (Join-Path $destination 'keil-rebuild.log') -Force }
        if ((FileHash $project) -ne $expected) { throw 'Formal project changed during build' }
        if (!(Test-Path -LiteralPath $log)) { throw "No Keil log; exit=$($process.ExitCode)" }
        $content=[IO.File]::ReadAllText($log)
        if ($process.ExitCode -ne 0 -or $content -notmatch '0 Error\(s\), 0 Warning\(s\)' -or $content -match 'Target not created') {
            Get-Content -LiteralPath $log | Select-Object -Last 60
            throw "Keil failed $variant exit=$($process.ExitCode)"
        }
        foreach ($extension in @('map','htm','hex','axf')) {
            $path=Join-Path $mdk "RailInspectionCar_V1/RailInspectionCar_V1.$extension"
            if (!(Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).LastWriteTimeUtc -lt $started.ToUniversalTime().AddSeconds(-2)) { throw "Missing/stale full-build artifact: $path" }
            Copy-Item -LiteralPath $path -Destination $destination -Force
        }
        if ((ProductionDigest).Digest -ne $source.Digest) { throw 'Production source changed during build' }
        $check=@{Map=(Join-Path $destination 'RailInspectionCar_V1.map');CallGraph=(Join-Path $destination 'RailInspectionCar_V1.htm');
            NoLcd=[bool]$config.NoLcd;NoUartRx=[bool]$config.NoUartRx;MinimalWorker=[bool]$config.MinimalWorker;
            DebugCli=[bool]$config.DebugCli;TuneUart=[bool]$config.TuneUart;ServoCli=[bool]$config.ServoCli;Pulse=[bool]$config.Pulse;
            RequireHmiLinkDiag=[bool]$config.DiagnosticEnabled;BaselinePhysicalBytes=$baseline.PhysicalRam;BaselineLabel=$BaselineCommit}
        try {
            & (Join-Path $root 'Tests/check_tune_memory.ps1') @check *>&1 |
                Tee-Object -FilePath (Join-Path $destination 'memory-stack.log')
        } catch {
            $_ | Out-String | Add-Content -LiteralPath (Join-Path $destination 'memory-stack.log')
            throw
        }
        $ram=RamSummary (Join-Path $destination 'RailInspectionCar_V1.map')
        $hashes=[ordered]@{}
        foreach ($file in @('RailInspectionCar_V1.hex','RailInspectionCar_V1.axf','RailInspectionCar_V1.map','RailInspectionCar_V1.htm','keil-rebuild.log','project.uvprojx.snapshot','configuration.json','memory-stack.log')) {
            $hashes[$file]=FileHash (Join-Path $destination $file)
        }
        $result=[ordered]@{Variant=$variant;Configuration=$config;Head=$head;TrackedWorktreeChanges=$dirty;SourceDigest=$source.Digest;
            Started=$started.ToString('o');Completed=(Get-Date).ToString('o');Exit=$process.ExitCode;Errors=0;Warnings=0;
            PhysicalRam=$ram.PhysicalRam;DeltaBaselinePhysical=($ram.PhysicalRam-$baseline.PhysicalRam);DeltaUserReferencePhysical=($ram.PhysicalRam-175136);
            DeltaBaselineCcm=($ram.CcmRam-$baseline.CcmRam);Ram=$ram;Hashes=$hashes;HardwareAcceptance='pending'}
        $result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding utf8
        $results+=,$result
        ConvertTo-Json -InputObject $results -Depth 12 | Set-Content -LiteralPath (Join-Path $artifact 'Keil构建结果.json') -Encoding utf8
        Write-Output "PASS $variant 0errors/0warnings physical=$($ram.PhysicalRam) delta-$BaselineCommit=$($ram.PhysicalRam-$baseline.PhysicalRam)"
    }
    $completed=$true
} finally {
    # Preserve unexpected project bytes for review before restoring this run's
    # byte-for-byte baseline. RTE is generated by Keil and restored as well.
    if ((FileHash $project) -ne $expected) {
        Copy-Item -LiteralPath $project -Destination (Join-Path $artifact ('unexpected-project-'+[guid]::NewGuid().ToString('N')+'.uvprojx')) -Force
    }
    [IO.File]::WriteAllBytes($project,$original)
    [IO.File]::WriteAllBytes($rte,$rteOriginal)
    $restore=[ordered]@{Completed=$completed;ProjectRestored=((FileHash $project) -eq $originalHash);RteRestored=((FileHash $rte) -eq $rteOriginalHash);
        ProductionUnchanged=((ProductionDigest).Digest -eq $source.Digest);ProjectSha256=$originalHash;RteSha256=$rteOriginalHash;LastSuccessfulVariant=$(if ($results.Count) { $results[-1].Variant } else { $null })}
    $restore | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $artifact 'restore-status.json') -Encoding utf8
    Pop-Location
    if (!$restore.ProjectRestored -or !$restore.RteRestored -or !$restore.ProductionUnchanged) { throw 'Build restoration or immutable production source verification failed; see restore-status.json' }
}
