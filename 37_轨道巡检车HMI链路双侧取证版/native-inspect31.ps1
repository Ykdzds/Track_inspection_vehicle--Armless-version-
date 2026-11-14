. (Join-Path $PSScriptRoot 'native-run31.ps1')
$copy=$t.GetField('EgErvQLbNxVTVEjeyfsODMZZZGgcA',$flags).GetValue($d)
foreach($n in @('h13','rpmSlider','sliderSpeedHint')){$c31=$copy.Layers[8].Elements|Where-Object Name -eq $n;Write-Output ('CTRL '+$n);$c31.GetType().GetProperties($flags)|ForEach-Object {$_.ToString()}}
$t.GetField('sptlnwNEeWINytssvYStiACLjpKx',$flags).SetValue($d,$copy.Layers[8])
$t.GetMethod('BlonFGjajYudmSgKcPxpfCQUShxe',$flags).Invoke($d,@())
Write-Output ('PAGE '+$d.CurrentPage.Name+' controls '+$d.PageControls.Count)
foreach($xy in @(@(600,320),@(508,320),@(80,320))){$hits31=$t.GetMethod('tZLQyBPOWDpxXBBbzumxudRwnpLe',$flags).Invoke($d,[object[]]@([double]$xy[0],[double]$xy[1]));Write-Output ('HIT '+$xy+' '+($hits31.Name -join ','))}
$vt=$vp.GetType('Visual_PIX.Modules.Home.Views.DebugView');Write-Output ('VIEW '+$vt);if($vt){$v31=[Activator]::CreateInstance($vt);$v31.DataContext=$d;$v31.GetType().GetProperties()|Where-Object {$_.Name -match 'Content'}|ForEach-Object {$_.ToString()}}
[Environment]::Exit(0)