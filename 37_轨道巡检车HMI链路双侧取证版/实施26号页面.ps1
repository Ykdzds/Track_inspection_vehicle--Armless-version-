throw '29号工程请使用安装29号事件.ps1或实施29号导航.ps1；本历史脚本禁止执行。'
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'pix-ui.ps1') -Action SelectPage -Name P8_ServoControl
throw '本次页面创建已经完成。禁止重复创建控件；请直接打开17_轨道巡检车.pix，脚本重装使用安装26号事件.ps1，读回使用核验26号工程.ps1。'
function CopyControl($source,$name,$text){
 SelectControl $source
 # Focus the page canvas before copy, to avoid copying property-editor text.
 $canvas=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Custom -and $_.Current.Name -eq 'P8_ServoControl'}|Select-Object -First 1
 $v=@(Props|ForEach-Object {$_.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value});Click ([int](487+([double]$v[4]+[double]$v[6]/2)*1.5)) ([int](239+([double]$v[5]+[double]$v[7]/2)*1.5))
 [System.Windows.Forms.SendKeys]::SendWait('^c')
 [System.Windows.Forms.SendKeys]::SendWait('^v')
 Start-Sleep -Milliseconds 200
 SetRow 2 $name;SetRow 14 $text
}

CopyControl 'linkStatus' 'recoveryDetail' '恢复详情：--'
CopyControl 'estop' 'recoveryButton' '故障恢复'
$rows=@(
 @('controlTitle',24,0,976,36),@('linkStatus',24,36,976,36),
 @('ownerStatus',24,72,460,36),@('axisMode',520,72,480,36),
 @('leftActual',24,108,460,36),@('rightActual',520,108,480,36),
 @('leftTarget',24,144,460,36),@('rightTarget',520,144,480,36),
 @('leftOutput',24,180,460,36),@('rightOutput',520,180,480,36),
 @('leftEnable',24,216,460,36),@('rightEnable',520,216,480,36),
 @('commonTarget',24,252,976,36),@('rpmSlider',24,288,976,40),
 @('authorize',24,336,230,44),@('estop',270,336,230,44),
 @('recoveryButton',516,336,230,44),@('backHome',762,336,238,44),
 @('authorityStatus',24,384,976,36),@('stopStatus',24,420,976,36),
 @('recoveryStatus',24,456,976,36),@('recoveryDetail',24,492,976,36),
 @('errorStatus',24,528,976,36),@('safetyNote',24,564,976,36)
)
foreach($r in $rows){SelectControl $r[0];SetRow 4 $r[1];SetRow 5 $r[2];SetRow 6 $r[3];SetRow 7 $r[4];Write-Output ('已布局 '+$r[0])}
SelectControl 'controlTitle';SetRow 14 '伺服控制 / 26号安全停车与恢复'
SelectControl 'h13';SetRow 5 180
SelectControl 'h13Status';SetRow 5 96
[System.Windows.Forms.SendKeys]::SendWait('^s')
