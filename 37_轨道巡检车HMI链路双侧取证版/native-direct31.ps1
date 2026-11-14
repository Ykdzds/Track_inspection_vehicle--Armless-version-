. (Join-Path $PSScriptRoot 'native-run31.ps1')
$nested=$vp.GetType('Visual_PIX.Modules.Home.ViewModels.DebugViewModel+cDQJaMNqInhaShABVmxuwLXEDpPQ')
$act=[Activator]::CreateInstance($nested,$true)
$nested.GetFields($flags)|ForEach-Object {$_.ToString()}
$nested.GetField('KlfNMexCflBJYCMmDkOYOqhUwLTS',$flags).SetValue($act,$d)
$nested.GetField('LkqDTJFjzIbBFUaXsCQureqWUpeD',$flags).SetValue($act,$proj.Layers[1])
$t.GetField('ZaVjfOHnWnoFkLFOzuNMcrnkBwTVA',$flags).SetValue($d,[Threading.SynchronizationContext]::new())
try{$nested.GetMethod('IathinblLWYtgdYxcFOmJLdrJYnp',$flags).Invoke($act,[object[]]@($null))}catch{Write-Output $_.Exception.ToString()}
Write-Output ('CURRENT '+$d.CurrentPage.Name)
$main=$t.GetField('EgErvQLbNxVTVEjeyfsODMZZZGgcA',$flags).GetValue($d).Layers[1];$button=$main.Elements|Where-Object Name -eq 'themeToggle';$mode=$t.GetField('EgErvQLbNxVTVEjeyfsODMZZZGgcA',$flags).GetValue($d).Layers[0].Elements|Where-Object Name -eq 'themeMode'
try{$t.GetMethod('UfMielyXDjRTUMggupgMVPHmiMdJ',$flags).Invoke($d,[object[]]@($button.PSObject.BaseObject,[int]2))}catch{Write-Output $_.Exception.ToString()}
Write-Output ('MODE '+$mode.Int+' BUTTON '+$button.Text)
& (Join-Path $PSScriptRoot 'dump-il31.ps1') -TypeName $nested.FullName -MethodName bBWgDhHonTfFlgYaAZdrSUCCprnoB
[Environment]::Exit(0)
