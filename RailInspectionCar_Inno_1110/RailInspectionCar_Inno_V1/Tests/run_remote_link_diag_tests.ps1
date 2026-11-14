param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
 foreach ($mode in @(@(0,0),@(1,0),@(0,1),@(1,1))) {
  $enabled=$mode[0]; $sync=$mode[1]
  $exe=Join-Path $env:TEMP "remote_link_diag_${enabled}_${sync}.exe"
  & $Compiler -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 -std=c99 -Wall -Wextra -Werror `
   -DAPP_LINK_DIAG_TEST=1 "-DAPP_NRF_DIAGNOSTIC_ENABLED=$enabled" "-DAPP_REMOTE_SERVO_SYNC_DEBUG_ENABLED=$sync" `
   -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
   -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IBSP/SERVO_PULSE `
   Tests/test_remote_link_diag.c APP/app_remote_servo_sync.c APP/app_dual_axis_motion.c APP/app_sync_params.c APP/app_config.c APP/HMI/app_control_arbiter.c -o $exe
  if ($LASTEXITCODE) {throw "Remote diag compile failed enabled=$enabled sync=$sync"}
  & $exe
  if ($LASTEXITCODE) {throw "Remote diag regression failed enabled=$enabled sync=$sync"}
 }
 foreach ($enabled in @(0,1)) {
  $exe=Join-Path $env:TEMP "nrf_link_diag_$enabled.exe"
  & $Compiler -DAPP_UNPROTECTED_MOTION_TEST_ENABLED=1 -std=c99 -Wall -Wextra -Werror -DAPP_LINK_DIAG_TEST=1 "-DAPP_NRF_DIAGNOSTIC_ENABLED=$enabled" `
   -ITests/mocks -IAPP -IDevice -IBSP/NRF24L01 Tests/test_nrf_link_diag.c APP/app_nrf_remote.c APP/nrf_remote_protocol.c -o $exe
  if ($LASTEXITCODE) {throw 'NRF application diag compile failed'}
  & $exe
  if ($LASTEXITCODE) {throw 'NRF application diag failed'}
  $exe=Join-Path $env:TEMP "nrf_compact_diag_$enabled.exe"
  & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_LINK_DIAG_TEST=1 "-DAPP_NRF_DIAGNOSTIC_ENABLED=$enabled" `
   -ITests/nrf_mocks -ITests/mocks -IAPP -IBSP/NRF24L01 Tests/test_nrf_compact_diag.c BSP/NRF24L01/NRF24L01.c -o $exe
  if ($LASTEXITCODE) {throw 'NRF driver compact diag compile failed'}
  & $exe
  if ($LASTEXITCODE) {throw 'NRF driver compact diag failed'}
  $exe=Join-Path $env:TEMP "telemetry_compact_diag_$enabled.exe"
  & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_LINK_DIAG_TEST=1 "-DAPP_NRF_DIAGNOSTIC_ENABLED=$enabled" `
   -DAPP_CAN_CONTROL_ENABLED=0 -DAPP_CAN_STOP_PARAMETERS_APPROVED=0 `
   -ITests/mocks -IAPP -IDevice -IBSP/SYSTEM -IBSP/NRF24L01 Tests/test_telemetry_compact_diag.c APP/nrf_status_telemetry.c APP/nrf_remote_protocol.c APP/app_config.c -o $exe
  if ($LASTEXITCODE) {throw 'Telemetry compact diag compile failed'}
  & $exe
  if ($LASTEXITCODE) {throw 'Telemetry compact diag failed'}
 }
} finally {Pop-Location}
