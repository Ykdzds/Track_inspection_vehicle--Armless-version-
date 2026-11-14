. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
foreach($tn in @('BxJgPPWhgfDOSbyMsbFQzJklLFgeb','WgsHpMfTlFFpaufXgwhdxAjqGiFJA')){$tt=$vp.GetType($tn);Write-Output $tn;$tt.GetConstructors($flags)|ForEach-Object {$_.ToString()};$tt.GetMethods($flags)|Where-Object {$_.DeclaringType -eq $tt}|ForEach-Object {$_.ToString()};$tt.GetFields($flags)|ForEach-Object {$_.ToString()}}
$vp.GetTypes()|ForEach-Object {$tp=$_;foreach($m in $tp.GetMethods($flags)){if($m.DeclaringType -eq $tp -and ($m.GetParameters().ParameterType -contains [string]) -and ($m.GetParameters().ParameterType.FullName -contains 'Visual_PIX.Model.PixProject')){Write-Output ($tp.FullName+' : '+$m.ToString())}}}

$tt=$vp.GetType("YfWokryEEyXYShnUWReANAvuxqev");$tt.GetConstructors($flags)|ForEach-Object {$_.ToString()};$tt.GetFields($flags)|ForEach-Object {$_.ToString()}
