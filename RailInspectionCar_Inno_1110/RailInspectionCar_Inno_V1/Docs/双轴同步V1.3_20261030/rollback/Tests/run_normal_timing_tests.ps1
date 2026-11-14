param([string]$Compiler = 'D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe', [switch]$MeasureOnly, [switch]$Quick)
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $axes = @(1,2,3); $syncs = @(0,1)
    if ($Quick) { $axes = @(3); $syncs = @(1) }
    foreach ($source in @(1,2,3)) { foreach ($axis in $axes) { foreach ($sync in $syncs) {
        $binary = Join-Path $env:TEMP "normal_timing_${source}_${axis}_${sync}.exe"
        $options = @('-std=c99','-Wall','-Wextra','-Werror',"-DTIMING_SOURCE=$source",
            "-DAPP_SERVO_QUERY_MODE=$axis","-DAPP_SERVO_CONTROL_AXIS=$axis", "-DAPP_REMOTE_SERVO_SYNC_ENABLED=$sync",
            '-DAPP_SERVO_READONLY_TEST=0','-DAPP_HMI_CONTROL_ENABLED=1',
            '-ITests/mocks','-IAPP','-IAPP/HMI','-IDevice','-IProtocol','-IProtocol/CAN','-IBSP/SERVO_PULSE',
            'Tests/test_normal_timing.c','APP/HMI/app_control_arbiter.c','APP/app_remote_servo_sync.c')
        if ($source -eq 1) { $options += @('-DAPP_CAN_CONTROL_ENABLED=0','-DAPP_CAN_STOP_PARAMETERS_APPROVED=0','APP/HMI/app_hmi_control.c') }
        elseif ($source -eq 2) { $options += @('-DAPP_CAN_CONTROL_ENABLED=0','-DAPP_CAN_STOP_PARAMETERS_APPROVED=0','APP/app_remote_servo.c','APP/app_config.c') }
        else { $options += @('-DAPP_CAN_CONTROL_ENABLED=1','-DAPP_CAN_STOP_PARAMETERS_APPROVED=1','APP/app_can_control.c','Protocol/CAN/can_protocol.c') }
        & $Compiler @options -lm -o $binary
        if ($LASTEXITCODE) { throw 'Delayed timing compile failed' }
        if ($MeasureOnly) { & $binary measure } else { & $binary }
        if ($LASTEXITCODE) { throw "Delayed timing regression failed source=$source axis=$axis sync=$sync" }
    } } }
} finally { Pop-Location }
