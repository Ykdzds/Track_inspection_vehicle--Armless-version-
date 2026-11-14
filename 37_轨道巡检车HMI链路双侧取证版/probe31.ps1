. (Join-Path $PSScriptRoot 'vp-reflect.ps1')
$io=[Activator]::CreateInstance($vp.GetType('blFUmdHRIKgcqLnrHkXULnrUGQNn'))
$path=Join-Path $PSScriptRoot '17_轨道巡检车.pix';$proj=$io.rqkGbDgDKWruWOpLfoxBcumhVTMG([string]$path)
$proj.Layers[1].Events[0].Code='P0_Boot.themeMode.val=0;P1_Main.themeToggle.txt="LOADED";'
$id=($proj.Images|Where-Object Name -eq 'Night_P1_Main').Id
($proj.Layers[1].Elements|Where-Object Name -eq 'themeToggle').Events[1].Code='P1_Main.themeToggle.txt="NIGHT";'
$io.wHAMcrUVnVnATbRNFqXJTFWDNjDo($proj,[string]$path)