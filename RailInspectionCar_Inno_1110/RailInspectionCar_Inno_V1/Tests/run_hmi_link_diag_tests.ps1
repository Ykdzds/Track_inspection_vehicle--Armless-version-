param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    foreach($enabled in @(0,1)) {
        $binary=Join-Path $env:TEMP ('rail_hmi_link_diag_'+$enabled+'.exe')
        $arguments=@('-std=c99','-Wall','-Wextra','-Werror',
            '-DAPP_HMI_LINK_DIAG_TEST=1',"-DAPP_HMI_LINK_DIAG_ENABLED=$enabled",
            '-DAPP_NRF_DIAGNOSTIC_ENABLED=0','-DAPP_CAN_SERVO_DIAG_ENABLED=0',
            '-DAPP_CAN_ARM_DIAG_ENABLED=0','-DAPP_SERVO_RS485_DIAG_ENABLED=0',
            '-DAPP_CAN_DEBUG_ENABLED=0','-DAPP_REMOTE_SERVO_SYNC_DEBUG_ENABLED=0',
            '-ITests/can_mocks','-ITests/mocks','-IAPP','-IAPP/HMI','-IDevice','-IProtocol',
            '-IProtocol/CAN','-IBSP/SYSTEM','-IBSP/HMI_UART','-IBSP/DEBUG_UART','-IBSP/SPI_LCD',
            'Tests/test_hmi_link_diag.c','APP/HMI/app_hmi_link_diag.c','-o',$binary)
        & $Compiler @arguments
        if($LASTEXITCODE){throw "HMI link diagnostics compile failed: enabled=$enabled"}
        & $binary
        if($LASTEXITCODE){throw "HMI link diagnostics tests failed: enabled=$enabled"}
    }
} finally {Pop-Location}
