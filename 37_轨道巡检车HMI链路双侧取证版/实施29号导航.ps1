throw '29号按钮已完成，禁止重复创建；重绑事件使用安装29号事件.ps1。'
$ErrorActionPreference='Stop'
if((Split-Path $PSScriptRoot -Leaf) -ne '29_轨道巡检车跨页运行与伺服快捷导航版'){throw '只能操作29号副本'}
$helper=Join-Path $PSScriptRoot 'pix-ui.ps1'
. $helper -Action SelectPage -Name P2_Diag
foreach($targetPage in @('P3_Position','P4_Info','P5_Alarm','P6_Sync','P7_System_Ext')){
 SelectPage P2_Diag
 SelectControl servoEntry
 $v=@(Props|ForEach-Object {$_.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value})
 if($v[2] -ne 'servoEntry' -or $v[4] -ne '832' -or $v[5] -ne '554'){throw '源按钮不正确'}
 # Canvas origin (483,269), UI scale 1.5; focus the button, not the property editor.
 Click 1844 1130
 [System.Windows.Forms.SendKeys]::SendWait('^c')
 SelectPage $targetPage
 Click 1844 1130
 [System.Windows.Forms.SendKeys]::SendWait('^v')
 Start-Sleep -Milliseconds 200
 $v=@(Props|ForEach-Object {$_.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value})
 if($v.Count -lt 28 -or $v[14] -ne '伺服控制'){throw "未复制按钮 $targetPage"}
 SetRow 2 servoEntry
 SetRow 4 832; SetRow 5 554; SetRow 6 150; SetRow 7 40
 SetRow 14 '伺服控制'
 Write-Output "新增按钮 $targetPage/servoEntry"
}
& $helper -Action Keys -Value '^s'

