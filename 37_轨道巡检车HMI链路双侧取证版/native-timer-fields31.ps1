. (Join-Path $PSScriptRoot 'native-run31.ps1')
foreach($w in $t.GetField('rwjNaJQpoUjcsdiYgWBdFWXxIrlB',$flags).GetValue($d)){for($tt=$w.GetType();$tt -and $tt -ne [object];$tt=$tt.BaseType){foreach($f in $tt.GetFields($flags -bor [Reflection.BindingFlags]::DeclaredOnly)){if($f.FieldType -ne [byte[]]){Write-Output ($f.ToString()+' = '+$f.GetValue($w))}}}}
[Environment]::Exit(0)