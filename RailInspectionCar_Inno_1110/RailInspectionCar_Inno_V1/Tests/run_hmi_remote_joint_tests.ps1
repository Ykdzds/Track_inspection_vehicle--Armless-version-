param([string]$Compiler="D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe")
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$binary=Join-Path $env:TEMP 'rail_hmi_remote_joint.exe'
Push-Location $root
try {
    foreach($axis in @(1,2,3)) {
        foreach($sync in @(0,1)) {
            $arguments=@('-std=c99','-Wall','-Wextra','-Werror',"-DAPP_SERVO_QUERY_MODE=$axis","-DAPP_SERVO_CONTROL_AXIS=$axis","-DAPP_REMOTE_SERVO_SYNC_ENABLED=$sync",'-ITests/mocks','-IAPP','-IAPP/HMI','-IDevice','-IProtocol','-IBSP/SERVO_PULSE','Tests/test_hmi_remote_joint.c','APP/app_remote_servo.c','APP/HMI/app_hmi_control.c','APP/HMI/app_control_arbiter.c','APP/app_remote_servo_sync.c', 'APP/app_dual_axis_motion.c','APP/app_sync_params.c','-o',$binary)
            & $Compiler @arguments
            if($LASTEXITCODE){throw "联合测试编译失败 axis=$axis sync=$sync"}
            & $binary
            if($LASTEXITCODE){throw "联合测试失败 axis=$axis sync=$sync"}
            Write-Output "PASS joint matrix axis=$axis sync=$sync"
        }
    }
} finally {Pop-Location}
