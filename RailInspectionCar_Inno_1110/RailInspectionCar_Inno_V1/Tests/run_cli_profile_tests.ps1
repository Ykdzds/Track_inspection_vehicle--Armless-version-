param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$temp=Join-Path $env:TEMP 'rail_cli_profile'
New-Item -ItemType Directory -Path $temp -Force | Out-Null
Push-Location $root
try {
    $flags=@('-std=c99','-Wall','-Wextra','-Werror')
    $includes=@('-ITests/rs485_mocks','-ITests/mocks','-IAPP','-IAPP/HMI','-IDevice','-IProtocol','-IProtocol/CAN','-IBSP/SYSTEM','-IBSP/RS485','-IBSP/SERVO_PULSE','-IBSP/DEBUG_UART','-IBSP/HMI_UART','-IBSP/NRF24L01','-IBSP/CAN','-ICore/Inc')
    $nm=Join-Path (Split-Path $Compiler -Parent) 'nm.exe'
    function Assert-NoSymbols([string]$Object,[string]$Pattern) {
        $symbols=& $nm -a $Object
        if($LASTEXITCODE){throw "nm failed: $Object"}
        if($symbols -match $Pattern){throw "Disabled storage still present: $Pattern"}
    }
    foreach($human in @(0,1)) {
        $binary=Join-Path $temp "session_$human.exe"
        & $Compiler @flags @includes "-DAPP_DEBUG_CLI_ENABLED=$human" -DAPP_SERVO_CLI_DEBUG_ENABLED=0 Tests/test_cli_session_disabled.c APP/app_cli_servo_session.c -o $binary
        if($LASTEXITCODE){throw 'Disabled session compile failed'}
        & $binary
        if($LASTEXITCODE){throw 'Disabled session test failed'}
    }
    $cli=Get-Content 'APP/app_debug_cli.c' -Raw
    $parser=[regex]::Match($cli,'(?s)static void app_cli_rx_service\(void\).*?(?=#endif /\* APP_UART_RX_ACTIVE \*/)').Value
    $init=[regex]::Match($cli,'(?s)void app_cli_init\(void\).*?(?=/\* Legacy bridges)').Value
    $process=[regex]::Match($cli,'(?s)void app_cli_process\(void\).*\z').Value
    if(!$parser -or !$init -or !$process){throw 'Production worker extraction failed'}
    $template=Get-Content 'Tests/test_cli_worker_profile.c' -Raw
    foreach($human in @(0,1)) {
        foreach($tune in @(0,1)) {
            $source=$template.Replace('/* PRODUCTION_INIT */',$init).Replace('/* PRODUCTION_PROCESS */',$process)
            $source=$source.Replace('/* PRODUCTION_PARSER */',"#if APP_UART_RX_ACTIVE`n$parser`n#endif")
            $temporary=Join-Path $temp "worker_$human$tune.c"
            [IO.File]::WriteAllText($temporary,$source)
            $binary=Join-Path $temp "worker_$human$tune.exe"
            & $Compiler @flags @includes "-DAPP_DEBUG_CLI_ENABLED=$human" "-DAPP_TUNE_UART_ENABLED=$tune" $temporary -o $binary
            if($LASTEXITCODE){throw "Worker compile failed: $human/$tune"}
            & $binary
            if($LASTEXITCODE){throw "Worker routing failed: $human/$tune"}
        }
    }
    $binary=Join-Path $temp 'logging_only.exe'
    $uartObject=Join-Path $temp 'uart_off.o'
    & $Compiler @flags -DAPP_DEBUG_CLI_ENABLED=0 -DAPP_TUNE_UART_ENABLED=0 -IAPP -ITests/debug_uart_mocks -IBSP/DEBUG_UART -c BSP/DEBUG_UART/debug_uart.c -o $uartObject
    if($LASTEXITCODE){throw 'Logging-only UART compile failed'}
    Assert-NoSymbols $uartObject 's_rx_(buffer|turns|epoch|consumed|running|recovery|aborting|aborted|seen|next_retry|last_poll|last_wraps|abort_at|total_base)'
    & $Compiler @flags -IAPP -ITests/debug_uart_mocks -IBSP/DEBUG_UART Tests/test_debug_uart_logging_only.c $uartObject -o $binary
    if($LASTEXITCODE){throw 'Logging-only UART harness compile failed'}
    & $binary
    if($LASTEXITCODE){throw 'Logging-only UART test failed'}
    foreach($name in @('app_debug_cli','app_tune_cli','app_tune_diagnostics','app_cli_servo_session')) {
        $object=Join-Path $temp "$name`_off.o"
        & $Compiler @flags @includes -DAPP_DEBUG_CLI_ENABLED=0 -DAPP_TUNE_UART_ENABLED=0 -c "APP/$name.c" -o $object
        if($LASTEXITCODE){throw "Disabled source compile failed: $name"}
        Assert-NoSymbols $object '(s_build_line|s_build_length|s_line_queue|s_line_overflow|s_rx_epoch|s_session|s_request|replay|diagnostic_pages|frame|system_state|current_fault|first_previous)'
    }
    # Compile the complete human executor in all four public D/T profiles.
    # Only the host UART type receives Init.BaudRate, which production HAL has.
    $uart=Get-Content 'Tests/rs485_mocks/usart.h' -Raw
    $uart=$uart.Replace('uint32_t ErrorCode, RxState;', 'uint32_t ErrorCode, RxState; struct { uint32_t BaudRate; } Init;')
    [IO.File]::WriteAllText((Join-Path $temp 'usart.h'),$uart)
    foreach($human in @(0,1)) {
        foreach($tune in @(0,1)) {
            foreach($session in @(0,1)) {
                if(!$human -and $session){continue}
                & $Compiler @flags "-I$temp" @includes -D__USE_MINGW_ANSI_STDIO=1 "-DAPP_DEBUG_CLI_ENABLED=$human" "-DAPP_TUNE_UART_ENABLED=$tune" "-DAPP_SERVO_CLI_DEBUG_ENABLED=$session" -c APP/app_debug_cli.c -o (Join-Path $temp "cli_$human$tune$session.o")
                if($LASTEXITCODE){throw "Full CLI compile failed: $human/$tune/$session"}
            }
        }
    }
    $guardSource=Join-Path $temp 'guard.c'
    [IO.File]::WriteAllText($guardSource,"#include `"app_config.h`"`n")
    foreach($macro in @('APP_DEBUG_CLI_ENABLED','APP_TUNE_UART_ENABLED','APP_SERVO_CLI_DEBUG_ENABLED','APP_SERVO_PULSE_ENABLED','APP_CHASSIS_LCD_ENABLED')) {
        foreach($value in @(-1,2)) {
            $messages=& $Compiler -E -IAPP "-D$macro=$value" $guardSource 2>&1
            if(!$LASTEXITCODE){throw "Invalid profile value accepted: $macro=$value"}
        }
    }
    $messages=& $Compiler -E -IAPP -DAPP_DEBUG_CLI_ENABLED=0 -DAPP_SERVO_CLI_DEBUG_ENABLED=1 $guardSource 2>&1
    if(!$LASTEXITCODE){throw 'CLI session without public human CLI accepted'}
    $binary=Join-Path $temp 'tune_worker.exe'
    & $Compiler @flags @includes -DAPP_DEBUG_CLI_ENABLED=0 -DAPP_TUNE_UART_ENABLED=1 Tests/test_tune_worker.c APP/app_debug_cli.c APP/app_tune_cli.c APP/app_sync_params.c APP/app_dual_axis_motion.c APP/app_remote_servo_sync.c -lm -o $binary
    if($LASTEXITCODE){throw 'Real tune-only worker compile failed'}
    & $binary
    if($LASTEXITCODE){throw 'Real tune-only worker regression failed'}
    Write-Output 'PASS CLI profile matrix and disabled-storage object assertions'
} finally {Pop-Location}
