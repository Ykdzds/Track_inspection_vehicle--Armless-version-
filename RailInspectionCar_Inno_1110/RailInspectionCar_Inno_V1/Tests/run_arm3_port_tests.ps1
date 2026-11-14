param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
 $bin=Join-Path $env:TEMP "test_arm3_port_$PID.exe"
 foreach($profile in @(0,1)) {
  & $Compiler -std=c99 -Wall -Wextra -Werror "-DAPP_ARM3_PROFILE_ENABLED=$profile" `
   -ITests/arm3_port_mocks -ITests/mocks -IAPP -IAPP/HMI -IDevice -IProtocol -IProtocol/CAN -IBSP/CAN -IBSP/DEBUG_UART -IBSP/NRF24L01 `
   Tests/test_arm3_port.c APP/app_arm3_port.c -o $bin
  if($LASTEXITCODE){throw 'ARM3 actual port compilation failed'}
  & $bin
  if($LASTEXITCODE){throw 'ARM3 actual port tests failed'}
 }
} finally {
 if($bin -and (Test-Path -LiteralPath $bin)){Remove-Item -LiteralPath $bin -Force}
 Pop-Location
}
