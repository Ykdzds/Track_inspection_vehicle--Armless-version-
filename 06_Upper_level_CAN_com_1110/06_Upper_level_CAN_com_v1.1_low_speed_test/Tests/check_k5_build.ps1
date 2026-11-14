param([Parameter(Mandatory=$true)][string]$BuildRoot,[int]$DebugEnabled=0)
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'check_resources.ps1') -BuildRoot $BuildRoot -Name 'F103_K5FIX' -DebugEnabled $DebugEnabled
