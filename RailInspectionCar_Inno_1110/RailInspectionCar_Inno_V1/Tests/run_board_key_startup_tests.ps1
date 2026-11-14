param([string]$Compiler='D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    $source=Get-Content -LiteralPath 'APP/app_rtos_task.c' -Raw
    $task=[regex]::Match($source,'(?s)void key_task\(void \*argument\).*?(?=\r?\n// 状态任务)')
    if(!$task.Success){throw 'Production key_task boundary missing'}
    [IO.File]::WriteAllText((Join-Path $env:TEMP 'rail_board_key_task.inc'),$task.Value,[Text.UTF8Encoding]::new($false))
    $binary=Join-Path $env:TEMP 'rail_board_key_startup.exe'
    foreach($lcd in @(0,1)) {
        & $Compiler -std=c99 -Wall -Wextra -Werror -DAPP_LINK_DIAG_TEST=1 "-DAPP_CHASSIS_LCD_ENABLED=$lcd" `
            -ITests/board_key_mocks -ITests/mocks -IAPP -IDevice -IBSP/KEY -IProtocol -IAPP/HMI "-I$env:TEMP" `
            Tests/test_board_key_startup.c BSP/KEY/key.c -o $binary
        if($LASTEXITCODE){throw 'Board key startup fixture compile failed'}
        & $binary
        if($LASTEXITCODE){throw 'Board key startup fixture failed'}
    }
} finally {Pop-Location}
