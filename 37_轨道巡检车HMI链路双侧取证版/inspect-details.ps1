. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$t=$vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'); $io=[Activator]::CreateInstance($t)
$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string](Join-Path $PSScriptRoot '17_轨道巡检车.pix'))
foreach($c in @($proj.Layers[1].Elements[1],$proj.Layers[1].Elements[18],$proj.Layers[8].Elements[22],$proj.Layers[8].Elements[23],$proj.Layers[8].Elements[28],$proj.Layers[8].Elements[5],$proj.Fonts[2].Font,$proj.Fonts[2].Font.Chars[0],$proj.Fonts[2].Font.Option)){
 Write-Output ('TYPE '+$c.GetType().FullName);$c|Format-List
}
foreach($type in $vp.GetTypes()){
 foreach($m in $type.GetMethods($flags)){
  if($m.DeclaringType -eq $type -and ($m.ReturnType.FullName -eq 'Visual_PIX.Model.PixelFont' -or $m.GetParameters().ParameterType.FullName -contains 'Visual_PIX.Model.FontConverter.PixelFontOption')){Write-Output ($type.FullName+' | '+$m.ToString())}
 }
}
Write-Output 'EVENT';$proj.Layers[8].Elements[5].Events|Format-List
