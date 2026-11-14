param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
 foreach($source in @(1,2,3)){
    $exe=Join-Path $env:TEMP "dual_source_random_$source.exe"
    $options=@('-std=c99','-Wall','-Wextra','-Werror',"-DTIMING_SOURCE=$source",'-DAPP_SERVO_QUERY_MODE=3','-DAPP_SERVO_CONTROL_AXIS=3',
        '-DAPP_SERVO_READONLY_TEST=0','-DAPP_HMI_CONTROL_ENABLED=1','-ITests/mocks','-IAPP','-IAPP/HMI','-IDevice','-IProtocol','-IProtocol/CAN','-IBSP/SERVO_PULSE',
        'Tests/test_dual_source_random.c','APP/HMI/app_control_arbiter.c','APP/app_remote_servo_sync.c','APP/app_dual_axis_motion.c','APP/app_sync_params.c')
    if($source -eq 1){$options+=@('-DAPP_CAN_CONTROL_ENABLED=0','-DAPP_CAN_STOP_PARAMETERS_APPROVED=0','APP/HMI/app_hmi_control.c')}
    elseif($source -eq 2){$options+=@('-DAPP_CAN_CONTROL_ENABLED=0','-DAPP_CAN_STOP_PARAMETERS_APPROVED=0','APP/app_remote_servo.c','APP/app_config.c')}
    else{$options+=@('-DAPP_CAN_CONTROL_ENABLED=1','-DAPP_CAN_STOP_PARAMETERS_APPROVED=1','APP/app_can_control.c','Protocol/CAN/can_protocol.c')}
    & $Compiler @options -lm -o $exe
    if($LASTEXITCODE){throw 'Random source compile failed'}
    & $exe
    if($LASTEXITCODE){throw "Random source failed source=$source"}
 }
} finally {Pop-Location}
