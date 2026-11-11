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

  -LeviLamina picks the LeviLamina version (and so the line) to build against. Every supported line
  is a lip variant (26.60 -> 26_60, 26.51 -> 26_51, lip.toml) with its own package lock,
  locks/<variant>.lock, which is copied over xmake-requires.lock before configuring.

  Exit code 0 = built, 1 = failed, 2 = NOT RUN (no build slot within -SlotTimeoutMinutes).

.EXAMPLE
  pwsh tools/build.ps1                          # both platforms, release
  pwsh tools/build.ps1 -Target server           # one platform (iterate with this)
  pwsh tools/build.ps1 -Target all -Tests       # both platforms + gtest (before a PR)
  pwsh tools/build.ps1 -Target server -KeepGoing # on a compile error, report every failing file
  pwsh tools/build.ps1 -Target all -LeviLamina 26.51.7 # the older supported line
#>
param(
    [ValidateSet('server', 'client', 'all')][string]$Target = 'all',
    [ValidateSet('release', 'debug')][string]$Mode = 'release',
    [switch]$Tests,
    [int]$Jobs = 6,
    [string]$Unit = $env:GF_UNIT,
    # The LeviLamina version to build against; its line names the lip variant and the lock file.
    [ValidatePattern('^\d+\.\d+\.\d+$')][string]$LeviLamina = '26.60.6',
    [int]$SlotTimeoutMinutes = 180,
    # Re-resolve the packages and rewrite locks/<variant>.lock with the entries of every target type (-Target all only).
    [switch]$UpdateLock,
    # After a failed compile, check every other out-of-date translation unit too (in the same slot) and
    # print all their errors: one slot wait surfaces every compile error, not just the first file's.
    [switch]$KeepGoing,
    # clang's per-file error limit in the -KeepGoing pass (0 = unlimited; clang's default is 20).
    [int]$ErrorLimit = 100
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

# The packages of each LeviLamina line are pinned by locks/<variant>.lock (26.60.6 -> 26_60), which
# xmake reads as xmake-requires.lock (not committed).
$llParts = $LeviLamina.Split('.')
$variant = "$($llParts[0])_$($llParts[1])"
$variantLock = Join-Path $root "locks/$variant.lock"
if (-not (Test-Path $variantLock) -and -not $UpdateLock) {
    Write-Host "==> no lock file locks/$variant.lock for LeviLamina $LeviLamina (create it with -Target all -UpdateLock)" -ForegroundColor Red
    exit 1
}
# xmake rewrites xmake-requires.lock on every configure with the entries of the configured target
# type only. The committed lock holds the entries of both (server and client): keep it as it is,
# or, with -UpdateLock, write the union of what both configures resolved.
$lockFile = Join-Path $root 'xmake-requires.lock'
$lockText = if (Test-Path $variantLock) { [IO.File]::ReadAllText($variantLock) } else { $null }
if ($null -ne $lockText) { [IO.File]::WriteAllText($lockFile, $lockText) }
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
    New-Item -ItemType Directory -Force (Split-Path $variantLock) | Out-Null
    [IO.File]::WriteAllText($variantLock, $text)
}
# -UpdateLock re-resolves both target types from scratch: a lock entry does not record its target
# type (only the package hash differs), so the stale entries of one target could not be told apart
# from the other target's after a version bump.
if ($UpdateLock -and $Target -ne 'all') {
    Write-Host '==> -UpdateLock needs -Target all (it rewrites the entries of both target types)' -ForegroundColor Red
    exit 1
}

# -KeepGoing: xmake has no keep-going mode (its job runner stops at the first failed compile), so after
# a failed build this checks every translation unit xmake would still compile, syntax only (/Zs), with
# xmake's own command lines (compile_commands.json) in parallel, and prints each one's errors.
# Out of date = no object file, or the object is older than its source or a project file its xmake
# depfile (build/<t>/.deps/**.obj.d) lists. $Prefixes: the source roots of the failed target.
function Invoke-KeepGoing([string]$t, [string[]]$Prefixes) {
    try {
        $buildDir = Join-Path $root "build/$t"
        $ccDir = Join-Path $buildDir 'keepgoing'
        # under the package lock like 'xmake f', and the committed lock file kept as it is
        $req = Enter-GfLock -Path (Join-Path (Get-GfThrottleHome) 'xmake-require.lock') -What 'xmake package lock' -TimeoutMinutes 60 -Unit $Unit
        try {
            xmake project -P $root -k compile_commands $ccDir | Out-Null
            $code = $LASTEXITCODE
            if ($null -ne $lockText -and [IO.File]::ReadAllText($lockFile) -ne $lockText) { [IO.File]::WriteAllText($lockFile, $lockText) }
        } finally { Exit-GfLock $req }
        $ccFile = Join-Path $ccDir 'compile_commands.json'
        if ($code -or -not (Test-Path $ccFile)) { Write-Host '==> keep-going: xmake project -k compile_commands failed'; return }
        $entries = Get-Content $ccFile -Raw | ConvertFrom-Json
        $mtimes = @{}
        function MTime([string]$p) {
            if (-not $mtimes.ContainsKey($p)) {
                # a path that is not a file (a fragment of a path with a space) never makes an object stale
                $mtimes[$p] = if (Test-Path -LiteralPath $p -PathType Leaf) { (Get-Item -LiteralPath $p).LastWriteTimeUtc } else { [datetime]::MinValue }
            }
            return $mtimes[$p]
        }
        $todo = [Collections.Generic.List[object]]::new()
        $seen = @{}
        foreach ($e in $entries) {
            $dir = $e.directory
            $file = if ([IO.Path]::IsPathRooted($e.file)) { $e.file } else { Join-Path $dir $e.file }
            $relFile = [IO.Path]::GetRelativePath($root, $file).Replace('\', '/')
            if (-not ($Prefixes | Where-Object { $relFile.StartsWith("$_/") })) { continue }
            $cmd = @($e.arguments)
            if (-not $cmd) { continue }
            $obj = $cmd | Where-Object { $_ -match '^[-/]Fo(.+)$' } | ForEach-Object { $_.Substring(3) } | Select-Object -First 1
            if (-not $obj) { continue }
            $objFull = if ([IO.Path]::IsPathRooted($obj)) { $obj } else { Join-Path $dir $obj }
            if ($seen.ContainsKey($objFull)) { continue }
            $seen[$objFull] = $true
            $stale = $true
            if (Test-Path -LiteralPath $objFull) {
                $objTime = (Get-Item -LiteralPath $objFull).LastWriteTimeUtc
                $deps = @($file)
                # build/<t>/o/<target>/<src>.obj (GloamingForest) or build/<t>/.objs/<target>/<plat>/<src>.obj
                $relObj = [IO.Path]::GetRelativePath($buildDir, $objFull).Replace('\', '/')
                $depFile = Join-Path $buildDir ('.deps/' + ($relObj -replace '^(o|\.objs)/', '') + '.d')
                if (Test-Path -LiteralPath $depFile) {
                    $m = [regex]::Match((Get-Content -LiteralPath $depFile -Raw), '(?s)depfiles\s*=\s*"(.*?)(?<!\\)",')
                    if ($m.Success) {
                        # a Lua string ('\\' = '\', '\'+newline = newline) holding a make rule
                        # ("<obj>: \<newline>  <dep> \<newline> ..."); project files are the relative paths
                        $text = $m.Groups[1].Value.Replace("`r", '').Replace('\\', "`u{1}").Replace("\`n", "`n").Replace("`u{1}", '\')
                        # make continuations, then escaped spaces kept inside their path
                        $text = $text.Replace("\`n", ' ').Replace('\ ', "`u{2}")
                        $paths = ($text -split '\s+') | Where-Object { $_ } | Select-Object -Skip 1 | ForEach-Object { $_.Replace("`u{2}", ' ') }
                        $deps += $paths | Where-Object { -not [IO.Path]::IsPathRooted($_) -and $_ -notmatch '^[A-Za-z]:' } | ForEach-Object { Join-Path $root $_ }
                    }
                }
                $stale = [bool]($deps | Where-Object { (MTime $_) -gt $objTime } | Select-Object -First 1)
            }
            if ($stale) {
                $args2 = @($cmd | Select-Object -Skip 1 | Where-Object { $_ -notmatch '^[-/]F[od]' }) + @('/Zs', "/clang:-ferror-limit=$ErrorLimit")
                $todo.Add([pscustomobject]@{ exe = $cmd[0]; args = $args2; dir = $dir; file = $relFile })
            }
        }
        if ($todo.Count -eq 0) { Write-Host '==> keep-going: no other out-of-date translation unit'; return }
        Write-Host "==> keep-going: checking $($todo.Count) out-of-date translation unit(s) (syntax only, $Jobs at a time)"
        $results = $todo | ForEach-Object -ThrottleLimit $Jobs -Parallel {
            Set-Location -LiteralPath $_.dir
            $out = & $_.exe @($_.args) 2>&1 | ForEach-Object { "$_" }
            [pscustomobject]@{ file = $_.file; ok = ($LASTEXITCODE -eq 0); out = $out }
        }
        $failed = @($results | Where-Object { -not $_.ok } | Sort-Object file)
        foreach ($r in $failed) {
            Write-Host "---- $($r.file)" -ForegroundColor Red
            $r.out | Where-Object { $_ } | ForEach-Object { Write-Host $_ }
        }
        Write-Host "==> keep-going: $($failed.Count) of $($todo.Count) translation unit(s) failed: $(($failed | ForEach-Object file) -join ', ')" -ForegroundColor Red
    } catch {
        Write-Host "==> keep-going pass failed: $_" -ForegroundColor Yellow
    }
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
            Write-Host "==> configuring $t ($Mode, LeviLamina $LeviLamina)"
            # The package cache is shared by every worktree: one 'xmake f' (package install) at a time.
            $req = Enter-GfLock -Path (Join-Path (Get-GfThrottleHome) 'xmake-require.lock') -What 'xmake package lock' -TimeoutMinutes 60 -Unit $Unit
            try {
                if ($UpdateLock -and (Test-Path $lockFile)) { Remove-Item $lockFile }
                xmake f -P $root -a x64 -m $Mode -p windows --target_type=$t --levilamina=$LeviLamina -o "build/$t" @vsArgs -y
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
            if ($LASTEXITCODE) {
                if ($KeepGoing) { Invoke-KeepGoing $t @('src') }
                throw "build failed for $t"
            }
            $out = Join-Path $root "bin/$t/GloamingForest"
            if (-not (Test-Path (Join-Path $out 'GloamingForest.dll'))) { throw "missing output $out" }
            Write-Host "==> $t ready: $out"
        }
        if ($Tests) {
            Write-Host '==> unit tests'
            xmake build -P $root -j $Jobs -y gf-tests
            if ($LASTEXITCODE) {
                if ($KeepGoing) { Invoke-KeepGoing $targets[-1] @('tests', 'src/core') }
                throw 'gf-tests build failed'
            }
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
