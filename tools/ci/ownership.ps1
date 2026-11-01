<#
.SYNOPSIS
  Checks that a unit's change touches only paths the unit owns (rules R1, R3, R25).

.DESCRIPTION
  Compares the working tree (committed, staged, modified and untracked files) with the merge base
  of HEAD and -Base (default main; origin/main when there is no local main) and resolves the owner
  of every changed path with the `path` rows of docs/spec/owners.tsv: the most specific glob wins
  (most literal characters); two different owners at the same specificity are an error.

  A path is allowed for unit <u> when
    - its owner is <u>, or
    - it lies under src/feature/ext/<name>/ (contract extensions, rule R18), or
    - its owning row has mode `gen` under assets/gen/<layer>/ and <u> is the tool unit owning
      assets/gen/<layer>/** (F1 import, F2 geo, F3 audio): the tools generate every unit's layer
      (rule R3: generated files are only ever (re)written by the tool), or
    - <u> is F0 (the scaffold) and the path is one of its `stub` rows (placeholders in W0b
      directories, replaced by their owners).
  -Unit takes a unit id (F0, F4, U12, A8) or a unit name (core, dimension, tgmove).

  Exit code 0 = the change is owned, 1 = violations (listed).

.EXAMPLE
  pwsh tools/ci/ownership.ps1 -Unit f0
  pwsh tools/ci/ownership.ps1 -Unit biomes -Base origin/main
#>
param(
    [Parameter(Mandatory)][string]$Unit,
    [string]$Base = 'main',
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
            $pathRules.Add([pscustomobject]@{ glob = $c[1]; owner = $c[2]; mode = $(if ($c.Count -gt 3) { $c[3] } else { 'manual' }); re = Get-GlobRegexes $alt; score = $score })
        }
        # the tool unit of a generated layer: the owner of "assets/gen/<layer>/**"
        if ($c[1] -match '^assets/gen/([^/{}*?]+)/\*\*$') { $genTools[$Matches[1]] = $c[2] }
    } elseif ($c[0] -eq 'stub') { $stubs[$c[1]] = $c[2] }
}

function Get-PathOwner([string]$path) {
    $best = $null; $tie = $null
    foreach ($r in $pathRules) {
        if (-not $r.re.IsMatch($path)) { continue }
        if (-not $best -or $r.score -gt $best.score) { $best = $r; $tie = $null }
        elseif ($r.score -eq $best.score -and $r.owner -ne $best.owner) { $tie = $r }
    }
    if ($tie) { return @{ owner = $null; why = "ambiguous: $($best.glob) ($($best.owner)) vs $($tie.glob) ($($tie.owner))" } }
    if (-not $best) { return @{ owner = $null; why = 'no owner in docs/spec/owners.tsv' } }
    return @{ owner = $best.owner; why = $best.glob; mode = $best.mode }
}

# changed files since the merge base
& git -C $root rev-parse --verify --quiet $Base *> $null
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
Write-Host "[ownership] OK: $($changed.Count) changed path(s), all owned by $unitId ($unitName) since $($mergeBase.Substring(0, 8))" -ForegroundColor Green
exit 0
