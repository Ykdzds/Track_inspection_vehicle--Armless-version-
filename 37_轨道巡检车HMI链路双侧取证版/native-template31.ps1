. (Join-Path $PSScriptRoot 'native-run31.ps1')
[Windows.Application]::ResourceAssembly=$vp
foreach($file31 in @('resource/buttontheme.xaml','resource/appdictionary.xaml','resource/pixcontroldatatemplate.xaml','resource/pixcontroldatatemplatedebug.xaml','themes/generic.xaml')){
 try{$rd31=[Windows.ResourceDictionary]::new();$rd31.GetType().GetProperty('Source').SetValue($rd31,[Uri]('pack://application:,,,/Visual-PIX;component/'+$file31),$null);[Windows.Application]::Current.Resources.MergedDictionaries.Add($rd31);Write-Output ('DICT '+$file31+' '+($rd31.Keys -join '|'));[Windows.Markup.XamlWriter]::Save($rd31)|Set-Content -LiteralPath (Join-Path $PSScriptRoot ('运行核验/31号实施_20261101/'+$file31.Replace('/','_').Replace('.xaml','.xml'))) -Encoding UTF8}catch{Write-Output $_.Exception.Message}
}
[Environment]::Exit(0)