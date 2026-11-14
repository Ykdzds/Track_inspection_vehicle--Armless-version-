param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe',[string]$Python='D:/Openmv_Python/python.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
 $exe=Join-Path $env:TEMP "arm3_joint_peer_$PID.exe"
 & $Compiler -std=c99 -Wall -Wextra -Werror -IAPP -IProtocol/CAN Tests/arm3_joint_peer.c APP/app_arm3.c Protocol/CAN/can_arm3.c Protocol/CAN/can_protocol.c -o $exe
 if($LASTEXITCODE){throw 'Joint C peer compile failed'}
 & $Python -B Tests/test_arm3_joint.py $exe
 if($LASTEXITCODE){throw 'Joint F407/RK business test failed'}
} finally {
 if($exe -and (Test-Path -LiteralPath $exe)){Remove-Item -LiteralPath $exe -Force}
 Pop-Location
}
