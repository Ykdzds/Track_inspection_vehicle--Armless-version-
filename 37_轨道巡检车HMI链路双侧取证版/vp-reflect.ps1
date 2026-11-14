$ErrorActionPreference='Stop'
$appDir=Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) '官网资料\Visual-PIX'
Set-Location -LiteralPath $appDir
[AppDomain]::CurrentDomain.add_AssemblyResolve({param($sender,$e) $p=Join-Path $appDir (($e.Name.Split(',')[0])+'.dll');if(Test-Path -LiteralPath $p){[Reflection.Assembly]::LoadFrom($p)}})
Add-Type -AssemblyName PresentationFramework,PresentationCore,WindowsBase,System.Drawing,System.Windows.Forms
$vp=[Reflection.Assembly]::LoadFrom((Join-Path $appDir 'Visual-PIX.exe'))
$flags=[Reflection.BindingFlags]'Public,NonPublic,Static,Instance'
