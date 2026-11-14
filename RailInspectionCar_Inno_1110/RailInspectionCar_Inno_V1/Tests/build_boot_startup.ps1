param(
    [string]$Keil='D:/NewMDK5.36/UV4/UV4.exe',
    [string[]]$Variants=@('current','lcd-off','alloff','maintenance','pulse-back','all','legacy','source-current'),
    [string]$BaselineMap='',
    [string]$BaselineCommit='0080780',
    [string]$Artifacts='',
    [switch]$PlanOnly
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$mdk=Join-Path $root 'MDK-ARM'
$project=Join-Path $mdk 'RailInspectionCar_V1.uvprojx'
$rte=Join-Path $mdk 'RTE/_RailInspectionCar_V1/RTE_Components.h'
if(!$Artifacts){$Artifacts=Join-Path $root 'Docs/F407_BOOT_20261108/验证'}
$Artifacts=[IO.Path]::GetFullPath($Artifacts)
if(!$BaselineMap){$BaselineMap=Join-Path $Artifacts 'baseline/RailInspectionCar_V1.map'}
$switches=@('APP_CHASSIS_LCD_ENABLED','APP_DEBUG_CLI_ENABLED','APP_TUNE_UART_ENABLED','APP_SERVO_CLI_DEBUG_ENABLED','APP_SERVO_PULSE_ENABLED',
    'APP_HMI_LINK_DIAG_ENABLED','APP_NRF_DIAGNOSTIC_ENABLED','APP_CAN_SERVO_DIAG_ENABLED','APP_CAN_ARM_DIAG_ENABLED','APP_SERVO_RS485_DIAG_ENABLED','APP_CAN_DEBUG_ENABLED','APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED')
$profiles=@{
    'current'=@(1,0,0,0,0,1,0,0,0,1,0,0)
    'lcd-off'=@(0,0,0,0,0,1,0,0,0,1,0,0)
    'alloff'=@(1,0,0,0,0,0,0,0,0,0,0,0)
    'maintenance'=@(1,1,1,1,0,1,0,0,0,1,0,0)
    'pulse-back'=@(1,1,1,1,1,1,0,0,0,1,0,0)
    'all'=@(1,0,0,0,0,1,1,1,1,1,0,0)
    'legacy'=@(1,0,0,0,0,1,1,1,1,1,1,1)
    'source-current'=@(1,0,0,0,0,1,0,0,0,1,0,0)
}
foreach($variant in $Variants){if(!$profiles.ContainsKey($variant)){throw "Unknown variant: $variant"}}
if(@($Variants|Group-Object|Where-Object Count -gt 1).Count){throw 'Duplicate variant'}
$Variants=@($Variants|Where-Object {$_ -ne 'source-current'})+@('source-current')
$configurations=@(foreach($variant in $Variants){
    $values=$profiles[$variant];$defines=@('USE_HAL_DRIVER','STM32F407xx')
    if($variant -ne 'source-current'){for($i=0;$i -lt $switches.Count;$i++){$defines+=($switches[$i]+'='+$values[$i])}}
    [ordered]@{Variant=$variant;Defines=$defines;Values=$values;NoLcd=(!$values[0]);DebugCli=([bool]$values[1]);TuneUart=([bool]$values[2]);
        ServoCli=([bool]$values[3]);Pulse=([bool]$values[4]);NoUartRx=(!$values[1] -and !$values[2]);
        DiagnosticEnabled=(@($values[5..11]|Where-Object {$_ -ne 0}).Count -gt 0);UsesUnmodifiedSourceConfiguration=($variant -eq 'source-current')}
})
if($PlanOnly){[ordered]@{Project=$project;BaselineMap=$BaselineMap;BaselineCommit=$BaselineCommit;Artifacts=$Artifacts;Configurations=$configurations}|ConvertTo-Json -Depth 8;return}
if(!(Test-Path -LiteralPath $Keil -PathType Leaf)){throw "Keil missing: $Keil"}
$armcc=Join-Path (Split-Path (Split-Path $Keil -Parent) -Parent) 'ARM/ARMCC/bin/armcc.exe'
$fromelf=Join-Path (Split-Path $armcc -Parent) 'fromelf.exe'
if(!(Test-Path -LiteralPath $fromelf -PathType Leaf)){throw "Fromelf missing: $fromelf"}
function FileHash([string]$path){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
function ObjectMacroEvidence($config,[string]$destination,[string]$content,[datetime]$started){
    $directory=Join-Path $destination 'object-macro-evidence';New-Item -ItemType Directory -Path $directory -Force|Out-Null
    $pattern='^\s*[0-9a-fA-F]+:\s+line\s+\d+\s+(define|undef)\s+('+(($switches|ForEach-Object {[regex]::Escape($_)}) -join '|')+')\b(.*)$'
    $evidence=@(foreach($stem in @('aimtor_servo','app_rtos_create','debug_uart','app_hmi_link_diag')){
        if($content -notmatch ('(?m)^compiling '+[regex]::Escape($stem)+'\.c\.\.\.\s*$')){throw "Missing full-build compilation: $stem"}
        foreach($extension in @('o','d')){
            $path=Join-Path $mdk "RailInspectionCar_V1/$stem.$extension"
            if(!(Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).LastWriteTimeUtc -lt $started.ToUniversalTime().AddSeconds(-2)){throw "Missing/stale object evidence: $path"}
            Copy-Item -LiteralPath $path -Destination $directory -Force
        }
        $dependency=Join-Path $directory ($stem+'.d');$object=Join-Path $directory ($stem+'.o')
        if([IO.File]::ReadAllText($dependency) -notmatch '(?im):[^\r\n]*[\\/]APP[\\/]app_config\.h\s*$'){throw "Object does not depend on app_config.h: $stem"}
        $lines=@(& $fromelf --text -g $object 2>&1);if($LASTEXITCODE){throw "Fromelf failed: $stem"}
        $records=@();$selected=@()
        foreach($line in $lines){$match=[regex]::Match([string]$line,$pattern);if($match.Success){
            $selected+=[string]$line;$records+=,[ordered]@{Name=$match.Groups[2].Value;Directive=$match.Groups[1].Value;Expression=$match.Groups[3].Value.Trim()}
        }}
        if($records.Count -ne $switches.Count){throw "Wrong macro record count: $stem / $($records.Count)"}
        for($i=0;$i -lt $switches.Count;$i++){
            $record=@($records|Where-Object Name -eq $switches[$i])
            if($record.Count -ne 1 -or $record[0].Directive -ne 'define' -or ($record[0].Expression -replace '[()\sUu]','') -ne [string]$config.Values[$i]){throw "Actual object macro mismatch: $stem / $($switches[$i])"}
        }
        $trace=Join-Path $directory ($stem+'.macro-trace.txt');$selected|Set-Content -LiteralPath $trace -Encoding utf8
        [ordered]@{Object=($stem+'.o');ObjectSha256=(FileHash $object);DependencySha256=(FileHash $dependency);TraceSha256=(FileHash $trace);MacroRecords=$records;ActualValuesVerified=$true}
    })
    ConvertTo-Json -InputObject $evidence -Depth 8|Set-Content -LiteralPath (Join-Path $destination 'object-macro-evidence.json') -Encoding utf8
}
function ProductionDigest {
    $paths=@(& rg --files -g '*.c' -g '*.h' -g '*.s' -g '*.sct' APP BSP Core Device Protocol Drivers Middlewares)
    if($LASTEXITCODE){throw 'Production enumeration failed'}
    $paths+=@('MDK-ARM/startup_stm32f407xx.s','MDK-ARM/RailInspectionCar_retained.sct')
    $entries=@($paths|Sort-Object -Unique|ForEach-Object {[ordered]@{Path=$_;SHA256=(FileHash (Join-Path $root $_))}})
    $json=ConvertTo-Json -InputObject $entries -Depth 3 -Compress;$sha=[Security.Cryptography.SHA256]::Create()
    try{$digest=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($json)))).Replace('-','')}finally{$sha.Dispose()}
    [ordered]@{Files=$entries;Digest=$digest}
}
function RamSummary([string]$path){
    $text=[IO.File]::ReadAllText($path);$physical=0;$regions=[ordered]@{}
    foreach($name in @('RW_CCM','RW_IRAM1','RW_IRAM2','RETAINED_FAULT')){
        $m=[regex]::Match($text,"Execution Region $name \([^\r\n]*Size: (0x[0-9a-fA-F]+), Max: (0x[0-9a-fA-F]+)")
        if(!$m.Success){throw "Missing region $name in $path"}
        $bytes=[Convert]::ToUInt32($m.Groups[1].Value,16);$capacity=[Convert]::ToUInt32($m.Groups[2].Value,16)
        if($bytes -gt $capacity){throw "Region overflow: $name"}
        $physical+=$bytes;$regions[$name]=[ordered]@{Bytes=$bytes;Capacity=$capacity;Free=($capacity-$bytes)}
    }
    [ordered]@{PhysicalRam=$physical;Regions=$regions}
}
Push-Location $root
$original=[IO.File]::ReadAllBytes($project);$rteOriginal=[IO.File]::ReadAllBytes($rte)
$projectHash=FileHash $project;$rteHash=FileHash $rte;$expected=$projectHash
$projectText=[IO.File]::ReadAllText($project);$marker='<Define>USE_HAL_DRIVER,STM32F407xx</Define>'
if([regex]::Matches($projectText,[regex]::Escape($marker)).Count -ne 1){Pop-Location;throw 'Expected one formal target define marker'}
[xml]$projectXml=$projectText
if(@($projectXml.SelectNodes('//Define')|Where-Object {$_.InnerText}).Count -ne 1){Pop-Location;throw 'Hidden per-file/group macro overrides'}
foreach($node in $projectXml.SelectNodes('//Undefine | //MiscControls | //CustomArgument')){if($node.InnerText){Pop-Location;throw 'Hidden macro override/custom argument'}}
$configText=[IO.File]::ReadAllText((Join-Path $root 'APP/app_config.h'))
for($i=0;$i -lt $switches.Count;$i++){
    $m=[regex]::Match($configText,'(?m)^#define\s+'+[regex]::Escape($switches[$i])+'\s+\((\d+)U\)')
    if(!$m.Success -or [int]$m.Groups[1].Value -ne $profiles['source-current'][$i]){Pop-Location;throw "Source macro changed: $($switches[$i]); update explicit source-current profile before building"}
}
$baseline=RamSummary $BaselineMap;$baselineRef=(& git rev-parse --verify "$BaselineCommit^{commit}").Trim()
if($LASTEXITCODE){Pop-Location;throw 'Baseline commit unavailable'}
New-Item -ItemType Directory -Path $Artifacts -Force|Out-Null
$baselineInfo=[ordered]@{Commit=$baselineRef;Map=(Resolve-Path -LiteralPath $BaselineMap).Path;MapSha256=(FileHash $BaselineMap);Ram=$baseline;MaximumSameMacroGrowthBytes=256}
$baselineInfo|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $Artifacts 'baseline.json') -Encoding utf8
$source=ProductionDigest;$source|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Artifacts 'production-source-sha256.json') -Encoding utf8
$head=(& git rev-parse HEAD).Trim();$dirty=@(& git status --porcelain --untracked-files=no);$results=@();$completed=$false
try{
    foreach($config in $configurations){
        $variant=$config.Variant
        if((FileHash $project) -ne $expected){throw 'Concurrent formal project change'}
        if((ProductionDigest).Digest -ne $source.Digest){throw 'Concurrent production source change'}
        if($config.UsesUnmodifiedSourceConfiguration){[IO.File]::WriteAllBytes($project,$original)}
        else{[IO.File]::WriteAllText($project,$projectText.Replace($marker,('<Define>'+($config.Defines -join ',')+'</Define>')),[Text.UTF8Encoding]::new($false))}
        $expected=FileHash $project;$destination=Join-Path $Artifacts $variant;New-Item -ItemType Directory -Path $destination -Force|Out-Null
        Copy-Item -LiteralPath $project -Destination (Join-Path $destination 'project.uvprojx.snapshot') -Force
        $config|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $destination 'configuration.json') -Encoding utf8
        $log=Join-Path $env:TEMP ('rail_boot_'+$variant+'_'+[guid]::NewGuid().ToString('N')+'.log');$started=Get-Date
        Write-Output "START full Keil $variant"
        $process=Start-Process -FilePath $Keil -WorkingDirectory $mdk -WindowStyle Hidden -Wait -PassThru -ArgumentList @('-r','RailInspectionCar_V1.uvprojx','-t','RailInspectionCar_V1','-o',('"'+$log+'"'))
        if(Test-Path -LiteralPath $log){Copy-Item -LiteralPath $log -Destination (Join-Path $destination 'keil-rebuild.log') -Force}
        if((FileHash $project) -ne $expected){throw 'Formal project changed during build'}
        if(!(Test-Path -LiteralPath $log)){throw "Missing Keil log, exit=$($process.ExitCode)"}
        $content=[IO.File]::ReadAllText($log)
        if($process.ExitCode -ne 0 -or $content -notmatch '0 Error\(s\), 0 Warning\(s\)' -or $content -match 'Target not created'){
            Get-Content -LiteralPath $log -Tail 60;throw "Keil failed: $variant exit=$($process.ExitCode)"
        }
        foreach($extension in @('map','htm','hex','axf')){
            $path=Join-Path $mdk "RailInspectionCar_V1/RailInspectionCar_V1.$extension"
            if(!(Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).LastWriteTimeUtc -lt $started.ToUniversalTime().AddSeconds(-2)){throw "Missing/stale artifact: $path"}
            Copy-Item -LiteralPath $path -Destination $destination -Force
        }
        if((ProductionDigest).Digest -ne $source.Digest){throw 'Production changed during build'}
        ObjectMacroEvidence $config $destination $content $started
        $same=$variant -in @('current','source-current')
        $check=@{Map=(Join-Path $destination 'RailInspectionCar_V1.map');CallGraph=(Join-Path $destination 'RailInspectionCar_V1.htm');
            NoLcd=[bool]$config.NoLcd;NoUartRx=[bool]$config.NoUartRx;MinimalWorker=[bool]$config.NoUartRx;
            DebugCli=[bool]$config.DebugCli;TuneUart=[bool]$config.TuneUart;ServoCli=[bool]$config.ServoCli;Pulse=[bool]$config.Pulse;
            RequireHmiLinkDiag=[bool]$config.DiagnosticEnabled;BaselinePhysicalBytes=$(if($same){$baseline.PhysicalRam}else{175136});
            BaselineLabel=$(if($same){$BaselineCommit+'/same-macros'}else{'legacy-a2c49d3/alloff'});MaximumGrowthBytes=$(if($same){256}else{4096})}
        try{& (Join-Path $root 'Tests/check_tune_memory.ps1') @check *>&1|Tee-Object -FilePath (Join-Path $destination 'memory-stack.log')}
        catch{$_|Out-String|Add-Content -LiteralPath (Join-Path $destination 'memory-stack.log');throw}
        $ram=RamSummary (Join-Path $destination 'RailInspectionCar_V1.map');$hashes=[ordered]@{}
        foreach($file in @('RailInspectionCar_V1.hex','RailInspectionCar_V1.axf','RailInspectionCar_V1.map','RailInspectionCar_V1.htm','keil-rebuild.log','project.uvprojx.snapshot','configuration.json','memory-stack.log','object-macro-evidence.json')){$hashes[$file]=FileHash (Join-Path $destination $file)}
        $result=[ordered]@{Variant=$variant;Configuration=$config;Head=$head;TrackedChanges=$dirty;SourceDigest=$source.Digest;
            Started=$started.ToString('o');Completed=(Get-Date).ToString('o');Exit=$process.ExitCode;Errors=0;Warnings=0;PhysicalRam=$ram.PhysicalRam;
            SameMacroComparison=$same;DeltaSameMacroBaseline=$(if($same){$ram.PhysicalRam-$baseline.PhysicalRam}else{$null});ActualObjectMacroEvidence=$true;Ram=$ram;Hashes=$hashes;HardwareAcceptance='pending'}
        $result|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding utf8
        $results+=,$result;ConvertTo-Json -InputObject $results -Depth 12|Set-Content -LiteralPath (Join-Path $Artifacts 'Keil构建结果.json') -Encoding utf8
        Write-Output "PASS $variant 0errors/0warnings physical=$($ram.PhysicalRam)"
    }
    $completed=$true
}finally{
    if((FileHash $project) -ne $expected){Copy-Item -LiteralPath $project -Destination (Join-Path $Artifacts ('unexpected-project-'+[guid]::NewGuid().ToString('N')+'.uvprojx.snapshot')) -Force}
    [IO.File]::WriteAllBytes($project,$original);[IO.File]::WriteAllBytes($rte,$rteOriginal)
    $restore=[ordered]@{Completed=$completed;ProjectRestored=((FileHash $project) -eq $projectHash);RteRestored=((FileHash $rte) -eq $rteHash);
        ProductionUnchanged=((ProductionDigest).Digest -eq $source.Digest);ProjectSha256=$projectHash;RteSha256=$rteHash;LastSuccessfulVariant=$(if($results.Count){$results[-1].Variant}else{$null})}
    $restore|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Artifacts 'restore-status.json') -Encoding utf8
    Pop-Location
    if(!$restore.ProjectRestored -or !$restore.RteRestored -or !$restore.ProductionUnchanged){throw 'Build restoration/source verification failed; inspect restore-status.json'}
}
