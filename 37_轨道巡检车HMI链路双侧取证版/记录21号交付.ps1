throw '29号工程请使用安装29号事件.ps1或实施29号导航.ps1；本历史脚本禁止执行。'
$ErrorActionPreference='Stop'
$f4Root='D:\wtzn_sx_\同济大学-轨道巡检车\同济大学轨道巡检车协作资料\5.工程代码\RailInspectionCar_Inno_V1'
$baselineRoot=Join-Path (Split-Path $PSScriptRoot -Parent) '20_轨道巡检车UART5跨源停车恢复修复版'
$baseline=Get-Content -LiteralPath (Join-Path $PSScriptRoot '20号基线SHA256_20261019.json') -Raw -Encoding utf8|ConvertFrom-Json
foreach($entry in $baseline){
    $file=Join-Path $baselineRoot $entry.Path
    if(!(Test-Path -LiteralPath $file) -or (Get-Item -LiteralPath $file).Length -ne $entry.Length -or (Get-FileHash -LiteralPath $file).Hash -ne $entry.SHA256){throw ('20号基线变化：'+$entry.Path)}
}
$names=@('17_轨道巡检车.pix','17_轨道巡检车.pix.bin','HMI13事务服务模板.txt','HMI13事务服务.txt','HMI13状态索引.json','HMI13界面刷新.txt','全局计龄刷新.txt','Default模式中断.txt','Modbus中断只读解析.txt','mod.txt','生成同步事务服务.cjs','hmi20-cross-source.test.cjs','hmi21-repeat-k5.test.cjs','20号基线SHA256_20261019.json','F4修复前基线SHA256_20261019.json','config-artifact.test.cjs','r2-simulator.cjs','pix-ui.ps1','安装13号事件.ps1','核验21号工程.ps1','记录21号交付.ps1','打包21号交付.ps1','19号基线SHA256_20261019.json','21号重复K5修复说明_20261019.md','烧录说明_21号.md','现场验收记录模板_21号.md')
$names+=Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot '事件') -File|ForEach-Object {'事件/'+$_.Name}
$names+=@('运行核验/21号配置重开核验_20261019.json','运行核验/21号重开事件绑定_20261019.json','运行核验/21号全页生命周期_20261019.json','运行核验/21号VisualPIX编译_20261019.txt','运行核验/21号F4控制矩阵_20261019.log','运行核验/21号F4遥控K5矩阵_20261019.log','运行核验/21号F4联合矩阵_20261019.log','运行核验/21号F4旧版失败复现_20261019.log','运行核验/21号F4编译_20261019.txt','运行核验/21号F4栈预算_20261019.log')
$files=@($names|ForEach-Object {
    $file=Join-Path $PSScriptRoot $_
    [ordered]@{Name=$_;Length=(Get-Item -LiteralPath $file).Length;SHA256=(Get-FileHash -LiteralPath $file).Hash}
})
$pix=$files|Where-Object Name -eq '17_轨道巡检车.pix'
$bin=$files|Where-Object Name -eq '17_轨道巡检车.pix.bin'
$record=[ordered]@{Project=$PSScriptRoot;Mode='Default';PIX=$pix.Name;PIX_SHA256=$pix.SHA256;BIN=$bin.Name;BIN_Length=$bin.Length;BIN_SHA256=$bin.SHA256;Files=$files;Baseline20Unchanged=$true;Baseline20Files=$baseline.Count;F4ProductionModified=$true;F4Hex_SHA256=(Get-FileHash -LiteralPath (Join-Path $f4Root 'MDK-ARM/RailInspectionCar_V1/RailInspectionCar_V1.hex')).Hash;PhysicalCommunicationVerified=$false;MotionVerified=$false}
$record|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/21号交付索引_20261019.json') -Encoding utf8
Write-Output ('21号交付索引已生成；BIN SHA256='+$bin.SHA256)
