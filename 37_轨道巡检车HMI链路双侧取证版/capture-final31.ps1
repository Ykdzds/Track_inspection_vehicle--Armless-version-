. (Join-Path $PSScriptRoot 'pix-ui.ps1') -Action SelectPage -Name P8_ServoControl
Add-Type -AssemblyName System.Drawing
$rect31=[Windows.Forms.SystemInformation]::VirtualScreen;$b31=[Drawing.Bitmap]::new($rect31.Width,$rect31.Height);$g31=[Drawing.Graphics]::FromImage($b31);$g31.CopyFromScreen($rect31.X,$rect31.Y,0,0,$b31.Size);$b31.Save((Join-Path $PSScriptRoot '运行核验/31号实施_20261101/最终VP编辑器_P8.png'));$g31.Dispose();$b31.Dispose()
SelectPage P1_Main
$p.CloseMainWindow()|Out-Null