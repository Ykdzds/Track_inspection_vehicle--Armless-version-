. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$t=$vp.GetType('Visual_PIX.Modules.Home.ViewModels.DebugViewModel');$t.GetConstructors($flags)|ForEach-Object {$_.ToString()}
$projTypes=$vp.GetTypes()|Where-Object {$_.FullName -match '^Visual_PIX.Modules.Home.ViewModels.DebugViewModel'}
$t.GetMethods($flags)|Where-Object {$_.DeclaringType -eq $t -and ($_.Name -match 'Show|On|Start|Execute|Hby' -or ($_.GetParameters().ParameterType.FullName -join ',') -match 'PixLayer|CtrlEvent')}|ForEach-Object {$_.ToString()}