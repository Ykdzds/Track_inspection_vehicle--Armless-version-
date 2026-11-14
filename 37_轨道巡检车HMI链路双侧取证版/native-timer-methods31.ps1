. (Join-Path $PSScriptRoot 'native-run31.ps1')
foreach($w in $t.GetField('rwjNaJQpoUjcsdiYgWBdFWXxIrlB',$flags).GetValue($d)){for($tt=$w.GetType();$tt -and $tt -ne [object];$tt=$tt.BaseType){$tt.GetMethods($flags -bor [Reflection.BindingFlags]::DeclaredOnly)|ForEach-Object {Write-Output $_.ToString()}};break}
[Environment]::Exit(0)