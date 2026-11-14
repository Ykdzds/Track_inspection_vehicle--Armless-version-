param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$upper=Join-Path $root '..\F103_TEST\06_Upper_level_CAN_com_v1.1_low_speed_test\APP'
$exe=Join-Path $env:TEMP 'rail_can_async_joint.exe'
Push-Location $root
try {
 & $Compiler -std=c99 -Wall -Wextra -Werror -ffunction-sections -fdata-sections `
  -DportTICK_PERIOD_MS=1U -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 -DUPPER_USART1_DEBUG_ENABLE=0 `
  -ITests/can_mocks -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IProtocol/CAN -IBSP/CAN -IBSP/DEBUG_UART -IBSP/NRF24L01 "-I$upper" `
  Tests/test_can_async_joint.c APP/app_can_status.c APP/app_can_config.c APP/app_can_control.c APP/app_dual_axis_motion.c APP/app_sync_params.c APP/app_remote_servo_sync.c `
  APP/app_arm3.c APP/app_arm3_inputs.c APP/HMI/app_hmi_arm3.c Protocol/CAN/can_arm3.c `
  Protocol/CAN/can_protocol.c Protocol/CAN/can_link.c Protocol/CAN/can_upper.c Protocol/CAN/can_arm.c `
  (Join-Path $upper 'can_upper_protocol.c') (Join-Path $upper 'can_upper_control.c') '-Wl,--gc-sections' -lm -o $exe
 if($LASTEXITCODE -ne 0){throw 'asynchronous CAN joint compile failed'}
 & $exe
 if($LASTEXITCODE -ne 0){throw 'asynchronous CAN joint regression failed'}
}finally{Pop-Location}
