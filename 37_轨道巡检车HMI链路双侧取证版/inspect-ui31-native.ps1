. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'))
$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string](Join-Path $PSScriptRoot '17_轨道巡检车.pix'))
$t=$vp.GetType('Visual_PIX.Model.Controls.TextCtrl')
foreach($name in @('HorizontalAlignment','Enable','Visbile')){
 $p=$t.GetProperty($name);Write-Output ($name+' '+$p.PropertyType.FullName)
 if($p.PropertyType.IsEnum){[Enum]::GetNames($p.PropertyType)|ForEach-Object {Write-Output ($_+'='+[int][Enum]::Parse($p.PropertyType,$_))}}
}
foreach($type in $vp.GetTypes()){
 foreach($m in $type.GetMethods($flags)){
  if($m.DeclaringType -ne $type){continue}
  $body=$m.GetMethodBody();if(!$body){continue};$il=$body.GetILAsByteArray()
  for($i=0;$i -lt $il.Length-4;$i++){
   if($il[$i] -ne 0x72){continue}
   try{$s=$m.Module.ResolveString([BitConverter]::ToInt32($il,$i+1))}catch{continue}
   if($s -cmatch '^(bgImg|font|txtHorAgn|tbColor)$' -or $s -match '模拟|仿真'){
    Write-Output ($type.FullName+' | '+$m.ToString()+' | '+$s)
   }
  }
 }
}

foreach($type in $vp.GetTypes()){foreach($m in $type.GetMethods($flags)){if($m.DeclaringType -eq $type -and (($m.GetParameters().ParameterType.FullName -join ',') -match 'ImageData|FontData|PixProject' -or $m.ReturnType.FullName -match 'ImageRefOption|FontRefOption')){Write-Output ($type.FullName+' | '+$m.ToString())}}}