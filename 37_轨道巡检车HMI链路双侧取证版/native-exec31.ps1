. (Join-Path $PSScriptRoot 'native-run31.ps1')
$fields=@('iCwfvIbOGIlaphtxvrbKIAKaOJKt','FYPgzghjrjdfafFbeoXyZnjdoDpXb')
foreach($fn in $fields){foreach($w31 in $t.GetField($fn,$flags).GetValue($d)){$wt=$w31.GetType();$elem=$wt.GetMethod('TNUFAMhprWkVMKRQzJArkCVEjflw',$flags).Invoke($w31,@());if($elem.Name -in @('themeToggle','P1_Main')){Write-Output ($elem.Name+' event '+$wt.GetField('XtwDHrQdEGtpnZgCArtELiWlgFzeA',$flags).GetValue($w31)); if($elem.Name -eq 'themeToggle'){$target31=$w31}}}}
foreach($method in @('hZdosMBvwFVGKyUJvqqtdcFwUVQd','VQHJIGmODxYDIDwmGPtbcmCSENze','lBmfEUojrNEZCnkDcbigmxYMguSG','VqIFPUapprxHsUMBqCUKgHIJSobEB')){
 try{$target31.GetType().GetMethod($method,$flags).Invoke($target31,@())}catch{Write-Output $_.Exception.ToString()}
 & (Join-Path $PSScriptRoot 'dump-il31.ps1') -TypeName $target31.GetType().FullName -MethodName $method
 $copy=$t.GetField('EgErvQLbNxVTVEjeyfsODMZZZGgcA',$flags).GetValue($d);$mode=$copy.Layers[0].Elements|Where-Object Name -eq 'themeMode';$btn=$copy.Layers[1].Elements|Where-Object Name -eq 'themeToggle';Write-Output ('STATE '+$method+' '+$mode.Int+' '+$btn.Text)
}
[Environment]::Exit(0)