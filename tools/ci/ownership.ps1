<#
.SYNOPSIS
  Checks that a unit's change touches only paths the unit owns (rules R1, R3, R25).

.DESCRIPTION
  Compares the working tree (committed, staged, modified and untracked files) with the merge base
  of HEAD and -Base and resolves the owner
  of every changed path with the `path` rows of docs/spec/owners.tsv: the most specific glob wins
  (most literal characters); two different owners at the same specificity are an error. Unit-folder
  rules (`<prefix>/**/<folder>/**`) that match a path count as one group with the score of the best
  of them, and inside the group the folder nearest the file wins: assets/pack/rp/entity/hydra/x.json
  belongs to hydra, not to the unit named entity (tools/xmake/assemble.lua resolves the same way).

  A path is allowed for unit <u> when
    - its owner is <u>, or
    - it lies under src/feature/ext/<name>/ (contract extensions, rule R18), or
    - its owning row has mode `gen` under assets/gen/<layer>/ and <u> is the tool unit owning
      assets/gen/<layer>/** (F1 import, F2 geo, F3 audio): the tools generate every unit's layer
      (rule R3: generated files are only ever (re)written by the tool), or
    - <u> is F0 (the scaffold) and the path is one of its `stub` rows (placeholders in W0b
      directories, replaced by their owners).
  -Unit takes a unit id (F0, F4, U12, A8) or a unit name (core, dimension, tgmove).
  -Base defaults to origin/main when that ref exists, else main: in a worktree the local main is
  usually behind origin/main, and a stale base lists other units' merged changes as violations
  (fetch first: git fetch origin). A -Base that does not exist is tried as origin/<Base>.

  Exit code 0 = the change is owned, 1 = violations (listed).

.EXAMPLE
  pwsh tools/ci/ownership.ps1 -Unit f0                       # against origin/main (main without a remote)
  pwsh tools/ci/ownership.ps1 -Unit biomes -Base origin/main
#>
param(
    [Parameter(Mandatory)][string]$Unit,
    [string]$Base,
    [string]$Owners
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
if (-not $Owners) { $Owners = Join-Path $root 'docs/spec/owners.tsv' }
$rules = Get-Content (Join-Path $root 'tools/spec/owners.rules.json') -Raw | ConvertFrom-Json -AsHashtable

# unit id from an id or a name
$unitId = $null
foreach ($k in $rules.units.Keys) {
    if ($k -ieq $Unit -or $rules.units[$k].name -ieq $Unit) { $unitId = $k; break }
}
if (-not $unitId) { Write-Host "[ownership] unknown unit '$Unit'" -ForegroundColor Red; exit 1 }
$unitName = $rules.units[$unitId].name

function Get-GlobRegexes([string]$glob) {
    # brace alternatives and "**/" (zero or more directories) as in tools/xmake/assemble.lua
    $sb = [Text.StringBuilder]::new('^')
    $i = 0
    while ($i -lt $glob.Length) {
        $c = $glob[$i]
        if ($c -eq '*' -and $i + 2 -lt $glob.Length -and $glob[$i + 1] -eq '*' -and $glob[$i + 2] -eq '/') { [void]$sb.Append('(?:.*/)?'); $i += 3; continue }
        if ($c -eq '*' -and $i + 1 -lt $glob.Length -and $glob[$i + 1] -eq '*') { [void]$sb.Append('.*'); $i += 2; continue }
        switch ($c) {
            '*' { [void]$sb.Append('[^/]*') }
            '?' { [void]$sb.Append('[^/]') }
            '{' { [void]$sb.Append('(?:') }
            '}' { [void]$sb.Append(')') }
            ',' { [void]$sb.Append('|') }
            default { [void]$sb.Append([regex]::Escape([string]$c)) }
        }
        $i++
    }
    [void]$sb.Append('$')
    return [regex]::new($sb.ToString())
}

# "a/{b,c}/d" -> "a/b/d", "a/c/d"
function Expand-Braces([string]$glob) {
    $m = [regex]::Match($glob, '\{([^{}]*)\}')
    if (-not $m.Success) { return @($glob) }
    $out = @()
    foreach ($alt in $m.Groups[1].Value.Split(',')) {
        $out += Expand-Braces ($glob.Substring(0, $m.Index) + $alt + $glob.Substring($m.Index + $m.Length))
    }
    return $out
}

$pathRules = [Collections.Generic.List[object]]::new()
$stubs = @{}
$genTools = @{}
foreach ($line in Get-Content $Owners) {
    if ($line.StartsWith('#') -or $line.StartsWith("kind`t")) { continue }
    $c = $line -split "`t"
    if ($c.Count -lt 3) { continue }
    if ($c[0] -eq 'path') {
        # one rule per brace alternative, so a rule's specificity is that of the alternative matching
        foreach ($alt in Expand-Braces $c[1]) {
            $score = ($alt -replace '[\*\?]', '').Length + $(if ($alt -notmatch '[\*\?]') { 1000 } else { 0 })
            # a unit-folder rule "<prefix>/**/<folder>/**" ranks by how deep its folder matches
            $folder = if ($alt -match '^.+/\*\*/([^/*?]+)/\*\*$') { $Matches[1] } else { $null }
            $pathRules.Add([pscustomobject]@{ glob = $c[1]; owner = $c[2]; mode = $(if ($c.Count -gt 3) { $c[3] } else { 'manual' }); re = Get-GlobRegexes $alt; score = $score; folder = $folder })
        }
        # the tool unit of a generated layer: the owner of "assets/gen/<layer>/**"
        if ($c[1] -match '^assets/gen/([^/{}*?]+)/\*\*$') { $genTools[$Matches[1]] = $c[2] }
    } elseif ($c[0] -eq 'stub') { $stubs[$c[1]] = $c[2] }
}

function Get-FolderDepth([string]$path, [string]$folder) {
    # index of the deepest directory of $path named $folder (the file name itself does not count)
    $segs = $path.Split('/')
    for ($i = $segs.Count - 2; $i -ge 0; $i--) { if ($segs[$i] -eq $folder) { return $i } }
    return -1
}

function Get-PathOwner([string]$path) {
    $matching = @($pathRules | Where-Object { $_.re.IsMatch($path) })
    if (-not $matching.Count) { return @{ owner = $null; why = 'no owner in docs/spec/owners.tsv' } }
    # unit-folder rules compete as one group scored by their best member; the deepest folder wins it
    $folders = @($matching | Where-Object { $_.folder })
    $cands = [Collections.Generic.List[object]]::new()
    foreach ($r in $matching) { if (-not $r.folder) { $cands.Add([pscustomobject]@{ rule = $r; score = $r.score; depth = 0 }) } }
    if ($folders.Count) {
        $groupScore = ($folders | Measure-Object -Property score -Maximum).Maximum
        foreach ($r in $folders) { $cands.Add([pscustomobject]@{ rule = $r; score = $groupScore; depth = (Get-FolderDepth $path $r.folder) * 10000 + $r.score }) }
    }
    $best = $null; $tie = $null
    foreach ($c in $cands) {
        if (-not $best -or $c.score -gt $best.score -or ($c.score -eq $best.score -and $c.depth -gt $best.depth)) { $best = $c; $tie = $null }
        elseif ($c.score -eq $best.score -and $c.depth -eq $best.depth -and $c.rule.owner -ne $best.rule.owner) { $tie = $c }
    }
    if ($tie) { return @{ owner = $null; why = "ambiguous: $($best.rule.glob) ($($best.rule.owner)) vs $($tie.rule.glob) ($($tie.rule.owner))" } }
    return @{ owner = $best.rule.owner; why = $best.rule.glob; mode = $best.rule.mode }
}

# changed files since the merge base; the default base is origin/main (the local main of a worktree
# is usually stale), main when there is no remote
if (-not $Base) {
    & git -C $root rev-parse --verify --quiet 'origin/main^{commit}' *> $null
    $Base = if ($LASTEXITCODE -eq 0) { 'origin/main' } else { 'main' }
}
& git -C $root rev-parse --verify --quiet "$Base^{commit}" *> $null
if ($LASTEXITCODE -ne 0) { $Base = "origin/$Base" }
$mergeBase = (& git -C $root merge-base HEAD $Base 2>$null)
if ($LASTEXITCODE -ne 0 -or -not $mergeBase) { Write-Host "[ownership] no merge base with $Base" -ForegroundColor Red; exit 1 }
$changed = @(& git -C $root diff --name-only --no-renames $mergeBase) + @(& git -C $root ls-files --others --exclude-standard)
$changed = @($changed | Where-Object { $_ } | Sort-Object -Unique)

$violations = [Collections.Generic.List[string]]::new()
foreach ($p in $changed) {
    if ($p.StartsWith("src/feature/ext/$unitName/")) { continue }
    if ($unitId -eq 'F0' -and $stubs.ContainsKey($p)) { continue }
    $o = Get-PathOwner $p
    if ($o.owner -and $o.owner -ne $unitId -and $o.mode -eq 'gen' -and $p -match '^assets/gen/([^/]+)/' -and $genTools[$Matches[1]] -eq $unitId) { continue }
    if ($o.owner -ne $unitId) {
        $who = if ($o.owner) { "owned by $($o.owner) ($($o.why))" } else { $o.why }
        $violations.Add("$p : $who")
    }
}
if ($violations.Count) {
    Write-Host "[ownership] $unitId ($unitName) touches $($violations.Count) path(s) it does not own:" -ForegroundColor Red
    $violations | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}
Write-Host "[ownership] OK: $($changed.Count) changed path(s), all owned by $unitId ($unitName) since $($mergeBase.Substring(0, 8)) ($Base)" -ForegroundColor Green
exit 0
