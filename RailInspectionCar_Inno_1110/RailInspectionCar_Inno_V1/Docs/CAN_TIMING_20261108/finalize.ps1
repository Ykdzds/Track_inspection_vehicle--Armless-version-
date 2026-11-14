$ErrorActionPreference='Stop'
$f4=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$f1=Join-Path (Split-Path $f4 -Parent) 'F103_TEST/06_Upper_level_CAN_com_v1.1_low_speed_test'
$v=Join-Path $PSScriptRoot '验证'
function Hash([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash}
function Sources([string]$root,[string]$name){
    Push-Location $root
    try {
        $files=@(& rg --files -g '*.c' -g '*.h' -g '*.s' -g '*.sct' APP BSP Core Drivers Middlewares MDK-ARM)
        if(Test-Path Protocol){$files+=@(& rg --files -g '*.c' -g '*.h' Protocol)}
        if(Test-Path Device){$files+=@(& rg --files -g '*.c' -g '*.h' Device)}
        $files=@($files|Where-Object{$_ -notmatch 'MDK-ARM[\\/](RailInspectionCar_V1|DigitalElectronicseCode)[\\/]'})
        $entries=@($files|Sort-Object -Unique|ForEach-Object{[ordered]@{Path=$_;SHA256=(Hash $_)}})
        $path=Join-Path $v ($name+'-source-sha256.json')
        ConvertTo-Json -InputObject $entries -Depth 4|Set-Content -LiteralPath $path -Encoding utf8
        $digest=Hash $path
        & git diff --binary -- APP BSP Core Device Protocol Drivers Middlewares MDK-ARM/*.sct MDK-ARM/*.uvprojx > (Join-Path $v ($name+'-production-change.patch'))
        [ordered]@{Head=((& git rev-parse HEAD).Trim());SourceManifestSha256=$digest;SourceFiles=$entries.Count;WorkingTreeImplementation=$true}
    }finally{Pop-Location}
}
$results=@()
foreach($pair in @(@($f4,'f407-user','RailInspectionCar_V1'),@($f1,'f103-debug','DigitalElectronicseCode'))){
    $root=$pair[0];$variant=$pair[1];$stem=$pair[2];$dest=Join-Path $v $variant
    $manifest=Get-Content -LiteralPath (Join-Path $dest 'manifest.json') -Raw|ConvertFrom-Json
    $project=Join-Path $root ('MDK-ARM/'+$stem+'.uvprojx')
    if((Hash $project) -ne (Hash (Join-Path $dest 'project.snapshot'))){throw 'Formal project is not restored to source defaults'}
    $hashes=[ordered]@{}
    foreach($ext in @('hex','axf','map','htm')){
        $name=$stem+'.'+$ext;$formal=Join-Path $root ('MDK-ARM/'+$stem+'/'+$name)
        $sha=Hash $formal
        if($sha -ne $manifest.Hashes.$name -or $sha -ne (Hash (Join-Path $dest $name))){throw "Formal/archive/hash mismatch: $variant/$ext"}
        $hashes[$name]=$sha
    }
    $hashes[$stem+'.bin']=Hash (Join-Path $dest ($stem+'.bin'))
    $label=if($variant -eq 'f407-user'){'f407'}else{'f103'}
    $results+=,[ordered]@{Variant=$variant;Source=(Sources $root $label);ProjectSha256=(Hash $project);
        FormalOutputsMatchArchive=$true;SourceDefaults=$true;PhysicalRam=$manifest.Ram.PhysicalBytes;FirmwareHashes=$hashes}
}
$rows=@(foreach($n in @('f407-off','f407-all','f407-user','f103-debug')){
    $m=Get-Content -LiteralPath (Join-Path $v ($n+'/manifest.json')) -Raw|ConvertFrom-Json
    $bname=if($n -eq 'f103-debug'){'baseline-f103-debug'}else{'baseline-'+$n}
    $b=Get-Content -LiteralPath (Join-Path $v ($bname+'/manifest.json')) -Raw|ConvertFrom-Json
    $delta=$m.Ram.PhysicalBytes-$b.Ram.PhysicalBytes
    $limit=if($n -eq 'f103-debug'){96}else{256}
    if($delta -gt $limit){throw 'Fixed RAM budget failed'}
    if($n -eq 'f103-debug' -and 20480-$m.Ram.PhysicalBytes -lt 2048){throw 'F103 free SRAM failed'}
    [ordered]@{Variant=$n;Baseline=$b.Ram.PhysicalBytes;Physical=$m.Ram.PhysicalBytes;Delta=$delta;Budget=$limit;Passed=$true}
})
$rows|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $v 'ram-comparison.json') -Encoding utf8
$summary=[ordered]@{F407Revision='link5-can-r3';F103Revision='CANAGE_20261108_R1';Builds=$results;RamComparisons=$rows;
    MacroF407=[ordered]@{HMI=0;Remote=0;CANServo=1;CANArm=0;RS485=1;LCD=1;CLI=0;Tune=0;Pulse=0};MacroF103=[ordered]@{USART1Debug=1;Summary=1;Raw=0};
    BaselineF407='819077b';BaselineF103='2c60782';NewCommits=0;Pushed=$false;Flashed=$false;HardwareAcceptance='pending'}
$summary|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $v 'final-manifest.json') -Encoding utf8
Write-Output 'PASS source-default formal outputs/hash/memory; hardware acceptance pending'
