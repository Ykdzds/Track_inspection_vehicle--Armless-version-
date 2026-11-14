param(
    [string]$Keil='D:/NewMDK5.36/UV4/UV4.exe',
    [string]$Armcc='',
    [switch]$PlanOnly
)
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$mdk=Join-Path $root 'MDK-ARM'
$project=Join-Path $mdk 'RailInspectionCar_V1.uvprojx'
$rte=Join-Path $mdk 'RTE/_RailInspectionCar_V1/RTE_Components.h'
$artifact=Join-Path $PSScriptRoot '验证'
$destination=Join-Path $artifact 'source-default'
if (!$Armcc) { $Armcc=Join-Path (Split-Path (Split-Path $Keil -Parent) -Parent) 'ARM/ARMCC/bin/armcc.exe' }
$fromelf=Join-Path (Split-Path $Armcc -Parent) 'fromelf.exe'
$expectedVariants=@('compat','lcd-off','hmi','remote','canservo','canarm','rs485','all','lcd-off-all','maintenance','tune-only','pulse-back','legacy','default')
$defaults=[ordered]@{
    APP_CHASSIS_LCD_ENABLED=1;APP_DEBUG_CLI_ENABLED=0;APP_TUNE_UART_ENABLED=0;
    APP_SERVO_CLI_DEBUG_ENABLED=0;APP_SERVO_PULSE_ENABLED=0;
    APP_HMI_LINK_DIAG_ENABLED=0;APP_NRF_DIAGNOSTIC_ENABLED=0;
    APP_CAN_SERVO_DIAG_ENABLED=0;APP_CAN_ARM_DIAG_ENABLED=0;
    APP_SERVO_RS485_DIAG_ENABLED=0;APP_CAN_DEBUG_ENABLED=0;
    APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED=0;APP_UART_RX_ACTIVE=0;APP_CLI_SERVO_ACTIVE=0
}
if ($PlanOnly) {
    [ordered]@{Variant='source-default';Project=$project;ProjectIsNeverWritten=$true;
        RequiresCompletedMatrix=$expectedVariants;RequiresRestoredProjectAndRte=$true;
        VerifiesMatrixSourceDigest=$true;Defaults=$defaults;Output=$destination;
        KeilArguments=@('-r','RailInspectionCar_V1.uvprojx','-t','RailInspectionCar_V1');
        ArmccPreprocessing=$Armcc;ActualObjectMacroEvidence=$true;Fromelf=$fromelf;
        ActualCompilerCommandsVerified=0;ActualCompilerCommandsAvailability='unavailable';
        MatrixMetadataIsReadOnly=$true;HardwareAcceptance='pending'} | ConvertTo-Json -Depth 6
    return
}
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
function Assert-FormalProject {
    if ((FileHash $project) -ne $restore.ProjectSha256) { throw 'Formal project is not the byte-for-byte restored original' }
    if ((FileHash $rte) -ne $restore.RteSha256) { throw 'RTE is not the byte-for-byte restored original' }
    if ((ProductionDigest).Digest -ne $source.Digest) { throw 'Production source differs from the completed matrix' }
}
if (!(Test-Path -LiteralPath $Keil -PathType Leaf)) { throw "Keil executable missing: $Keil" }
if (!(Test-Path -LiteralPath $Armcc -PathType Leaf)) { throw "ARMCC executable missing: $Armcc" }
if (!(Test-Path -LiteralPath $fromelf -PathType Leaf)) { throw "Fromelf executable missing: $fromelf" }
Push-Location $root
try {
    $metadata=[ordered]@{}
    foreach ($name in @('restore-status.json','production-source-sha256.json','Keil构建结果.json','baseline.json')) {
        $path=Join-Path $artifact $name
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required matrix metadata missing: $path" }
        $metadata[$name]=FileHash $path
    }
    $restore=Get-Content -LiteralPath (Join-Path $artifact 'restore-status.json') -Raw | ConvertFrom-Json
    if (!$restore.Completed -or !$restore.ProjectRestored -or !$restore.RteRestored -or
        !$restore.ProductionUnchanged -or $restore.LastSuccessfulVariant -ne 'default') {
        throw 'The 14-configuration matrix has not completed and restored the original project/RTE/source'
    }
    $source=Get-Content -LiteralPath (Join-Path $artifact 'production-source-sha256.json') -Raw | ConvertFrom-Json
    $matrix=@(Get-Content -LiteralPath (Join-Path $artifact 'Keil构建结果.json') -Raw | ConvertFrom-Json)
    if ($matrix.Count -ne 14 -or @($matrix | Group-Object Variant | Where-Object Count -ne 1).Count -or
        @($matrix | Where-Object { $_.Variant -notin $expectedVariants }).Count -or
        $matrix[-1].Variant -ne 'default') { throw 'Expected the complete original 14-configuration matrix with default last' }
    foreach ($item in $matrix) {
        if ($item.Exit -ne 0 -or $item.Errors -ne 0 -or $item.Warnings -ne 0 -or $item.SourceDigest -ne $source.Digest) {
            throw "Matrix configuration failed or source digest differs: $($item.Variant)"
        }
        $manifest=Get-Content -LiteralPath (Join-Path (Join-Path $artifact $item.Variant) 'manifest.json') -Raw | ConvertFrom-Json
        if ($manifest.Variant -ne $item.Variant -or $manifest.SourceDigest -ne $source.Digest -or
            $manifest.Exit -ne 0 -or $manifest.Errors -ne 0 -or $manifest.Warnings -ne 0) {
            throw "Matrix manifest does not confirm success: $($item.Variant)"
        }
    }
    $baseline=Get-Content -LiteralPath (Join-Path $artifact 'baseline.json') -Raw | ConvertFrom-Json
    Assert-FormalProject
    [xml]$xml=[IO.File]::ReadAllText($project)
    $targets=@($xml.Project.Targets.Target)
    if ($targets.Count -ne 1 -or $targets[0].TargetName -ne 'RailInspectionCar_V1') { throw 'Unexpected formal target layout' }
    $cads=$targets[0].TargetOption.TargetArmAds.Cads.VariousControls
    if ([string]$cads.Define -ne 'USE_HAL_DRIVER,STM32F407xx') { throw 'Formal target has extra C macro definitions' }
    $nonemptyDefines=@($xml.SelectNodes('//Define') | Where-Object { $_.InnerText })
    if ($nonemptyDefines.Count -ne 1 -or $nonemptyDefines[0].InnerText -ne 'USE_HAL_DRIVER,STM32F407xx') { throw 'Per-file/group/assembler macro overrides remain' }
    foreach ($node in $xml.SelectNodes('//Undefine | //MiscControls | //CustomArgument')) {
        if ($node.InnerText) { throw "Hidden macro override or custom argument remains: $($node.InnerText)" }
    }
    if (Test-Path -LiteralPath (Join-Path $destination 'manifest.json')) {
        throw 'source-default already has a manifest; preserve/rename that archive before an explicit rerun'
    }
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Copy-Item -LiteralPath $project -Destination (Join-Path $destination 'project.uvprojx.snapshot') -Force
    Copy-Item -LiteralPath $rte -Destination (Join-Path $destination 'RTE_Components.h.snapshot') -Force
    $config=[ordered]@{Variant='source-default';Profile=@(1,0,0,0,0);Defaults=$defaults;
        Defines=@('USE_HAL_DRIVER','STM32F407xx');DiagnosticEnabled=$false;NoLcd=$false;
        NoUartRx=$true;MinimalWorker=$true;DebugCli=$false;TuneUart=$false;ServoCli=$false;Pulse=$false;
        ProjectWritten=$false;UsesSourceDefaults=$true;MatrixSourceDigest=$source.Digest;
        EvidenceKind='project-preprocess-fresh-object-dwarf-macros-dependencies';ActualObjectMacroEvidence=$true;
        ActualCompilerCommandsVerified=0;ActualCompilerCommandsAvailability='unavailable'}
    $config | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $destination 'configuration.json') -Encoding utf8
    $metadata | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $destination 'matrix-metadata-sha256.json') -Encoding utf8
    $switchPattern='(?m)^\s*#\s*(define|undef)\s+('+((@($defaults.Keys) | ForEach-Object { [regex]::Escape($_) }) -join '|')+')\b([^\r\n]*)'
    $sites=@(foreach ($file in $source.Files) {
        if ($file.Path -notmatch '\.[ch]$') { continue }
        $fileText=[IO.File]::ReadAllText((Join-Path $root $file.Path))
        foreach ($match in [regex]::Matches($fileText,$switchPattern)) {
            [ordered]@{Name=$match.Groups[2].Value;Directive=$match.Groups[1].Value;
                Path=$file.Path;Expression=$match.Groups[3].Value.Trim()}
        }
    })
    foreach ($name in $defaults.Keys) {
        $definition=@($sites | Where-Object Name -eq $name)
        if ($definition.Count -ne 1 -or $definition[0].Directive -ne 'define' -or ($definition[0].Path -replace '\\','/') -ne 'APP/app_config.h') {
            throw "Local translation-unit profile override or missing source default: $name"
        }
    }
    $sites | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $destination 'source-switch-definition-sites.json') -Encoding utf8

    # Real ARMCC preprocessing verifies the #ifndef defaults without defining any APP_* switch.
    $probe=Join-Path $destination 'source-default-probe.c'
    $probeLines=@('#include "app_config.h"')
    foreach ($name in $defaults.Keys) {
        $probeLines+=@("#if $name != $($defaults[$name])","#error Source default mismatch: $name",'#endif')
    }
    $probeLines+=@('#define SOURCE_DEFAULT_STRING_(x) #x','#define SOURCE_DEFAULT_STRING(x) SOURCE_DEFAULT_STRING_(x)')
    foreach ($name in $defaults.Keys) { $probeLines+="const char source_default_$name[] = SOURCE_DEFAULT_STRING($name);" }
    [IO.File]::WriteAllLines($probe,$probeLines,[Text.UTF8Encoding]::new($false))
    $preprocessArgs=@('--c99','-E','--cpu','Cortex-M4.fp.sp','-D__MICROLIB','-DUSE_HAL_DRIVER','-DSTM32F407xx','-I',(Join-Path $root 'APP'),$probe)
    $preprocessArgs | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destination 'preprocess-arguments.json') -Encoding utf8
    $preprocessed=@(& $Armcc @preprocessArgs 2>&1)
    $preprocessExit=$LASTEXITCODE
    $preprocessed | Set-Content -LiteralPath (Join-Path $destination 'source-default-probe.i') -Encoding utf8
    if ($preprocessExit -ne 0 -or ($preprocessed -join "`n") -match '(?m)^.*(?:Error:|#error)') { throw "ARMCC default macro verification failed: exit=$preprocessExit" }
    Assert-FormalProject
    $head=(& git rev-parse HEAD).Trim(); $dirty=@(& git status --porcelain --untracked-files=no)
    $started=Get-Date
    $log=Join-Path $env:TEMP ('rail_sram_source_default_'+[guid]::NewGuid().ToString('N')+'.log')
    Write-Output 'START full Keil source-default, restored formal project is not modified'
    $process=Start-Process -FilePath $Keil -WorkingDirectory $mdk -WindowStyle Hidden -Wait -PassThru -ArgumentList @('-r','RailInspectionCar_V1.uvprojx','-t','RailInspectionCar_V1','-o',('"'+$log+'"'))
    if (Test-Path -LiteralPath $log) { Copy-Item -LiteralPath $log -Destination (Join-Path $destination 'keil-rebuild.log') -Force }
    if (!(Test-Path -LiteralPath $log)) { throw "No Keil log; exit=$($process.ExitCode)" }
    $content=[IO.File]::ReadAllText($log)
    if ($process.ExitCode -ne 0 -or $content -notmatch '0 Error\(s\), 0 Warning\(s\)' -or $content -match 'Target not created') {
        Get-Content -LiteralPath $log | Select-Object -Last 60
        throw "Keil source-default failed: exit=$($process.ExitCode)"
    }
    Assert-FormalProject
    $output=Join-Path $mdk 'RailInspectionCar_V1'
    foreach ($extension in @('map','htm','hex','axf')) {
        $path=Join-Path $output "RailInspectionCar_V1.$extension"
        if (!(Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).LastWriteTimeUtc -lt $started.ToUniversalTime().AddSeconds(-2)) { throw "Missing/stale full-build artifact: $path" }
        Copy-Item -LiteralPath $path -Destination $destination -Force
    }
    # CreateBatchFile=0 does not emit .__i commands. Read the actual object DWARF
    # macro records instead; only selected records are archived, never tool/license text.
    $objectDirectory=Join-Path $destination 'object-macro-evidence'
    New-Item -ItemType Directory -Path $objectDirectory -Force | Out-Null
    $derived=@{APP_UART_RX_ACTIVE='(APP_DEBUG_CLI_ENABLED || APP_TUNE_UART_ENABLED)';
        APP_CLI_SERVO_ACTIVE='(APP_DEBUG_CLI_ENABLED && APP_SERVO_CLI_DEBUG_ENABLED)'}
    $macroPattern='^\s*[0-9a-fA-F]+:\s+line\s+\d+\s+(define|undef)\s+('+((@($defaults.Keys) | ForEach-Object { [regex]::Escape($_) }) -join '|')+')\b(.*)$'
    $objectEvidence=@(foreach ($stem in @('app_rtos_create','debug_uart','app_hmi_link_diag')) {
        if ($content -notmatch ('(?m)^compiling '+[regex]::Escape($stem)+'\.c\.\.\.\s*$')) { throw "Full rebuild compilation missing: $stem.c" }
        foreach ($extension in @('o','d')) {
            $path=Join-Path $output ($stem+'.'+$extension)
            if (!(Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).LastWriteTimeUtc -lt $started.ToUniversalTime().AddSeconds(-2)) { throw "Missing/stale object evidence: $path" }
            Copy-Item -LiteralPath $path -Destination $objectDirectory -Force
        }
        $dependency=Join-Path $objectDirectory ($stem+'.d')
        if ([IO.File]::ReadAllText($dependency) -notmatch '(?im):[^\r\n]*[\\/]APP[\\/]app_config\.h\s*$') { throw "Object does not depend on app_config.h: $stem" }
        $object=Join-Path $objectDirectory ($stem+'.o')
        $debugLines=@(& $fromelf --text -g $object 2>&1)
        if ($LASTEXITCODE -ne 0) { throw "Fromelf failed: $stem" }
        $selectedLines=@();$records=@()
        foreach ($line in $debugLines) {
            $match=[regex]::Match([string]$line,$macroPattern)
            if (!$match.Success) { continue }
            $selectedLines+=[string]$line
            $records+=,[ordered]@{Name=$match.Groups[2].Value;Directive=$match.Groups[1].Value;Expression=$match.Groups[3].Value.Trim()}
        }
        if ($records.Count -ne $defaults.Count) { throw "Expected exactly 14 object macro records: $stem" }
        foreach ($name in $defaults.Keys) {
            $record=@($records | Where-Object Name -eq $name)
            if ($record.Count -ne 1 -or $record[0].Directive -ne 'define') { throw "Object macro missing/redefined: $stem / $name" }
            $expression=$record[0].Expression
            if ($derived.ContainsKey($name)) {
                if (($expression -replace '\s','') -ne ($derived[$name] -replace '\s','')) { throw "Object derived macro differs: $stem / $name" }
            } elseif (($expression -replace '[()\s]','') -ne ([string]$defaults[$name]+'U')) { throw "Object source-default macro differs: $stem / $name = $expression" }
        }
        $trace=Join-Path $objectDirectory ($stem+'.macro-trace.txt')
        $selectedLines | Set-Content -LiteralPath $trace -Encoding utf8
        [ordered]@{Object=($stem+'.o');ObjectSha256=(FileHash $object);
            Dependency=($stem+'.d');DependencySha256=(FileHash $dependency);
            Trace=($stem+'.macro-trace.txt');TraceSha256=(FileHash $trace);
            DependsOnAppConfig=$true;FreshObjectAndDependency=$true;RebuildCompilationRecorded=$true;
            MacroRecords=$records;ActualObjectMacroEvidence=$true;DefaultValuesVerified=$true}
    })
    $objectEvidence | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $destination 'object-macro-evidence.json') -Encoding utf8
    $check=@{Map=(Join-Path $destination 'RailInspectionCar_V1.map');CallGraph=(Join-Path $destination 'RailInspectionCar_V1.htm');
        NoLcd=$false;NoUartRx=$true;MinimalWorker=$true;DebugCli=$false;TuneUart=$false;ServoCli=$false;Pulse=$false;
        RequireHmiLinkDiag=$false;BaselinePhysicalBytes=[int]$baseline.Ram.PhysicalRam;BaselineLabel=[string]$baseline.Commit}
    try {
        & (Join-Path $root 'Tests/check_tune_memory.ps1') @check *>&1 | Tee-Object -FilePath (Join-Path $destination 'memory-stack.log')
    } catch {
        $_ | Out-String | Add-Content -LiteralPath (Join-Path $destination 'memory-stack.log')
        throw
    }
    Assert-FormalProject
    foreach ($name in $metadata.Keys) {
        if ((FileHash (Join-Path $artifact $name)) -ne $metadata[$name]) { throw "Original matrix metadata changed: $name" }
    }
    $hashes=[ordered]@{}
    foreach ($file in @('RailInspectionCar_V1.hex','RailInspectionCar_V1.axf','RailInspectionCar_V1.map','RailInspectionCar_V1.htm','keil-rebuild.log','project.uvprojx.snapshot','RTE_Components.h.snapshot','configuration.json','memory-stack.log','matrix-metadata-sha256.json','source-default-probe.c','source-default-probe.i','preprocess-arguments.json','object-macro-evidence.json','source-switch-definition-sites.json')) {
        $hashes[$file]=FileHash (Join-Path $destination $file)
    }
    foreach ($file in Get-ChildItem -LiteralPath $objectDirectory -File) { $hashes['object-macro-evidence/'+$file.Name]=FileHash $file.FullName }
    $formalHashes=[ordered]@{}
    foreach ($extension in @('map','htm','hex','axf')) {
        $name="RailInspectionCar_V1.$extension"
        $formalHashes[$name]=FileHash (Join-Path $output $name)
        if ($formalHashes[$name] -ne $hashes[$name]) { throw "Formal MDK output differs from source-default archive: $name" }
    }
    $ram=RamSummary (Join-Path $destination 'RailInspectionCar_V1.map')
    $result=[ordered]@{Variant='source-default';Configuration=$config;Head=$head;TrackedWorktreeChanges=$dirty;
        SourceDigest=$source.Digest;MatrixSourceDigest=$source.Digest;MatrixConfigurationsVerified=14;
        Started=$started.ToString('o');Completed=(Get-Date).ToString('o');Exit=$process.ExitCode;Errors=0;Warnings=0;
        PhysicalRam=$ram.PhysicalRam;DeltaBaselinePhysical=($ram.PhysicalRam-$baseline.Ram.PhysicalRam);
        DeltaUserReferencePhysical=($ram.PhysicalRam-175136);DeltaBaselineCcm=($ram.CcmRam-$baseline.Ram.CcmRam);
        Ram=$ram;Hashes=$hashes;ProjectNeverWritten=$true;ProjectSha256=$restore.ProjectSha256;RteSha256=$restore.RteSha256;
        ProductionUnchanged=$true;OriginalMatrixMetadataUnchanged=$true;SourceDefaultsPreprocessed=$true;
        EvidenceKind=$config.EvidenceKind;ActualObjectMacroEvidence=$true;ActualObjectMacroEvidenceCount=$objectEvidence.Count;
        ActualCompilerCommandsVerified=0;ActualCompilerCommandsAvailability='unavailable';
        FormalOutputsMatchArchive=$true;FormalOutputHashes=$formalHashes;
        HardwareAcceptance='pending'}
    $result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding utf8
    Write-Output "PASS source-default 0errors/0warnings physical=$($ram.PhysicalRam), original project/RTE/source and matrix metadata unchanged, formal MDK outputs match archive"
} finally {
    # This script never rewrites the project or the original matrix metadata.
    Pop-Location
}
