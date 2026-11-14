<#
.SYNOPSIS
  Sound lint (unit F3, audit A7): the assembled sound definitions against upstream's 328 events and 9 tracks.

.DESCRIPTION
  Reads an assembled mod directory (tools/assemble.ps1 -Check writes build/assemble-check; the build
  writes bin/<target>/GloamingForest) and the recipes, and checks:

    events    every event of upstream's inventory (tools/sounds/baseline/inventory.json, 328) is in the
              resource pack's sounds/sound_definitions.json with upstream's variant count; every sound file a
              definition names is a file of the pack (.ogg) or of Bedrock's vanilla resource packs
              (-Vanilla, their contents.json); every packed .ogg is an Ogg stream.
    design    how each event sounds: "refined" (its owner's recipe, tools/sounds/recipes[.d]/<u>.json,
              defines the event or a file / patch it plays), "baseline" (F3's name-derived design from
              tools/sounds/baseline/<u>.json only: the placeholder level), "vanilla" (upstream plays a vanilla
              sound: Bedrock's own file of it). Printed per owner; -MaxBaseline fails when more events
              than that are baseline-only.
    music     upstream's 9 tracks (tools/sounds/baseline/music/*.json) each have U10's composition
              (tools/sounds/music/<track>.json), not the baseline placeholder; the music events and the
              biome music of sounds/music_definitions.json are defined.

  Never opens upstream's audio (rule R10).

  Exit code 0 = OK, 1 = a check failed (listed), 2 = no assembled directory.

.EXAMPLE
  pwsh tools/assemble.ps1 -Check; pwsh tools/sounds/lint.ps1
  pwsh tools/sounds/lint.ps1 -Out bin/client/GloamingForest -MaxBaseline 0 -Json build/sound-lint.json
#>
param(
    [string]$Out,
    [string]$Vanilla,
    [int]$MaxBaseline = -1,
    [string]$Json
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = (Resolve-Path "$PSScriptRoot/../..").Path

if (-not $Out) {
    foreach ($c in 'build/assemble-check', 'bin/server/GloamingForest') {
        if (Test-Path (Join-Path $root "$c/resource_packs/GloamingForest_RP")) { $Out = Join-Path $root $c; break }
    }
}
if (-not $Out -or -not (Test-Path (Join-Path $Out 'resource_packs/GloamingForest_RP'))) {
    Write-Host '[sound-lint] no assembled mod: run pwsh tools/assemble.ps1 -Check (or a build) first, or pass -Out' -ForegroundColor Red
    exit 2
}
$Out = (Resolve-Path $Out).Path
$rp = Join-Path $Out 'resource_packs/GloamingForest_RP'
if (-not $Vanilla) {
    $v = Join-Path $env:APPDATA 'levilauncher.exe/versions/1.26.51.01/data/resource_packs'
    if (Test-Path $v) { $Vanilla = $v }
}

$problems = [Collections.Generic.List[string]]::new()
function Problem([string]$msg) { $problems.Add($msg) }
function Read-Json([string]$path) {
    try {
        $r = Get-Content -LiteralPath $path -Raw -Encoding utf8 | ConvertFrom-Json -AsHashtable -Depth 100
        if ($null -ne $r) { return $r }
        Problem "${path}: empty"
    } catch { Problem "${path}: $($_.Exception.Message)" }
    return @{}
}

# ---- inventory, owners, recipes -----------------------------------------------------------------------
$inventory = (Read-Json (Join-Path $root 'tools/sounds/baseline/inventory.json'))['events']
if ($inventory -isnot [Collections.IDictionary]) { Write-Host '[sound-lint] tools/sounds/baseline/inventory.json has no events' -ForegroundColor Red; exit 1 }
$recipes = @{}
function Get-Refinement([string]$u) {
    if (-not $recipes.ContainsKey($u)) {
        $doc = $null
        foreach ($rel in "tools/sounds/recipes/$u.json", "tools/sounds/recipes.d/$u.json") {
            $p = Join-Path $root $rel
            if (Test-Path $p) { $doc = Read-Json $p; break }
        }
        $recipes[$u] = $doc
    }
    return $recipes[$u]
}
$baselines = @{}
function Get-Baseline([string]$u) {
    if (-not $baselines.ContainsKey($u)) {
        $p = Join-Path $root "tools/sounds/baseline/$u.json"
        $baselines[$u] = if (Test-Path $p) { Read-Json $p } else { @{ files = @{}; events = @{} } }
    }
    return $baselines[$u]
}
function Section($doc, [string]$name) {
    if ($doc -is [Collections.IDictionary] -and $doc.Contains($name) -and $doc[$name] -is [Collections.IDictionary]) { return $doc[$name] }
    return @{}
}

# The design level of one event: vanilla, refined or baseline (aliases follow the aliased event).
function Get-Design([string]$e, [int]$depth = 0) {
    if ($depth -gt 8 -or -not $inventory.Contains($e)) { return 'baseline' }
    $u = $inventory[$e]['owner']
    $ref = Get-Refinement $u
    $refEvents = Section $ref 'events'
    $refFiles = Section $ref 'files'
    $refPatches = Section $ref 'patches'
    if ($refEvents.Contains($e)) {
        # the owner rewrote the event: refined unless every variant is still a baseline file
        $def = $refEvents[$e]
    } else {
        $def = (Section (Get-Baseline $u) 'events')[$e]
    }
    if ($def -isnot [Collections.IDictionary] -or -not $def.Contains('variants')) {
        Problem "${e}: neither $u's recipe nor its baseline (tools/sounds/baseline/$u.json) defines it"
        return 'baseline'
    }
    $kinds = @()
    foreach ($v in @($def['variants'])) {
        if ($v.Contains('vanilla')) { $kinds += 'vanilla'; continue }
        if ($v.Contains('event')) { $kinds += Get-Design (($v['event']) -replace '^gloamingforest:', '') ($depth + 1); continue }
        if ($v.Contains('file')) {
            $f = $v['file']
            if ($refFiles.Contains($f)) { $kinds += 'refined'; continue }
            $spec = (Section (Get-Baseline $u) 'files')[$f]
            $patch = if ($spec -is [Collections.IDictionary] -and $spec.Contains('patch')) { $spec['patch'] } else { $null }
            if ($spec -is [Collections.IDictionary] -and $spec.Contains('track')) { $kinds += 'refined'; continue } # a composed track (checked below)
            if ($patch -is [string] -and $refPatches.Contains($patch)) { $kinds += 'refined'; continue }
            $kinds += $(if ($refEvents.Contains($e)) { 'refined' } else { 'baseline' })
            continue
        }
        $kinds += $(if ($refEvents.Contains($e)) { 'refined' } else { 'baseline' }) # an inline patch
    }
    if ($kinds -contains 'refined') { return 'refined' }
    if ($kinds.Count -and -not ($kinds | Where-Object { $_ -ne 'vanilla' })) { return 'vanilla' }
    return 'baseline'
}

# ---- the assembled definitions ----------------------------------------------------------------------------
$defsPath = Join-Path $rp 'sounds/sound_definitions.json'
if (-not (Test-Path $defsPath)) { Write-Host "[sound-lint] $defsPath is missing" -ForegroundColor Red; exit 1 }
$defs = (Read-Json $defsPath)['sound_definitions']
if ($defs -isnot [Collections.IDictionary]) { Write-Host "[sound-lint] $defsPath has no sound_definitions" -ForegroundColor Red; exit 1 }

$packFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($f in Get-ChildItem (Join-Path $rp 'sounds') -Recurse -File) { [void]$packFiles.Add($f.FullName.Substring($rp.Length + 1).Replace('\', '/')) }
$vanillaFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
if ($Vanilla -and (Test-Path $Vanilla)) {
    foreach ($d in Get-ChildItem $Vanilla -Directory -Filter 'vanilla*') {
        $list = Join-Path $d.FullName 'contents.json'
        if (Test-Path $list) { foreach ($e in (Read-Json $list)['content']) { [void]$vanillaFiles.Add([string]$e['path']) } }
    }
}
$unverified = 0

function Test-SoundName([string]$event, [string]$name) {
    foreach ($ext in '.ogg', '.fsb', '.wav') { if ($packFiles.Contains("$name$ext")) { return } }
    if ($name -like 'sounds/gloamingforest/*') { Problem "${event}: $name is not in the pack"; return }
    if ($vanillaFiles.Count) {
        foreach ($ext in '.ogg', '.fsb', '.wav') { if ($vanillaFiles.Contains("$name$ext")) { return } }
        Problem "${event}: $name is neither in the pack nor in Bedrock's vanilla resource packs"
    } else { $script:unverified++ }
}

$byOwner = [ordered]@{}
$baselineEvents = @()
foreach ($e in $inventory.Keys) {
    $u = $inventory[$e]['owner']
    if (-not $byOwner.Contains($u)) { $byOwner[$u] = [ordered]@{ refined = 0; baseline = 0; vanilla = 0 } }
    if (-not $defs.Contains($e)) { Problem "$e (owner $u) is not in sound_definitions.json"; continue }
    $sounds = @($defs[$e]['sounds'])
    $want = @($inventory[$e]['variants']).Count
    if ($sounds.Count -ne $want) { Problem "${e}: $($sounds.Count) sounds, upstream plays $want" }
    foreach ($s in $sounds) { Test-SoundName $e $(if ($s -is [string]) { $s } else { $s['name'] }) }
    $design = Get-Design $e
    $byOwner[$u][$design]++
    if ($design -eq 'baseline') { $baselineEvents += $e }
}
foreach ($e in $defs.Keys) {
    if (-not $inventory.Contains($e) -and $e -like '*gloamingforest*') { Problem "$e is defined but is not one of upstream's sound events" }
}

# every packed .ogg is an Ogg stream
$bad = 0
foreach ($f in Get-ChildItem (Join-Path $rp 'sounds') -Recurse -Filter *.ogg) {
    $ok = $false
    if ($f.Length -gt 64) {
        $fs = [IO.File]::OpenRead($f.FullName)
        try { $buf = [byte[]]::new(4); [void]$fs.Read($buf, 0, 4); $ok = [Text.Encoding]::ASCII.GetString($buf) -eq 'OggS' } finally { $fs.Dispose() }
    }
    if (-not $ok) { $bad++; if ($bad -le 5) { Problem "$($f.FullName.Substring($rp.Length + 1)): not an Ogg stream" } }
}
if ($bad -gt 5) { Problem "$bad packed .ogg files are not Ogg streams in total (5 listed)" }

# ---- music ---------------------------------------------------------------------------------------------------
$tracks = @(Get-ChildItem (Join-Path $root 'tools/sounds/baseline/music') -Filter *.json | ForEach-Object BaseName | Sort-Object)
$placeholders = @($tracks | Where-Object { -not (Test-Path (Join-Path $root "tools/sounds/music/$_.json")) })
if ($tracks.Count -ne 9) { Problem "$($tracks.Count) baseline tracks, upstream has 9" }
foreach ($t in $placeholders) { Problem "track $t is still the baseline placeholder (U10: tools/sounds/music/$t.json)" }
$md = Join-Path $rp 'sounds/music_definitions.json'
if (Test-Path $md) {
    $music = Read-Json $md
    foreach ($b in $music.Keys) {
        $ev = $music[$b]['event_name']
        if ($ev -and $ev -like '*gloamingforest*' -and -not $defs.Contains($ev)) { Problem "music_definitions.json ${b}: $ev is not defined" }
    }
}

# ---- report ----------------------------------------------------------------------------------------------------
$tot = [ordered]@{ refined = 0; baseline = 0; vanilla = 0 }
foreach ($u in $byOwner.Keys) { foreach ($k in 'refined', 'baseline', 'vanilla') { $tot[$k] += $byOwner[$u][$k] } }
Write-Host "[sound-lint] $Out"
Write-Host "[sound-lint] $($inventory.Count) upstream events: $($tot.refined) refined by their owner, $($tot.vanilla) vanilla, $($tot.baseline) baseline-only (F3's name-derived design); $($tracks.Count - $placeholders.Count)/$($tracks.Count) tracks composed"
foreach ($u in $byOwner.Keys) {
    $o = $byOwner[$u]
    if ($o.baseline) { Write-Host ("             {0,-15} {1,3} refined {2,3} vanilla {3,3} baseline" -f $u, $o.refined, $o.vanilla, $o.baseline) }
}
if (-not $vanillaFiles.Count) { Write-Host "[sound-lint] no vanilla resource packs (-Vanilla): $unverified vanilla sound names unverified" -ForegroundColor Yellow }
if ($MaxBaseline -ge 0 -and $tot.baseline -gt $MaxBaseline) { Problem "$($tot.baseline) events are baseline-only (at most $MaxBaseline allowed)" }
if ($Json) {
    [ordered]@{ out = $Out; events = $inventory.Count; totals = $tot; owners = $byOwner; baselineEvents = $baselineEvents
        tracks = $tracks; placeholderTracks = $placeholders; problems = @($problems)
    } | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 $Json
}
if ($problems.Count) {
    foreach ($p in $problems | Select-Object -First 40) { Write-Host "[sound-lint] $p" -ForegroundColor Red }
    Write-Host "[sound-lint] FAILED: $($problems.Count) problem(s)" -ForegroundColor Red
    exit 1
}
Write-Host '[sound-lint] OK' -ForegroundColor Green
exit 0
