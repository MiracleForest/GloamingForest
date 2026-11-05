<#
.SYNOPSIS
  Contract "assemble" (F0): runs tools/xmake/assemble.lua without building the mod.

.DESCRIPTION
  -Check assembles the packs, lang, data and structures into a scratch directory and runs every
  check of the build (integer block states, permutation budget, dangling twilightforest:
  references, owners of every asset file, the pack path probes, the ARR hash guard), then holds the
  pack path probes to tools/ci/ownership.ps1's owner rule as well (-PackProbes). CI runs it on every push; the build
  (tools/build.ps1) runs the same assembly after linking.

  Exit code 0 = OK, 1 = a check failed.

.EXAMPLE
  pwsh tools/assemble.ps1 -Check
  pwsh tools/assemble.ps1 -Check -Out build/assemble-check
#>
param(
    [switch]$Check,
    [string]$Out
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/..").Path
if (-not $Check) { Write-Host 'usage: pwsh tools/assemble.ps1 -Check [-Out <dir>] (the build assembles bin/<target>/ itself)'; exit 1 }
if (-not $Out) { $Out = Join-Path $root 'build/assemble-check' }
$env:Path = $env:Path + ';' + [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
if (-not (Get-Command xmake -ErrorAction SilentlyContinue)) { Write-Host 'xmake not found on PATH' -ForegroundColor Red; exit 1 }
# -P: this checkout's project (a worktree nested in another checkout would otherwise see the outer one)
& xmake lua -P $root (Join-Path $root 'tools/xmake/assemble_check.lua') $root $Out
$assembled = $LASTEXITCODE -eq 0 # xmake exits 255 on a raise
# the pack path probes against tools/ci/ownership.ps1's resolver too (assemble.lua checked its own)
& (Join-Path $root 'tools/ci/ownership.ps1') -PackProbes
exit $(if ($assembled -and $LASTEXITCODE -eq 0) { 0 } else { 1 })