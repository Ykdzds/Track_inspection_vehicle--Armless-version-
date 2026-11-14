param(
    [string]$F4Root = 'D:\wtzn_sx_\同济大学-轨道巡检车\同济大学轨道巡检车协作资料\5.工程代码\RailInspectionCar_Inno_V1',
    [switch]$SkipF4
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$indexPath = Join-Path $root '运行核验\17号最新修复交付版本.json'
$index = Get-Content -LiteralPath $indexPath -Raw -Encoding UTF8 | ConvertFrom-Json

function Resolve-Artifact([string]$RelativePath, [string]$BaseRoot) {
    if ([System.IO.Path]::IsPathRooted($RelativePath)) { return $RelativePath }
    $screenPath = Join-Path $root ($RelativePath -replace '/', '\')
    if (Test-Path -LiteralPath $screenPath) { return $screenPath }
    return (Join-Path $BaseRoot ($RelativePath -replace '/', '\'))
}

function Assert-Hash([string]$RelativePath, [string]$ExpectedHash, [string]$Label) {
    $path = Resolve-Artifact $RelativePath $root
    if (!(Test-Path -LiteralPath $path)) { throw "$Label文件不存在：$RelativePath" }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $ExpectedHash) { throw "$Label哈希不匹配：实际=$actual 期望=$ExpectedHash" }
    Write-Output "PASS $Label：$actual"
}

Assert-Hash $index.PIX $index.PIX_SHA256 'PIX'
Assert-Hash $index.BIN $index.BIN_SHA256 'BIN'
Assert-Hash $index.Service $index.Service_SHA256 '事务服务'
Assert-Hash $index.EstopEvent $index.EstopEvent_SHA256 '急停事件'

$service = Join-Path $root ($index.Service -replace '/', '\')
$serviceBefore = (Get-FileHash -LiteralPath $service -Algorithm SHA256).Hash
& node (Join-Path $root '生成同步事务服务.cjs')
if ($LASTEXITCODE -ne 0) { throw '事务服务生成器执行失败' }
$serviceAfter = (Get-FileHash -LiteralPath $service -Algorithm SHA256).Hash
if ($serviceBefore -ne $serviceAfter) { throw '事务服务不是生成器唯一产物，禁止下载' }
Write-Output 'PASS 事务服务生成源一致'

if ($index.EventBindings -ne 12) { throw "事件绑定数量异常：$($index.EventBindings)" }
if ($index.StateArrayLength -ne 161) { throw "状态数组长度异常：$($index.StateArrayLength)" }
if ($index.PhysicalCommunicationVerified -or $index.MotionVerified) {
    throw '交付索引错误地宣称实物已通过，请保留未验证状态直到现场证据完成'
}
Write-Output 'PASS 交付索引仍保留实物未验证边界'

if (!$SkipF4) {
    $f4 = Resolve-Artifact $index.F4Firmware $F4Root
    if (!(Test-Path -LiteralPath $f4)) { throw "F4固件不存在：$($index.F4Firmware)；F4Root=$F4Root" }
    $f4Hash = (Get-FileHash -LiteralPath $f4 -Algorithm SHA256).Hash
    if ($f4Hash -ne $index.F4Firmware_SHA256) { throw "F4固件哈希不匹配：实际=$f4Hash 期望=$($index.F4Firmware_SHA256)" }
    Write-Output "PASS F4 HEX：$f4Hash"
}

Write-Output '交付核验完成：可进入现场下载与分阶段验收，不能跳过物理证据。'
