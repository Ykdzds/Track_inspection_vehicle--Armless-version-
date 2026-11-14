$ErrorActionPreference='Stop'
$evidence=$PSScriptRoot
. (Join-Path $PSScriptRoot 'pix-ui.ps1') -Action SelectPage -Name P1_Main
Add-Type -AssemblyName System.Drawing
$zoom=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::ComboBox -and $_.Current.BoundingRectangle.X -lt 1000}|Select-Object -First 1
$zoom.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
$item=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::ListItem -and $_.Current.Name -eq '80%'}|Select-Object -First 1
$rect=$item.Current.BoundingRectangle;Click ([int]($rect.X+$rect.Width/2)) ([int]($rect.Y+$rect.Height/2))
foreach($page in @('P0_Boot','P1_Main','P2_Diag','P3_Position','P4_Info','P5_Alarm','P6_Sync','P7_System_Ext','P8_ServoControl')){
 SelectPage $page
 Start-Sleep -Milliseconds 1100
 $screen=[System.Windows.Forms.SystemInformation]::VirtualScreen
 $bitmap=[Drawing.Bitmap]::new($screen.Width,$screen.Height);$graphics=[Drawing.Graphics]::FromImage($bitmap)
 try{$graphics.CopyFromScreen($screen.X,$screen.Y,0,0,$bitmap.Size);$bitmap.Save((Join-Path $evidence ($page+'_VP.png')))}finally{$graphics.Dispose();$bitmap.Dispose()}
 Write-Output $page
}
SelectPage P1_Main
