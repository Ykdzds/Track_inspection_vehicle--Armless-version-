. (Join-Path $PSScriptRoot 'native-run31.ps1')
$copy=$t.GetField('EgErvQLbNxVTVEjeyfsODMZZZGgcA',$flags).GetValue($d)
$pg31=$copy.Layers[8];$h31=$pg31.Elements|Where-Object Name -eq 'h13';$sl31=$pg31.Elements|Where-Object Name -eq 'rpmSlider';$hint31=$pg31.Elements|Where-Object Name -eq 'sliderSpeedHint'
$wrappers31=@{};foreach($fn in @('FYPgzghjrjdfafFbeoXyZnjdoDpXb','iCwfvIbOGIlaphtxvrbKIAKaOJKt')){foreach($w31 in $t.GetField($fn,$flags).GetValue($d)){$el31=$w31.GetType().GetMethod('TNUFAMhprWkVMKRQzJArkCVEjflw',$flags).Invoke($w31,@());$wrappers31[$el31.Name]=$w31}}
function Run31($name){$w31=$wrappers31[$name];$w31.GetType().GetMethod('VQHJIGmODxYDIDwmGPtbcmCSENze',$flags).Invoke($w31,@())|Out-Null}
$mode31=$copy.Layers[0].Elements|Where-Object Name -eq 'themeMode';Run31 P1_Main;Run31 themeToggle
Write-Output ('THEME '+$mode31.Int+' image '+$copy.Layers[1].ImageId+' expected '+($copy.Images|Where-Object Name -eq 'Night_P1_Main').Id)
Run31 P8_ServoControl
foreach($v31 in @(0,800,1599,1600,1601,2400,3200)){$sl31.Value=$v31;Run31 rpmSlider;Write-Output ('SLIDER '+$v31+' '+$hint31.X+' '+$hint31.HorizontalAlignment+' '+$hint31.Text+' control='+$h31.IntArray[10])}
Write-Output 'CONTROL TYPES'
$a31=[Reflection.Assembly]::LoadFrom((Join-Path $appDir 'CtrlLib.dll'));Write-Output ($a31.GetTypes().FullName -join "`n")
$stream31=$vp.GetManifestResourceStream('Visual-PIX.g.resources');if($stream31){$rd31=[Resources.ResourceReader]::new($stream31);$rd31.GetEnumerator()|ForEach-Object {$_.Key}|Where-Object {$_ -match 'style|theme|app|control'}|Select-Object -First 35}
[Environment]::Exit(0)