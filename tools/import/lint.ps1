<#
.SYNOPSIS
  Asset lint (unit F1, audit A7): lang coverage, texture and model completeness of an assembled mod.

.DESCRIPTION
  Reads an assembled mod directory (tools/assemble.ps1 -Check writes build/assemble-check; the build
  writes bin/<target>/GloamingForest) and checks what the packs reference against what they contain:

    lang      en_US has every one of upstream's en_us keys (2061) in lang/en_US.json and texts/en_US.lang;
              every Bedrock locale has every key upstream translates for it (upstream's lang file of that locale,
              mapped as tools/import/src/Lang.cs maps it), with upstream's text; every BP block, item and
              entity has its name key in texts/en_US.lang; every mod string of assets/lang/en_US is
              merged. Prints the coverage of each of the 29 locales (keys of 2061 that are upstream's own
              translation; the rest falls back to en_US, as in upstream).
    textures  every path of terrain_texture.json, item_texture.json, flipbook_textures.json, the client
              entities, attachables and particles is a file of the pack (.png / .tga) or of Bedrock's
              vanilla resource pack (-Vanilla: its contents.json); every texture key a BP block
              (material_instances), an item (minecraft:icon) or blocks.json names is defined.
    models    every geometry a BP block, a client entity or an attachable names is defined in the pack
              (vanilla minecraft:geometry.* and non-upstream ids are counted as external); every animation,
              animation controller and render controller a client entity / attachable names is defined
              in the pack, unless it is a vanilla one (no "gloamingforest" in it).
    complete  every block, item, spawn egg and entity of upstream's skeleton (data/gf/import/skeleton.json) is
              in the BP; every upstream entity has a client entity; every item has an icon.

  Never reads upstream's .ogg or .nbt files (only the lang JSON of the shared checkout, rule R24).

  Exit code 0 = OK, 1 = a check failed (listed), 2 = no assembled directory.

.PARAMETER Out
  The assembled mod directory. Default: build/assemble-check, else bin/server/GloamingForest.
.PARAMETER Vanilla
  The game's data/resource_packs directory: the file lists (contents.json) of every vanilla* pack in
  it (vanilla, vanilla_1.20.0, ...: Bedrock layers them all) are the vanilla files. Default: the
  LeviLauncher instance %APPDATA%\levilauncher.exe\versions\1.26.51.01\data\resource_packs when it
  exists; without it, paths outside the pack are reported as unverified (not failures).
.PARAMETER GloamingDir
  The upstream checkout (default %LOCALAPPDATA%\gf-upstream\view, tools/upstream/fetch.ps1).
.PARAMETER Json
  Also writes the report (coverage, counts, problems) to this JSON file.

.EXAMPLE
  pwsh tools/assemble.ps1 -Check; pwsh tools/import/lint.ps1
  pwsh tools/import/lint.ps1 -Out bin/client/GloamingForest -Json build/asset-lint.json
#>
param(
    [string]$Out,
    [string]$Vanilla,
    [string]$GloamingDir,
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
    Write-Host '[lint] no assembled mod: run pwsh tools/assemble.ps1 -Check (or a build) first, or pass -Out' -ForegroundColor Red
    exit 2
}
$Out = (Resolve-Path $Out).Path
$rp = Join-Path $Out 'resource_packs/GloamingForest_RP'
$bp = Join-Path $Out 'behavior_packs/GloamingForest_BP'
if (-not $GloamingDir) { $GloamingDir = if ($env:GF_UPSTREAM_HOME) { $env:GF_UPSTREAM_HOME } else { Join-Path $env:LOCALAPPDATA 'gf-upstream/view' } }
if (-not $Vanilla) {
    $v = Join-Path $env:APPDATA 'levilauncher.exe/versions/1.26.51.01/data/resource_packs'
    if (Test-Path $v) { $Vanilla = $v }
}

$problems = [Collections.Generic.List[string]]::new()
$notes = [Collections.Generic.List[string]]::new()
function Problem([string]$area, [string]$msg) { $problems.Add("[$area] $msg") }

function Read-Json([string]$path) {
    try {
        $r = Get-Content -LiteralPath $path -Raw -Encoding utf8 | ConvertFrom-Json -AsHashtable -Depth 100
        if ($null -eq $r) { Problem 'json' "${path}: empty"; return @{} }
        return $r
    }
    catch { Problem 'json' "${path}: $($_.Exception.Message)"; return @{} } # an empty document: the checks go on
}
function Rel([string]$path) { return $path.Substring($Out.Length + 1).Replace('\', '/') }
function Get-Desc($doc, [string]$key) {
    if ($doc -isnot [Collections.IDictionary] -or -not $doc.Contains($key)) { return $null }
    $d = $doc[$key]
    if ($d -is [Collections.IDictionary] -and $d.Contains('description')) { return $d['description'] }
    return $null
}
function Values($map) {
    if ($map -is [Collections.IDictionary]) { return @($map.Values) }
    if ($null -eq $map) { return @() }
    return @($map)
}

# ---- files of the pack and of the vanilla pack --------------------------------------------------------
$packFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($f in Get-ChildItem $rp -Recurse -File) { [void]$packFiles.Add($f.FullName.Substring($rp.Length + 1).Replace('\', '/')) }
$vanillaFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
if ($Vanilla -and (Test-Path $Vanilla)) {
    foreach ($d in Get-ChildItem $Vanilla -Directory -Filter 'vanilla*') {
        $list = Join-Path $d.FullName 'contents.json'
        if (Test-Path $list) { foreach ($e in (Read-Json $list)['content']) { [void]$vanillaFiles.Add([string]$e['path']) } }
    }
}
$unverified = [Collections.Generic.SortedSet[string]]::new([StringComparer]::Ordinal)

# A texture path (no extension) is a pack file, a vanilla file, or (without -Vanilla) unverified.
function Test-TexturePath([string]$area, [string]$path, [string]$where) {
    if ([string]::IsNullOrEmpty($path)) { return }
    $p = $path.Replace('\', '/').TrimStart('/')
    foreach ($ext in '', '.png', '.tga', '.jpg') { if ($packFiles.Contains("$p$ext")) { return } }
    if ($vanillaFiles.Count) {
        foreach ($ext in '', '.png', '.tga', '.jpg') { if ($vanillaFiles.Contains("$p$ext")) { return } }
        Problem $area "$where names $p, which is neither in the pack nor in Bedrock's vanilla resource pack"
    } elseif ($p -match 'gloamingforest|/upstream-derived/' -or $p -like 'textures/blocks/*/*' ) {
        Problem $area "$where names $p, which is not in the pack"
    } else {
        [void]$unverified.Add($p)
    }
}

# ---- lang -----------------------------------------------------------------------------------------------
$merges = Read-Json (Join-Path $root 'tools/xmake/merges.json')
$locales = @($merges['locales'])
$aliases = if ($merges.Contains('localeAliases')) { $merges['localeAliases'] } else { @{} }

function Read-LangFile([string]$path) {
    $m = @{}
    if (-not (Test-Path $path)) { return $m }
    foreach ($line in [IO.File]::ReadAllLines($path, [Text.Encoding]::UTF8)) {
        if ($line.Length -eq 0 -or $line.StartsWith('#') -or $line.StartsWith('##')) { continue }
        $i = $line.IndexOf('=')
        if ($i -le 0) { continue }
        $v = $line.Substring($i + 1)
        $t = $v.IndexOf("`t#")
        if ($t -ge 0) { $v = $v.Substring(0, $t) }
        $m[$line.Substring(0, $i)] = $v
    }
    return $m
}

$coverage = [ordered]@{}
$gloamingEnPath = Join-Path $GloamingDir 'src/generated/resources/assets/gloamingforest/lang/en_us.json'
$gloamingLangDir = Join-Path $GloamingDir 'src/main/resources/assets/gloamingforest/lang'
if (-not (Test-Path $gloamingEnPath)) {
    $notes.Add("upstream checkout not found at $GloamingDir (pwsh tools/upstream/fetch.ps1): lang coverage skipped")
} else {
    $gloamingEn = Read-Json $gloamingEnPath
    # upstream's translations per Bedrock locale (an exact file name beats an alias, as in Lang.cs)
    $gloamingByLocale = @{ 'en_US' = $gloamingEn }
    foreach ($f in Get-ChildItem $gloamingLangDir -Filter *.json | Sort-Object Name) {
        $p = $f.BaseName.Split('_')
        if ($p.Count -ne 2) { continue }
        $loc = $p[0].ToLowerInvariant() + '_' + $p[1].ToUpperInvariant()
        if ($aliases.Contains($loc)) { $loc = $aliases[$loc] }
        if ($locales -notcontains $loc -or $loc -eq 'en_US') { continue }
        $exact = $f.BaseName.ToLowerInvariant() -eq $loc.ToLowerInvariant()
        if ($gloamingByLocale.ContainsKey($loc) -and -not $exact) { continue }
        $j = Read-Json $f.FullName
        $m = @{}
        foreach ($k in $j.Keys) { if ($gloamingEn.Contains($k) -and $j[$k] -is [string] -and $j[$k].Length -gt 0) { $m[$k] = $j[$k] } }
        $gloamingByLocale[$loc] = $m
    }
    foreach ($loc in $locales) {
        $langPath = Join-Path $Out "lang/$loc.json"
        $mod = if (Test-Path $langPath) { Read-Json $langPath } else { @{} }
        $texts = Read-LangFile (Join-Path $rp "texts/$loc.lang")
        $want = if ($gloamingByLocale.ContainsKey($loc)) { $gloamingByLocale[$loc] } else { @{} }
        $missingMod = 0; $missingTexts = 0; $differs = 0; $first = @()
        foreach ($k in $want.Keys) {
            if (-not $mod.Contains($k)) { $missingMod++; if ($first.Count -lt 3) { $first += "$k (lang/$loc.json)" } }
            elseif ($mod[$k] -cne $want[$k]) { $differs++; if ($first.Count -lt 3) { $first += "$k differs from upstream's text" } }
            if (-not $texts.ContainsKey($k)) { $missingTexts++; if ($first.Count -lt 3) { $first += "$k (texts/$loc.lang)" } }
        }
        if ($missingMod + $missingTexts + $differs) {
            Problem 'lang' "${loc}: $missingMod of upstream's $($want.Count) keys missing from lang/$loc.json, $missingTexts from texts/$loc.lang, $differs with another text (e.g. $($first -join '; '))"
        }
        $coverage[$loc] = [ordered]@{ upsrc = $want.Count; percent = [math]::Round(100.0 * $want.Count / $gloamingEn.Count, 1); modKeys = $mod.Count; texts = $texts.Count }
    }
    if ($gloamingEn.Count -ne 2061) { $notes.Add("upstream's en_us has $($gloamingEn.Count) keys (2061 expected for upstream 4.8)") }
}

# name keys of every BP block / item / entity in en_US (Bedrock shows the raw key otherwise)
$enTexts = Read-LangFile (Join-Path $rp 'texts/en_US.lang')
$bpIds = @{ block = @{}; item = @{}; entity = @{} }
foreach ($kind in 'blocks', 'items', 'entities') {
    $dir = Join-Path $bp $kind
    if (-not (Test-Path $dir)) { continue }
    foreach ($f in Get-ChildItem $dir -Recurse -Filter *.json) {
        $doc = Read-Json $f.FullName
        foreach ($k in 'minecraft:block', 'minecraft:item', 'minecraft:entity') {
            $d = Get-Desc $doc $k
            if ($d -and $d['identifier']) { $bpIds[$k.Substring(10)][[string]$d['identifier']] = $doc }
        }
    }
}
$nameMissing = @()
foreach ($id in $bpIds.block.Keys) {
    if ($id -notlike 'gloamingforest:*') { continue }
    if (-not $enTexts.ContainsKey("tile.$id.name")) { $nameMissing += "tile.$id.name" }
}
foreach ($id in $bpIds.item.Keys) {
    if ($id -notlike 'gloamingforest:*') { continue }
    $comps = $bpIds.item[$id]['minecraft:item']['components']
    $key = "item.$id.name"
    if ($comps -is [Collections.IDictionary] -and $comps.Contains('minecraft:display_name')) {
        $v = $comps['minecraft:display_name']['value']
        if ($v -is [string] -and $v -notmatch '\.name$|^item\.|^tile\.|^entity\.') { continue } # a literal name
        if ($v -is [string]) { $key = $v }
    }
    if (-not $enTexts.ContainsKey($key) -and -not $enTexts.ContainsKey("tile.$id.name")) { $nameMissing += $key }
}
# upstream's entities (the skeleton's 80; the multipart helpers such as naga_part are GloamingForest's own and unnamed)
$skelPath = Join-Path $Out 'data/gf/import/skeleton.json'
$skel = if (Test-Path $skelPath) { Read-Json $skelPath } else { $null }
$gloamingEntities = if ($skel) { @($skel['entities']) } else { @($bpIds.entity.Keys | Where-Object { $_ -like 'gloamingforest:*' }) }
foreach ($id in $gloamingEntities) {
    if (-not $enTexts.ContainsKey("entity.$id.name")) { $nameMissing += "entity.$id.name" }
}
if ($nameMissing.Count) { Problem 'lang' "$($nameMissing.Count) BP name keys missing from texts/en_US.lang: $(($nameMissing | Sort-Object | Select-Object -First 12) -join ', ')$(if ($nameMissing.Count -gt 12) { ', ...' })" }

# every mod string of assets/lang/en_US is merged
$enModPath = Join-Path $Out 'lang/en_US.json'
$enMod = if (Test-Path $enModPath) { Read-Json $enModPath } else { Problem 'lang' 'lang/en_US.json is missing'; @{} }
$modMissing = @()
foreach ($f in Get-ChildItem (Join-Path $root 'assets/lang/en_US') -Filter *.json -ErrorAction SilentlyContinue) {
    $j = Read-Json $f.FullName
    foreach ($k in $j.Keys) { if (-not $enMod.Contains($k)) { $modMissing += "$k ($($f.Name))" } }
}
if ($modMissing.Count) { Problem 'lang' "$($modMissing.Count) keys of assets/lang/en_US missing from lang/en_US.json: $(($modMissing | Select-Object -First 8) -join ', ')" }

# ---- textures -------------------------------------------------------------------------------------------
function Get-AtlasKeys([string]$file, [string]$area) {
    $keys = @{}
    $p = Join-Path $rp $file
    if (-not (Test-Path $p)) { Problem $area "$file is missing"; return $keys }
    $doc = Read-Json $p
    if (-not $doc.Contains('texture_data')) { Problem $area "$file has no texture_data"; return $keys }
    foreach ($k in $doc['texture_data'].Keys) {
        $keys[$k] = $true
        $t = $doc['texture_data'][$k]['textures']
        foreach ($entry in @($t)) {
            if ($entry -is [string]) { Test-TexturePath $area $entry "$file $k" }
            elseif ($entry -is [Collections.IDictionary]) {
                if ($entry.Contains('path')) { Test-TexturePath $area $entry['path'] "$file $k" }
                foreach ($v in @($(if ($entry.Contains('variations')) { $entry['variations'] } else { @() }))) { Test-TexturePath $area $v['path'] "$file $k" }
            }
        }
    }
    return $keys
}
$terrain = Get-AtlasKeys 'textures/terrain_texture.json' 'textures'
$itemTex = Get-AtlasKeys 'textures/item_texture.json' 'textures'
$fb = Join-Path $rp 'textures/flipbook_textures.json'
if (Test-Path $fb) { foreach ($e in @(Read-Json $fb)) { Test-TexturePath 'textures' $e['flipbook_texture'] 'flipbook_textures.json' } }
# blocks.json / BP blocks may name Bedrock's own atlas keys: the vanilla atlas is archived, so keys
# without an upstream prefix are counted, not checked
$externalKeys = [Collections.Generic.SortedSet[string]]::new([StringComparer]::Ordinal)
function Test-TerrainKey([string]$key, [string]$where) {
    if ([string]::IsNullOrEmpty($key) -or $terrain.ContainsKey($key)) { return }
    if ($key -match '^gf_|gloamingforest') { Problem 'textures' "$where names terrain texture '$key', which terrain_texture.json does not define" }
    else { [void]$externalKeys.Add($key) }
}
$blocksJson = if (Test-Path (Join-Path $rp 'blocks.json')) { Read-Json (Join-Path $rp 'blocks.json') } else { @{} }
foreach ($id in @($blocksJson.Keys)) {
    if ($id -eq 'format_version' -or $blocksJson[$id] -isnot [Collections.IDictionary]) { continue }
    $t = $blocksJson[$id]['textures']
    if ($null -eq $t) { continue }
    foreach ($k in @($(if ($t -is [Collections.IDictionary]) { $t.Values } else { $t }))) { Test-TerrainKey $k "blocks.json $id" }
}
foreach ($id in $bpIds.block.Keys) {
    $b = $bpIds.block[$id]['minecraft:block']
    $lists = @($b['components'])
    foreach ($perm in @($(if ($b.Contains('permutations')) { $b['permutations'] } else { @() }))) { $lists += $perm['components'] }
    foreach ($c in $lists) {
        if ($c -isnot [Collections.IDictionary] -or -not $c.Contains('minecraft:material_instances')) { continue }
        foreach ($mi in Values $c['minecraft:material_instances']) {
            if ($mi -is [Collections.IDictionary] -and $mi.Contains('texture')) { Test-TerrainKey $mi['texture'] "BP block $id" }
        }
    }
}
$noIcon = @(); $placerNoIcon = @()
foreach ($id in $bpIds.item.Keys) {
    $c = $bpIds.item[$id]['minecraft:item']['components']
    if ($c -isnot [Collections.IDictionary] -or -not $c.Contains('minecraft:icon')) {
        if ($id -like 'gloamingforest:*' -and -not $bpIds.block.ContainsKey($id)) {
            # a block placer without an icon: the game draws the placed block's icon (checked on screenshots, not here)
            if ($c -is [Collections.IDictionary] -and $c.Contains('minecraft:block_placer')) { $placerNoIcon += $id } else { $noIcon += $id }
        }
        continue
    }
    $icon = $c['minecraft:icon']
    $keys = if ($icon -is [string]) { @($icon) } elseif ($icon.Contains('textures')) { Values $icon['textures'] } else { @($icon['texture']) }
    foreach ($k in $keys) {
        if ($k -and -not $itemTex.ContainsKey($k)) {
            if ($k -match '^gf_|gloamingforest') { Problem 'textures' "BP item $id names icon '$k', which item_texture.json does not define" } else { [void]$externalKeys.Add($k) }
        }
    }
}
if ($placerNoIcon.Count) { $notes.Add("$($placerNoIcon.Count) block-placer items without minecraft:icon (the inventory shows the game's block-placer fallback: check a client screenshot): $(($placerNoIcon | Sort-Object | Select-Object -First 4) -join ', ')$(if ($placerNoIcon.Count -gt 4) { ', ...' })") }
if ($noIcon.Count) { Problem 'textures' "$($noIcon.Count) upstream items without minecraft:icon: $(($noIcon | Sort-Object | Select-Object -First 10) -join ', ')" }

# ---- client entities, attachables, particles: textures and models ----------------------------------------
$geometries = @{}; $animations = @{}; $controllers = @{}; $renderControllers = @{}
foreach ($f in Get-ChildItem (Join-Path $rp 'models') -Recurse -Filter *.json -ErrorAction SilentlyContinue) {
    $doc = Read-Json $f.FullName
    if ($null -eq $doc) { continue }
    if ($doc.Contains('minecraft:geometry')) { foreach ($g in $doc['minecraft:geometry']) { $geometries[[string]$g['description']['identifier']] = Rel $f.FullName } }
    foreach ($k in $doc.Keys) { if ($k -like 'geometry.*') { $geometries[($k -split ':')[0]] = Rel $f.FullName } } # legacy format
}
foreach ($f in Get-ChildItem (Join-Path $rp 'animations') -Recurse -Filter *.json -ErrorAction SilentlyContinue) {
    $doc = Read-Json $f.FullName; if ($doc -and $doc.Contains('animations')) { foreach ($k in $doc['animations'].Keys) { $animations[$k] = $true } }
}
foreach ($f in Get-ChildItem (Join-Path $rp 'animation_controllers') -Recurse -Filter *.json -ErrorAction SilentlyContinue) {
    $doc = Read-Json $f.FullName; if ($doc -and $doc.Contains('animation_controllers')) { foreach ($k in $doc['animation_controllers'].Keys) { $controllers[$k] = $true } }
}
foreach ($f in Get-ChildItem (Join-Path $rp 'render_controllers') -Recurse -Filter *.json -ErrorAction SilentlyContinue) {
    $doc = Read-Json $f.FullName; if ($doc -and $doc.Contains('render_controllers')) { foreach ($k in $doc['render_controllers'].Keys) { $renderControllers[$k] = $true } }
}
$externalGeo = [Collections.Generic.SortedSet[string]]::new([StringComparer]::Ordinal)
function Test-Geometry([string]$id, [string]$where) {
    if ([string]::IsNullOrEmpty($id) -or $geometries.ContainsKey($id) -or $id -like 'minecraft:geometry.*') { return }
    if ($id -match 'gloamingforest|\.upsrc\.') { Problem 'models' "$where names geometry $id, which no model file of the pack defines" } else { [void]$externalGeo.Add($id) }
}
function Test-Named($set, [string]$kind, [string]$id, [string]$where) {
    if ([string]::IsNullOrEmpty($id) -or $set.ContainsKey($id)) { return }
    if ($id -match 'gloamingforest|gloamingforest|\.upsrc\.') { Problem 'models' "$where names $kind $id, which the pack does not define" }
}
foreach ($id in $bpIds.block.Keys) {
    $b = $bpIds.block[$id]['minecraft:block']
    $lists = @($b['components'])
    foreach ($perm in @($(if ($b.Contains('permutations')) { $b['permutations'] } else { @() }))) { $lists += $perm['components'] }
    foreach ($c in $lists) {
        if ($c -isnot [Collections.IDictionary] -or -not $c.Contains('minecraft:geometry')) { continue }
        $g = $c['minecraft:geometry']
        Test-Geometry $(if ($g -is [string]) { $g } else { $g['identifier'] }) "BP block $id"
    }
}
$clientEntities = @{}; $spawnEggs = @{}
foreach ($pair in @(@('entity', 'minecraft:client_entity'), @('attachables', 'minecraft:attachable'))) {
    foreach ($f in Get-ChildItem (Join-Path $rp $pair[0]) -Recurse -Filter *.json -ErrorAction SilentlyContinue) {
        $doc = Read-Json $f.FullName
        $d = Get-Desc $doc $pair[1]
        if ($null -eq $d) { continue }
        $where = Rel $f.FullName
        if ($pair[0] -eq 'entity') {
            $clientEntities[[string]$d['identifier']] = $where
            if ($d.Contains('spawn_egg')) {
                $spawnEggs[[string]$d['identifier'] + '_spawn_egg'] = $true
                if ($d['spawn_egg'].Contains('texture') -and -not $itemTex.ContainsKey($d['spawn_egg']['texture']) -and $d['spawn_egg']['texture'] -match '^gf_|gloamingforest') {
                    Problem 'textures' "$where names spawn egg texture '$($d['spawn_egg']['texture'])', which item_texture.json does not define"
                }
            }
        }
        if ($d.Contains('textures')) { foreach ($t in Values $d['textures']) { Test-TexturePath 'textures' $t $where } }
        if ($d.Contains('geometry')) { foreach ($g in Values $d['geometry']) { Test-Geometry $g $where } }
        if ($d.Contains('animations')) {
            foreach ($a in Values $d['animations']) {
                if ($a -like 'controller.*') { Test-Named $controllers 'animation controller' $a $where } else { Test-Named $animations 'animation' $a $where }
            }
        }
        if ($d.Contains('render_controllers')) {
            foreach ($r in @($d['render_controllers'])) {
                $name = if ($r -is [string]) { $r } elseif ($r -is [Collections.IDictionary] -and $r.Count -eq 1) { @($r.Keys)[0] } else { '' }
                if (-not $name) { Problem 'models' "$where has a malformed render controller entry" } else { Test-Named $renderControllers 'render controller' $name $where }
            }
        }
    }
}
foreach ($f in Get-ChildItem (Join-Path $rp 'particles') -Recurse -Filter *.json -ErrorAction SilentlyContinue) {
    $doc = Read-Json $f.FullName
    $d = Get-Desc $doc 'particle_effect'
    if ($d -and $d.Contains('basic_render_parameters')) { Test-TexturePath 'textures' $d['basic_render_parameters']['texture'] (Rel $f.FullName) }
}

# ---- completeness against upstream's skeleton -------------------------------------------------------------------
$counts = [ordered]@{}
if ($skel) {
    $miss = @()
    foreach ($id in $skel['blocks'].Keys) { if (-not $bpIds.block.ContainsKey($id)) { $miss += "block $id" } }
    foreach ($id in @($skel['items'])) { if (-not $bpIds.item.ContainsKey($id) -and -not $bpIds.block.ContainsKey($id)) { $miss += "item $id" } }
    # Bedrock makes <entity>_spawn_egg from the client entity's spawn_egg (or a BP item of that id)
    foreach ($id in @($skel['spawnEggs'])) { if (-not $spawnEggs.ContainsKey($id) -and -not $bpIds.item.ContainsKey($id)) { $miss += "spawn egg $id" } }
    foreach ($id in $skel['entities']) {
        if (-not $bpIds.entity.ContainsKey($id)) { $miss += "entity $id" }
        if (-not $clientEntities.ContainsKey($id)) { $miss += "client entity $id" }
    }
    if ($miss.Count) { Problem 'complete' "$($miss.Count) skeleton ids not in the packs: $(($miss | Select-Object -First 12) -join ', ')" }
    $counts = [ordered]@{ blocks = $skel['blocks'].Count; items = @($skel['items']).Count; spawnEggs = @($skel['spawnEggs']).Count; entities = @($skel['entities']).Count }
} else {
    Problem 'complete' 'data/gf/import/skeleton.json is missing'
}

# ---- report ------------------------------------------------------------------------------------------------
Write-Host "[lint] $Out"
if ($coverage.Count) {
    Write-Host '[lint] lang coverage of upstream''s keys per Bedrock locale (upstream''s own translations; the rest falls back to en_US as in upstream):'
    $line = ($coverage.GetEnumerator() | ForEach-Object { '{0} {1}%' -f $_.Key, $_.Value.percent }) -join ', '
    Write-Host "       $line"
}
Write-Host ("[lint] {0} terrain textures, {1} item textures, {2} geometries, {3} animations, {4} render controllers, {5} client entities; skeleton {6}" -f `
        $terrain.Count, $itemTex.Count, $geometries.Count, $animations.Count, $renderControllers.Count, $clientEntities.Count, (($counts.GetEnumerator() | ForEach-Object { "$($_.Value) $($_.Key)" }) -join ', '))
if (-not $vanillaFiles.Count) { Write-Host "[lint] no vanilla contents.json (-Vanilla): $($unverified.Count) non-upstream texture paths unverified" -ForegroundColor Yellow }
if ($externalKeys.Count) { Write-Host "[lint] $($externalKeys.Count) vanilla atlas keys used (not checked: $(($externalKeys | Select-Object -First 6) -join ', ')$(if ($externalKeys.Count -gt 6) { ', ...' }))" }
if ($externalGeo.Count) { Write-Host "[lint] $($externalGeo.Count) vanilla geometries used: $(($externalGeo | Select-Object -First 8) -join ', ')$(if ($externalGeo.Count -gt 8) { ', ...' })" }
foreach ($n in $notes) { Write-Host "[lint] note: $n" -ForegroundColor Yellow }
if ($Json) {
    [ordered]@{ out = $Out; coverage = $coverage; skeleton = $counts; terrainTextures = $terrain.Count; itemTextures = $itemTex.Count
        geometries = $geometries.Count; clientEntities = $clientEntities.Count; vanillaChecked = [bool]$vanillaFiles.Count
        unverified = @($unverified); externalAtlasKeys = @($externalKeys); externalGeometries = @($externalGeo); problems = @($problems)
    } | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 $Json
}
if ($problems.Count) {
    foreach ($p in $problems) { Write-Host "[lint] $p" -ForegroundColor Red }
    Write-Host "[lint] FAILED: $($problems.Count) problem(s)" -ForegroundColor Red
    exit 1
}
Write-Host '[lint] OK' -ForegroundColor Green
exit 0
