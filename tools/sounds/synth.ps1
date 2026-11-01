<#
.SYNOPSIS
  Contract "synth" (F3): renders GloamingForest's original sounds and music from recipes.

.DESCRIPTION
  Every sound event of The Twilight Forest (docs/spec/owners.tsv, kind sound_event: 328) gets a
  Bedrock sound definition rendered by the procedural synthesizer (src/core/synth, compiled
  into tools/sounds/cli). TF's own audio is All Rights Reserved and is never read (rule R10):
  everything is synthesized from recipes, or refers to Bedrock's vanilla sounds where TF itself
  plays a vanilla event.

  Recipes, per owning unit <u> (the unit's name, e.g. fauna):
    tools/sounds/baseline/<u>.json   F3's baseline (generated once from TF's event inventory)
    tools/sounds/recipes/<u>.json    the owner's refinements (optional; wins per event, file and patch)
    tools/sounds/recipes.d/<u>.json  the same for owners without a recipes/ row in owners.tsv (the W0b
                                     units F4 dimension and F8 entity own it as a *.d/<u>.json
                                     fragment, rule R3); a unit keeps one of the two
  Shared patches: tools/sounds/patches/**/*.json ("creature/growl"). Music: tracker tracks
  tools/sounds/music/<track>.json (U10) or tools/sounds/baseline/music/<track>.json.

  Output (generated layer, never edit by hand, rule R3), per owner <u>, only from <u>'s recipes
  (plus the shared patches and tracks):
    assets/gen/audio/<u>/rp/sounds/twilightforest/<u>/<file>.ogg
    assets/gen/audio/<u>/rp/sounds.d/<u>.json          sound_definitions fragment
    assets/gen/audio/<u>/aliases.json                  snapshot of the other owners' sounds that <u>'s
                                                       events play ({"event": id} variants), rendered
                                                       as copies under sounds/twilightforest/<u>/alias/

  Every definition must match TF's inventory (tools/sounds/baseline/inventory.json, generated
  from TF's sounds.json): variant count, volume, pitch, stream, subtitle and category.

  The recipe format, the patch language and the tracker format are documented in
  tools/sounds/README.md.

  The renderer is compiled with clang-cl from tools/sounds/cli/SynthCli.cpp and
  src/core/synth/*.cpp (standard library only) into %LOCALAPPDATA%\gf-synth\<hash>\ the first time
  a source changes: a few seconds of a single compiler process.

.PARAMETER Unit
  Only this owner (unit id or name: U16, fauna). Default: every owner.
.PARAMETER Check
  Renders everything into a temporary directory and fails unless the committed tree is
  byte-identical (deterministic output), every event is covered and matches TF's inventory, and
  src/feature/ext/synth/SoundEvents.inc matches the inventory. Alias copies are rendered from the
  owner's snapshot (aliases.json); a snapshot older than the aliased owner's current recipe is a
  warning (the aliasing owner refreshes it with -Unit, A8 with a full render), not a failure.
.PARAMETER Event
  With -Preview: only this event (or a wildcard).
.PARAMETER Preview
  Renders .wav previews into %TEMP%\gf-synth-preview\ instead of writing the tree.

.EXAMPLE
  pwsh tools/sounds/synth.ps1                       # every owner (refreshes every alias snapshot)
  pwsh tools/sounds/synth.ps1 -Unit fauna           # one owner
  pwsh tools/sounds/synth.ps1 -Check                # CI: deterministic, complete, TF's inventory
  pwsh tools/sounds/synth.ps1 -Preview -Event 'entity.twilightforest.naga.*'
#>
param(
    [string]$Unit,
    [switch]$Check,
    [string]$Event,
    [switch]$Preview,
    [int]$Threads = 4
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = (Resolve-Path "$PSScriptRoot/../..").Path

function Fail([string]$msg) { Write-Host "[synth] $msg" -ForegroundColor Red; exit 1 }
function Warn([string]$msg) {
    Write-Host "[synth] warning: $msg" -ForegroundColor Yellow
    if ($env:GITHUB_ACTIONS) { Write-Host "::warning title=synth::$msg" }
}

. (Join-Path $PSScriptRoot 'lib/SynthJson.ps1')
. (Join-Path $PSScriptRoot 'lib/Inventory.ps1')

function Read-JsonFile([string]$path) {
    try { return Get-Content -LiteralPath $path -Raw | ConvertFrom-Json -Depth 100 }
    catch { Fail "bad JSON in $($path.Substring($root.Length + 1)): $($_.Exception.Message)" }
}

function Get-Prop($obj, [string]$name, $default = $null) {
    if ($null -eq $obj) { return $default }
    if ($obj -is [Collections.IDictionary]) { if ($obj.Contains($name)) { return $obj[$name] } return $default }
    $p = $obj.PSObject.Properties[$name]
    if ($p) { return $p.Value }
    return $default
}

# ---- units, owners, events ----------------------------------------------------------------------------
$rules = Get-Content (Join-Path $root 'tools/spec/owners.rules.json') -Raw | ConvertFrom-Json -AsHashtable
$unitName = @{}
$unitWave = @{} # by name
foreach ($k in $rules.units.Keys) { $unitName[$k] = $rules.units[$k].name; $unitWave[$rules.units[$k].name] = $rules.units[$k].wave }
$eventOwner = [ordered]@{}
foreach ($line in Get-Content (Join-Path $root 'docs/spec/owners.tsv')) {
    $c = $line -split "`t"
    if ($c.Count -ge 3 -and $c[0] -eq 'sound_event') { $eventOwner[$c[1]] = $unitName[$c[2]] }
}
if ($eventOwner.Count -eq 0) { Fail 'docs/spec/owners.tsv lists no sound_event rows' }
$owners = @($eventOwner.Values | Sort-Object -Unique)

if ($Unit) {
    $u = $null
    foreach ($k in $unitName.Keys) { if ($k -ieq $Unit -or $unitName[$k] -ieq $Unit) { $u = $unitName[$k] } }
    if (-not $u) { Fail "unknown unit '$Unit'" }
    if ($owners -notcontains $u) { Fail "unit '$u' owns no sound event" }
    $owners = @($u)
}

$inventoryPath = Join-Path $root 'tools/sounds/baseline/inventory.json'
try { $inventory = Read-Inventory $inventoryPath } catch { Fail $_.Exception.Message }

# ---- patch library --------------------------------------------------------------------------------------
$library = [ordered]@{}
$patchDir = Join-Path $root 'tools/sounds/patches'
if (Test-Path $patchDir) {
    foreach ($f in Get-ChildItem $patchDir -Recurse -Filter *.json | Sort-Object FullName) {
        $name = $f.FullName.Substring($patchDir.Length + 1).Replace('\', '/') -replace '\.json$', ''
        $library[$name] = Read-JsonFile $f.FullName
    }
}

function Get-Track([string]$name) {
    foreach ($dir in 'tools/sounds/music', 'tools/sounds/baseline/music') {
        $p = Join-Path $root "$dir/$name.json"
        if (Test-Path $p) { return Read-JsonFile $p }
    }
    Fail "unknown track '$name' (tools/sounds/music/$name.json or tools/sounds/baseline/music/$name.json)"
}

# ---- recipes: baseline + owner refinements --------------------------------------------------------------
function Merge-Section($into, $from) {
    if ($null -eq $from) { return }
    foreach ($p in $from.PSObject.Properties) { $into[$p.Name] = $p.Value }
}

# The owner's refinements: tools/sounds/recipes/<u>.json (the W1 units' row in owners.tsv) or the
# fragment path tools/sounds/recipes.d/<u>.json, which rule R3 gives every unit ("every *.d/<u>.json
# fragment"): the W0b owners of sound events (F4 dimension, F8 entity) have no recipes/ row.
function Get-RefinementPath([string]$u) {
    $found = @("tools/sounds/recipes/$u.json", "tools/sounds/recipes.d/$u.json" | Where-Object { Test-Path (Join-Path $root $_) })
    if ($found.Count -gt 1) { Fail "$u has two recipes ($($found -join ', ')): keep one" }
    # a W1 unit owns recipes/<u>.json; its recipes.d path can tie with tools/sounds/** in owners.tsv
    # (a short name such as naga), which tools/ci/ownership.ps1 rejects as ambiguous
    if ($found.Count -and $found[0] -like 'tools/sounds/recipes.d/*' -and $unitWave[$u] -eq 'W1') {
        Fail "$($found[0]): $u is a W1 unit, its recipe is tools/sounds/recipes/$u.json"
    }
    return $(if ($found.Count) { $found[0] } else { $null })
}

$recipes = @{}
function Get-Recipe([string]$u) {
    if ($recipes.ContainsKey($u)) { return $recipes[$u] }
    $r = @{ patches = [ordered]@{}; files = [ordered]@{}; events = [ordered]@{}; sources = @() }
    foreach ($rel in @("tools/sounds/baseline/$u.json", (Get-RefinementPath $u) | Where-Object { $_ })) {
        $p = Join-Path $root $rel
        if (-not (Test-Path $p)) { continue }
        $doc = Read-JsonFile $p
        Merge-Section $r.patches (Get-Prop $doc 'patches')
        Merge-Section $r.files (Get-Prop $doc 'files')
        Merge-Section $r.events (Get-Prop $doc 'events')
        $r.sources += $rel
    }
    $recipes[$u] = $r
    return $r
}

$validCategories = 'ambient', 'block', 'bottle', 'bucket', 'hostile', 'music', 'neutral', 'player', 'record', 'ui', 'weather'
$problems = [Collections.Generic.List[string]]::new()

# every event owned by its unit and covered
foreach ($u in @($eventOwner.Values | Sort-Object -Unique)) {
    $r = Get-Recipe $u
    foreach ($e in $r.events.Keys) {
        if (-not $eventOwner.Contains($e)) { $problems.Add("$($r.sources -join ', '): '$e' is not a TF sound event") }
        elseif ($eventOwner[$e] -ne $u) { $problems.Add("$($r.sources -join ', '): '$e' belongs to $($eventOwner[$e])") }
    }
    foreach ($file in $r.files.Keys) {
        if ($file -like 'alias/*') { $problems.Add("$($r.sources -join ', '): file '$file': alias/ is reserved for copies of other owners' sounds") }
    }
}
foreach ($e in $eventOwner.Keys) {
    if (-not (Get-Recipe $eventOwner[$e]).events.Contains($e)) { $problems.Add("no recipe for $e (owner $($eventOwner[$e]))") }
}

# ---- alias snapshots ----------------------------------------------------------------------------------------
# An event of owner <u> may play another owner's event ({"event": id}). Its sounds are copied into
# <u>'s own tree (sounds/twilightforest/<u>/alias/<owner>/<file>), so <u>'s output never depends on
# what another unit commits. assets/gen/audio/<u>/aliases.json records what was copied: -Check
# reproduces <u>'s tree from it and warns when the aliased owner changed since.
$aliasDir = Join-Path $root 'assets/gen/audio'
$snapshots = @{}
function Get-Snapshot([string]$u) {
    if (-not $snapshots.ContainsKey($u)) {
        $p = Join-Path $aliasDir "$u/aliases.json"
        $snapshots[$u] = if (Test-Path $p) { Read-JsonFile $p } else { $null }
    }
    return $snapshots[$u]
}
$aliasLive = @{}   # owner -> event -> live copy (ordered)
$aliasUsed = @{}   # owner -> event -> copy written to the owner's aliases.json

# variants of an event, aliases ({"event": id}) resolved; returns hashtables with kind/name/fields:
#   kind vanilla (name), file (file of the root owner), inline (file inline/<e>/<i>, spec),
#   copy (file alias/<owner>/<file>, a self-contained spec)
function Resolve-Variants([string]$e, [string]$rootOwner, [int]$depth = 0, [bool]$crossed = $false) {
    if ($depth -gt 8) { $problems.Add("alias loop at $e"); return @() }
    $u   = $eventOwner[$e]
    $def = (Get-Recipe $u).events[$e]
    $out = @()
    $i   = 0
    foreach ($v in @(Get-Prop $def 'variants' @())) {
        $i++
        $alias = Get-Prop $v 'event'
        if ($alias) {
            $alias = $alias -replace '^twilightforest:', ''
            if (-not $eventOwner.Contains($alias)) { $problems.Add("${e}: alias to unknown event $alias"); continue }
            $sub = if (-not $crossed -and $eventOwner[$alias] -ne $rootOwner) { Get-AliasCopy $rootOwner $alias ($depth + 1) }
                   else { Resolve-Variants $alias $rootOwner ($depth + 1) $crossed }
            foreach ($x in @($sub | Where-Object { $null -ne $_ })) {
                $y = $x.Clone()
                # Java: the referenced sound's volume and pitch multiplied, stream if either streams
                foreach ($k in 'volume', 'pitch', 'weight') {
                    $m = Get-Prop $v $k
                    if ($null -ne $m) { $y[$k] = [double]$(if ($y.ContainsKey($k) -and $null -ne $y[$k]) { $y[$k] * $m } else { $m }) }
                }
                if ([bool](Get-Prop $v 'stream' $false)) { $y.stream = $true }
                $out += $y
            }
            continue
        }
        $entry = @{ owner = $u; event = $e }
        foreach ($k in 'volume', 'pitch', 'weight') { $val = Get-Prop $v $k; if ($null -ne $val) { $entry[$k] = [double]$val } }
        foreach ($k in 'stream', 'is3D', 'load_on_low_memory') { $val = Get-Prop $v $k; if ($null -ne $val) { $entry[$k] = [bool]$val } }
        $vanilla = Get-Prop $v 'vanilla'
        $file    = Get-Prop $v 'file'
        if ($vanilla) {
            $entry.kind = 'vanilla'; $entry.name = $vanilla
        } elseif ($file) {
            if (-not (Get-Recipe $u).files.Contains($file)) { $problems.Add("${e}: unknown file '$file' (define it under ""files"" in $u's recipe)") }
            $entry.kind = 'file'; $entry.file = $file
        } elseif (Get-Prop $v 'patch') {
            $entry.kind = 'inline'; $entry.file = "inline/$e/$i"; $entry.spec = $v
        } else {
            $problems.Add("${e}: variant $i needs patch, file, vanilla or event")
            continue
        }
        $out += $entry
    }
    if ($out.Count -eq 0 -and $depth -eq 0) { $problems.Add("$e has no variants") }
    return $out
}

# A patch with the unit-local patches its "patch" layers name inlined (the renderer only knows the
# shared library).
function Expand-LocalPatches($patch, $locals, [int]$depth = 0) {
    if ($depth -gt 8 -or $null -eq $patch -or $patch -is [string]) { return $patch }
    $layers = Get-Prop $patch 'layers'
    if ($null -eq $layers) { return $patch }
    $changed = $false
    $newLayers = foreach ($L in @($layers)) {
        $inner = Get-Prop $L 'patch'
        if ((Get-Prop $L 'type') -eq 'patch' -and $null -ne $inner) {
            $resolved = if ($inner -is [string] -and $locals.Contains($inner)) { $locals[$inner] } else { $inner }
            $expanded = Expand-LocalPatches $resolved $locals ($depth + 1)
            if (-not [object]::ReferenceEquals($expanded, $inner)) {
                $o = [ordered]@{}
                foreach ($q in $L.PSObject.Properties) { $o[$q.Name] = $q.Value }
                $o.patch = $expanded
                $changed = $true
                [pscustomobject]$o
                continue
            }
        }
        $L
    }
    if (-not $changed) { return $patch }
    $out = [ordered]@{}
    foreach ($q in $patch.PSObject.Properties) { $out[$q.Name] = $q.Value }
    $out.layers = @($newLayers)
    return [pscustomobject]$out
}

# A self-contained render spec of another owner's file: its unit-local patch and its track inlined,
# its seed fixed (the default seed comes from the file name, which the copy changes).
function Get-CopySpec([string]$u, $x) {
    $spec = if ($x.kind -eq 'inline') { $x.spec } else { (Get-Recipe $u).files[$x.file] }
    $out  = [ordered]@{}
    foreach ($p in $spec.PSObject.Properties) { $out[$p.Name] = $p.Value }
    $patch = Get-Prop $spec 'patch'
    $locals = (Get-Recipe $u).patches
    if ($patch -is [string] -and $locals.Contains($patch)) { $out.patch = Expand-LocalPatches $locals[$patch] $locals }
    elseif ($null -ne $patch -and $patch -isnot [string]) { $out.patch = Expand-LocalPatches $patch $locals }
    $track = Get-Prop $spec 'track'
    if ($track -is [string]) { $out.track = Get-Track $track }
    if (-not $track -and $null -eq (Get-Prop $spec 'seed')) { $out.seed = Get-NameSeed $x.file }
    return [pscustomobject]$out
}

# A variant of a snapshot ([ordered], as written to aliases.json) from a resolved entry.
function ConvertTo-CopyVariant([string]$rootOwner, $x) {
    $v = [ordered]@{}
    switch ($x.kind) {
        'vanilla' { $v.vanilla = $x.name }
        'copy' { $v.copy = $x.file; $v.spec = $x.spec }
        default {
            if ($x.owner -eq $rootOwner) { $v.file = $x.file; if ($x.kind -eq 'inline') { $v.spec = $x.spec } }
            else { $v.copy = "alias/$($x.owner)/$($x.file)"; $v.spec = Get-CopySpec $x.owner $x }
        }
    }
    foreach ($k in 'volume', 'pitch', 'weight', 'stream', 'is3D', 'load_on_low_memory') { if ($x.ContainsKey($k) -and $null -ne $x[$k]) { $v[$k] = $x[$k] } }
    return $v
}

# A resolved entry from a snapshot variant.
function ConvertFrom-CopyVariant([string]$rootOwner, [string]$e, $v) {
    $entry = @{ owner = $rootOwner; event = $e }
    if (Get-Prop $v 'vanilla') { $entry.kind = 'vanilla'; $entry.name = Get-Prop $v 'vanilla' }
    elseif (Get-Prop $v 'copy') { $entry.kind = 'copy'; $entry.file = Get-Prop $v 'copy'; $entry.spec = Get-Prop $v 'spec' }
    elseif (Get-Prop $v 'spec') { $entry.kind = 'inline'; $entry.file = Get-Prop $v 'file'; $entry.spec = Get-Prop $v 'spec' }
    else { $entry.kind = 'file'; $entry.file = Get-Prop $v 'file' }
    foreach ($k in 'volume', 'pitch', 'weight') { $val = Get-Prop $v $k; if ($null -ne $val) { $entry[$k] = [double]$val } }
    foreach ($k in 'stream', 'is3D', 'load_on_low_memory') { $val = Get-Prop $v $k; if ($null -ne $val) { $entry[$k] = [bool]$val } }
    return $entry
}

function Get-AliasCopy([string]$rootOwner, [string]$a, [int]$depth) {
    if (-not $aliasLive.ContainsKey($rootOwner)) { $aliasLive[$rootOwner] = @{}; $aliasUsed[$rootOwner] = @{} }
    if (-not $aliasLive[$rootOwner].ContainsKey($a)) {
        $live = [ordered]@{ owner = $eventOwner[$a]; variants = @(Resolve-Variants $a $rootOwner $depth $true | ForEach-Object { ConvertTo-CopyVariant $rootOwner $_ }) }
        $aliasLive[$rootOwner][$a] = $live
        $used = $live
        $snap = if ($Check) { Get-Snapshot $rootOwner } else { $null }
        $snapEvent = if ($snap) { Get-Prop (Get-Prop $snap 'events') $a } else { $null }
        if ($snapEvent) { $used = [ordered]@{ owner = Get-Prop $snapEvent 'owner'; variants = @(Get-Prop $snapEvent 'variants' @()) } }
        $aliasUsed[$rootOwner][$a] = $used
    }
    $used = $aliasUsed[$rootOwner][$a]
    return @($used.variants | ForEach-Object { ConvertFrom-CopyVariant $rootOwner $a $_ })
}

$resolved = [ordered]@{}
foreach ($e in $eventOwner.Keys) { if ((Get-Recipe $eventOwner[$e]).events.Contains($e)) { $resolved[$e] = @(Resolve-Variants $e $eventOwner[$e]) } }

if ($problems.Count) {
    $problems | Select-Object -First 50 | ForEach-Object { Write-Host "[synth] $_" -ForegroundColor Red }
    Fail "$($problems.Count) recipe problem(s)"
}

# ---- render jobs ----------------------------------------------------------------------------------------
function Get-RenderSpec([string]$u, $entry) {
    if ($entry.kind -in 'inline', 'copy') { return $entry.spec }
    return (Get-Recipe $u).files[$entry.file]
}

function New-Job([string]$u, [string]$file, $spec, [string]$outPath, [bool]$copy = $false) {
    $name  = "$u/$file"
    $track = Get-Prop $spec 'track'
    $job   = [ordered]@{ name = $name; out = $outPath }
    if ($track) {
        $t = if ($track -is [string]) { Get-Track $track } else { $track }
        $job.kind    = 'track'
        $job.track   = $t
        $job.rate    = [int](Get-Prop $spec 'rate' (Get-Prop $t 'rate' 22050))
        $job.quality = [double](Get-Prop $spec 'quality' (Get-Prop $t 'quality' 0.2))
    } else {
        $patch = Get-Prop $spec 'patch'
        if ($patch -is [string]) {
            $local = (Get-Recipe $u).patches
            if (-not $copy -and $local.Contains($patch)) { $patch = Expand-LocalPatches $local[$patch] $local } # a copy's own unit-local patch is inlined
            elseif (-not $library.Contains($patch)) { Fail "${name}: unknown patch '$patch'" }
        } elseif (-not $copy -and $null -ne $patch) { $patch = Expand-LocalPatches $patch (Get-Recipe $u).patches }
        $job.kind    = 'patch'
        $job.patch   = $patch
        $job.params  = Get-Prop $spec 'params' ([pscustomobject]@{})
        $job.seed    = [long](Get-Prop $spec 'seed' (Get-NameSeed $file)) # by file name: copies in two units match
        $job.rate    = [int](Get-Prop $spec 'rate' 44100)
        $job.quality = [double](Get-Prop $spec 'quality' 0.4)
    }
    return $job
}

function Get-CategoryDefault([string]$e) {
    switch -Regex ($e) {
        '^music_disc\.' { return 'record' }
        '^music\.' { return 'music' }
        '^environment\.' { return 'weather' }
        '^block\.' { return 'block' }
        '^item\.' { return 'player' }
        default { return 'neutral' }
    }
}

# per owner: files to render and the sound_definitions fragment
$plans = [ordered]@{}
foreach ($u in $owners) {
    $files = [ordered]@{}
    $defs  = [ordered]@{}
    foreach ($e in ($resolved.Keys | Where-Object { $eventOwner[$_] -eq $u } | Sort-Object)) {
        $def    = (Get-Recipe $u).events[$e]
        $cat    = Get-Prop $def 'category' (Get-CategoryDefault $e)
        if ($validCategories -notcontains $cat) { Fail "${e}: category '$cat' is not a Bedrock sound category ($($validCategories -join ', '))" }
        $sounds = @()
        foreach ($x in $resolved[$e]) {
            $s = [ordered]@{}
            if ($x.kind -eq 'vanilla') {
                $s.name = $x.name
            } else {
                if ($x.owner -ne $u) { Fail "${e}: internal error, $($x.owner)'s file $($x.file) outside a copy" }
                $s.name = "sounds/twilightforest/$u/$($x.file)"
                if (-not $files.Contains($x.file)) { $files[$x.file] = @{ spec = Get-RenderSpec $u $x; copy = ($x.kind -eq 'copy') } }
            }
            foreach ($k in 'volume', 'pitch') { if ($x.ContainsKey($k) -and $x[$k] -ne 1.0) { $s[$k] = $x[$k] } }
            if ($x.ContainsKey('weight') -and $x.weight -ne 1.0) { $s.weight = [int][math]::Round($x.weight) }
            foreach ($k in 'stream', 'is3D', 'load_on_low_memory') { if ($x.ContainsKey($k)) { $s[$k] = $x[$k] } }
            $sounds += $s
        }
        $d = [ordered]@{ category = $cat }
        $sub = Get-Prop $def 'subtitle'
        if ($sub) { $d.subtitle = [string]$sub }
        foreach ($k in 'min_distance', 'max_distance') { $val = Get-Prop $def $k; if ($null -ne $val) { $d[$k] = [double]$val } }
        $d.sounds = $sounds
        $defs[$e] = $d
    }
    $plans[$u] = @{ files = $files; defs = $defs }
}

# ---- TF's inventory: every definition has TF's variants, subtitle and category ---------------------------
$parity = [Collections.Generic.List[string]]::new()
foreach ($u in $owners) {
    foreach ($e in $plans[$u].defs.Keys) {
        $d = $plans[$u].defs[$e]
        if (-not $inventory.events.Contains($e)) { $parity.Add("${e} ($u): not in TF's inventory (tools/sounds/baseline/inventory.json; run tools/sounds/baseline/generate.ps1)"); continue }
        $exp = $inventory.events[$e]
        if ($exp.owner -ne $u) { $parity.Add("${e}: owned by $u in docs/spec/owners.tsv but by $($exp.owner) in tools/sounds/baseline/inventory.json (run tools/sounds/baseline/generate.ps1)") }
        $expSub = if ($exp.Contains('subtitle')) { [string]$exp.subtitle } else { '' }
        $gotSub = if ($d.Contains('subtitle')) { [string]$d.subtitle } else { '' }
        if ($gotSub -ne $expSub) { $parity.Add("${e} ($u): subtitle '$gotSub', TF has '$expSub'") }
        if ($d.category -ne $exp.category) { $parity.Add("${e} ($u): category '$($d.category)', TF plays it as '$($exp.category)' (tools/sounds/categories.json)") }
        $got  = @(Get-VariantKeys $resolved[$e])
        $want = @(Get-VariantKeys $exp.variants)
        if ($got.Count -ne $want.Count) { $parity.Add("${e} ($u): $($got.Count) variants, TF has $($want.Count)") }
        elseif (($got -join ';') -ne ($want -join ';')) { $parity.Add("${e} ($u): variants (volume|pitch|stream) $($got -join ', ') differ from TF's $($want -join ', ')") }
    }
}
if ($parity.Count) {
    $parity | Select-Object -First 50 | ForEach-Object { Write-Host "[synth] $_" -ForegroundColor Red }
    Fail "$($parity.Count) definition(s) differ from TF's inventory: refine the sounds, keep TF's variants (tools/sounds/README.md)"
}

# ---- the renderer ---------------------------------------------------------------------------------------
function Get-Renderer {
    # another build of tools/sounds/cli (e.g. another compiler, to verify cross-compiler determinism)
    if ($env:GF_SYNTH_EXE) { return $env:GF_SYNTH_EXE }
    $env:Path =$env:Path + ';' + [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
    if ($env:LLVM_PATH -and (Test-Path (Join-Path $env:LLVM_PATH 'bin\clang-cl.exe'))) { $env:Path = (Join-Path $env:LLVM_PATH 'bin') + ";$env:Path" }
    $cc = Get-Command clang-cl -ErrorAction SilentlyContinue
    if (-not $cc) {
        foreach ($dir in "$env:ProgramFiles\LLVM\bin", "${env:ProgramFiles(x86)}\LLVM\bin") {
            if (Test-Path (Join-Path $dir 'clang-cl.exe')) { $cc = Get-Command (Join-Path $dir 'clang-cl.exe'); break }
        }
    }
    if (-not $cc) { Fail 'clang-cl not found (install LLVM or put it on PATH)' }
    $sources = @(Join-Path $root 'tools/sounds/cli/SynthCli.cpp') + @(Get-ChildItem (Join-Path $root 'src/core/synth') -Filter *.cpp | Sort-Object Name | ForEach-Object FullName)
    $headers = @(Get-ChildItem (Join-Path $root 'src/core/synth') -Filter *.h | Sort-Object Name | ForEach-Object FullName)
    $flags   = @('/nologo', '/std:c++20', '/O2', '/EHsc', '/utf-8', '/clang:-ffp-contract=off', '-fuse-ld=lld')
    $version = (& $cc.Source --version | Select-Object -First 1)
    $sha     = [Security.Cryptography.SHA256]::Create()
    $text    = ($flags -join ' ') + "`n$version`n" + (($sources + $headers) | ForEach-Object { [IO.Path]::GetFileName($_) + "`n" + ((Get-Content -LiteralPath $_ -Raw) -replace "`r`n", "`n") }) -join "`n"
    $key     = -join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($text))[0..7] | ForEach-Object { $_.ToString('x2') })
    $cache   = Join-Path $env:LOCALAPPDATA "gf-synth\$key"
    $exe     = Join-Path $cache 'gf-synth.exe'
    if (Test-Path $exe) { return $exe }
    $tmp = Join-Path $env:LOCALAPPDATA "gf-synth\build-$PID"
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $tmp | Out-Null
    Write-Host "[synth] compiling the renderer ($version)"
    $out = & $cc.Source @flags "/I$(Join-Path $root 'src')" @sources "/Fe$(Join-Path $tmp 'gf-synth.exe')" "/Fo$tmp\" 2>&1
    if ($LASTEXITCODE -ne 0) { $out | ForEach-Object { Write-Host $_ }; Fail 'renderer compilation failed' }
    New-Item -ItemType Directory -Force $cache | Out-Null
    try { Move-Item (Join-Path $tmp 'gf-synth.exe') $exe -ErrorAction Stop } catch { if (-not (Test-Path $exe)) { throw } }
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    return $exe
}

function Invoke-Render($jobs) {
    if ($jobs.Count -eq 0) { return }
    $exe     = Get-Renderer
    $jobFile = Join-Path ([IO.Path]::GetTempPath()) "gf-synth-jobs-$PID.json"
    $doc     = [ordered]@{ threads = $Threads; library = $library; jobs = $jobs }
    [IO.File]::WriteAllText($jobFile, ($doc | ConvertTo-Json -Depth 100 -Compress), [Text.UTF8Encoding]::new($false))
    $failed = $false
    & $exe render $jobFile | ForEach-Object {
        if ($_ -like 'ERR *') { Write-Host "[synth] $_" -ForegroundColor Red; $failed = $true }
    }
    $code = $LASTEXITCODE
    Remove-Item $jobFile -ErrorAction SilentlyContinue
    if ($failed -or $code -ne 0) { Fail 'rendering failed' }
}

# ---- preview ----------------------------------------------------------------------------------------------
if ($Preview) {
    $dir = Join-Path ([IO.Path]::GetTempPath()) 'gf-synth-preview'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $jobs = @()
    $seen = @{}
    foreach ($u in $owners) {
        foreach ($e in $plans[$u].defs.Keys) {
            if ($Event -and $e -notlike $Event) { continue }
            foreach ($x in $resolved[$e]) {
                if ($x.kind -eq 'vanilla' -or $seen.ContainsKey("$u/$($x.file)")) { continue }
                $seen["$u/$($x.file)"] = $true
                $f    = $plans[$u].files[$x.file]
                $base = Join-Path $dir (($u + '_' + $x.file) -replace '[\\/]', '_')
                $job  = New-Job $u $x.file $f.spec "$base.ogg" $f.copy
                $job.wav = "$base.wav"
                $jobs += $job
            }
        }
    }
    Invoke-Render $jobs
    Write-Host "[synth] $($jobs.Count) preview(s) in $dir" -ForegroundColor Green
    exit 0
}

# ---- write (or check) the tree -------------------------------------------------------------------------
$audioRoot = Join-Path $root 'assets/gen/audio'
$target    = if ($Check) { Join-Path ([IO.Path]::GetTempPath()) "gf-synth-check-$PID" } else { $audioRoot }
if ($Check) { Remove-Item -Recurse -Force $target -ErrorAction SilentlyContinue }

$jobs = @()
foreach ($u in $owners) {
    $unitDir = Join-Path $target "$u/rp"
    $sndDir  = Join-Path $unitDir "sounds/twilightforest/$u"
    New-Item -ItemType Directory -Force (Join-Path $unitDir 'sounds.d') | Out-Null
    $fragment = Format-Json $plans[$u].defs
    [IO.File]::WriteAllText((Join-Path $unitDir "sounds.d/$u.json"), $fragment + "`n", [Text.UTF8Encoding]::new($false))
    # the alias snapshot (outside rp/: not packed)
    $snapPath = Join-Path $target "$u/aliases.json"
    if ($aliasUsed.ContainsKey($u) -and $aliasUsed[$u].Count) {
        $events = [ordered]@{}
        foreach ($a in ($aliasUsed[$u].Keys | Sort-Object)) { $events[$a] = $aliasUsed[$u][$a] }
        $snap = [ordered]@{
            about  = "Sounds of other owners that $u's events play ({""event"": id} variants), copied under sounds/twilightforest/$u/alias/ as rendered from their recipes at $u's last render, by tools/sounds/synth.ps1: do not edit. synth.ps1 -Check renders the copies from here and warns when an owner changed its event since: pwsh tools/sounds/synth.ps1 -Unit $u refreshes them."
            events = $events
        }
        [IO.File]::WriteAllText($snapPath, (Format-Json $snap) + "`n", [Text.UTF8Encoding]::new($false))
    } elseif (-not $Check -and (Test-Path $snapPath)) {
        Remove-Item -Force $snapPath
    }
    $wanted = @{}
    foreach ($file in $plans[$u].files.Keys) { $wanted[[IO.Path]::GetFullPath((Join-Path $sndDir "$file.ogg"))] = $true }
    if (-not $Check -and (Test-Path $sndDir)) {
        # stale files of removed variants, then empty directories
        Get-ChildItem $sndDir -Recurse -File | Where-Object { -not $wanted.ContainsKey($_.FullName) } | Remove-Item -Force
        Get-ChildItem $sndDir -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending |
            Where-Object { -not (Get-ChildItem $_.FullName -Force) } | Remove-Item -Force
    }
    foreach ($file in $plans[$u].files.Keys) {
        $out = Join-Path $sndDir "$file.ogg"
        New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
        $f = $plans[$u].files[$file]
        $jobs += New-Job $u $file $f.spec $out $f.copy
    }
}
Invoke-Render $jobs

# alias copies older than the aliased owner's current recipe (a -Check renders them from the snapshot)
$stale = [Collections.Generic.List[string]]::new()
foreach ($u in $owners) {
    if (-not $aliasUsed.ContainsKey($u)) { continue }
    foreach ($a in ($aliasUsed[$u].Keys | Sort-Object)) {
        if ((Format-Json $aliasUsed[$u][$a]) -ne (Format-Json $aliasLive[$u][$a])) {
            $stale.Add("$u's copy of $a ($($aliasLive[$u][$a].owner)) predates $($aliasLive[$u][$a].owner)'s current recipe: pwsh tools/sounds/synth.ps1 -Unit $u refreshes it (A8 refreshes all with a full render)")
        }
    }
}

# events of other owners that play this unit's events keep their copies until they render again
function Show-Dependents([string]$u) {
    foreach ($w in @($eventOwner.Values | Sort-Object -Unique)) {
        if ($w -eq $u) { continue }
        $snap = Get-Snapshot $w
        if (-not $snap) { continue }
        foreach ($p in (Get-Prop $snap 'events').PSObject.Properties) {
            if ((Get-Prop $p.Value 'owner') -eq $u) { Write-Host "[synth] note: $w plays a copy of $($p.Name); it follows this change at its next render (pwsh tools/sounds/synth.ps1 -Unit $w, or A8's full render)" }
        }
    }
}

$incPath = Join-Path $root 'src/feature/ext/synth/SoundEvents.inc'
if ($Check) {
    $diffs = [Collections.Generic.List[string]]::new()
    foreach ($u in $owners) {
        $a = Join-Path $target $u
        $b = Join-Path $audioRoot $u
        $fa = @{}; $fb = @{}
        if (Test-Path $a) { Get-ChildItem $a -Recurse -File | ForEach-Object { $fa[$_.FullName.Substring($a.Length + 1)] = $_.FullName } }
        if (Test-Path $b) { Get-ChildItem $b -Recurse -File | ForEach-Object { $fb[$_.FullName.Substring($b.Length + 1)] = $_.FullName } }
        foreach ($k in $fa.Keys) {
            if (-not $fb.ContainsKey($k)) { $diffs.Add("missing assets/gen/audio/$u/$($k.Replace('\', '/'))"); continue }
            $ha = (Get-FileHash $fa[$k] -Algorithm SHA256).Hash
            $hb = (Get-FileHash $fb[$k] -Algorithm SHA256).Hash
            if ($ha -ne $hb) { $diffs.Add("differs  assets/gen/audio/$u/$($k.Replace('\', '/'))") }
        }
        foreach ($k in $fb.Keys) { if (-not $fa.ContainsKey($k)) { $diffs.Add("stale    assets/gen/audio/$u/$($k.Replace('\', '/'))") } }
    }
    if (-not $Unit) {
        foreach ($d in Get-ChildItem $audioRoot -Directory) {
            if ($owners -notcontains $d.Name) { $diffs.Add("stale    assets/gen/audio/$($d.Name)/ (owns no sound event)") }
        }
    }
    # the selftests' C++ inventory is the JSON inventory's
    $cur = if (Test-Path $incPath) { (Get-Content $incPath -Raw) -replace "`r`n", "`n" } else { '' }
    if ($cur -ne (Format-SoundEventsInc $inventory)) { $diffs.Add('differs  src/feature/ext/synth/SoundEvents.inc (run tools/sounds/baseline/generate.ps1)') }
    Remove-Item -Recurse -Force $target -ErrorAction SilentlyContinue
    if ($diffs.Count) {
        $diffs | Select-Object -First 40 | ForEach-Object { Write-Host "[synth] $_" -ForegroundColor Red }
        Fail "$($diffs.Count) difference(s): run pwsh tools/sounds/synth.ps1 -Unit <owner> and commit the result"
    }
    foreach ($s in $stale) { Warn $s }
    Write-Host "[synth] -Check OK: $($resolved.Count) events match TF's inventory, $($jobs.Count) rendered files reproduce the committed tree$(if ($stale.Count) { " ($($stale.Count) stale alias cop$(if ($stale.Count -eq 1) { 'y' } else { 'ies' }))" })" -ForegroundColor Green
    exit 0
}

if ($Unit) { Show-Dependents $owners[0] }
Write-Host "[synth] $($owners.Count) owner(s), $($resolved.Count) events, $($jobs.Count) files rendered into assets/gen/audio" -ForegroundColor Green
