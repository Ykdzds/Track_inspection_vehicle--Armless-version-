. (Join-Path $PSScriptRoot 'native-run31.ps1')
[Threading.SynchronizationContext]::SetSynchronizationContext([Threading.SynchronizationContext]::new())
$t.GetField('ZaVjfOHnWnoFkLFOzuNMcrnkBwTVA',$flags).SetValue($d,[Threading.SynchronizationContext]::Current)
function Call($name,$args31){try{$t.GetMethod($name,$flags).Invoke($d,[object[]]$args31)}catch{Write-Output ('ERROR '+$name+' '+$_.Exception.ToString())}}
$t.GetMethod('kdXRPxNCPpRyEPMZWkfuEAccRoHC',$flags).Invoke($d,[object[]]@( $proj.Layers[1].PSObject.BaseObject ))
Write-Output ('PAGE '+$d.CurrentPage.Name)
$copy=$t.GetField('EgErvQLbNxVTVEjeyfsODMZZZGgcA',$flags).GetValue($d)
function State31($label){$main=$copy.Layers[1];$button=$main.Elements|Where-Object Name -eq 'themeToggle';$mode=$copy.Layers[0].Elements|Where-Object Name -eq 'themeMode';Write-Output ($label+' mode '+$mode.Int+' text '+$button.Text+' image '+$main.ImageId+' status '+$d.Status)}
State31 'before'
Call 'aONpSgRZvypzbvbLORbXPmMREXwl' @([double]450,[double]28)
Call 'TQjvHtSVHCisnjIrrbQSKGrHNJuvA' @([double]450,[double]28)
State31 'touch'
Call 'UfMielyXDjRTUMggupgMVPHmiMdJ' @(($copy.Layers[1].Elements|Where-Object Name -eq 'themeToggle'),[int]2)
State31 'event'
Call 'ogYolGhPwvQoPVAgjjVedDvLJwPw' @()
State31 'engine'