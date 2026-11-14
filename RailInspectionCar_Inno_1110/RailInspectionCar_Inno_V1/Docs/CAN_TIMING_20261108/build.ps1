param([switch]$BaselineOnly, [switch]$MatrixOnly,
    [string[]]$Variants=@('f407-off','f407-all','f407-user','f103-off','f103-debug'),
    [string]$Keil='D:/NewMDK5.36/UV4/UV4.exe')
$ErrorActionPreference='Stop'
$f4=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$f1=Join-Path (Split-Path $f4 -Parent) 'F103_TEST/06_Upper_level_CAN_com_v1.1_low_speed_test'
$out=Join-Path $PSScriptRoot '验证'
$fromelf=Join-Path (Split-Path (Split-Path $Keil -Parent) -Parent) 'ARM/ARMCC/bin/fromelf.exe'
New-Item -ItemType Directory -Force -Path $out | Out-Null
function Hash([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash}
function Ram([string]$p,[bool]$f407){
    $text=[IO.File]::ReadAllText($p);$total=0;$regions=[ordered]@{}
    $names=if($f407){@('RW_CCM','RW_IRAM1','RW_IRAM2','RETAINED_FAULT')}else{@('RW_IRAM1')}
    foreach($n in $names){
        $m=[regex]::Match($text,"Execution Region $n \([^\r\n]*Size: (0x[0-9a-fA-F]+), Max: (0x[0-9a-fA-F]+)")
        if(!$m.Success){throw "Missing region $n in $p"}
        $used=[Convert]::ToUInt32($m.Groups[1].Value,16);$capacity=[Convert]::ToUInt32($m.Groups[2].Value,16)
        if($used -gt $capacity){throw "Region overflow: $n"}
        $total+=$used;$regions[$n]=[ordered]@{Used=$used;Capacity=$capacity;Free=($capacity-$used)}
    }
    [ordered]@{PhysicalBytes=$total;Regions=$regions}
}
function Export-Commit([string]$root,[string]$commit,[string]$name){
    $temp=Join-Path $env:TEMP ('rail_can_rebuild_'+$name+'_'+[guid]::NewGuid().ToString('N'))
    $zip=$temp+'.zip'
    & git -C $root archive --format=zip ('--output='+$zip) $commit
    if($LASTEXITCODE){throw 'git archive failed'}
    Expand-Archive -LiteralPath $zip -DestinationPath $temp
    $temp
}
function Build-One([string]$root,[bool]$is4,[string]$name,[string[]]$extra,[string]$commit){
    $stem=if($is4){'RailInspectionCar_V1'}else{'DigitalElectronicseCode'}
    $mcu=if($is4){'STM32F407xx'}else{'STM32F103xB'}
    $mdk=Join-Path $root 'MDK-ARM';$proj=Join-Path $mdk ($stem+'.uvprojx')
    $rte=Join-Path $mdk ('RTE/_'+$stem+'/RTE_Components.h')
    $dest=Join-Path $out $name;New-Item -ItemType Directory -Force -Path $dest | Out-Null
    $original=[IO.File]::ReadAllBytes($proj);$rteOriginal=[IO.File]::ReadAllBytes($rte)
    $originalHash=Hash $proj;$rteHash=Hash $rte
    $originalText=[IO.File]::ReadAllText($proj)
    $marker='<Define>USE_HAL_DRIVER,'+$mcu+'</Define>'
    if([regex]::Matches($originalText,[regex]::Escape($marker)).Count -ne 1){throw 'Unexpected formal project C defines'}
    try{
        if($extra.Count){
            $replacement='<Define>'+((@('USE_HAL_DRIVER',$mcu)+$extra)-join ',')+'</Define>'
            [IO.File]::WriteAllText($proj,$originalText.Replace($marker,$replacement),[Text.UTF8Encoding]::new($false))
        }
        $expected=Hash $proj
        Copy-Item -LiteralPath $proj -Destination (Join-Path $dest 'project.snapshot') -Force
        $log=Join-Path $env:TEMP ('rail_can_'+[guid]::NewGuid().ToString('N')+'.log')
        $started=Get-Date
        Write-Output "START full Keil $name"
        $p=Start-Process -FilePath $Keil -WorkingDirectory $mdk -WindowStyle Hidden -Wait -PassThru -ArgumentList @('-r',($stem+'.uvprojx'),'-t',$stem,'-o',('"'+$log+'"'))
        if(!(Test-Path -LiteralPath $log)){throw "No Keil log: $name"}
        Copy-Item -LiteralPath $log -Destination (Join-Path $dest 'keil-rebuild.log') -Force
        $text=[IO.File]::ReadAllText($log)
        if($p.ExitCode -ne 0 -or $text -notmatch '0 Error\(s\), 0 Warning\(s\)' -or $text -match 'Target not created'){
            Get-Content -LiteralPath $log -Tail 45;throw "Keil failed: $name exit=$($p.ExitCode)"
        }
        if((Hash $proj) -ne $expected){throw 'Project modified during rebuild'}
        foreach($ext in @('map','htm','hex','axf')){
            $artifact=Join-Path $mdk ($stem+'/'+$stem+'.'+$ext)
            if(!(Test-Path -LiteralPath $artifact) -or (Get-Item -LiteralPath $artifact).LastWriteTimeUtc -lt $started.ToUniversalTime().AddSeconds(-2)){throw "Missing or stale artifact: $artifact"}
            Copy-Item -LiteralPath $artifact -Destination $dest -Force
        }
        & $fromelf --bin --output (Join-Path $dest ($stem+'.bin')) (Join-Path $dest ($stem+'.axf'))
        if($LASTEXITCODE){throw 'fromelf binary conversion failed'}
        $ram=Ram (Join-Path $dest ($stem+'.map')) $is4
        $hashes=[ordered]@{}
        foreach($file in Get-ChildItem -LiteralPath $dest -File | Where-Object Name -ne 'manifest.json'){$hashes[$file.Name]=Hash $file.FullName}
        $result=[ordered]@{Variant=$name;BaselineCommit=$commit;Project=$proj;SourceDefaults=(!$extra.Count);Defines=$extra;
            Started=$started.ToString('o');Completed=(Get-Date).ToString('o');Exit=$p.ExitCode;Errors=0;Warnings=0;
            Ram=$ram;Hashes=$hashes;HardwareAcceptance='pending'}
        $result|ConvertTo-Json -Depth 9|Set-Content -LiteralPath (Join-Path $dest 'manifest.json') -Encoding utf8
        Write-Output "PASS $name physical=$($ram.PhysicalBytes)"
    }finally{
        [IO.File]::WriteAllBytes($proj,$original);[IO.File]::WriteAllBytes($rte,$rteOriginal)
        if((Hash $proj) -ne $originalHash -or (Hash $rte) -ne $rteHash){throw 'Project/RTE byte restoration failed'}
    }
}
$switches=@('APP_HMI_LINK_DIAG_ENABLED','APP_NRF_DIAGNOSTIC_ENABLED','APP_CAN_SERVO_DIAG_ENABLED','APP_CAN_ARM_DIAG_ENABLED','APP_SERVO_RS485_DIAG_ENABLED')
$off=@($switches|ForEach-Object{$_+'=0'})
$all=@($switches|ForEach-Object{$_+'=1'})
if(!$MatrixOnly){
    $base4=Export-Commit $f4 '819077b' 'f407'
    Build-One $base4 $true 'baseline-f407-off' $off '819077b'
    Build-One $base4 $true 'baseline-f407-all' $all '819077b'
    Build-One $base4 $true 'baseline-f407-user' @() '819077b'
    $base1=Export-Commit $f1 '2c60782' 'f103'
    Build-One $base1 $false 'baseline-f103-debug' @('UPPER_USART1_DEBUG_ENABLE=1') '2c60782'
}
if(!$BaselineOnly){
    if('f407-off' -in $Variants){Build-One $f4 $true 'f407-off' $off '819077b'}
    if('f407-all' -in $Variants){Build-One $f4 $true 'f407-all' $all '819077b'}
    if('f407-user' -in $Variants){Build-One $f4 $true 'f407-user' @() '819077b'}
    if('f103-off' -in $Variants){Build-One $f1 $false 'f103-off' @('UPPER_USART1_DEBUG_ENABLE=0') '2c60782'}
    if('f103-debug' -in $Variants){Build-One $f1 $false 'f103-debug' @() '2c60782'}
}
