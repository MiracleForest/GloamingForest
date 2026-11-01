<#
.SYNOPSIS
  Contract "synth" (F3): generates the baseline recipes tools/sounds/baseline/<unit>.json.

.DESCRIPTION
  Reads TF's event inventory - the event names and the structure of
  src/generated/resources/assets/twilightforest/sounds.json in the shared checkout
  (%LOCALAPPDATA%\gf-tf, tools/tf/fetch.ps1; LGPL datagen output) - and writes one recipe per
  owning unit (docs/spec/owners.tsv) with exactly TF's variants:
    - each TF sound file becomes a synthesized file ("files") designed below from its name
      (never from its audio: TF's .ogg files are All Rights Reserved and absent from the checkout);
    - a vanilla event or file that TF plays becomes the Bedrock vanilla sound of the same name
      (tools/sounds/vanilla.json), or a synthesized stand-in where Bedrock has none;
    - a reference to another TF event stays an alias ({"event": id});
    - TF's per-entry volume, pitch and stream flags are kept.
  The owning units refine their events in tools/sounds/recipes/<unit>.json (rule R3); this
  baseline guarantees nothing is silent. -Check verifies that the committed baseline matches.

.EXAMPLE
  pwsh tools/sounds/baseline/generate.ps1
  pwsh tools/sounds/baseline/generate.ps1 -Check
#>
param([switch]$Check)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
. (Join-Path $root 'tools/sounds/lib/SynthJson.ps1')

$tf = if ($env:GF_TF_DIR) { $env:GF_TF_DIR } else { Join-Path $env:LOCALAPPDATA 'gf-tf' }
$soundsJson = Join-Path $tf 'src/generated/resources/assets/twilightforest/sounds.json'
if (-not (Test-Path $soundsJson)) { Write-Host "[baseline] TF checkout missing: pwsh tools/tf/fetch.ps1" -ForegroundColor Red; exit 1 }
$tfSounds = Get-Content $soundsJson -Raw | ConvertFrom-Json -AsHashtable
$vanilla  = Get-Content (Join-Path $root 'tools/sounds/vanilla.json') -Raw | ConvertFrom-Json -AsHashtable

# owners
$rules = Get-Content (Join-Path $root 'tools/spec/owners.rules.json') -Raw | ConvertFrom-Json -AsHashtable
$owner = @{}
foreach ($line in Get-Content (Join-Path $root 'docs/spec/owners.tsv')) {
    $c = $line -split "`t"
    if ($c.Count -ge 3 -and $c[0] -eq 'sound_event') { $owner[$c[1]] = $rules.units[$c[2]].name }
}

# hostile entities: MobCategory.MONSTER in TFEntities.java
$hostile = @{}
foreach ($line in Get-Content (Join-Path $tf 'src/main/java/twilightforest/init/TFEntities.java')) {
    if ($line -match '^\s*public static final .*?\s([A-Z_]+)\s*=\s*make\(.*MobCategory\.([A-Z_]+)') { $hostile[$Matches[1].ToLower()] = $Matches[2] -eq 'MONSTER' }
}

function Get-Category([string]$e) {
    switch -Regex ($e) {
        '^music_disc\.' { return 'record' }
        '^music\.' { return 'music' }
        '^environment\.' { return 'weather' }
        '^block\.' { return 'block' }
        '^item\.' { return 'player' }
        '^entity\.twilightforest\.(parrot|loyal_zombie)\.' { return 'neutral' } # parrots imitating TF mobs; the tamed zombie
        '^entity\.twilightforest\.([a-z_]+)\.' {
            $m = $Matches[1]
            if ($hostile.ContainsKey($m)) { return $(if ($hostile[$m]) { 'hostile' } else { 'neutral' }) }
            return 'hostile'
        }
        default { return 'neutral' }
    }
}

# ---- sound design per TF file name (the baseline's creative part) ---------------------------------------
function P($patch, [hashtable]$params) {
    $sorted = [ordered]@{}
    foreach ($k in ($params.Keys | Sort-Object)) { $sorted[$k] = $params[$k] } # stable order (hash order varies per process)
    return [ordered]@{ patch = $patch; params = $sorted }
}

function Get-Design([string]$path) {
    $n = 1
    if ($path -match '(\d+)$') { $n = [int]$Matches[1] }
    $v  = 1.0 + 0.045 * ($n - 2)           # small per-variant spread
    $vf = [math]::Round($v, 3)
    switch -Regex ($path) {
        '^music/(.+)$' { return [ordered]@{ track = $Matches[1] } }
        '^mob/alpha_yeti/alert' { return P 'creature/roar' @{ pitch = 85; len = 1.0; size = 0.6 } }
        '^mob/alpha_yeti/death' { return P 'creature/roar' @{ pitch = 60; len = 2.2; rise = -3; room = 0.75 } }
        '^mob/alpha_yeti/grab' { return P 'creature/growl' @{ pitch = 75; len = 0.6; rough = 4 } }
        '^mob/alpha_yeti/growl' { return P 'creature/growl' @{ pitch = [math]::Round(70 * $v, 2); len = 1.0; size = 0.6 } }
        '^mob/alpha_yeti/hurt' { return P 'creature/call' @{ pitch = [math]::Round(120 * $v, 2); len = 0.35; rise = 2; fall = -7; size = 0.6; rough = 3.5; vowelA = 0; vowelB = 3 } }
        '^mob/alpha_yeti/pant' { return P 'creature/pant' @{ len = [math]::Round(0.9 * $v, 3); count = 3; size = 0.6 } }
        '^mob/alpha_yeti/roar' { return P 'creature/roar' @{ pitch = 65; len = 1.8; rise = 4; size = 0.6 } }
        '^mob/alpha_yeti/throw' { return P 'object/whoosh' @{ len = 0.5; f0 = 180; f1 = 1400 } }
        '^mob/cicada' { return P 'creature/cicada' @{ len = [math]::Round(2.4 * $v, 3); pulse = [math]::Round(110 * $v, 2) } }
        '^mob/deer/death' { return P 'creature/bleat' @{ pitch = 520; len = 0.9; vib = 1.0 } }
        '^mob/deer/hurt' { return P 'creature/bleat' @{ pitch = [math]::Round(720 * $v, 2); len = 0.25; vib = 1.3 } }
        '^mob/deer/idle' { return P 'creature/bleat' @{ pitch = [math]::Round(650 * $v, 2); len = 0.45; size = 1.4 } }
        '^mob/fire_beetle/death' { return P 'creature/insect' @{ pitch = 130; len = 0.8; wing = 40; band = 900; bend = -12 } }
        '^mob/fire_beetle/hurt' { return P 'creature/insect' @{ pitch = [math]::Round(170 * $v, 2); len = 0.22; wing = 55; band = 1100; bend = 5 } }
        '^mob/fire_beetle/idle' { return P 'creature/insect' @{ pitch = [math]::Round(140 * $v, 2); len = 0.6; wing = 45; band = 900 } }
        '^mob/helmet_crab/hurt' { return P 'creature/squeak' @{ pitch = [math]::Round(1400 * $v, 2); len = 0.12; bend = -3 } }
        '^mob/helmet_crab/idle' { return P 'creature/clack' @{ freq = [math]::Round(1800 * $v, 2); count = 4; gap = 0.06 } }
        '^mob/helmet_crab/step' { return P 'creature/clack' @{ freq = [math]::Round(2200 * $v, 2); count = 1; decay = 0.6 } }
        '^mob/hydra/death' { return P 'creature/roar' @{ pitch = 50; len = 3.0; rise = -6; size = 0.5; room = 0.85 } }
        '^mob/hydra/growl' { return P 'creature/growl' @{ pitch = [math]::Round(55 * $v, 2); len = 1.4; size = 0.55 } }
        '^mob/hydra/hurt' { return P 'creature/roar' @{ pitch = [math]::Round(80 * $v, 2); len = 0.6; rise = 5; size = 0.55 } }
        '^mob/hydra/roar' { return P 'creature/roar' @{ pitch = [math]::Round(55 * $v, 2); len = 2.2; size = 0.5; room = 0.8 } }
        '^mob/hydra/warn' { return P 'creature/growl' @{ pitch = 65; len = 1.0; rough = 4; rasp = 0.6; size = 0.55 } }
        '^mob/ice/crackle' { return P 'ambient/crackle' @{ len = [math]::Round(0.8 * $v, 3); density = 28 } }
        '^mob/ice/death' { return P 'impact/crunch' @{ len = 0.5; freq = [math]::Round(2400 * $v, 2); bright = 1.4 } }
        '^mob/ice/hurt' { return P 'impact/knock_ice' @{ freq = [math]::Round(2500 * $v, 2); count = 2; gap = 0.05 } }
        '^mob/kobold/ambient' { return P 'creature/chatter' @{ pitch = [math]::Round(330 * $v, 2); count = 2 + ($n % 4); size = 1.5 } }
        '^mob/kobold/death' { return P 'creature/call' @{ pitch = [math]::Round(380 * $v, 2); len = 0.6; rise = 2; fall = -12; size = 1.5 } }
        '^mob/kobold/hurt' { return P 'creature/call' @{ pitch = [math]::Round(420 * $v, 2); len = 0.2; rise = 4; fall = -4; size = 1.5; rough = 2 } }
        '^mob/mist_wolf/hurt' { return P 'creature/call' @{ pitch = [math]::Round(600 * $v, 2); len = 0.25; rise = 5; fall = -8; vowelA = 2; vowelB = 0 } }
        '^mob/mist_wolf/idle' { return P 'creature/growl' @{ pitch = [math]::Round(120 * $v, 2); len = 0.8; size = 0.9; rasp = 0.3 } }
        '^mob/mist_wolf/target' { return P 'creature/growl' @{ pitch = 110; len = 1.2; rough = 4; size = 0.9 } }
        '^mob/mosquito/' { return P 'creature/whine' @{ pitch = 620; len = 1.5 } }
        '^mob/naga/hiss' { return P 'creature/hiss' @{ len = [math]::Round(0.8 * $v, 3); bright = $vf } }
        '^mob/naga/hurt' { return P 'creature/call' @{ pitch = [math]::Round(160 * $v, 2); len = 0.35; size = 0.7; rough = 3; breath = 0.6; vowelA = 1; vowelB = 4 } }
        '^mob/naga/rattle' { return P 'creature/rattle' @{ len = [math]::Round(0.8 * $v, 3); rate = 34 } }
        '^mob/penguin/death' { return P 'creature/call' @{ pitch = 500; len = 0.5; fall = -9; size = 1.3; vowelA = 0; vowelB = 4 } }
        '^mob/penguin/hurt' { return P 'creature/caw' @{ pitch = 650; len = 0.15; size = 1.3; rough = 2 } }
        '^mob/penguin/idle' { return P 'creature/caw' @{ pitch = [math]::Round(520 * $v, 2); len = 0.18; count = 1 + ($n % 2); size = 1.3; rough = 1.5 } }
        '^mob/pinch_beetle/attack' { return P 'creature/clack' @{ freq = [math]::Round(1200 * $v, 2); count = 2; gap = 0.05 } }
        '^mob/pinch_beetle/death' { return P 'creature/insect' @{ pitch = 160; len = 0.9; wing = 50; bend = -12 } }
        '^mob/pinch_beetle/hurt' { return P 'creature/insect' @{ pitch = [math]::Round(200 * $v, 2); len = 0.2; wing = 65; bend = 5 } }
        '^mob/pinch_beetle/idle' { return P 'creature/insect' @{ pitch = [math]::Round(180 * $v, 2); len = 0.5; wing = 60 } }
        '^mob/raven/caw' { return P 'creature/caw' @{ pitch = [math]::Round(700 * $v, 2); len = 0.25; count = 2 } }
        '^mob/raven/squawk' { return P 'creature/caw' @{ pitch = [math]::Round(850 * $v, 2); len = 0.18; rough = 4 } }
        '^mob/redcap/muffled/die' { return P 'creature/call' @{ pitch = [math]::Round(300 * $v, 2); len = 0.7; fall = -14; size = 1.3; cutoff = 900 } }
        '^mob/redcap/muffled/hurt' { return P 'creature/call' @{ pitch = [math]::Round(330 * $v, 2); len = 0.22; rise = 5; fall = -5; size = 1.3; rough = 2; cutoff = 900 } }
        '^mob/redcap/muffled/redcap' { return P 'creature/chatter' @{ pitch = [math]::Round(240 * $v, 2); count = 3 + ($n % 3); size = 1.3; cutoff = 900 } }
        '^mob/redcap/die' { return P 'creature/call' @{ pitch = [math]::Round(300 * $v, 2); len = 0.7; fall = -14; size = 1.3 } }
        '^mob/redcap/hurt' { return P 'creature/call' @{ pitch = [math]::Round(330 * $v, 2); len = 0.22; rise = 5; fall = -5; size = 1.3; rough = 2 } }
        '^mob/redcap/redcap' { return P 'creature/chatter' @{ pitch = [math]::Round(240 * $v, 2); count = 3 + ($n % 3); size = 1.3 } }
        '^mob/slime_beetle/death' { return P 'creature/squish' @{ pitch = 100; len = 0.6; wet = 0.8 } }
        '^mob/slime_beetle/hurt' { return P 'creature/squish' @{ pitch = [math]::Round(200 * $v, 2); len = 0.18 } }
        '^mob/slime_beetle/idle' { return P 'creature/squish' @{ pitch = [math]::Round(140 * $v, 2); len = 0.35 } }
        '^mob/squirrel/death' { return P 'creature/squeak' @{ pitch = 2000; len = 0.3; bend = -12 } }
        '^mob/squirrel/hurt' { return P 'creature/squeak' @{ pitch = 2400; len = 0.12; bend = 4 } }
        '^mob/squirrel/idle' { return P 'creature/chirp' @{ f0 = [math]::Round(2800 * $v, 2); f1 = 4200; len = 0.05; count = 3 + ($n % 3); gap = 0.07 } }
        '^mob/tiny_bird/chirp' { return P 'creature/chirp' @{ f0 = [math]::Round(3500 * $v, 2); f1 = 5200; count = 2 } }
        '^mob/tiny_bird/hurt' { return P 'creature/squeak' @{ pitch = [math]::Round(3000 * $v, 2); len = 0.1 } }
        '^mob/tiny_bird/song' { return P 'creature/chirp' @{ f0 = [math]::Round(3200 * $v, 2); f1 = 5600; len = 0.07; count = 8; gap = 0.12; spread = 5 } }
        '^mob/tome/death' { return P 'object/rustle' @{ len = 0.9; density = 500; f0 = 1200; f1 = 8000 } }
        '^mob/tome/hurt' { return P 'object/rustle' @{ len = 0.25; density = [math]::Round(400 * $v, 1); f0 = 1500; f1 = 7000 } }
        '^mob/tome/idle' { return P 'object/rustle' @{ len = [math]::Round(0.6 * $v, 3); density = 180; f0 = 2000; f1 = 8000 } }
        '^mob/troll/death' { return P 'creature/call' @{ pitch = 120; len = 1.2; fall = -12; size = 0.7; rough = 2.5; vowelA = 0; vowelB = 4 } }
        '^mob/troll/grab' { return P 'creature/growl' @{ pitch = 95; len = 0.5; size = 0.75 } }
        '^mob/troll/hurt' { return P 'creature/call' @{ pitch = 140; len = 0.3; rise = 3; fall = -6; size = 0.7; rough = 2.5 } }
        '^mob/troll/idle' { return P 'creature/call' @{ pitch = [math]::Round(110 * $v, 2); len = 0.7; rise = 1; fall = -3; size = 0.7; rough = 2.5; vowelA = 3; vowelB = 4 } }
        '^mob/troll/throw' { return P 'object/whoosh' @{ len = 0.45; f0 = 200; f1 = 1600 } }
        '^mob/ur_ghast/trap_on' { return P 'magic/whirr' @{ f0 = [math]::Round(300 * $v, 2); f1 = [math]::Round(600 * $v, 2); len = 0.6 } }
        '^mob/ur_ghast/trap_spin_down' { return P 'magic/whirr' @{ f0 = 900; f1 = 150; len = 1.5 } }
        '^mob/ur_ghast/trap_warmup' { return P 'magic/whirr' @{ f0 = 150; f1 = 900; len = 2.0 } }
        '^mob/wraith/' { return P 'creature/ghost' @{ pitch = [math]::Round(200 * $v, 2); len = 1.5; glide = -4 - $n } }
        '^random/beanstalk_grow' { return P 'object/creakgrow' @{ len = 1.5 } }
        '^random/creakgo' { return P 'object/creak' @{ pitch = 50; len = 1.0 } }
        '^random/candelabra/light' { return P 'object/ignite' @{ len = 0.6 } }
        '^random/candelabra/ominous' { return P 'creature/ghost' @{ pitch = 110; len = 1.2; whisper = 0.5; glide = -2 } }
        '^random/casket/close' { return P 'impact/knock' @{ freq = 220; count = 2; gap = 0.05; decay = 0.6 } }
        '^random/casket/open' { return P 'object/creak' @{ pitch = 70; len = 0.6; bend = 6 } }
        '^random/casket/repair' { return P 'impact/knock_metal' @{ freq = 900; count = 3; gap = 0.15 } }
        '^random/jar/jar_in' { return P 'object/slide' @{ f0 = 2000; f1 = 900; clink = 1900 } }
        '^random/jar/jar_out' { return P 'object/slide' @{ f0 = 900; f1 = 2200; clink = 2300 } }
        '^random/jar/jar_place' { return P 'impact/knock_glass' @{ freq = 1300; decay = 0.5 } }
        '^random/jar/jar_swap' { return P 'object/slide' @{ len = 0.2; f0 = 1500; f1 = 1800; clink = 2100 } }
        '^random/jar/jar_wiggle' { return P 'impact/knock_glass' @{ freq = [math]::Round(2100 * $v, 2); count = 3; gap = 0.06; decay = 0.4 } }
        '^random/jar/jar\d' { return P 'impact/knock_glass' @{ freq = [math]::Round(2100 * $v, 2); decay = 0.7 } }
        '^random/scepter/drain' { return P 'magic/drain' @{ pitch = [math]::Round(300 * $v, 2); len = 0.8 } }
        '^random/scepter/shield_break' { return P 'impact/knock_glass' @{ freq = 1600; count = 5; gap = 0.035; spread = 0.6 } }
        '^random/scepter/shield_stop' { return P 'magic/shimmer' @{ freq = 440; sparkle = 0.2 } }
        '^random/scepter/shield_use' { return P 'magic/shimmer' @{ freq = 660 } }
        '^random/scepter/twilight_hit' { return P 'magic/zap' @{ pitch = [math]::Round(400 * $v, 2); len = 0.3; sweep = -12 } }
        '^random/scepter/twilight_use' { return P 'magic/zap' @{ pitch = [math]::Round(700 * $v, 2); len = 0.5; sweep = 12 } }
        '^random/scepter/zombie' { return P 'creature/growl' @{ pitch = 100; len = 1.0; size = 0.8 } }
        '^random/travellers/double_jump' { return P 'object/whoosh' @{ len = 0.3; f0 = 300; f1 = 2500 } }
        '^random/travellers/perfect_dodge' { return P 'object/whoosh' @{ len = 0.25; f0 = 2500; f1 = 600; q = 2.5 } }
        '^random/travellers/side_step_ready' { return P 'object/pop' @{ pitch = 1200 } }
        '^random/travellers/side_step' { return P 'object/whoosh' @{ len = 0.2; f0 = 800; f1 = 3000 } }
    }
    throw "no baseline design for TF sound file '$path': add one to tools/sounds/baseline/generate.ps1"
}

function Get-Synth($spec) { return P $spec.patch $spec.params }

# ---- recipes per owner -----------------------------------------------------------------------------------
$recipes = [ordered]@{}
foreach ($u in ($owner.Values | Sort-Object -Unique)) {
    $recipes[$u] = [ordered]@{
        about  = "Baseline sounds of $u, generated by tools/sounds/baseline/generate.ps1 from TF's event inventory: do not edit (refine in tools/sounds/recipes/$u.json)."
        files  = [ordered]@{}
        events = [ordered]@{}
    }
}

function Add-File([string]$u, [string]$file, $spec) {
    $files = $recipes[$u].files
    if (-not $files.Contains($file)) {
        if ($spec.Contains('patch')) { $spec.seed = Get-NameSeed $file }
        $files[$file] = $spec
    }
}

foreach ($e in ($tfSounds.Keys | Sort-Object)) {
    if (-not $owner.ContainsKey($e)) { throw "TF event $e has no owner in docs/spec/owners.tsv" }
    $u   = $owner[$e]
    $def = $tfSounds[$e]
    $variants = @()
    foreach ($s in $def.sounds) {
        if ($s -is [string]) { $s = @{ name = $s } }
        $name = [string]$s.name
        if ($s.ContainsKey('type') -and $s.type -eq 'event') {
            if ($name -like 'twilightforest:*') { $variants += [ordered]@{ event = $name.Substring(15) }; continue }
            $map = $vanilla.events[$name]
            if ($null -eq $map) { throw "vanilla event $name (in $e) is not mapped in tools/sounds/vanilla.json" }
            if ($map -is [Collections.IDictionary]) {
                $file = "vanilla/$name"
                Add-File $u $file (Get-Synth $map.synth)
                $variants += [ordered]@{ file = $file }
            } else {
                foreach ($p in $map) { $variants += [ordered]@{ vanilla = $p } }
            }
            continue
        }
        $entry = [ordered]@{}
        if ($name -like 'twilightforest:*') {
            $file = $name.Substring(15)
            Add-File $u $file (Get-Design $file)
            $entry.file = $file
        } else {
            $path = $name -replace '^minecraft:', ''
            $map  = $vanilla.files[$path]
            if ($null -eq $map) { throw "vanilla file $path (in $e) is not mapped in tools/sounds/vanilla.json" }
            if ($map -is [Collections.IDictionary]) {
                $file = "vanilla/$path"
                Add-File $u $file (Get-Synth $map.synth)
                $entry.file = $file
            } else {
                $entry.vanilla = $map[0]
            }
        }
        foreach ($k in 'volume', 'pitch') { if ($s.ContainsKey($k)) { $entry[$k] = [double]$s[$k] } }
        if ($s.ContainsKey('stream')) { $entry.stream = [bool]$s.stream }
        $variants += $entry
    }
    $ev = [ordered]@{ category = Get-Category $e }
    if ($def.ContainsKey('subtitle')) { $ev.subtitle = $def.subtitle }
    if ($e -like 'music_disc.*') { $ev.max_distance = 64.0 } # jukebox songs carry 64 blocks (Java: volume 4 x 16)
    $ev.variants = $variants
    $recipes[$u].events[$e] = $ev
}

$diffs = 0
foreach ($u in $recipes.Keys) {
    $path = Join-Path $PSScriptRoot "$u.json"
    $text = (Format-Json $recipes[$u]) + "`n"
    if ($Check) {
        $cur = if (Test-Path $path) { (Get-Content $path -Raw) -replace "`r`n", "`n" } else { '' }
        if ($cur -ne $text) { Write-Host "[baseline] differs: tools/sounds/baseline/$u.json" -ForegroundColor Red; $diffs++ }
    } else {
        [IO.File]::WriteAllText($path, $text, [Text.UTF8Encoding]::new($false))
    }
}
if ($Check) {
    if ($diffs) { exit 1 }
    Write-Host "[baseline] -Check OK: $($recipes.Count) baseline recipes match TF's inventory" -ForegroundColor Green
    exit 0
}
$files = ($recipes.Values | ForEach-Object { $_.files.Count } | Measure-Object -Sum).Sum
Write-Host "[baseline] $($recipes.Count) recipes, $($tfSounds.Count) events, $files files" -ForegroundColor Green
