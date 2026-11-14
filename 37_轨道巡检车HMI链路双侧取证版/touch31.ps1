. (Join-Path $PSScriptRoot 'pix-ui.ps1') -Action Tree|Out-Null
if(-not ('VpTouch31' -as [type])){Add-Type @"
using System;using System.Runtime.InteropServices;public class VpTouch31{[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);[DllImport("user32.dll")]public static extern IntPtr WindowFromPoint(Pt p);[DllImport("user32.dll")]public static extern bool GetCursorPos(out Pt p);[DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);public struct Pt{public int X;public int Y;}}
"@}
$d=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Window -and $_.Current.Name -eq '调试'}|Select-Object -First 1
$hw=[IntPtr]$d.Current.NativeWindowHandle
Write-Output ('handle '+$hw)
[VpTouch31]::PostMessage($hw,0x200,[IntPtr]0,[IntPtr](97*65536+1230))|Out-Null
[VpTouch31]::PostMessage($hw,0x201,[IntPtr]1,[IntPtr](97*65536+1230))|Out-Null
Start-Sleep -Milliseconds 150
[VpTouch31]::PostMessage($hw,0x202,[IntPtr]0,[IntPtr](97*65536+1230))|Out-Null
Start-Sleep -Milliseconds 400
& (Join-Path $PSScriptRoot 'debug-capture31.ps1')