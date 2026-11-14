Add-Type -AssemblyName System.Drawing,System.Windows.Forms,UIAutomationClient
$bounds=[System.Windows.Forms.SystemInformation]::VirtualScreen
$bitmap=New-Object System.Drawing.Bitmap $bounds.Width,$bounds.Height
$graphics=[System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($bounds.Left,$bounds.Top,0,0,$bounds.Size)
$bitmap.Save((Join-Path $PSScriptRoot '运行核验/33号实施_20261102/desktop33.png'))
$graphics.Dispose();$bitmap.Dispose()
[System.Windows.Automation.AutomationElement]::RootElement.FindAll('Children',[System.Windows.Automation.Condition]::TrueCondition)|ForEach-Object {'{0}|{1}|{2}' -f $_.Current.ProcessId,$_.Current.Name,$_.Current.BoundingRectangle}
