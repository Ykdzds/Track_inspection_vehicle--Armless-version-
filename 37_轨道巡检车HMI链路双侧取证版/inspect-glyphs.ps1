. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'))
$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string](Join-Path $PSScriptRoot '17_轨道巡检车.pix'))
$proj.Fonts[3].Font.Chars[100]|Format-List *
$proj.Fonts[3].Font|Format-List *
foreach($t in $vp.GetTypes()|Where-Object {$_.Name -match 'Screen' -and $_.Namespace -match 'Model'}){$t.FullName;$t.GetProperties()|ForEach-Object {$_.ToString()}}
$proj.GetType().GetProperties()|ForEach-Object {$_.ToString()}
$fonts=foreach($f in $proj.Fonts|Where-Object {$_.Name -like 'UI30*'}){[ordered]@{Id=$f.Id;Height=$f.Font.PixelHeight;Type=[string]$f.Font.FontType;Glyphs=@($f.Font.Chars|ForEach-Object {[ordered]@{Char=[string]$_.Char;Width=$_.PixelWidth;Data=[Convert]::ToBase64String($_.Data)}})}}
$fonts|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $PSScriptRoot '运行核验/30号实施_20261101/实际字模.json') -Encoding UTF8
