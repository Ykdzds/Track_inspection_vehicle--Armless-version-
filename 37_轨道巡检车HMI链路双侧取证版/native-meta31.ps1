. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
foreach($name in @('NSBvXRnAJUCHreJZPPKswyFhOpAX','Visual_PIX.Modules.Home.ViewModels.DebugViewModel','RpgRniqBlWjCFiSagdbuqoKvDXsaA')){
 $t=$vp.GetType($name);Write-Output ('TYPE '+$name)
 $t.GetConstructors($flags)|ForEach-Object {$_.ToString()}
 $t.GetFields($flags)|ForEach-Object {$_.ToString()}
 $t.GetProperties($flags)|ForEach-Object {$_.ToString()}
 $t.GetMethods($flags)|Where-Object {$_.DeclaringType -eq $t -and $_.Name -notmatch '^get_|^set_'}|ForEach-Object {$_.ToString()}
}