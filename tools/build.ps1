<#
.SYNOPSIS
  Builds GloamingForest for the server (BDS), the client, or both, inside a build throttle slot.

.DESCRIPTION
  Adapted from LiteLDev/Levitate v0.1.0 (tools/build.ps1), LGPL-3.0-or-later.

  The only entry point for builds (rule R23). It takes a slot of the "build" pool
  (tools/lib/Throttle.psm1: 3 slots, granted only while >= 10 GB of memory is free) for the whole
  run, so '-Target all -Tests' builds server, client and the unit tests in one slot. 'xmake f'
  (which may install packages into the shared cache) runs under a separate machine-wide mutex.

  Each platform has its own build dir (build/<target>); outputs land in
    bin/<target>/GloamingForest/  (dll, pdb, manifest.json, lang/, data/, behavior_packs/, resource_packs/)
  Copy that folder to `plugins/` (BDS) or `mods/` (LeviLauncher client).

  Exit code 0 = built, 1 = failed, 2 = NOT RUN (no build slot within -SlotTimeoutMinutes).

.EXAMPLE
  pwsh tools/build.ps1                          # both platforms, release
  pwsh tools/build.ps1 -Target server           # one platform (iterate with this)
  pwsh tools/build.ps1 -Target all -Tests       # both platforms + gtest (before a PR)
#>
param(
    [ValidateSet('server', 'client', 'all')][string]$Target = 'all',
    [ValidateSet('release', 'debug')][string]$Mode = 'release',
    [switch]$Tests,
    [int]$Jobs = 6,
    [string]$Unit = $env:GF_UNIT,
    [int]$SlotTimeoutMinutes = 180,
    # Re-resolve the packages and rewrite xmake-requires.lock with the entries of every target type (-Target all only).
    [switch]$UpdateLock
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/..").Path
Import-Module (Join-Path $PSScriptRoot 'lib/Throttle.psm1') -Force

# Fresh shells may not have the tools on PATH yet. Append (never prepend) the registry PATH so the
# caller's PATH wins: on CI, setup-llvm puts its LLVM on PATH, and the runner's preinstalled
# C:\Program Files\LLVM (listed in the machine PATH) must not shadow it.
$env:Path = $env:Path + ';' + [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
if ($env:LLVM_PATH -and (Test-Path (Join-Path $env:LLVM_PATH 'bin\clang-cl.exe'))) {
    $env:Path = (Join-Path $env:LLVM_PATH 'bin') + ";$env:Path"
}
if (-not (Get-Command clang-cl -ErrorAction SilentlyContinue)) {
    foreach ($dir in "$env:ProgramFiles\LLVM\bin", "${env:ProgramFiles(x86)}\LLVM\bin") {
        if (Test-Path (Join-Path $dir 'clang-cl.exe')) { $env:Path = "$dir;$env:Path"; break }
    }
}
if (-not (Get-Command xmake -ErrorAction SilentlyContinue)) { Write-Host 'xmake not found on PATH' -ForegroundColor Red; exit 1 }
$clang = Get-Command clang-cl -ErrorAction SilentlyContinue
if ($clang) { Write-Host "==> using $($clang.Source) ($((& $clang.Source --version | Select-Object -First 1)))" }
git config --global core.longpaths true | Out-Null

# xmake needs to be told about Visual Studio 18 (2026) explicitly.
$vsArgs = @()
foreach ($pf in $env:ProgramFiles, ${env:ProgramFiles(x86)}) {
    if ($pf -and (Test-Path (Join-Path $pf 'Microsoft Visual Studio\18'))) { $vsArgs = @('--vs=2026'); break }
}

# xmake rewrites xmake-requires.lock on every configure with the entries of the configured target
# type only. The committed lock holds the entries of both (server and client): keep it as it is,
# or, with -UpdateLock, write the union of what both configures resolved.
$lockFile = Join-Path $root 'xmake-requires.lock'
$lockText = if (Test-Path $lockFile) { [IO.File]::ReadAllText($lockFile) } else { $null }
$lockEntries = [ordered]@{}
function Read-LockEntries([string]$text) {
    $out = [ordered]@{}
    foreach ($m in [regex]::Matches($text, '(?s)\n        \["([^"]+)"\] = \{.*?\n        \}')) { $out[$m.Groups[1].Value] = $m.Value.TrimStart("`n") }
    return $out
}
function Write-MergedLock {
    $keys = @($lockEntries.Keys | Sort-Object)
    $body = ($keys | ForEach-Object { $lockEntries[$_] }) -join ",`n"
    $text = "{`n    __meta__ = {`n        version = `"1.0`"`n    },`n    [`"windows|x64`"] = {`n$body`n    }`n}"
    [IO.File]::WriteAllText($lockFile, $text)
}
# -UpdateLock re-resolves both target types from scratch: a lock entry does not record its target
# type (only the package hash differs), so the stale entries of one target could not be told apart
# from the other target's after a version bump.
if ($UpdateLock -and $Target -ne 'all') {
    Write-Host '==> -UpdateLock needs -Target all (it rewrites the entries of both target types)' -ForegroundColor Red
    exit 1
}

$slot = $null
try {
    # xmake and its clang-cl children die with this process: a killed build never leaves compiles
    # running after its slot was released (tools/lib/Throttle.psm1, Enable-GfKillOnExit). Only when
    # this script is the process's own script (`pwsh tools/build.ps1`), never an interactive shell.
    if ([Environment]::GetCommandLineArgs() | Where-Object { $_ -like '*build.ps1' }) { Enable-GfKillOnExit | Out-Null }
    if ($env:GF_NO_THROTTLE -ne '1') {
        try {
            $slot = Enter-GfSlot -Pool build -Unit $Unit -TimeoutMinutes $SlotTimeoutMinutes
        } catch [System.TimeoutException] {
            Write-Host "==> NOT RUN: $($_.Exception.Message)" -ForegroundColor Yellow
            exit 2
        }
    }
    Push-Location $root
    try {
        # `-P $root` everywhere: a worktree nested in another checkout (.claude/worktrees/...) would
        # otherwise make xmake pick the outer project.
        $targets = if ($Target -eq 'all') { @('server', 'client') } else { @($Target) }
        foreach ($t in $targets) {
            Write-Host "==> configuring $t ($Mode)"
            # The package cache is shared by every worktree: one 'xmake f' (package install) at a time.
            $req = Enter-GfLock -Path (Join-Path (Get-GfThrottleHome) 'xmake-require.lock') -What 'xmake package lock' -TimeoutMinutes 60 -Unit $Unit
            try {
                if ($UpdateLock -and (Test-Path $lockFile)) { Remove-Item $lockFile }
                xmake f -P $root -a x64 -m $Mode -p windows --target_type=$t -o "build/$t" @vsArgs -y
                if ($LASTEXITCODE) { throw "xmake config failed for $t" }
                if ($UpdateLock) {
                    $resolved = Read-LockEntries ([IO.File]::ReadAllText($lockFile))
                    foreach ($k in $resolved.Keys) { $lockEntries[$k] = $resolved[$k] }
                    Write-MergedLock
                } elseif ($null -ne $lockText -and [IO.File]::ReadAllText($lockFile) -ne $lockText) {
                    [IO.File]::WriteAllText($lockFile, $lockText) # keep the entries of both target types
                }
            } finally { Exit-GfLock $req }
            Write-Host "==> building $t"
            xmake build -P $root -j $Jobs -y GloamingForest
            if ($LASTEXITCODE) { throw "build failed for $t" }
            $out = Join-Path $root "bin/$t/GloamingForest"
            if (-not (Test-Path (Join-Path $out 'GloamingForest.dll'))) { throw "missing output $out" }
            Write-Host "==> $t ready: $out"
        }
        if ($Tests) {
            Write-Host '==> unit tests'
            xmake build -P $root -j $Jobs -y gf-tests
            if ($LASTEXITCODE) { throw 'gf-tests build failed' }
            xmake run -P $root gf-tests
            if ($LASTEXITCODE) { throw 'unit tests failed' }
        }
    } finally {
        Pop-Location
    }
} catch {
    Write-Host "==> FAILED: $_" -ForegroundColor Red
    exit 1
} finally {
    Exit-GfSlot $slot
}
exit 0
