param(
    [string]$Keil = 'D:\NewMDK5.36\UV4\UV4.exe',
    [string]$Artifacts = ''
)
# Run only after production sources are frozen. Both variants build the exact
# formal project. The final on-disk firmware is the macro=1 full rebuild.
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$mdk = Join-Path $root 'MDK-ARM'
$project = Join-Path $mdk 'RailInspectionCar_V1.uvprojx'
$rte = Join-Path $mdk 'RTE/_RailInspectionCar_V1/RTE_Components.h'
$tests = Join-Path $root 'Tests'
$targetName = 'RailInspectionCar_V1'
if (!$Artifacts) { $Artifacts = Join-Path $PSScriptRoot '验证' }
$Artifacts = [System.IO.Path]::GetFullPath($Artifacts)
New-Item -ItemType Directory -Path $Artifacts -Force | Out-Null
if (!(Test-Path -LiteralPath $Keil -PathType Leaf)) { throw "Keil missing: $Keil" }
$projectBytes = [System.IO.File]::ReadAllBytes($project)
$projectHash = (Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash
$rteBytes = [System.IO.File]::ReadAllBytes($rte)
$rteHash = (Get-FileHash -LiteralPath $rte -Algorithm SHA256).Hash
$expectedProjectHash = $projectHash
$token = [guid]::NewGuid().ToString('N').Substring(0,8)
$results = @()

function Get-ProductionHashes {
    $files = @()
    foreach ($folder in @('APP','BSP','Core','Device','Drivers','Middlewares','Protocol')) {
        $directory = Join-Path $root $folder
        if (Test-Path -LiteralPath $directory) {
            $files += Get-ChildItem -LiteralPath $directory -Recurse -File |
                Where-Object { $_.Extension -in @('.c','.h','.s') }
        }
    }
    $files += Get-Item -LiteralPath (Join-Path $mdk 'startup_stm32f407xx.s')
    @($files | Sort-Object FullName | ForEach-Object {
        [ordered]@{Path=$_.FullName;Sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
    })
}
$sourceHashes = @(Get-ProductionHashes)
if ($sourceHashes.Count -lt 286) { throw "Incomplete production source inventory: $($sourceHashes.Count), expected at least 286." }

function Assert-ProjectExpected {
    if ((Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash -ne $script:expectedProjectHash) {
        throw 'Formal uvprojx changed concurrently; refusing to overwrite another editor.'
    }
}
function Assert-SourcesUnchanged {
    $current = @(Get-ProductionHashes)
    if ($current.Count -ne $sourceHashes.Count) { throw 'Production source inventory changed during build; repeat after sources are frozen.' }
    for ($index = 0; $index -lt $sourceHashes.Count; $index++) {
        if ($current[$index].Path -ne $sourceHashes[$index].Path -or
            $current[$index].Sha256 -ne $sourceHashes[$index].Sha256) {
            throw "Production source changed during build: $($current[$index].Path); repeat verification."
        }
    }
}
function Restore-FormalProject {
    Assert-ProjectExpected
    [System.IO.File]::WriteAllBytes($project,$projectBytes)
    $script:expectedProjectHash = $projectHash
    Assert-ProjectExpected
}
function Restore-Rte {
    [System.IO.File]::WriteAllBytes($rte,$rteBytes)
    if ((Get-FileHash -LiteralPath $rte -Algorithm SHA256).Hash -ne $rteHash) { throw 'RTE original-byte restoration failed.' }
}
function Read-MemorySummary([string]$Map, [string[]]$CheckLines) {
    $mapText = Get-Content -LiteralPath $Map -Raw
    $regions = [ordered]@{}
    foreach ($name in @('RW_CCM','RW_IRAM1','RW_IRAM2','RETAINED_FAULT')) {
        $match = [regex]::Match($mapText,"Execution Region $name \(Exec base: (0x[0-9a-f]+), Load base: (0x[0-9a-f]+), Size: (0x[0-9a-f]+), Max: (0x[0-9a-f]+)")
        if (!$match.Success) { throw "Missing region $name" }
        $size = [Convert]::ToUInt32($match.Groups[3].Value,16)
        $capacity = [Convert]::ToUInt32($match.Groups[4].Value,16)
        $regions[$name] = [ordered]@{Base=$match.Groups[1].Value;Bytes=$size;Capacity=$capacity;FreeBytes=($capacity-$size)}
    }
    $physical = 0
    foreach ($value in $regions.Values) { $physical += $value.Bytes }
    if ($physical - 174876 -gt 4096) { throw 'Fixed physical RAM growth exceeds the 4096 B delivery budget.' }
    $module = [regex]::Match($mapText,'(?m)^\s*\d+\s+\d+\s+\d+\s+(\d+)\s+(\d+)\s+\d+\s+app_hmi_link_diag\.o\s*$')
    $moduleRam = 0
    if ($module.Success) { $moduleRam = [int]$module.Groups[1].Value + [int]$module.Groups[2].Value }
    $stacks = @()
    foreach ($line in $CheckLines) {
        $match = [regex]::Match($line,'PASS stack (.+?)\s*:?\s+allocated=(\d+) depth=(\d+) margin=(\d+) bytes')
        if ($match.Success) {
            $stacks += [ordered]@{Task=$match.Groups[1].Value.TrimEnd(' ',':');AllocatedBytes=[int]$match.Groups[2].Value;DepthBytes=[int]$match.Groups[3].Value;MarginBytes=[int]$match.Groups[4].Value}
        }
    }
    if ($stacks.Count -ne 12) { throw "Expected all 12 task stack checks, found $($stacks.Count)." }
    [ordered]@{Regions=$regions;ModuleRamBytes=$moduleRam;PhysicalRamBytes=$physical;PhysicalBaselineBytes=174876;PhysicalGrowthBytes=($physical-174876);Stacks=$stacks;MinimumStaticStackMarginBytes=($stacks | ForEach-Object { $_.MarginBytes } | Measure-Object -Minimum).Minimum}
}
function Invoke-FormalBuild([int]$Enabled) {
    Assert-ProjectExpected
    Assert-SourcesUnchanged
    $phase = 'macro' + $Enabled
    $destination = Join-Path $Artifacts $phase
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    $variantHash = (Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash
    Copy-Item -LiteralPath $project -Destination (Join-Path $destination 'built-project.uvprojx.snapshot') -Force
    $temporaryLog = Join-Path $env:TEMP ('hmi_window_' + $phase + '_' + $token + '.log')
    $log = Join-Path $destination 'keil-rebuild.log'
    $started = Get-Date
    Write-Output "START $phase formal full rebuild: $project"
    $arguments = @('-r',('"'+(Split-Path $project -Leaf)+'"'),'-t',$targetName,'-o',('"'+$temporaryLog+'"'))
    $process = Start-Process -FilePath $Keil -WindowStyle Hidden -Wait -PassThru -WorkingDirectory $mdk -ArgumentList $arguments
    Assert-ProjectExpected
    Assert-SourcesUnchanged
    if (!(Test-Path -LiteralPath $temporaryLog)) { throw "Keil did not produce a log: exit=$($process.ExitCode), path=$temporaryLog" }
    Copy-Item -LiteralPath $temporaryLog -Destination $log -Force
    $buildText = Get-Content -LiteralPath $log -Raw
    if ($process.ExitCode -ne 0 -or $buildText -notmatch '0 Error\(s\), 0 Warning\(s\)') {
        Write-Output $buildText
        throw "Keil $phase failed: exit=$($process.ExitCode), log=$log"
    }
    $artifactResults = @()
    foreach ($extension in @('axf','hex','map','htm')) {
        $source = Join-Path $formalOutput ($formalName+'.'+$extension)
        if (!(Test-Path -LiteralPath $source)) { throw "Missing $phase artifact: $source" }
        if ((Get-Item -LiteralPath $source).LastWriteTime -lt $started.AddSeconds(-2)) { throw "Stale $phase artifact: $source" }
        $copy = Join-Path $destination ($formalName+'.'+$extension)
        Copy-Item -LiteralPath $source -Destination $copy -Force
        $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        if ((Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -ne $hash) { throw "Artifact copy hash mismatch: $copy" }
        $artifactResults += [ordered]@{Kind=$extension;ActualPath=$source;EvidencePath=$copy;Sha256=$hash}
    }
    $map = Join-Path $formalOutput ($formalName+'.map')
    $htm = Join-Path $formalOutput ($formalName+'.htm')
    $checkLog = Join-Path $destination 'memory-stack.log'
    $checkArguments = @{Map=$map;CallGraph=$htm}
    if ($Enabled -eq 1) { $checkArguments.RequireHmiLinkDiag = $true }
    $checkLines = @()
    try {
        $checkLines = @(& (Join-Path $tests 'check_tune_memory.ps1') @checkArguments)
    } catch {
        @($checkLines; $_ | Out-String) | Set-Content -LiteralPath $checkLog -Encoding UTF8
        throw
    }
    $checkLines | Set-Content -LiteralPath $checkLog -Encoding UTF8
    $checkLines | Write-Output
    $memory = Read-MemorySummary $map $checkLines
    $buildDate = $null
    $buildTime = $null
    $object = Join-Path $formalOutput 'app_hmi_link_diag.o'
    if ($Enabled -eq 1) {
        if (!(Test-Path -LiteralPath $object)) { throw 'Missing diagnostic object for BOOT build identity.' }
        $objectText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($object))
        $dateMatch = [regex]::Match($objectText,'(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [ \d]\d 20\d\d')
        $timeMatch = [regex]::Match($objectText,'\d{2}:\d{2}:\d{2}')
        if (!$dateMatch.Success -or !$timeMatch.Success) { throw 'Cannot extract diagnostic BOOT build date/time.' }
        $buildDate = $dateMatch.Value
        $buildTime = $timeMatch.Value
    }
    $bootBuildId = $null
    if ($buildDate -and $buildTime) { $bootBuildId = $buildDate+'_'+$buildTime }
    $script:results += [ordered]@{Macro=$Enabled;ProjectPath=$project;VariantProjectSha256=$variantHash;FormalProjectSha256=$projectHash;StartedLocal=$started.ToString('o');FinishedLocal=(Get-Date).ToString('o');ExitCode=$process.ExitCode;ZeroErrorsZeroWarnings=$true;LogPath=$log;MapPath=(Join-Path $destination ($formalName+'.map'));CallGraphPath=(Join-Path $destination ($formalName+'.htm'));ActualBuildMapPath=$map;ActualBuildCallGraphPath=$htm;MemoryStackLogPath=$checkLog;Memory=$memory;BootBuildDate=$buildDate;BootBuildTime=$buildTime;BootBuildId=$bootBuildId;Artifacts=$artifactResults}
    Write-Output "PASS $phase formal full rebuild: 0 errors, 0 warnings; evidence=$destination"
}

try {
    $xml = New-Object System.Xml.XmlDocument
    $xml.PreserveWhitespace = $true
    $xml.Load($project)
    $targets = $xml.SelectNodes('//Target[TargetName="RailInspectionCar_V1"]')
    if ($targets.Count -ne 1) { throw 'Expected exactly one formal RailInspectionCar_V1 target.' }
    $target = $targets[0]
    $define = $target.SelectSingleNode('TargetOption/TargetArmAds/Cads/VariousControls/Define')
    if (!$define -or $define.InnerText -match 'APP_HMI_LINK_DIAG_ENABLED\s*=\s*0') { throw 'Formal project must use diagnostic macro=1/default.' }
    $config = Get-Content -LiteralPath (Join-Path $root 'APP/app_config.h') -Raw
    if ($config -notmatch '#define\s+APP_HMI_LINK_DIAG_ENABLED\s+\(?1U?\)?') { throw 'Expected diagnostic macro default 1 in app_config.h.' }
    foreach ($hook in $xml.SelectNodes('//TargetCommonOption/BeforeCompile | //TargetCommonOption/BeforeMake | //TargetCommonOption/AfterMake')) {
        foreach ($number in @(1,2)) {
            $enabledNode = $hook.SelectSingleNode('RunUserProg'+$number)
            $commandNode = $hook.SelectSingleNode('UserProg'+$number+'Name')
            if ($enabledNode -and $enabledNode.InnerText -eq '1' -and $commandNode -and $commandNode.InnerText.Trim()) { throw 'Enabled Keil user command requires review before building.' }
        }
    }
    $formalOutput = [System.IO.Path]::GetFullPath((Join-Path $mdk $target.SelectSingleNode('TargetOption/TargetCommonOption/OutputDirectory').InnerText))
    $formalName = $target.SelectSingleNode('TargetOption/TargetCommonOption/OutputName').InnerText
    if (!$formalName) { throw 'Missing formal output name.' }
    $define.InnerText = ((@($define.InnerText -split ',' | Where-Object { $_ -notmatch '^\s*APP_HMI_LINK_DIAG_ENABLED\b' })) + @('APP_HMI_LINK_DIAG_ENABLED=0')) -join ','
    $settings = New-Object System.Xml.XmlWriterSettings
    $settings.Encoding = New-Object System.Text.UTF8Encoding($false)
    $stream = New-Object System.IO.MemoryStream
    $writer = [System.Xml.XmlWriter]::Create($stream,$settings)
    try { $xml.Save($writer) } finally { $writer.Dispose() }
    $macro0Bytes = $stream.ToArray()
    $stream.Dispose()
    Assert-ProjectExpected
    [System.IO.File]::WriteAllBytes($project,$macro0Bytes)
    $expectedProjectHash = (Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash
    Invoke-FormalBuild 0
    Restore-FormalProject
    Restore-Rte
    Invoke-FormalBuild 1
    Assert-SourcesUnchanged
}
finally {
    # A concurrent project edit is reported and preserved, never overwritten.
    try { Restore-FormalProject } finally { Restore-Rte }
}
Assert-ProjectExpected
Assert-SourcesUnchanged
$resultPath = Join-Path $Artifacts 'Keil构建结果.json'
[ordered]@{GeneratedLocal=(Get-Date).ToString('o');FormalProjectPath=$project;FormalProjectSha256=$projectHash;OriginalRteSha256=$rteHash;KeilPath=$Keil;BothVariantsUseFormalProject=$true;FinalFormalOutputMacro=1;ProductionSourceCount=$sourceHashes.Count;Results=$results;SourceHashes=$sourceHashes} |
    ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $resultPath -Encoding UTF8
Write-Output "PASS final macro1 artifacts, original project/RTE restored: $resultPath"
