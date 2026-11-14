. (Join-Path $PSScriptRoot 'native-run31.ps1')
foreach($field in $t.GetFields($flags)|Where-Object {$_.FieldType.IsGenericType -and $_.Name -in @('iCwfvIbOGIlaphtxvrbKIAKaOJKt','FYPgzghjrjdfafFbeoXyZnjdoDpXb','rwjNaJQpoUjcsdiYgWBdFWXxIrlB')}){
 $ls=$field.GetValue($d);Write-Output ($field.Name+' COUNT '+$ls.Count)
 foreach($o in $ls|Select-Object -First 3){$o.GetType().GetFields($flags)|ForEach-Object {Write-Output ($_.ToString()+' = '+$_.GetValue($o))};$o.GetType().GetMethods($flags)|Where-Object {$_.DeclaringType -eq $o.GetType()}|ForEach-Object {$_.ToString()}}
}
[Environment]::Exit(0)