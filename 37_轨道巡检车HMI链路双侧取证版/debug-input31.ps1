param([int]$X=0,[int]$Y=0,[string]$Code)
. (Join-Path $PSScriptRoot 'pix-ui.ps1') -Action Tree | Out-Null
$d=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Window -and $_.Current.Name -eq '调试'}|Select-Object -First 1
[ControlPixInput]::SetForegroundWindow(([IntPtr]$d.Current.NativeWindowHandle))|Out-Null
if($Code){
 $ed=$d.FindAll('Descendants',[System.Windows.Automation.Condition]::TrueCondition)|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Document -and $_.Current.BoundingRectangle.X -lt 100}|Select-Object -First 1
 $ed.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).SetValue($Code)
 $send=$d.FindAll('Descendants',[System.Windows.Automation.Condition]::TrueCondition)|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Button -and $_.Current.Name -eq '发送'}|Select-Object -First 1
 $send.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
}else{
 [ControlPixInput]::mouse_event(32769,[uint32]($X*65535/2559),[uint32]($Y*65535/1599),0,[UIntPtr]::Zero)
 [ControlPixInput]::mouse_event(2,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 200;[ControlPixInput]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
}
Start-Sleep -Milliseconds 700
Add-Type -AssemblyName System.Drawing
$b=[Drawing.Bitmap]::new(2560,1600);$g=[Drawing.Graphics]::FromImage($b);$g.CopyFromScreen(0,0,0,0,$b.Size);$b.Save((Join-Path $PSScriptRoot '运行核验/31号实施_20261101/debug.png'));$g.Dispose();$b.Dispose()