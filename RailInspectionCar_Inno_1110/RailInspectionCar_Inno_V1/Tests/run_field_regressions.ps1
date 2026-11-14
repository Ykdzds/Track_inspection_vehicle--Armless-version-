param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe',
 [ValidateSet('All','Release','Reverse')][string]$Case='All', [switch]$MeasureOnly, [switch]$Quick)
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
 $common=@('-std=c99','-Wall','-Wextra','-Werror','-DportTICK_PERIOD_MS=1U','-DAPP_SERVO_READONLY_TEST=0',
 '-ITests/mocks','-ITests','-IAPP','-IAPP/HMI','-IDevice','-IProtocol','-IProtocol/CAN','-IBSP/SERVO_PULSE','-IBSP/CAN','-IBSP/DEBUG_UART')
 if($Case -in @('All','Release')) {
  $upper=(Resolve-Path -LiteralPath '../F103_TEST/06_Upper_level_CAN_com_v1.1_low_speed_test').Path
  $exe=Join-Path $env:TEMP 'rail_lcd_warning_release.exe'
  $sources=@("$upper/Tests/test_lcd_warning_release.c","$upper/APP/can_upper_protocol.c","$upper/APP/can_upper_control.c","$upper/APP/can_upper_ui.c",
  'APP/app_can_status.c','APP/app_can_config.c','APP/app_can_control.c','APP/app_remote_servo_sync.c', 'APP/app_dual_axis_motion.c','APP/app_sync_params.c','APP/HMI/app_control_arbiter.c',
  'Protocol/CAN/can_protocol.c','Protocol/CAN/can_link.c','Protocol/CAN/can_upper.c')
  & $Compiler @common '-DAPP_CAN_CONTROL_ENABLED=1' '-DAPP_CAN_STOP_PARAMETERS_APPROVED=1' ('-I'+$upper+'/APP') @sources -lm -o $exe
  if($LASTEXITCODE){throw 'LCD warning release compile failed'}
  if($MeasureOnly){& $exe measure}else{& $exe}
  if($LASTEXITCODE){throw 'LCD warning release regression failed'}
 }
 if($Case -in @('All','Reverse')) {
  $axes=@(1,2,3); $syncs=@(0,1); if($Quick){$axes=@(3);$syncs=@(1)}
  foreach($axis in $axes){foreach($sync in $syncs){
   $exe=Join-Path $env:TEMP "rail_reverse_${axis}_${sync}.exe"
   & $Compiler @common '-DAPP_CAN_CONTROL_ENABLED=0' '-DAPP_CAN_STOP_PARAMETERS_APPROVED=0' "-DAPP_SERVO_QUERY_MODE=$axis" "-DAPP_SERVO_CONTROL_AXIS=$axis" "-DAPP_REMOTE_SERVO_SYNC_ENABLED=$sync" `
    Tests/test_remote_reversal_timing.c APP/app_remote_servo.c APP/app_remote_servo_sync.c APP/app_dual_axis_motion.c APP/app_sync_params.c APP/HMI/app_control_arbiter.c APP/app_config.c -lm -o $exe
   if($LASTEXITCODE){throw 'Reversal timing compile failed'}
   if($MeasureOnly){& $exe measure}else{& $exe}
   if($LASTEXITCODE){throw "Reversal timing regression failed axis=$axis sync=$sync"}
  }}
 }
} finally {Pop-Location}
