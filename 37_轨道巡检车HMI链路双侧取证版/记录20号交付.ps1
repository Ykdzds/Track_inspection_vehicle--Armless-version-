throw '29号工程请使用安装29号事件.ps1或实施29号导航.ps1；本历史脚本禁止执行。'
$ErrorActionPreference='Stop'
$baselineRoot=Join-Path (Split-Path $PSScriptRoot -Parent) '19_轨道巡检车通信状态稳定修复版'
$baseline=Get-Content -LiteralPath (Join-Path $PSScriptRoot '19号基线SHA256_20261019.json') -Raw -Encoding utf8|ConvertFrom-Json
foreach($entry in $baseline){
    $file=Join-Path $baselineRoot $entry.Path
    if(!(Test-Path -LiteralPath $file) -or (Get-Item -LiteralPath $file).Length -ne $entry.Length -or (Get-FileHash -LiteralPath $file).Hash -ne $entry.SHA256){throw ('19号基线变化：'+$entry.Path)}
}
$names=@('17_轨道巡检车.pix','17_轨道巡检车.pix.bin','HMI13事务服务模板.txt','HMI13事务服务.txt','HMI13状态索引.json','HMI13界面刷新.txt','全局计龄刷新.txt','Default模式中断.txt','Modbus中断只读解析.txt','mod.txt','生成同步事务服务.cjs','hmi20-cross-source.test.cjs','config-artifact.test.cjs','r2-simulator.cjs','pix-ui.ps1','安装13号事件.ps1','核验20号工程.ps1','记录20号交付.ps1','打包20号交付.ps1','19号基线SHA256_20261019.json','20号跨源停车修复说明_20261019.md','烧录说明_20号.md','现场验收记录模板_20号.md')
$names+=Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot '事件') -File|ForEach-Object {'事件/'+$_.Name}
$names+=@('运行核验/20号配置重开核验_20261019.json','运行核验/20号重开事件绑定_20261019.json','运行核验/20号全页生命周期_20261019.json','运行核验/20号VisualPIX编译_20261019.txt','运行核验/20号F4控制矩阵_20261019.log','运行核验/20号F4遥控K5矩阵_20261019.log')
$files=@($names|ForEach-Object {
    $file=Join-Path $PSScriptRoot $_
    [ordered]@{Name=$_;Length=(Get-Item -LiteralPath $file).Length;SHA256=(Get-FileHash -LiteralPath $file).Hash}
})
$pix=$files|Where-Object Name -eq '17_轨道巡检车.pix'
$bin=$files|Where-Object Name -eq '17_轨道巡检车.pix.bin'
$record=[ordered]@{Project=$PSScriptRoot;Mode='Default';PIX=$pix.Name;PIX_SHA256=$pix.SHA256;BIN=$bin.Name;BIN_Length=$bin.Length;BIN_SHA256=$bin.SHA256;Files=$files;Baseline19Unchanged=$true;Baseline19Files=$baseline.Count;F4ProductionModified=$false;F4Hex_SHA256='15D78E1FE1ACC6264C09B0DCE1798A37C92EE3595E028821AB7F83777C8F3E28';PhysicalCommunicationVerified=$false;MotionVerified=$false}
$record|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/20号交付索引_20261019.json') -Encoding utf8
Write-Output ('20号交付索引已生成；BIN SHA256='+$bin.SHA256)
