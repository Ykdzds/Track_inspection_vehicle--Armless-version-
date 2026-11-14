param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    $switches=@('APP_HMI_LINK_DIAG_ENABLED','APP_NRF_DIAGNOSTIC_ENABLED','APP_CAN_SERVO_DIAG_ENABLED','APP_CAN_ARM_DIAG_ENABLED','APP_SERVO_RS485_DIAG_ENABLED')
    foreach($mask in 0..32) {
        $binary=Join-Path $env:TEMP ('rail_link5_'+$mask+'.exe')
        $arguments=@('-std=c99','-Wall','-Wextra','-Werror','-DAPP_LINK_DIAG_TEST=1',
            '-ITests/can_mocks','-ITests/mocks','-IAPP','-IAPP/HMI','-IDevice','-IProtocol',
            '-IProtocol/CAN','-IBSP/SYSTEM','-IBSP/HMI_UART','-IBSP/DEBUG_UART','-IBSP/SPI_LCD')
        for($i=0;$i -lt 5;$i++) {$arguments+='-D'+$switches[$i]+'='+[int](($mask -eq 32) -or (($mask -band (1 -shl $i)) -ne 0))}
        if($mask -eq 32) {$arguments+='-DAPP_CAN_DEBUG_ENABLED=1';$arguments+='-DAPP_REMOTE_SERVO_SYNC_DEBUG_ENABLED=1'}
        $arguments+=@('Tests/test_link_diag.c','APP/HMI/app_hmi_link_diag.c','-o',$binary)
        & $Compiler @arguments
        if($LASTEXITCODE){throw "Link5 compile failed mask=$mask"}
        & $binary
        if($LASTEXITCODE){throw "Link5 test failed mask=$mask"}
        Write-Output "PASS configuration mask=$mask"
    }
} finally {Pop-Location}
