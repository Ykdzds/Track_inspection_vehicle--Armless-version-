param(
    [string]$Compiler = 'D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe',
    [string]$F4Root = (Join-Path $PSScriptRoot '..\..\..\RailInspectionCar_Inno_V1'),
    [ValidateSet(0,1)][int]$DebugEnabled = 0
)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$F4Root = (Resolve-Path -LiteralPath $F4Root).Path
$output = Join-Path $project 'Verification'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$unit = Join-Path $env:TEMP ('f103_unit_' + (Split-Path $project -Leaf) + '.exe')
$common = @(('-DUPPER_USART1_DEBUG_ENABLE='+$DebugEnabled),'-std=c99','-Wall','-Wextra','-Werror',('-I'+(Join-Path $project 'APP')))
$sources = @('can_upper_protocol.c','can_upper_control.c','can_upper_ui.c') | ForEach-Object { Join-Path $project ('APP\'+$_) }
& $Compiler @common (Join-Path $project 'Tests\test_f103_unit.c') @sources -o $unit
if ($LASTEXITCODE -ne 0) { throw 'F103单元测试编译失败' }
& $unit 2>&1 | Tee-Object -FilePath (Join-Path $output 'unit.log')
if ($LASTEXITCODE -ne 0) { throw 'F103单元测试失败' }
$includes = @('Tests\mocks','Tests','APP','APP\HMI','Device','Protocol','Protocol\CAN','BSP\CAN','BSP\DEBUG_UART') | ForEach-Object { '-I'+(Join-Path $F4Root $_) }
$motion = (Get-Content -LiteralPath (Join-Path $project 'APP\can_upper_config.h') -Raw) -match 'UPPER_CAN_MOTION_ENABLED 1U'
$pair = Join-Path $env:TEMP ('f103_pair_' + (Split-Path $project -Leaf) + '.exe')
$defs = @('-DportTICK_PERIOD_MS=1U',('-DAPP_CAN_CONTROL_ENABLED='+[int]$motion),('-DAPP_CAN_STOP_PARAMETERS_APPROVED='+[int]$motion))
$lower = @('APP\app_can_status.c','APP\app_can_config.c','Protocol\CAN\can_protocol.c','Protocol\CAN\can_link.c','Protocol\CAN\can_upper.c')
if ($motion) {
    $test = Join-Path $project 'Tests\test_f103_f4_pair.c'
    $lower += @('APP\app_can_control.c','APP\app_remote_servo_sync.c','APP\app_dual_axis_motion.c','APP\app_sync_params.c','APP\HMI\app_control_arbiter.c')
} else {
    $test = Join-Path $project 'Tests\test_f103_readonly_pair.c'
    $sources = @(Join-Path $project 'APP\can_upper_protocol.c')
}
$lower = $lower | ForEach-Object { Join-Path $F4Root $_ }
& $Compiler @common @defs @includes $test @sources @lower -lm -o $pair
if ($LASTEXITCODE -ne 0) { throw '双端联合回归编译失败' }
& $pair 2>&1 | Tee-Object -FilePath (Join-Path $output 'pair.log')
if ($LASTEXITCODE -ne 0) { throw '双端联合回归失败' }
$logExe = Join-Path $env:TEMP ('f103_logs_' + (Split-Path $project -Leaf) + '.exe')
$logPath = Join-Path $output 'logs.log'
Set-Content -LiteralPath $logPath -Value '' -Encoding utf8
$logSources = @('can_upper_protocol.c','can_upper_control.c','can_upper_ui.c') | ForEach-Object { Join-Path $project ('APP\'+$_) }
foreach ($level in 0,1,2) {
    & $Compiler @common '-UUPPER_USART1_DEBUG_ENABLE' '-Wno-unused-function' '-ffunction-sections' '-fdata-sections' '-Wl,--gc-sections' ('-DUPPER_LOG_LEVEL='+$level) '-DUPPER_USART1_DEBUG_ENABLE=1' ('-I'+(Join-Path $project 'Tests\log_mocks')) (Join-Path $project 'Tests\test_f103_logs.c') @logSources -o $logExe
    if ($LASTEXITCODE -ne 0) { throw '日志任务测试编译失败' }
    & $logExe 2>&1 | Tee-Object -FilePath $logPath -Append
    if ($LASTEXITCODE -ne 0) { throw '日志任务测试失败' }
}
Write-Output 'PASS F103软件回归；不代表真实CAN和机械验收'
$bus = Join-Path $env:TEMP 'f103_bus.exe'
$busObject = Join-Path $env:TEMP 'f103_bus_bridge.o'
$busIncludes = @('Tests\bus_mocks','Core\Inc','APP') | ForEach-Object { '-I'+(Join-Path $project $_) }
& $Compiler '-std=c99' '-Wall' '-Wextra' '-Werror' @busIncludes (Join-Path $project 'Tests\test_f103_bus.c') -o $bus
if ($LASTEXITCODE -ne 0) { throw 'TXFIX bus compile failed' }
& $bus 2>&1 | Tee-Object -FilePath (Join-Path $output 'TXFIX_bus.log')
if ($LASTEXITCODE -ne 0) { throw 'TXFIX bus tests failed' }
& $Compiler '-std=c99' '-Wall' '-Wextra' '-Werror' '-DF103_BUS_BRIDGE' @busIncludes -c (Join-Path $project 'Tests\test_f103_bus.c') -o $busObject
if ($LASTEXITCODE -ne 0) { throw 'TXFIX bridge compile failed' }
& $Compiler @common @defs '-DF103_PAIR_REAL_BUS' @includes $test @sources @lower $busObject -lm -o $pair
if ($LASTEXITCODE -ne 0) { throw 'TXFIX real bus pair compile failed' }
& $pair 2>&1 | Tee-Object -FilePath (Join-Path $output 'TXFIX_real_bus_pair.log')
if ($LASTEXITCODE -ne 0) { throw 'TXFIX real bus pair tests failed' }
foreach ($case in 'auth_repro','auth_prepare','speed400','txfix','k5_recovery','stop_verify') {
    $exe = Join-Path $env:TEMP ('f103_' + $case + '.exe')
    & $Compiler @common (Join-Path $project ('Tests\test_f103_' + $case + '.c')) @logSources -o $exe
    if ($LASTEXITCODE -ne 0) { throw ('AUTHFIX compile failed: ' + $case) }
    & $exe 2>&1 | Tee-Object -FilePath (Join-Path $output ($case + '.log'))
    if ($LASTEXITCODE -ne 0) { throw ('AUTHFIX regression failed: ' + $case) }
}

& (Join-Path $PSScriptRoot 'run_oled_tests.ps1') -Compiler $Compiler
