. (Join-Path $PSScriptRoot 'native-run31.ps1')
[Windows.Application]::ResourceAssembly=$vp
foreach($file31 in @('resource/buttontheme.xaml','resource/textboxtheme.xaml','resource/checkboxtheme.xaml','resource/appdictionary.xaml','resource/pixcontroldatatemplate.xaml','resource/pixcontroldatatemplatedebug.xaml')){try{$rd31=[Windows.ResourceDictionary]::new();$rd31.GetType().GetProperty('Source').SetValue($rd31,[Uri]('pack://application:,,,/Visual-PIX;component/'+$file31),$null);[Windows.Application]::Current.Resources.MergedDictionaries.Add($rd31);Write-Output ('LOADED '+$file31)}catch{Write-Output ('DICT '+$_.Exception.Message)}}
$copy=$t.GetField('EgErvQLbNxVTVEjeyfsODMZZZGgcA',$flags).GetValue($d)
foreach($type31 in @('Visual_PIX.Model.Controls.TextCtrl','Visual_PIX.Model.Controls.SliderCtrl')){
 $key31=[Windows.DataTemplateKey]::new($vp.GetType($type31));$dt31=[Windows.Application]::Current.FindResource($key31);$el31=$dt31.LoadContent();Write-Output ('TEMPLATE '+$type31+' root '+$el31.GetType().FullName)
 $ctrl31=$copy.Layers[8].Elements|Where-Object {$_.GetType().FullName -eq $type31}|Select-Object -Last 1
 $el31.DataContext=$ctrl31
 $el31.Measure([Windows.Size]::new($ctrl31.Width,$ctrl31.Height));$el31.Arrange([Windows.Rect]::new(0,0,$ctrl31.Width,$ctrl31.Height));$el31.UpdateLayout()
 function Tree31($el,$dep){Write-Output ((' '*$dep)+$el.GetType().FullName+' '+$el.Name+' '+$el.ActualWidth+'x'+$el.ActualHeight);for($i31=0;$i31 -lt [Windows.Media.VisualTreeHelper]::GetChildrenCount($el);$i31++){Tree31 ([Windows.Media.VisualTreeHelper]::GetChild($el,$i31)) ($dep+1)}}
 Tree31 $el31 0
 $el31.GetType().GetProperties()|Where-Object {$_.Name -match 'Text|Font|Thumb|Value|Range|Background'}|ForEach-Object {$_.ToString()}
 try{[Windows.Markup.XamlWriter]::Save($dt31)|Set-Content -LiteralPath (Join-Path $PSScriptRoot ('运行核验/31号实施_20261101/'+$type31+'.xml')) -Encoding UTF8}catch{Write-Output $_.Exception.Message}
}
[Environment]::Exit(0)