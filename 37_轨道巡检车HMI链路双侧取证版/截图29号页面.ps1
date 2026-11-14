$ErrorActionPreference='Stop'
$helper=Join-Path $PSScriptRoot 'pix-ui.ps1'
$evidence=Join-Path $PSScriptRoot '运行核验/29号实施_20261101'
. $helper -Action SelectPage -Name P1_Main
Add-Type -AssemblyName System.Drawing
foreach($page in @('P1_Main','P2_Diag','P3_Position','P4_Info','P5_Alarm','P6_Sync','P7_System_Ext','P8_ServoControl')){
 SelectPage $page
 SelectControl $page
 $zoom=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::ComboBox -and $_.Current.BoundingRectangle.X -lt 1000}|Select-Object -First 1
 $zoom.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
 $desired=if($page -eq 'P8_ServoControl'){'80%'}else{'100%'}
 $item=Nodes|Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::ListItem -and $_.Current.Name -eq $desired}|Select-Object -First 1
 $rect=$item.Current.BoundingRectangle;Click ([int]($rect.X+$rect.Width/2)) ([int]($rect.Y+$rect.Height/2))
 Start-Sleep -Milliseconds 100
 $screen=[System.Windows.Forms.SystemInformation]::VirtualScreen
 $bitmap=[System.Drawing.Bitmap]::new($screen.Width,$screen.Height)
 $graphics=[System.Drawing.Graphics]::FromImage($bitmap)
 try{
  $graphics.CopyFromScreen($screen.X,$screen.Y,0,0,$bitmap.Size)
  $bitmap.Save((Join-Path $evidence ($page+'.png')))
  # P8 has hidden services below its canvas; use 80% to fit the full page.
  $bounds=if($page -eq 'P8_ServoControl'){[System.Drawing.Rectangle]::new(636,314,1229,720)}else{[System.Drawing.Rectangle]::new(483,269,1536,900)}
  $crop=$bitmap.Clone($bounds,$bitmap.PixelFormat)
  try{$crop.Save((Join-Path $evidence ($page+'_页面.png')))}finally{$crop.Dispose()}
 }finally{$graphics.Dispose();$bitmap.Dispose()}
 Write-Output "截图 $page"
}
SelectPage P1_Main
