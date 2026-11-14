param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe', [string]$Output='')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $configs=@(
        @('-DTEST_DUAL_CONFIG_VALID=1'),
        @('-DTEST_DUAL_CONFIG_VALID=0','-DAPP_DUAL_S_ENABLED=1'),
        @('-DTEST_DUAL_CONFIG_VALID=0','-DAPP_DUAL_PI_ENABLED=1'),
        @('-DTEST_DUAL_CONFIG_VALID=0','-DAPP_DUAL_FRONT_GEAR=0.0f'),
        @('-DTEST_DUAL_CONFIG_VALID=0','-DAPP_DUAL_REAR_SIGN=0'),
        @('-DTEST_DUAL_CONFIG_VALID=0','-DAPP_DUAL_S_ENABLED=1','-DAPP_DUAL_JERK_MPS3=-0.4f'),
        @('-DTEST_DUAL_CONFIG_VALID=1','-DAPP_DUAL_S_ENABLED=1','-DAPP_DUAL_JERK_MPS3=0.4f'),
        @('-DTEST_DUAL_CONFIG_VALID=1','-DAPP_DUAL_PI_ENABLED=1','-DAPP_DUAL_KI=0.02f','-DAPP_DUAL_KAW=1.0f'))
    foreach($config in $configs){
        $configExe=Join-Path $env:TEMP 'dual_config.exe'
        & $Compiler -std=c99 -Wall -Wextra -Werror -IAPP @config Tests/test_dual_config.c -o $configExe
        if($LASTEXITCODE){throw 'Dual configuration compile failed'}
        & $configExe
        if($LASTEXITCODE){throw 'Dual configuration gate failed'}
    }
    foreach($variant in @(0,1,2,3)) {
        $exe=Join-Path $env:TEMP "dual_axis_$variant.exe"
        $argsList=@('-std=c99','-Wall','-Wextra','-Werror','-ITests/mocks','-IAPP','-IDevice',
            'Tests/test_dual_axis_motion.c','APP/app_dual_axis_motion.c','APP/app_sync_params.c','APP/app_remote_servo_sync.c')
        if($variant -eq 0){$argsList+='-DAPP_REMOTE_SERVO_SYNC_ENABLED=0'}
        if($variant -ge 2){$argsList+=@('-DAPP_DUAL_S_ENABLED=1','-DAPP_DUAL_JERK_MPS3=0.4f')}
        if($variant -eq 3){$argsList+=@('-DAPP_DUAL_PI_ENABLED=1','-DAPP_DUAL_KI=0.02f','-DAPP_DUAL_KAW=1.0f')}
        & $Compiler @argsList -lm -o $exe
        if($LASTEXITCODE){throw 'Dual-axis compile failed'}
        if($Output){$env:DUAL_CURVE_FILE=Join-Path $Output "dynamic_model_$variant.csv"}
        & $exe
        if($LASTEXITCODE){throw "Dual-axis tests failed variant=$variant"}
    }
} finally { Remove-Item Env:DUAL_CURVE_FILE -ErrorAction SilentlyContinue; Pop-Location }
