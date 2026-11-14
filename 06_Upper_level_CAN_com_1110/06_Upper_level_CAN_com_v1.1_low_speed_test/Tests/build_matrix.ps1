param([string]$Keil='D:\NewMDK5.36\UV4\UV4.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$mdk=Join-Path $root 'MDK-ARM'
$project=Join-Path $mdk 'DigitalElectronicseCode.uvprojx'
$output=Join-Path $root 'Verification\OLEDUART'
$config=Get-Content -LiteralPath (Join-Path $root 'APP\can_upper_config.h') -Raw
if($config -notmatch '#define UPPER_USART1_DEBUG_ENABLE 0U'){throw '交付矩阵要求最终默认总开关为0U'}
New-Item -ItemType Directory -Force -Path $output | Out-Null
foreach($mode in @('summary','quiet','raw','invalid')) {
    $directory=Join-Path $output $mode
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    $source=Get-Content -LiteralPath $project -Raw
    if($source -notmatch '<Define>USE_HAL_DRIVER,STM32F103xB</Define>'){throw '工程宏基线已变化，不能静默构建错误配置'}
    $define='USE_HAL_DRIVER,STM32F103xB,UPPER_USART1_DEBUG_ENABLE=1'
    if($mode -eq 'quiet'){$define+=',UPPER_LOG_LEVEL=0'}
    if($mode -eq 'raw'){$define+=',UPPER_LOG_LEVEL=2,UPPER_LOG_RAW_RECORD=1'}
    if($mode -eq 'invalid'){$define='USE_HAL_DRIVER,STM32F103xB,UPPER_USART1_DEBUG_ENABLE=2'}
    $source=$source.Replace('<Define>USE_HAL_DRIVER,STM32F103xB</Define>',('<Define>'+$define+'</Define>'))
    $source=$source.Replace('<OutputDirectory>DigitalElectronicseCode\</OutputDirectory>',('<OutputDirectory>oled_'+$mode+'\</OutputDirectory>'))
    $source=$source.Replace('<OutputName>DigitalElectronicseCode</OutputName>',('<OutputName>F103_OLEDUART_'+$mode+'</OutputName>'))
    $temporary=Join-Path $mdk ('oled_'+$mode+'.uvprojx')
    try {
        [System.IO.File]::WriteAllText($temporary,$source,[System.Text.UTF8Encoding]::new($false))
        $log=Join-Path $env:TEMP ('f103_oled_'+$mode+'.log')
        if(Test-Path -LiteralPath $log){Remove-Item -LiteralPath $log}
        $process=Start-Process -FilePath $Keil -ArgumentList @('-r',('"'+$temporary+'"'),'-t','DigitalElectronicseCode','-j0','-o',('"'+$log+'"')) -WindowStyle Hidden -PassThru
        $process.WaitForExit()
        Write-Output "UV mode=$mode exit=$($process.ExitCode)"
        $content=Get-Content -LiteralPath $log -Raw
        Copy-Item -LiteralPath $log -Destination (Join-Path $directory 'keil_build.log')
        if($mode -ne 'invalid'){
            foreach($extension in @('hex','map','htm','axf')){
                Copy-Item -LiteralPath (Join-Path $mdk ('oled_'+$mode+'\F103_OLEDUART_'+$mode+'.'+$extension)) -Destination $directory -Force
            }
        }
        ('UPPER_USART1_DEBUG_ENABLE='+$(if($mode -eq 'invalid'){2}else{1})+'; '+$define) | Set-Content -LiteralPath (Join-Path $directory 'build_defines.txt') -Encoding utf8
        if($mode -eq 'invalid') {
            if($content -notmatch 'UPPER_USART1_DEBUG_ENABLE_must_be_zero_or_one'){throw '非法宏值未被拒绝'}
            Write-Output 'PASS invalid macro rejected'
            continue
        }
        if($content -notmatch '0 Error\(s\), 0 Warning\(s\)'){Write-Output $content;throw "构建失败:$mode"}
        & (Join-Path $PSScriptRoot 'check_resources.ps1') -BuildRoot $directory -Name ('F103_OLEDUART_'+$mode) -DebugEnabled 1
    } finally {
        $generated=Join-Path $mdk ('oled_'+$mode)
        $resolved=[System.IO.Path]::GetFullPath($generated)
        if(!$resolved.StartsWith(([System.IO.Path]::GetFullPath($mdk)+'\'),[System.StringComparison]::OrdinalIgnoreCase)){throw '临时输出目录越界'}
        if(Test-Path -LiteralPath $generated){Remove-Item -LiteralPath $generated -Recurse -Force}
        foreach($extension in @('.uvprojx','.uvoptx','.uvguix.15056','.uvgui.15056')) {
            $path=Join-Path $mdk ('oled_'+$mode+$extension)
            if(Test-Path -LiteralPath $path){Remove-Item -LiteralPath $path}
        }
    }
}
& (Join-Path $PSScriptRoot 'build.ps1') -Keil $Keil
$default=Join-Path $output 'default'
New-Item -ItemType Directory -Force -Path $default | Out-Null
foreach($extension in @('hex','map','htm','axf')) {
    Copy-Item -LiteralPath (Join-Path $mdk ('DigitalElectronicseCode\DigitalElectronicseCode.'+$extension)) -Destination (Join-Path $default ('F103_OLEDUART_default.'+$extension))
}
'UPPER_USART1_DEBUG_ENABLE=0; UPPER_LOG_LEVEL=1 (compiled out)' | Set-Content -LiteralPath (Join-Path $default 'build_defines.txt') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $root 'Verification\keil_build.log') -Destination (Join-Path $default 'keil_build.log')
& (Join-Path $PSScriptRoot 'check_resources.ps1') -BuildRoot $default -Name 'F103_OLEDUART_default' -DebugEnabled 0
Get-ChildItem -LiteralPath $output -Filter *.hex -Recurse | Get-FileHash -Algorithm SHA256 | Format-List | Out-File -LiteralPath (Join-Path $output 'SHA256.txt') -Encoding utf8
