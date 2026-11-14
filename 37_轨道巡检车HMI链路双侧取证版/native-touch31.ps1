. (Join-Path $PSScriptRoot 'native-run31.ps1')
$copy=$t.GetField('EgErvQLbNxVTVEjeyfsODMZZZGgcA',$flags).GetValue($d)
foreach($n in @('h13','rpmSlider','sliderSpeedHint')){$c31=$copy.Layers[8].Elements|Where-Object Name -eq $n;Write-Output ('CTRL '+$n);$c31.GetType().GetProperties($flags)|ForEach-Object {$_.ToString()}}
$t.GetField('sptlnwNEeWINytssvYStiACLjpKx',$flags).SetValue($d,$copy.Layers[8])
$t.GetMethod('BlonFGjajYudmSgKcPxpfCQUShxe',$flags).Invoke($d,@())
Write-Output ('PAGE '+$d.CurrentPage.Name+' controls '+$d.PageControls.Count)
foreach($xy in @(@(600,320),@(508,320),@(80,320))){$hits31=$t.GetMethod('tZLQyBPOWDpxXBBbzumxudRwnpLe',$flags).Invoke($d,[object[]]@([double]$xy[0],[double]$xy[1]));Write-Output ('HIT '+$xy+' '+($hits31.Name -join ','))}
$h31=$copy.Layers[8].Elements|Where-Object Name -eq 'h13';$sl31=$copy.Layers[8].Elements|Where-Object Name -eq 'rpmSlider';$hint31=$copy.Layers[8].Elements|Where-Object Name -eq 'sliderSpeedHint'
Write-Output ('BEFORE '+$sl31.Value+' '+$hint31.X+' '+$hint31.Text+' H '+$h31.IntArray[10])
foreach($xy in @(@(600,320),@(40,320),@(984,320),@(508,320))){
 foreach($method31 in @('aONpSgRZvypzbvbLORbXPmMREXwl','TQjvHtSVHCisnjIrrbQSKGrHNJuvA')){try{$t.GetMethod($method31,$flags).Invoke($d,[object[]]@([double]$xy[0],[double]$xy[1]))}catch{Write-Output $_.Exception.Message}}
 Write-Output ('TOUCH '+$xy+' VALUE '+$sl31.Value+' '+$hint31.X+' '+$hint31.Text+' H '+$h31.IntArray[10])
}
[Environment]::Exit(0)