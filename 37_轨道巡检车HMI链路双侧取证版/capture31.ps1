. (Join-Path $PSScriptRoot 'pix-ui.ps1') -Action Keys -Value '{ESC}'
Add-Type -AssemblyName System.Drawing
$b=[Drawing.Bitmap]::new(2560,1600);$g=[Drawing.Graphics]::FromImage($b);$g.CopyFromScreen(0,0,0,0,$b.Size);$b.Save((Join-Path $PSScriptRoot '运行核验/31号实施_20261101/current.png'));$g.Dispose();$b.Dispose()