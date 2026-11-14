throw '29号工程请使用安装29号事件.ps1或实施29号导航.ps1；本历史脚本禁止执行。'
$ErrorActionPreference='Stop'
$base='D:\wtzn_sx_\同济大学-轨道巡检车\同济大学轨道巡检车协作资料'
$target=Join-Path $base '6.方案设计/20号车载屏跨源停车恢复修复最终烧录包_20261019'
$f4Hex=Join-Path $base '6.方案设计/19号车载屏通信状态稳定修复最终烧录包_20261018/F4主控/RailInspectionCar_V1.hex'
if((Get-FileHash -LiteralPath $f4Hex).Hash -ne '15D78E1FE1ACC6264C09B0DCE1798A37C92EE3595E028821AB7F83777C8F3E28'){throw 'F4归档哈希不匹配'}
$delivery=Get-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/20号交付索引_20261019.json') -Raw -Encoding utf8|ConvertFrom-Json
foreach($entry in $delivery.Files){if((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $entry.Name)).Hash -ne $entry.SHA256){throw ('20号源文件已变化：'+$entry.Name)}}
foreach($folder in @('20号车载屏','F4主控','追溯','测试与核验')){New-Item -ItemType Directory -Path (Join-Path $target $folder) -Force|Out-Null}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot $delivery.BIN) -Destination (Join-Path $target '20号车载屏/17_轨道巡检车.pix.bin')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot $delivery.PIX) -Destination (Join-Path $target '追溯/17_轨道巡检车.pix')
Copy-Item -LiteralPath $f4Hex -Destination (Join-Path $target 'F4主控/RailInspectionCar_V1.hex')
foreach($entry in $delivery.Files){
    if($entry.Name -match '^运行核验/'){$destination=Join-Path $target ('测试与核验/'+(Split-Path $entry.Name -Leaf))}
    elseif($entry.Name -in @($delivery.PIX,$delivery.BIN)){continue}
    else{$destination=Join-Path $target ('追溯/'+$entry.Name)}
    New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force|Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $entry.Name) -Destination $destination
}
foreach($name in @('20号全部回归_20261019.log','20号全模式只读显示_20261019.log','20号重开核验执行_20261019.log','20号事件安装_20261019.log','20号交付索引_20261019.json')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('运行核验/'+$name)) -Destination (Join-Path $target ('测试与核验/'+$name))}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '烧录说明_20号.md') -Destination (Join-Path $target '烧录说明.md')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '现场验收记录模板_20号.md') -Destination (Join-Path $target '现场验收记录模板.md')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '20号跨源停车修复说明_20261019.md') -Destination (Join-Path $target '20号跨源停车修复说明.md')
$testFile=Join-Path $base '5.工程代码/RailInspectionCar_Inno_V1/Tests/test_hmi_control.c'
Copy-Item -LiteralPath $testFile -Destination (Join-Path $target '追溯/test_hmi_control.c')
$logs=Get-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/20号全部回归_20261019.log') -Raw -Encoding utf8
if($logs -notmatch 'fail 0' -or $logs -notmatch 'pass 208'){throw '全量208项回归记录不满足交付条件'}
$index=[ordered]@{Package=(Split-Path $target -Leaf);ScreenBin='20号车载屏/17_轨道巡检车.pix.bin';ScreenBin_Length=$delivery.BIN_Length;ScreenBin_SHA256=$delivery.BIN_SHA256;PIX='追溯/17_轨道巡检车.pix';PIX_SHA256=$delivery.PIX_SHA256;F4Hex='F4主控/RailInspectionCar_V1.hex';F4Hex_SHA256=$delivery.F4Hex_SHA256;F4ProductionModified=$false;F4DownloadRequired=$false;Baseline19Unchanged=$true;Baseline19Files=281;Regression='屏端208执行项全部通过；全模式只读36项；F4控制12配置、遥控K5专项20配置；12事件和18生命周期全文读回；Visual-PIX编译成功';PhysicalCommunicationVerified=$false;MotionVerified=$false}
$index|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $target '交付索引.json') -Encoding utf8
$checksums=Get-ChildItem -LiteralPath $target -Recurse -File|Where-Object Name -ne 'SHA256SUMS.txt'|Sort-Object FullName|ForEach-Object {(Get-FileHash -LiteralPath $_.FullName).Hash+'  '+[IO.Path]::GetRelativePath($target,$_.FullName).Replace('\','/')}
$checksums|Set-Content -LiteralPath (Join-Path $target 'SHA256SUMS.txt') -Encoding utf8
Write-Output $target
