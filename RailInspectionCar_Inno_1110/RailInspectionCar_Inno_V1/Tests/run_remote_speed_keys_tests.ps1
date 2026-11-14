param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
 foreach ($axis in @(1,2,3)) { foreach ($sync in @(0,1)) {
  $exe=Join-Path $env:TEMP "remote_speed_keys_${axis}_${sync}.exe"
  & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_SERVO_READONLY_TEST=0 -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
   "-DAPP_SERVO_QUERY_MODE=$axis" "-DAPP_SERVO_CONTROL_AXIS=$axis" "-DAPP_REMOTE_SERVO_SYNC_ENABLED=$sync" `
   -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IBSP/SERVO_PULSE Tests/test_remote_speed_keys.c `
   APP/app_remote_servo.c APP/app_remote_servo_sync.c APP/app_dual_axis_motion.c APP/app_sync_params.c APP/app_config.c APP/HMI/app_control_arbiter.c -lm -o $exe
  if ($LASTEXITCODE) {throw 'Speed mode test compile failed'}
  & $exe
  if ($LASTEXITCODE) {throw "Speed mode test failed axis=$axis sync=$sync"}
 } }
 $exe=Join-Path $env:TEMP 'remote_speed_delayed.exe'
 & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_SERVO_READONLY_TEST=0 -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
  -DAPP_SERVO_QUERY_MODE=3 -DAPP_SERVO_CONTROL_AXIS=3 -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IBSP/SERVO_PULSE Tests/test_remote_speed_delayed.c `
  APP/app_remote_servo.c APP/app_remote_servo_sync.c APP/app_dual_axis_motion.c APP/app_sync_params.c APP/app_config.c APP/HMI/app_control_arbiter.c -lm -o $exe
 if ($LASTEXITCODE) {throw 'Delayed speed compile failed'}
 & $exe
 if ($LASTEXITCODE) {throw 'Delayed speed test failed'}
 $remote=Join-Path (Split-Path $PSScriptRoot -Parent) '../Remote_control'
 $exe=Join-Path $env:TEMP 'remote_speed_link.exe'
 & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_SERVO_READONLY_TEST=0 -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
  -DAPP_SERVO_QUERY_MODE=3 -DAPP_SERVO_CONTROL_AXIS=3 -ITests/speed_key_mocks -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IBSP/SERVO_PULSE `
  "-I$remote/BSP/KEY" Tests/test_remote_speed_link.c "$remote/BSP/KEY/key.c" APP/nrf_remote_protocol.c `
  APP/app_remote_servo.c APP/app_remote_servo_sync.c APP/app_dual_axis_motion.c APP/app_sync_params.c APP/app_config.c APP/HMI/app_control_arbiter.c -lm -o $exe
 if ($LASTEXITCODE) {throw 'Cross scanner/controller compile failed'}
 & $exe
 if ($LASTEXITCODE) {throw 'Cross scanner/controller timing failed'}
} finally {Pop-Location}
