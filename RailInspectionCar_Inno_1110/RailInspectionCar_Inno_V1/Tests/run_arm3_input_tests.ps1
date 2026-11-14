param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
 $bin=Join-Path $env:TEMP 'test_arm3_inputs.exe'
 & $Compiler -std=c99 -Wall -Wextra -Werror -Wno-misleading-indentation -IAPP -IAPP/HMI -IProtocol/CAN Tests/test_arm3_inputs.c APP/app_arm3_inputs.c APP/HMI/app_hmi_arm3.c -o $bin
 if($LASTEXITCODE){throw 'ARM3 inputs compilation failed'}
 & $bin
 if($LASTEXITCODE){throw 'ARM3 inputs tests failed'}
} finally {Pop-Location}