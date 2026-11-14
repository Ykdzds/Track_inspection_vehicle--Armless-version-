param([string]$Compiler='D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try{
    $output=Join-Path $root 'Verification\OLEDUART'
    New-Item -ItemType Directory -Force -Path $output | Out-Null
    $common=@('-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-function','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-IAPP')
    $sources=@('APP/can_upper_protocol.c','APP/can_upper_control.c','APP/can_upper_ui.c')
    $equivalence=@()
    foreach($debug in 0,1){
        $exe=Join-Path $env:TEMP ('oled_runtime_'+$debug+'.exe')
        & $Compiler @common '-ITests/log_mocks' ('-DUPPER_USART1_DEBUG_ENABLE='+$debug) 'Tests/test_oled_runtime.c' @sources -o $exe
        if($LASTEXITCODE -ne 0){throw 'OLED runtime compile failed'}
        $result=& $exe 2>&1
        $result | Tee-Object -FilePath (Join-Path $output ('runtime_'+$debug+'.log'))
        if($LASTEXITCODE -ne 0){throw 'OLED runtime assertions failed'}
        $equivalence+=($result | Where-Object {$_ -match '^EQUIVALENCE '})
    }
    $left=Get-Content -LiteralPath (Join-Path $output 'runtime_0.log')
    $right=Get-Content -LiteralPath (Join-Path $output 'runtime_1.log')
    if(Compare-Object $left $right){throw '日志双模式故障与控制轨迹不一致'}
    if($equivalence.Count -ne 2 -or $equivalence[0] -ne $equivalence[1]){throw '日志双模式控制结果不一致'}
    & $Compiler @common '-ITests/log_mocks' '-DUPPER_USART1_DEBUG_ENABLE=1' '-DUPPER_LOG_RAW_RECORD=1' 'Tests/test_f103_logs.c' @sources -o (Join-Path $env:TEMP 'oled_raw_records.exe')
    if($LASTEXITCODE -ne 0){throw 'RAW explicit record compile failed'}
    & (Join-Path $env:TEMP 'oled_raw_records.exe') 2>&1 | Tee-Object -FilePath (Join-Path $output 'explicit_raw_records.log')
    if($LASTEXITCODE -ne 0){throw 'RAW explicit record tests failed'}
    $exe=Join-Path $env:TEMP 'oled_bounds.exe'
    & $Compiler @common '-Wno-unused-variable' '-Wno-sign-compare' '-Wno-missing-braces' '-ITests/oled_mocks' '-IBSP/OLED' 'Tests/test_oled_bounds.c' 'BSP/OLED/OLED.c' 'BSP/OLED/OLED_Data.c' @sources -lm -o $exe
    if($LASTEXITCODE -ne 0){throw 'OLED boundary compile failed'}
    & $exe 2>&1 | Tee-Object -FilePath (Join-Path $output 'bounds.log')
    if($LASTEXITCODE -ne 0){throw 'OLED boundary assertions failed'}
}finally{Pop-Location}
