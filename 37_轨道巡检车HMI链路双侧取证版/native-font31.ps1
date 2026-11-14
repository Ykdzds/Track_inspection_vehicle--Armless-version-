. (Join-Path $PSScriptRoot 'native-run31.ps1')
$proj.GetType().GetProperties($flags)|Where-Object {$_.Name -match 'Path|File|Dir'}|ForEach-Object {$_.ToString()+' VALUE='+$_.GetValue($proj,$null)}
$tt31=$vp.GetType('grDDQMstRKrkaGSqToqpPEnqYDDt');$tt31.GetProperties()|Where-Object {$_.Name -match '^Text|^Font$|Foreground'}|ForEach-Object {$_.ToString()}
[Environment]::Exit(0)