throw '29号工程请使用安装29号事件.ps1或实施29号导航.ps1；本历史脚本禁止执行。'
$ErrorActionPreference='Stop'
$base='D:\wtzn_sx_\同济大学-轨道巡检车\同济大学轨道巡检车协作资料'
$target=Join-Path $base '6.方案设计/21号车载屏重复K5跨源停车恢复最终烧录包_20261019'
$f4Root=Join-Path $base '5.工程代码/RailInspectionCar_Inno_V1'
$f4Hex=Join-Path $f4Root 'MDK-ARM/RailInspectionCar_V1/RailInspectionCar_V1.hex'

$delivery=Get-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/21号交付索引_20261019.json') -Raw -Encoding utf8|ConvertFrom-Json
if((Get-FileHash -LiteralPath $f4Hex).Hash -ne $delivery.F4Hex_SHA256){throw 'F4本轮固件哈希不匹配'}
foreach($entry in $delivery.Files){if((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $entry.Name)).Hash -ne $entry.SHA256){throw ('21号源文件已变化：'+$entry.Name)}}
foreach($folder in @('21号车载屏','F4主控','追溯','测试与核验')){New-Item -ItemType Directory -Path (Join-Path $target $folder) -Force|Out-Null}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot $delivery.BIN) -Destination (Join-Path $target '21号车载屏/17_轨道巡检车.pix.bin')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot $delivery.PIX) -Destination (Join-Path $target '追溯/17_轨道巡检车.pix')
Copy-Item -LiteralPath $f4Hex -Destination (Join-Path $target 'F4主控/RailInspectionCar_V1.hex')
foreach($entry in $delivery.Files){
    if($entry.Name -match '^运行核验/'){$destination=Join-Path $target ('测试与核验/'+(Split-Path $entry.Name -Leaf))}
    elseif($entry.Name -in @($delivery.PIX,$delivery.BIN)){continue}
    else{$destination=Join-Path $target ('追溯/'+$entry.Name)}
    New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force|Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $entry.Name) -Destination $destination
}
foreach($name in @('21号全部回归_20261019.log','21号全模式只读显示_20261019.log','21号重开核验执行_20261019.log','21号事件安装_20261019.log','21号交付索引_20261019.json')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('运行核验/'+$name)) -Destination (Join-Path $target ('测试与核验/'+$name))}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '烧录说明_21号.md') -Destination (Join-Path $target '烧录说明.md')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '现场验收记录模板_21号.md') -Destination (Join-Path $target '现场验收记录模板.md')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '21号重复K5修复说明_20261019.md') -Destination (Join-Path $target '21号重复K5修复说明.md')
foreach($relative in @('APP/app_remote_servo.c','APP/app_remote_servo.h','APP/HMI/app_hmi_control.c','Tests/test_core.c','Tests/test_hmi_control.c','Tests/test_hmi_remote_joint.c','Tests/run_hmi_remote_joint_tests.ps1','MDK-ARM/RailInspectionCar_V1.uvprojx')){
    $destination=Join-Path $target ('追溯/F4/'+$relative)
    New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force|Out-Null
    Copy-Item -LiteralPath (Join-Path $f4Root $relative) -Destination $destination
}
foreach($relative in @('MDK-ARM/RailInspectionCar_V1/RailInspectionCar_V1.map','MDK-ARM/RailInspectionCar_V1/RailInspectionCar_V1.htm')){Copy-Item -LiteralPath (Join-Path $f4Root $relative) -Destination (Join-Path $target ('测试与核验/'+(Split-Path $relative -Leaf)))}
foreach($name in @('21号F4全量入口阻塞_20261019.log','21号F4相关回归_20261019.log','21号F4其它回归_20261019.log','21号F4源码变化_20261019.json')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('运行核验/'+$name)) -Destination (Join-Path $target ('测试与核验/'+$name))}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '运行核验/20号编辑器自动备份轮转现场') -Destination (Join-Path $target '测试与核验/20号编辑器自动备份轮转现场') -Recurse -Force
$logs=Get-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/21号全部回归_20261019.log') -Raw -Encoding utf8
if($logs -notmatch 'fail 0' -or $logs -notmatch 'pass 227'){throw '全量227项回归记录不满足交付条件'}
$index=[ordered]@{Package=(Split-Path $target -Leaf);ScreenBin='21号车载屏/17_轨道巡检车.pix.bin';ScreenBin_Length=$delivery.BIN_Length;ScreenBin_SHA256=$delivery.BIN_SHA256;PIX='追溯/17_轨道巡检车.pix';PIX_SHA256=$delivery.PIX_SHA256;F4Hex='F4主控/RailInspectionCar_V1.hex';F4Hex_SHA256=$delivery.F4Hex_SHA256;F4ProductionModified=$true;F4DownloadRequired=$true;Baseline20Unchanged=$true;Baseline20Files=304;Regression='屏端227执行项全部通过；全模式只读36项；F4控制12配置、遥控K5专项20配置、真实服务联合六配置；12事件和18生命周期全文读回；Visual-PIX编译成功';KnownRegressionBlockers=@('主控总入口与CLI测试桩缺HAL_UART_RXEVENT_HT/TC','NRF总入口UART5 CubeMX既有配置检查失败');F4Compile='0 Error(s), 0 Warning(s)';PhysicalCommunicationVerified=$false;MotionVerified=$false}
$index|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $target '交付索引.json') -Encoding utf8
$checksums=Get-ChildItem -LiteralPath $target -Recurse -File|Where-Object Name -ne 'SHA256SUMS.txt'|Sort-Object FullName|ForEach-Object {(Get-FileHash -LiteralPath $_.FullName).Hash+'  '+[IO.Path]::GetRelativePath($target,$_.FullName).Replace('\','/')}
$checksums|Set-Content -LiteralPath (Join-Path $target 'SHA256SUMS.txt') -Encoding utf8
Write-Output $target
