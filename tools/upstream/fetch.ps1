<#
.SYNOPSIS
  Fetches the shared, read-only upstream reference that every unit reads, and builds its view.

.DESCRIPTION
  GloamingForest's Java reference is the upstream source pinned in NOTICE (repository, branch
  1.21.1, commit 9e40a75721c716b2fdf80c16d1058fe718825e9f). Every worktree and every agent reads it
  from this one local copy (rule R24), never through the GitHub API.

  - Clone: $env:GF_UPSTREAM_CLONE or %LOCALAPPDATA%\gf-upstream\clone (outside every repository).
  - View: $env:GF_UPSTREAM_HOME or %LOCALAPPDATA%\gf-upstream\view, the clone under this project's
    names (tools/upstream/Upstream.psm1). Every tool reads the view.
  - Blob-less partial clone (--filter=blob:none, depth 1): git downloads the trees, and blobs
    only for the files the sparse checkout materialises.
  - The sparse checkout EXCLUDES *.ogg and *.nbt. Those files are All Rights Reserved: their blobs
    are never downloaded, and nobody opens them (rule R10). The tree listing (names and blob ids)
    is still there, which is what tools/ci/arr-guard.ps1 compares against.
  - Verified: HEAD is the pinned commit, the working tree is clean, and no .ogg/.nbt file exists.
  - Read-only: checked-out files get the read-only attribute; never edit the clone.
  - Idempotent and safe for concurrent callers: holds <throttle home>\upstream-fetch.lock while it
    works; a caller that finds a verified clone and a current view returns at once.

  Prints the view path on success. Exit code 0 = ready, 1 = failed.

.EXAMPLE
  pwsh tools/upstream/fetch.ps1                  # no-op when the clone and the view are ready
  pwsh tools/upstream/fetch.ps1 -Force           # verify everything again (re-checkout if needed)
  $up = pwsh tools/upstream/fetch.ps1 -Quiet     # the view path, for scripts
#>
param(
    [string]$Dest,
    [switch]$Force,
    [switch]$Quiet,
    [int]$TimeoutMinutes = 60
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../lib/Throttle.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'Upstream.psm1') -Force

$pin = Get-UpstreamPin
$Commit = $pin.Commit
$Branch = $pin.Branch
$Url = $pin.Url
if (-not $Dest) { $Dest = Get-UpstreamClone }
$Stamp = Join-Path $Dest '.git/gf-upstream-ready'
$SparsePatterns = @('/*', '!*.ogg', '!*.nbt')

function Say([string]$m) { if (-not $Quiet) { Write-Host "[upstream] $m" } }

function Invoke-UpGit {
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
    $head = "$(Invoke-UpGit rev-parse HEAD)".Trim()
    if ($head -ne $Commit) { throw "HEAD is $head, expected $Commit" }
    # The commit object hashes the whole tree: matching it verifies the tree listing.
    $type = "$(Invoke-UpGit cat-file -t $Commit)".Trim()
    if ($type -ne 'commit') { throw "$Commit is not a commit in $Dest" }
    $dirty = @(Invoke-UpGit status --porcelain --untracked-files=no)
    if ($dirty.Count -gt 0) { throw "the checkout has local changes ($($dirty.Count) files); run with -Force after removing $Dest" }
    $forbidden = @(Get-ChildItem -LiteralPath $Dest -Recurse -File -Force -Include '*.ogg', '*.nbt' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '[\\/]\.git[\\/]' })
    if ($forbidden.Count -gt 0) { throw "the checkout contains $($forbidden.Count) .ogg/.nbt file(s); they must never be checked out" }
    $listed = @(Invoke-UpGit ls-tree -r --name-only $Commit | Where-Object { $_ -match '\.(ogg|nbt)$' }).Count
    Say "verified $Commit ($listed .ogg/.nbt entries listed in the tree, none checked out)"
}

$lock = $null
try {
    $lock = Enter-GfLock -Path (Join-Path (Get-GfThrottleHome) 'upstream-fetch.lock') -What 'upstream reference lock' -TimeoutMinutes $TimeoutMinutes
    if (-not $Force -and (Test-Ready)) {
        $view = Update-UpstreamView -Clone $Dest -Commit $Commit
        Say "ready: $Dest (view $view)"; if ($Quiet) { $view }; exit 0
    }

    if (-not (Test-Path (Join-Path $Dest '.git'))) {
        if ((Test-Path $Dest) -and (Get-ChildItem -LiteralPath $Dest -Force | Select-Object -First 1)) {
            throw "$Dest exists and is not a git checkout; move it away first"
        }
        New-Item -ItemType Directory -Force $Dest | Out-Null
        Say "initialising a blob-less partial clone in $Dest"
        Invoke-UpGit init --quiet | Out-Null
        Invoke-UpGit remote add origin $Url | Out-Null
    }
    Invoke-UpGit config core.longpaths true | Out-Null
    Invoke-UpGit config core.autocrlf false | Out-Null
    Invoke-UpGit config remote.origin.promisor true | Out-Null
    Invoke-UpGit config remote.origin.partialclonefilter 'blob:none' | Out-Null
    Invoke-UpGit config core.sparseCheckout true | Out-Null
    Invoke-UpGit config core.sparseCheckoutCone false | Out-Null
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
        Invoke-UpGit fetch --quiet --filter=blob:none --depth 1 origin $Commit | Out-Null
    }
    Set-ReadOnly $false
    Say 'checking out (sparse: without *.ogg and *.nbt)'
    Invoke-UpGit -c advice.detachedHead=false checkout --quiet --force --detach $Commit | Out-Null
    Invoke-UpGit sparse-checkout reapply | Out-Null
    Assert-Checkout
    Set-ReadOnly $true
    Set-Content -Path $Stamp -Value $Commit -Encoding ascii
    $view = Update-UpstreamView -Clone $Dest -Commit $Commit -Force
    Say "ready: $Dest (view $view)"
    if ($Quiet) { $view }
    exit 0
} catch {
    Write-Host "[upstream] FAILED: $_" -ForegroundColor Red
    exit 1
} finally {
    Exit-GfLock $lock
}
