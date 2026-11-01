<#
.SYNOPSIS
  Fetches the shared, read-only checkout of The Twilight Forest 4.8 that every unit reads.

.DESCRIPTION
  GloamingForest reproduces TF 4.8: TeamTwilight/twilightforest, branch 1.21.1, pinned commit
  9e40a75721c716b2fdf80c16d1058fe718825e9f. Every worktree and every agent reads TF from this one
  local checkout (rule R24), never through the GitHub API.

  - Location: $env:GF_TF_HOME or %LOCALAPPDATA%\gf-tf (outside every repository).
  - Blob-less partial clone (--filter=blob:none, depth 1): git downloads the trees, and blobs
    only for the files the sparse checkout materialises.
  - The sparse checkout EXCLUDES *.ogg and *.nbt. Those files are All Rights Reserved: their blobs
    are never downloaded, and nobody opens them (rule R10). The tree listing (names and blob ids)
    is still there, which is what tools/ci/arr-guard.ps1 compares against.
  - Verified: HEAD is the pinned commit, the working tree is clean, and no .ogg/.nbt file exists.
  - Read-only: checked-out files get the read-only attribute; never edit the checkout.
  - Idempotent and safe for concurrent callers: holds <throttle home>\tf-fetch.lock while it
    works; a caller that finds a verified checkout returns at once.

  Prints the checkout path on success. Exit code 0 = ready, 1 = failed.

.EXAMPLE
  pwsh tools/tf/fetch.ps1                 # no-op when the checkout is ready
  pwsh tools/tf/fetch.ps1 -Force          # verify everything again (re-checkout if needed)
  $tf = pwsh tools/tf/fetch.ps1 -Quiet    # the path, for scripts
#>
param(
    [string]$Dest,
    [switch]$Force,
    [switch]$Quiet,
    [int]$TimeoutMinutes = 60
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../lib/Throttle.psm1') -Force

$Commit = '9e40a75721c716b2fdf80c16d1058fe718825e9f'
$Branch = '1.21.1'
$Url = if ($env:GF_TF_URL) { $env:GF_TF_URL } else { 'https://github.com/TeamTwilight/twilightforest.git' }
if (-not $Dest) { $Dest = if ($env:GF_TF_HOME) { $env:GF_TF_HOME } else { Join-Path $env:LOCALAPPDATA 'gf-tf' } }
$Stamp = Join-Path $Dest '.git/gf-tf-ready'
$SparsePatterns = @('/*', '!*.ogg', '!*.nbt')

function Say([string]$m) { if (-not $Quiet) { Write-Host "[tf] $m" } }

function Invoke-TfGit {
    $out = & git -C $Dest @args 2>&1
    if ($LASTEXITCODE -ne 0) { throw "git $($args -join ' ') failed (exit $LASTEXITCODE): $($out -join "`n")" }
    return $out
}

function Test-Ready {
    if (-not (Test-Path $Stamp)) { return $false }
    if ((Get-Content $Stamp -Raw).Trim() -ne $Commit) { return $false }
    $head = (& git -C $Dest rev-parse HEAD 2>$null)
    return $LASTEXITCODE -eq 0 -and "$head".Trim() -eq $Commit
}

function Set-ReadOnly([bool]$On) {
    Get-ChildItem -LiteralPath $Dest -Recurse -File -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '[\\/]\.git[\\/]' } |
        ForEach-Object { if ($_.IsReadOnly -ne $On) { $_.IsReadOnly = $On } }
}

function Assert-Checkout {
    $head = "$(Invoke-TfGit rev-parse HEAD)".Trim()
    if ($head -ne $Commit) { throw "HEAD is $head, expected $Commit" }
    # The commit object hashes the whole tree: matching it verifies the tree listing.
    $type = "$(Invoke-TfGit cat-file -t $Commit)".Trim()
    if ($type -ne 'commit') { throw "$Commit is not a commit in $Dest" }
    $dirty = @(Invoke-TfGit status --porcelain --untracked-files=no)
    if ($dirty.Count -gt 0) { throw "the checkout has local changes ($($dirty.Count) files); run with -Force after removing $Dest" }
    $forbidden = @(Get-ChildItem -LiteralPath $Dest -Recurse -File -Force -Include '*.ogg', '*.nbt' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '[\\/]\.git[\\/]' })
    if ($forbidden.Count -gt 0) { throw "the checkout contains $($forbidden.Count) .ogg/.nbt file(s); they must never be checked out" }
    $listed = @(Invoke-TfGit ls-tree -r --name-only $Commit | Where-Object { $_ -match '\.(ogg|nbt)$' }).Count
    Say "verified $Commit ($listed .ogg/.nbt entries listed in the tree, none checked out)"
}

$lock = $null
try {
    if (-not $Force -and (Test-Ready)) { Say "ready: $Dest"; if ($Quiet) { $Dest }; exit 0 }
    $lock = Enter-GfLock -Path (Join-Path (Get-GfThrottleHome) 'tf-fetch.lock') -What 'TF checkout lock' -TimeoutMinutes $TimeoutMinutes
    if (-not $Force -and (Test-Ready)) { Say "ready (fetched by another caller): $Dest"; if ($Quiet) { $Dest }; exit 0 }

    if (-not (Test-Path (Join-Path $Dest '.git'))) {
        if ((Test-Path $Dest) -and (Get-ChildItem -LiteralPath $Dest -Force | Select-Object -First 1)) {
            throw "$Dest exists and is not a git checkout; move it away first"
        }
        New-Item -ItemType Directory -Force $Dest | Out-Null
        Say "initialising a blob-less partial clone in $Dest"
        Invoke-TfGit init --quiet | Out-Null
        Invoke-TfGit remote add origin $Url | Out-Null
    }
    Invoke-TfGit config core.longpaths true | Out-Null
    Invoke-TfGit config core.autocrlf false | Out-Null
    Invoke-TfGit config remote.origin.promisor true | Out-Null
    Invoke-TfGit config remote.origin.partialclonefilter 'blob:none' | Out-Null
    Invoke-TfGit config core.sparseCheckout true | Out-Null
    Invoke-TfGit config core.sparseCheckoutCone false | Out-Null
    # The sparse patterns are written before any checkout, so excluded blobs are never fetched.
    $info = Join-Path $Dest '.git/info'
    New-Item -ItemType Directory -Force $info | Out-Null
    Set-Content -Path (Join-Path $info 'sparse-checkout') -Value $SparsePatterns -Encoding ascii

    # GIT_NO_LAZY_FETCH: a promisor remote would otherwise fetch the commit to answer the check.
    $env:GIT_NO_LAZY_FETCH = '1'
    $have = (& git -C $Dest cat-file -t $Commit 2>$null)
    Remove-Item Env:GIT_NO_LAZY_FETCH
    if ("$have".Trim() -ne 'commit') {
        Say "fetching $Commit ($Branch) from $Url (trees only; blobs on checkout)"
        Invoke-TfGit fetch --quiet --filter=blob:none --depth 1 origin $Commit | Out-Null
    }
    Set-ReadOnly $false
    Say 'checking out (sparse: without *.ogg and *.nbt)'
    Invoke-TfGit -c advice.detachedHead=false checkout --quiet --force --detach $Commit | Out-Null
    Invoke-TfGit sparse-checkout reapply | Out-Null
    Assert-Checkout
    Set-ReadOnly $true
    Set-Content -Path $Stamp -Value $Commit -Encoding ascii
    Say "ready: $Dest"
    if ($Quiet) { $Dest }
    exit 0
} catch {
    Write-Host "[tf] FAILED: $_" -ForegroundColor Red
    exit 1
} finally {
    Exit-GfLock $lock
}
