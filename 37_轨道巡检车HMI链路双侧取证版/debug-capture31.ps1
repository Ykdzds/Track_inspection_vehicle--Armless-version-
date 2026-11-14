param([int]$X=0,[int]$Y=0,[string]$Code)
. (Join-Path $PSScriptRoot 'pix-ui.ps1') -Action Tree | Out-Null
$d=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Window -and $_.Current.Name -eq '调试'}|Select-Object -First 1
[ControlPixInput]::SetForegroundWindow(([IntPtr]$d.Current.NativeWindowHandle))|Out-Null
Start-Sleep -Milliseconds 700
Add-Type -AssemblyName System.Drawing
$b=[Drawing.Bitmap]::new(2560,1600);$g=[Drawing.Graphics]::FromImage($b);$g.CopyFromScreen(0,0,0,0,$b.Size);$b.Save((Join-Path $PSScriptRoot '运行核验/31号实施_20261101/debug.png'));$g.Dispose();$b.Dispose()