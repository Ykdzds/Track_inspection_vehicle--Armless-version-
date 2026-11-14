$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'pix-ui.ps1') -Action SelectPage -Name P8_ServoControl
SelectControl 'linkStatus'
$fields=@(
    @('ownerStatus',24,98,460,36,'控制源：未知'),
    @('axisMode',520,98,480,36,'执行轴：主控决定'),
    @('leftActual',24,146,460,40,'左轴实际：-- rpm'),
    @('rightActual',520,146,480,40,'右轴实际：-- rpm'),
    @('leftTarget',24,192,460,36,'最终目标：-- rpm'),
    @('rightTarget',520,192,480,36,'最终目标：-- rpm'),
    @('leftOutput',24,234,460,36,'执行输出：-- rpm'),
    @('rightOutput',520,234,480,36,'执行输出：-- rpm'),
    @('leftEnable',24,276,460,36,'使能反馈：未知'),
    @('rightEnable',520,276,480,36,'使能反馈：未知'),
    @('commonTarget',24,322,976,36,'设定：0 / 接受：-- rpm'),
    @('authorityStatus',24,438,976,36,'未授权'),
    @('stopStatus',24,482,976,36,'停车阶段：无'),
    @('errorStatus',24,524,976,36,'错误：无'),
    @('safetyNote',24,565,976,32,'软件受控停车，非硬件急停/STO')
)
foreach($field in $fields){
    [System.Windows.Forms.SendKeys]::SendWait('^c')
    [System.Windows.Forms.SendKeys]::SendWait('^v')
    Start-Sleep -Milliseconds 80
    foreach($pair in @(@(2,$field[0]),@(4,$field[1]),@(5,$field[2]),@(6,$field[3]),@(7,$field[4]),@(12,180),@(14,$field[5]))){SetRow $pair[0] $pair[1]}
}
[System.Windows.Forms.SendKeys]::SendWait('^s')
