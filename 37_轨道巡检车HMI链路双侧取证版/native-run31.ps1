. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'));$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string](Join-Path $PSScriptRoot '17_轨道巡检车.pix'))
$screen=[Activator]::CreateInstance($vp.GetType('RpgRniqBlWjCFiSagdbuqoKvDXsaA'));$screen.Width=1024;$screen.Height=600;$proj.GetType().GetProperty('Screen',$flags).SetValue($proj,$screen,$null)
if(-not [UriParser]::IsKnownScheme('pack')){[UriParser]::Register([GenericUriParser]::new([GenericUriParserOptions]::GenericAuthority),'pack',-1)};[Windows.Application]::new()|Out-Null;
[Reflection.Assembly]::LoadFrom((Join-Path $appDir 'Caliburn.Micro.dll'))|Out-Null;[Reflection.Assembly]::LoadFrom((Join-Path $appDir 'Caliburn.Micro.Platform.Core.dll'))|Out-Null;Add-Type -ReferencedAssemblies @((Join-Path $appDir 'Caliburn.Micro.dll'),'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\System.Runtime.dll') -TypeDefinition 'public class VpIoC31 { public static object Get(System.Type t, string k){System.IO.File.AppendAllText("C:/Users/15056/.codex/tmp/vp31-ioc.txt",t.FullName+"\n");return null;} public static void Init(){Caliburn.Micro.IoC.GetInstance=Get;} }'
[VpIoC31]::Init()

$t=$vp.GetType('Visual_PIX.Modules.Home.ViewModels.DebugViewModel');try{$d=[Activator]::CreateInstance($t)}catch{$e=$_.Exception;while($e){Write-Output $e.ToString();$e=$e.InnerException};throw}
Add-Type -TypeDefinition 'public class SyncContext31: System.Threading.SynchronizationContext {public override void Send(System.Threading.SendOrPostCallback d,object o){d(o);}public override void Post(System.Threading.SendOrPostCallback d,object o){d(o);}}'
$t.GetField('ZaVjfOHnWnoFkLFOzuNMcrnkBwTVA',$flags).SetValue($d,[SyncContext31]::new())
[Threading.SynchronizationContext]::SetSynchronizationContext([SyncContext31]::new())
try{$t.GetMethod('HbyhnshXjsIJZRMujxjkFeSWTToM',$flags).Invoke($d,[object[]]@($proj))}catch{$_|Format-List * -Force;throw}
foreach($fld31 in $t.GetFields($flags)){$ob31=$fld31.GetValue($d);if($null -ne $ob31 -and $ob31.GetType().FullName -like 'Visual_PIX.Modules.Home.ViewModels.DebugViewModel+*'){foreach($sf31 in $ob31.GetType().GetFields($flags)|Where-Object {$_.FieldType -eq [Threading.SynchronizationContext]}){$sf31.SetValue($ob31,[SyncContext31]::new())}}}