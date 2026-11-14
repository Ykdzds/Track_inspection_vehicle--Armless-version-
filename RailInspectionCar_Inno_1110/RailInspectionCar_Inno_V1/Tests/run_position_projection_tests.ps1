param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
 foreach ($axis in @(1,2,3)) {
  foreach ($monitor in @(0,1)) {
   foreach ($readonly in @(0,1)) {
    $exe=Join-Path $env:TEMP "position_monitor_${axis}_${monitor}_${readonly}.exe"
    $can=1-$readonly
    & $Compiler -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 -std=c99 -Wall -Wextra -Werror `
     "-DAPP_SERVO_QUERY_MODE=$axis" "-DAPP_SERVO_CONTROL_AXIS=$axis" `
     "-DAPP_POSITION_MONITOR_ENABLED=$monitor" "-DAPP_SERVO_READONLY_TEST=$readonly" `
     -DAPP_CAN_ENABLED=1 "-DAPP_CAN_CONTROL_ENABLED=$can" "-DAPP_CAN_STOP_PARAMETERS_APPROVED=$can" `
     -ITests/mocks -IAPP -IDevice -IBSP/SERVO_PULSE Tests/test_position_monitor.c APP/app_config.c -lm -o $exe
    if($LASTEXITCODE){throw "Position monitor compile $axis/$monitor/$readonly"}
    & $exe
    if($LASTEXITCODE){throw "Position monitor regression $axis/$monitor/$readonly"}
   }
  }
  $exe=Join-Path $env:TEMP "position_projection_$axis.exe"
  & $Compiler -std=c99 -Wall -Wextra -Werror -ffunction-sections -fdata-sections '-Wl,--gc-sections' `
   -DAPP_SERVO_PULSE_ENABLED=0 -DAPP_SERVO_READONLY_TEST=0 "-DAPP_SERVO_QUERY_MODE=$axis" "-DAPP_SERVO_CONTROL_AXIS=$axis" `
   -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
   -ITests/mocks -IAPP -IDevice -IProtocol -IBSP/RS485 -IBSP/SERVO_PULSE -ICore/Inc `
   Tests/test_position_projection.c Tests/aimtor_position_projection_test_unit.c APP/app_config.c APP/motion_control.c `
   APP/servo_debug_control.c BSP/SERVO_PULSE/servo_pulse.c Protocol/modbus_rtu.c -lm -o $exe
  if($LASTEXITCODE){throw "Position projection compile $axis"}
  & $exe
  if($LASTEXITCODE){throw "Position projection regression $axis"}
 }
} finally {Pop-Location}
