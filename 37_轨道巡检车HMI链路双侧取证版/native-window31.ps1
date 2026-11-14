. (Join-Path $PSScriptRoot 'native-run31.ps1')
[Windows.Application]::ResourceAssembly=$vp
foreach($file31 in @('resource/buttontheme.xaml','resource/textboxtheme.xaml','resource/checkboxtheme.xaml','resource/appdictionary.xaml','resource/pixcontroldatatemplate.xaml','resource/pixcontroldatatemplatedebug.xaml')){try{$rd31=[Windows.ResourceDictionary]::new();$rd31.GetType().GetProperty('Source').SetValue($rd31,[Uri]('pack://application:,,,/Visual-PIX;component/'+$file31),$null);[Windows.Application]::Current.Resources.MergedDictionaries.Add($rd31);Write-Output ('LOADED '+$file31)}catch{Write-Output ('DICT '+$_.Exception.Message)}}
$v31=[Activator]::CreateInstance($vp.GetType('Visual_PIX.Modules.Home.Views.DebugView'));$v31.DataContext=$d
$copy=$t.GetField('EgErvQLbNxVTVEjeyfsODMZZZGgcA',$flags).GetValue($d);$pg31=$copy.Layers[8]
$t.GetField('sptlnwNEeWINytssvYStiACLjpKx',$flags).SetValue($d,$pg31);$t.GetMethod('BlonFGjajYudmSgKcPxpfCQUShxe',$flags).Invoke($d,@())
$win31=$v31;$win31.Width=1280;$win31.Height=900;$win31.Show();$win31.UpdateLayout()
function Tree31($el,$dep){if($dep -gt 16){return};Write-Output ((' '*$dep)+$el.GetType().FullName+' '+$el.Name+' '+$el.ActualWidth+'x'+$el.ActualHeight+' DATA='+$el.DataContext.Name+' HIT='+$el.IsHitTestVisible+' ENABLE='+$el.IsEnabled);for($i31=0;$i31 -lt [Windows.Media.VisualTreeHelper]::GetChildrenCount($el);$i31++){Tree31 ([Windows.Media.VisualTreeHelper]::GetChild($el,$i31)) ($dep+1)}}
Tree31 $v31 0
$win31.Close();[Environment]::Exit(0)