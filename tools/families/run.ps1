<#
.SYNOPSIS
  Contract "families+blockevents" (F11): generates the block family definitions into assembled packs.

.DESCRIPTION
  tools/families/families.lua turns the family data (assets/data/families/<unit>.json, assembled
  into data/gf/families/) into Bedrock block definitions, shared geometries, block sounds and
  double slab loot tables, and the fixture families (tools/families/fixtures/*.json) into the
  gf_test packs. It is meant to run inside tools/xmake/assemble.lua right after the packs are
  built (one call, see the PR of F11); until that hook exists this script applies it to an
  assembled directory:

    -Out <dir>   one or more assembled mod directories (default: bin/server/GloamingForest and
                 bin/client/GloamingForest, those that exist). The e2e scenarios of F11 run it on
                 the deployed copy (scenarios/families.prelaunch.ps1).
    -Check       assembles into build/assemble-check (tools/assemble.ps1 -Check) and runs the
                 generator and its checks there (integer states, permutation budget, geometry).

  The packs it changed get a higher patch version (games never reuse a cached pack without the
  generated blocks).

  Exit code 0 = OK, 1 = an error (listed).

.EXAMPLE
  pwsh tools/families/run.ps1
  pwsh tools/families/run.ps1 -Check
  pwsh tools/families/run.ps1 -Out C:\bds\plugins\GloamingForest
#>
param(
    [string[]]$Out,
    [switch]$Check
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$env:Path = $env:Path + ';' + [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
if (-not (Get-Command xmake -ErrorAction SilentlyContinue)) { Write-Host '[families] xmake not found on PATH' -ForegroundColor Red; exit 1 }

if ($Check) {
    $dir = Join-Path $root 'build/assemble-check'
    & pwsh -NoProfile -File (Join-Path $root 'tools/assemble.ps1') -Check -Out $dir
    if ($LASTEXITCODE -ne 0) { exit 1 }
    $Out = @($dir)
}
if (-not $Out) {
    $Out = @('server', 'client') | ForEach-Object { Join-Path $root "bin/$_/GloamingForest" } | Where-Object { Test-Path (Join-Path $_ 'behavior_packs') }
}
if (-not $Out) { Write-Host '[families] nothing to do: no assembled directory (build first or pass -Out)' -ForegroundColor Yellow; exit 0 }

$failed = $false
foreach ($dir in $Out) {
    $full = (Resolve-Path $dir).Path
    & xmake lua (Join-Path $root 'tools/families/run.lua') $root $full 'stamp'
    if ($LASTEXITCODE -ne 0) { $failed = $true }
}
if ($failed) { exit 1 }
exit 0
