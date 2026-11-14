. (Join-Path $PSScriptRoot 'native-run31.ps1')
$copy=$t.GetField('EgErvQLbNxVTVEjeyfsODMZZZGgcA',$flags).GetValue($d)
foreach($fn in @('FYPgzghjrjdfafFbeoXyZnjdoDpXb','iCwfvIbOGIlaphtxvrbKIAKaOJKt')){foreach($wrapper31 in $t.GetField($fn,$flags).GetValue($d)){$wt=$wrapper31.GetType();$el=$wt.GetMethod('TNUFAMhprWkVMKRQzJArkCVEjflw',$flags).Invoke($wrapper31,@());if($el.Name -in @('P1_Main','themeToggle')){Write-Output ('RUN '+$el.Name);try{$wt.GetMethod('VQHJIGmODxYDIDwmGPtbcmCSENze',$flags).Invoke($wrapper31,@())}catch{Write-Output $_.Exception.ToString()};$errors=$t.GetField('BBLYIVcgmApjnlSElGZdacQwrRhf',$flags).GetValue($d);Write-Output ('ERR '+($errors -join '|'));$mode=$copy.Layers[0].Elements|Where-Object Name -eq 'themeMode';Write-Output ('MODE '+$mode.Int)}}}
foreach($p in @($proj,$copy,$t.GetField('WMyjeJwGLkXDvVoFwhNgLDbbBMpjA',$flags).GetValue($d))){$mode=$p.Layers[0].Elements|Where-Object Name -eq 'themeMode';$btn=$p.Layers[1].Elements|Where-Object Name -eq 'themeToggle';Write-Output ('STATE '+$mode.Int+' '+$btn.Text)}


[Environment]::Exit(0)