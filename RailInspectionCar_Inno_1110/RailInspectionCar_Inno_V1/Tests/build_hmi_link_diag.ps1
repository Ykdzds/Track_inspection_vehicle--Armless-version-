param(
    [string]$Keil = 'D:\NewMDK5.36\UV4\UV4.exe',
    [string]$Artifacts = '',
    [switch]$FormalOnly
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$mdk = Join-Path $root 'MDK-ARM'
$project = Join-Path $mdk 'RailInspectionCar_V1.uvprojx'
$projectHash = (Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash
$rte = Join-Path $mdk 'RTE/_RailInspectionCar_V1/RTE_Components.h'
$rteBytes = [System.IO.File]::ReadAllBytes($rte)
$rteHash = (Get-FileHash -LiteralPath $rte -Algorithm SHA256).Hash
$token = [guid]::NewGuid().ToString('N').Substring(0,8)
$clone = Join-Path $mdk ('hld_' + $token + '.uvprojx')
$cloneOutput = Join-Path $mdk ('.hld-build\' + $token + '\off')
if (!$Artifacts) { $Artifacts = Join-Path $root 'Docs/HMI37_20261107/验证' }
$Artifacts = [System.IO.Path]::GetFullPath($Artifacts)
New-Item -ItemType Directory -Path $Artifacts -Force | Out-Null
$results = @()
$sources = @()
foreach ($folder in @('APP','BSP','Core','Device','Drivers','Middlewares','Protocol')) {
    $directory = Join-Path $root $folder
    if (Test-Path -LiteralPath $directory) {
        $sources += Get-ChildItem -LiteralPath $directory -Recurse -File | Where-Object { $_.Extension -in @('.c','.h','.s') }
    }
}
$sources += Get-Item -LiteralPath (Join-Path $mdk 'startup_stm32f407xx.s')
$sourceHashes = @($sources | Sort-Object FullName | ForEach-Object {
    [pscustomobject]@{Path=$_.FullName;Sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
})
function Assert-ProjectUnchanged {
    if ((Get-FileHash -LiteralPath $project -Algorithm SHA256).Hash -ne $projectHash) {
        throw 'Formal uvprojx changed during build; refusing to overwrite another editor or agent.'
    }
}
function Read-MemorySummary([string]$Map, [string[]]$CheckLines) {
    $mapText = Get-Content -LiteralPath $Map -Raw
    $regions = [ordered]@{}
    foreach ($name in @('RW_CCM','RW_IRAM1','RW_IRAM2','RETAINED_FAULT')) {
        $m = [regex]::Match($mapText, "Execution Region $name \(Exec base: (0x[0-9a-f]+), Load base: (0x[0-9a-f]+), Size: (0x[0-9a-f]+), Max: (0x[0-9a-f]+)")
        if (!$m.Success) { throw "Missing region $name" }
        $size = [Convert]::ToUInt32($m.Groups[3].Value,16)
        $capacity = [Convert]::ToUInt32($m.Groups[4].Value,16)
        $regions[$name] = [ordered]@{Base=$m.Groups[1].Value;Bytes=$size;Capacity=$capacity;FreeBytes=($capacity-$size)}
    }
    $physical = 0
    foreach ($value in $regions.Values) { $physical += $value.Bytes }
    $module = [regex]::Match($mapText,'(?m)^\s*\d+\s+\d+\s+\d+\s+(\d+)\s+(\d+)\s+\d+\s+app_hmi_link_diag\.o\s*$')
    $moduleRam = 0
    if ($module.Success) { $moduleRam = [int]$module.Groups[1].Value + [int]$module.Groups[2].Value }
    $stack = @()
    foreach ($line in $CheckLines) {
        $match = [regex]::Match($line,'PASS stack (.+?)\s*:?\s+allocated=(\d+) depth=(\d+) margin=(\d+) bytes')
        if ($match.Success) {
            $stack += [ordered]@{Task=$match.Groups[1].Value.TrimEnd(' ',':');AllocatedBytes=[int]$match.Groups[2].Value;DepthBytes=[int]$match.Groups[3].Value;MarginBytes=[int]$match.Groups[4].Value}
        }
    }
    [ordered]@{Regions=$regions;ModuleRamBytes=$moduleRam;PhysicalRamBytes=$physical;PhysicalBaselineBytes=174876;PhysicalGrowthBytes=($physical-174876);Stacks=$stack;MinimumStaticStackMarginBytes=($stack | ForEach-Object { $_.MarginBytes } | Measure-Object -Minimum).Minimum}
}
function Invoke-Build([int]$Enabled, [string]$ProjectPath, [string]$OutputDirectory, [string]$OutputName) {
    Assert-ProjectUnchanged
    $phase = 'macro' + $Enabled
    $destination = Join-Path $Artifacts $phase
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    $variantProjectHash = (Get-FileHash -LiteralPath $ProjectPath -Algorithm SHA256).Hash
    Copy-Item -LiteralPath $ProjectPath -Destination (Join-Path $destination 'built-project.uvprojx.snapshot') -Force
    $temporaryLog = Join-Path $env:TEMP ('hldbuild_' + $phase + '_' + $token + '.log')
    $log = Join-Path $destination 'keil-rebuild.log'
    $started = Get-Date
    Write-Output "START $phase full rebuild: $ProjectPath"
    $arguments = @('-r',('"' + (Split-Path $ProjectPath -Leaf) + '"'),'-t','RailInspectionCar_V1','-o',('"' + $temporaryLog + '"'))
    $process = Start-Process -FilePath $Keil -WindowStyle Hidden -Wait -PassThru -WorkingDirectory $mdk -ArgumentList $arguments
    Assert-ProjectUnchanged
    if (!(Test-Path -LiteralPath $temporaryLog)) { throw "Keil did not produce a build log: exit=$($process.ExitCode), project=$ProjectPath, log=$temporaryLog" }
    Copy-Item -LiteralPath $temporaryLog -Destination $log -Force
    $buildText = Get-Content -LiteralPath $log -Raw
    if ($process.ExitCode -ne 0 -or $buildText -notmatch '0 Error\(s\), 0 Warning\(s\)') {
        Write-Output $buildText
        throw "Keil $phase failed: exit=$($process.ExitCode), log=$log"
    }
    $map = Join-Path $OutputDirectory ($OutputName + '.map')
    $htm = Join-Path $OutputDirectory ($OutputName + '.htm')
    $checkLog = Join-Path $destination 'memory-stack.log'
    $checkArguments = @{Map=$map;CallGraph=$htm}
    if ($Enabled -eq 1) { $checkArguments.RequireHmiLinkDiag = $true }
    $checkLines = @(& (Join-Path $PSScriptRoot 'check_tune_memory.ps1') @checkArguments)
    $checkLines | Set-Content -LiteralPath $checkLog -Encoding UTF8
    $checkLines | Write-Output
    $artifactResults = @()
    foreach ($extension in @('axf','hex','map','htm')) {
        $source = Join-Path $OutputDirectory ($OutputName + '.' + $extension)
        if (!(Test-Path -LiteralPath $source)) { throw "Missing build artifact: $source" }
        if ((Get-Item -LiteralPath $source).LastWriteTime -lt $started.AddSeconds(-2)) { throw "Stale build artifact: $source" }
        $copy = Join-Path $destination ($OutputName + '.' + $extension)
        Copy-Item -LiteralPath $source -Destination $copy -Force
        $artifactResults += [ordered]@{Kind=$extension;ActualPath=$source;EvidencePath=$copy;Sha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash}
    }
    $buildDate = $null
    $buildTime = $null
    $object = Join-Path $OutputDirectory 'app_hmi_link_diag.o'
    if ($Enabled -eq 1 -and (Test-Path -LiteralPath $object)) {
        $binaryText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($object))
        $dateMatch = [regex]::Match($binaryText,'(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [ \d]\d 20\d\d')
        $timeMatch = [regex]::Match($binaryText,'\d{2}:\d{2}:\d{2}')
        if ($dateMatch.Success) { $buildDate = $dateMatch.Value }
        if ($timeMatch.Success) { $buildTime = $timeMatch.Value }
    }
    $bootBuildId = $null
    if ($buildDate -and $buildTime) { $bootBuildId = $buildDate+'_'+$buildTime }
    $script:results += [ordered]@{Macro=$Enabled;ProjectPath=$ProjectPath;VariantProjectSha256=$variantProjectHash;FormalProjectSha256=$projectHash;StartedLocal=$started.ToString('o');FinishedLocal=(Get-Date).ToString('o');ExitCode=$process.ExitCode;ZeroErrorsZeroWarnings=$true;LogPath=$log;MapPath=(Join-Path $destination ($OutputName+'.map'));CallGraphPath=(Join-Path $destination ($OutputName+'.htm'));ActualBuildMapPath=$map;ActualBuildCallGraphPath=$htm;MemoryStackLogPath=$checkLog;Memory=(Read-MemorySummary $map $checkLines);BootBuildDate=$buildDate;BootBuildTime=$buildTime;BootBuildId=$bootBuildId;Artifacts=$artifactResults}
    Write-Output "PASS $phase : 0 errors, 0 warnings; evidence=$destination"
}
try {
    if (!(Test-Path -LiteralPath $Keil)) { throw "Keil missing: $Keil" }
    $xml = New-Object System.Xml.XmlDocument
    $xml.PreserveWhitespace = $true
    $xml.Load($project)
    $define = $xml.SelectSingleNode('//Target/TargetOption/TargetArmAds/Cads/VariousControls/Define')
    if (!$define -or $define.InnerText -match 'APP_HMI_LINK_DIAG_ENABLED\s*=\s*0') { throw 'Formal project must use diagnostics enabled default.' }
    $config = Get-Content -LiteralPath (Join-Path $root 'APP/app_config.h') -Raw
    if ($config -notmatch '#define\s+APP_HMI_LINK_DIAG_ENABLED\s+\(?1U?\)?') { throw 'Expected diagnostic macro default 1 in app_config.h.' }
    foreach ($hook in $xml.SelectNodes('//TargetCommonOption/BeforeCompile | //TargetCommonOption/BeforeMake | //TargetCommonOption/AfterMake')) {
        foreach ($number in @(1,2)) {
            $enabledNode = $hook.SelectSingleNode('RunUserProg'+$number)
            $commandNode = $hook.SelectSingleNode('UserProg'+$number+'Name')
            if ($enabledNode -and $enabledNode.InnerText -eq '1' -and $commandNode -and $commandNode.InnerText.Trim()) { throw 'Formal build contains enabled user command; inspect before running.' }
        }
    }
    if (!$FormalOnly) {
        $define.InnerText = (($define.InnerText -split ',' | Where-Object { $_ -notmatch '^\s*APP_HMI_LINK_DIAG_ENABLED\b' }) + 'APP_HMI_LINK_DIAG_ENABLED=0') -join ','
        $xml.SelectSingleNode('//Target/TargetOption/TargetCommonOption/OutputDirectory').InnerText = '.hld-build\' + $token + '\off\'
        $xml.SelectSingleNode('//Target/TargetOption/TargetCommonOption/ListingPath').InnerText = '.hld-build\' + $token + '\off\'
        $xml.SelectSingleNode('//Target/TargetOption/TargetCommonOption/OutputName').InnerText = 'hmi_link_diag_off'
        foreach ($node in $xml.SelectNodes('//BeforeCompile/RunUserProg1 | //BeforeCompile/RunUserProg2 | //BeforeMake/RunUserProg1 | //BeforeMake/RunUserProg2 | //AfterMake/RunUserProg1 | //AfterMake/RunUserProg2')) { $node.InnerText = '0' }
        $settings = New-Object System.Xml.XmlWriterSettings
        $settings.Encoding = New-Object System.Text.UTF8Encoding($false)
        $writer = [System.Xml.XmlWriter]::Create($clone,$settings)
        try { $xml.Save($writer) } finally { $writer.Dispose() }
        Invoke-Build 0 $clone $cloneOutput 'hmi_link_diag_off'
    }
    $formalXml = New-Object System.Xml.XmlDocument
    $formalXml.Load($project)
    $formalOutput = Join-Path $mdk $formalXml.SelectSingleNode('//Target/TargetOption/TargetCommonOption/OutputDirectory').InnerText
    $formalName = $formalXml.SelectSingleNode('//Target/TargetOption/TargetCommonOption/OutputName').InnerText
    Invoke-Build 1 $project $formalOutput $formalName
    foreach ($sourceHash in $sourceHashes) {
        if ((Get-FileHash -LiteralPath $sourceHash.Path -Algorithm SHA256).Hash -ne $sourceHash.Sha256) { throw "Source changed during builds: $($sourceHash.Path); repeat final verification." }
    }
    $resultPath = Join-Path $Artifacts 'Keil构建结果.json'
    [ordered]@{GeneratedLocal=(Get-Date).ToString('o');FormalProjectPath=$project;FormalProjectSha256=$projectHash;OriginalRteSha256=$rteHash;KeilPath=$Keil;FormalOnly=[bool]$FormalOnly;FinalFormalOutputMacro=1;Results=$results;SourceHashes=$sourceHashes} | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $resultPath -Encoding UTF8
    Write-Output "PASS final build result: $resultPath"
}
finally {
    [System.IO.File]::WriteAllBytes($rte,$rteBytes)
    if ((Get-FileHash -LiteralPath $rte -Algorithm SHA256).Hash -ne $rteHash) { throw 'RTE restoration failed.' }
    Assert-ProjectUnchanged
    foreach ($path in @($clone,[System.IO.Path]::ChangeExtension($clone,'.uvoptx'),[System.IO.Path]::ChangeExtension($clone,'.uvguix.15056'))) {
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
    $allowed = [System.IO.Path]::GetFullPath((Join-Path $mdk '.hld-build')) + [System.IO.Path]::DirectorySeparatorChar
    $cleanup = [System.IO.Path]::GetFullPath((Join-Path $mdk ('.hld-build\'+$token)))
    if (!$cleanup.StartsWith($allowed,[System.StringComparison]::OrdinalIgnoreCase)) { throw "Refuse cleanup outside temporary build root: $cleanup" }
    if (Test-Path -LiteralPath $cleanup) { Remove-Item -LiteralPath $cleanup -Recurse -Force }
}
