<#
.SYNOPSIS
  Generates docs/spec/owners.tsv: the owner of every id of every kind of The Twilight Forest 4.8,
  plus the paths, channels, config keys, selftest ranges and scenarios of every unit.

.DESCRIPTION
  Contract "owners" (F0). Reads the TF registries from the shared local checkout
  (tools/tf/fetch.ps1, rule R24) and partitions every id with the rules of
  tools/spec/owners.rules.json, which encode the content fields of the unit records
  (docs/spec/units.json). Columns (tab-separated, sorted by kind then id):

    kind    id-or-glob    owner    mode

  - kind: block, block_item, item, spawn_egg, entity, biome, structure, structure_set, ...,
    lang (en_us keys), config_section, config_key, net, subcommand, function, gamerule, stat,
    attribute, keybind, registry, registry_entry, selftest, selftest_dx, worlddb, e2e, path, stub;
    plus the kinds of the ids outside TF's namespace a unit declares (units.<u>.ids in the rules,
    e.g. mod_entity).
  - id-or-glob: TF ids with their namespace ("twilightforest:naga_stone"); globs for path, net,
    selftest and worlddb rows; declared ids as written ("gloamingforest:sign_text").
  - owner: a unit id (F0..F16, U1..U30, A8).
  - mode: gen (produced by a tool: import, models, synth; regenerate, never hand-edit) or manual.

  Every id gets exactly one owner (first matching rule). Ids no rule claims are reported on the
  console and in docs/spec/owners-unclaimed.txt (an empty file when everything is claimed); -Strict
  makes them fail the run. -Check compares the generated table with the committed one (CI).

.EXAMPLE
  pwsh tools/spec/owners.ps1                 # regenerate docs/spec/owners.tsv
  pwsh tools/spec/owners.ps1 -Check          # fail if the committed table is stale
#>
param(
    [string]$TfDir,
    [string]$Out,
    [switch]$Check,
    [switch]$Strict
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = (Resolve-Path "$PSScriptRoot/../..").Path
if (-not $TfDir) { $TfDir = if ($env:GF_TF_HOME) { $env:GF_TF_HOME } else { Join-Path $env:LOCALAPPDATA 'gf-tf' } }
if (-not $Out) { $Out = Join-Path $root 'docs/spec/owners.tsv' }
$unclaimedFile = Join-Path (Split-Path $Out) 'owners-unclaimed.txt'
if (-not (Test-Path (Join-Path $TfDir 'src/main/java/twilightforest'))) {
    throw "TF checkout not found at $TfDir (run: pwsh tools/tf/fetch.ps1)"
}
$JavaDir = Join-Path $TfDir 'src/main/java/twilightforest'
$GenRes = Join-Path $TfDir 'src/generated/resources'
$MainRes = Join-Path $TfDir 'src/main/resources'
$rules = Get-Content (Join-Path $PSScriptRoot 'owners.rules.json') -Raw | ConvertFrom-Json -AsHashtable

# ---- helpers --------------------------------------------------------------------------------------

$globCache = @{}
# glob -> anchored regex: '*' any text, '?' one char, '{a,b}' alternation
function Get-GlobRegex([string]$glob) {
    if ($globCache.ContainsKey($glob)) { return $globCache[$glob] }
    $sb = [Text.StringBuilder]::new('^')
    foreach ($c in $glob.ToCharArray()) {
        switch ($c) {
            '*' { [void]$sb.Append('.*') }
            '?' { [void]$sb.Append('.') }
            '{' { [void]$sb.Append('(?:') }
            '}' { [void]$sb.Append(')') }
            ',' { [void]$sb.Append('|') }
            default { [void]$sb.Append([regex]::Escape([string]$c)) }
        }
    }
    [void]$sb.Append('$')
    $re = [regex]::new($sb.ToString(), 'Compiled')
    $globCache[$glob] = $re
    return $re
}

function Read-Text([string]$path) { [IO.File]::ReadAllText($path) }
function Strip-Comments([string]$text) { [regex]::Replace($text, '(?m)^\s*//.*$|//[^\n"]*$', '') }

# The first string argument of every `static final ... = call("id"` declaration of a Java file.
function Get-JavaIds([string]$file, [string]$declPattern = 'static\s+final\s+[^=;]+=\s*[\w.]+\(\s*"([A-Za-z0-9_./]+)"') {
    $text = Strip-Comments (Read-Text (Join-Path $JavaDir $file))
    $seen = [ordered]@{}
    foreach ($m in [regex]::Matches($text, $declPattern)) { $seen[$m.Groups[1].Value] = $true }
    return @($seen.Keys)
}

# Files under a data dir as ids ("<relative path without .json>").
function Get-DataIds([string]$dir, [string]$filter = '*.json') {
    if (-not (Test-Path $dir)) { return @() }
    $base = (Resolve-Path $dir).Path.TrimEnd('\') + '\'
    return @(Get-ChildItem $dir -Recurse -File -Filter $filter | ForEach-Object {
        $r = $_.FullName.Substring($base.Length).Replace('\', '/')
        $r.Substring(0, $r.Length - [IO.Path]::GetExtension($r).Length)
    } | Sort-Object)
}

$ids = [ordered]@{}        # kind -> [list of ids]
$owner = @{}               # kind -> @{ id -> owner }
$modes = @{}               # kind -> gen|manual
$idModes = @{}             # kind -> @{ id -> gen|manual } (ids units declare, units.<u>.ids)
$declared = [Collections.Generic.List[object]]::new() # [kind, id, unit] of units.<u>.ids
$unclaimed = [Collections.Generic.List[string]]::new()
$notes = [Collections.Generic.List[string]]::new()

function Add-Ids([string]$kind, [string[]]$list, [string]$mode = 'gen') {
    if (-not $ids.Contains($kind)) { $ids[$kind] = [Collections.Generic.List[string]]::new(); $owner[$kind] = @{} }
    foreach ($i in $list) { if ($i -and -not $ids[$kind].Contains($i)) { $ids[$kind].Add($i) } }
    $modes[$kind] = $mode
}

function Set-Owner([string]$kind, [string]$id, [string]$o) {
    if (-not $ids.Contains($kind)) { Add-Ids $kind @() }
    if (-not $ids[$kind].Contains($id)) { $ids[$kind].Add($id) }
    $owner[$kind][$id] = $o
}

# Applies the ordered [glob, owner] rules of a kind; '$<kind>' owners resolve through `resolver`.
function Resolve-Rules([string]$kind, $ruleList, [scriptblock]$resolver = $null) {
    foreach ($id in $ids[$kind]) {
        if ($owner[$kind].ContainsKey($id)) { continue }
        $o = $null
        foreach ($r in $ruleList) {
            if ((Get-GlobRegex $r[0]).IsMatch($id)) {
                $o = $r[1]
                if ($o.StartsWith('$')) { $o = & $resolver $o.Substring(1) $id }
                break
            }
        }
        if ($o) { $owner[$kind][$id] = $o } else { $unclaimed.Add("${kind}`t$id") }
    }
}

function Owner-Of([string]$kind, [string]$id) {
    if ($owner.ContainsKey($kind) -and $owner[$kind].ContainsKey($id)) { return $owner[$kind][$id] }
    return $null
}

# ---- extraction -------------------------------------------------------------------------------------

# blocks, items, block items, entities, spawn eggs
Add-Ids block (Get-JavaIds 'init/TFBlocks.java' 'DeferredBlock<[^=;]*>\s+\w+\s*=\s*[\w.]+\(\s*"([a-z0-9_]+)"')
Add-Ids item (Get-JavaIds 'init/TFItems.java' 'DeferredItem<[^=;]*>\s+\w+\s*=\s*[\w.]+\(\s*"([a-z0-9_]+)"')
$itemModels = @(Get-DataIds (Join-Path $GenRes 'assets/twilightforest/models/item')) + @(Get-DataIds (Join-Path $MainRes 'assets/twilightforest/models/item'))
$blockItems = @($ids['block'] | Where-Object { $itemModels -contains $_ -and -not $ids['item'].Contains($_) })
Add-Ids block_item $blockItems
$entityNames = @{}
foreach ($m in [regex]::Matches((Read-Text (Join-Path $JavaDir 'entity/TFEntityNames.java')), 'ResourceLocation (\w+) = prefix\("([a-z0-9_]+)"\)')) {
    $entityNames[$m.Groups[1].Value] = $m.Groups[2].Value
}
$entityText = Strip-Comments (Read-Text (Join-Path $JavaDir 'init/TFEntities.java'))
$entities = [Collections.Generic.List[string]]::new()
$eggs = [Collections.Generic.List[string]]::new()
foreach ($m in [regex]::Matches($entityText, 'DeferredHolder<EntityType<\?>, EntityType<\w+>> \w+ = (\w+)\(TFEntityNames\.(\w+)([^;]*);')) {
    $name = $entityNames[$m.Groups[2].Value]
    $entities.Add($name)
    if ($m.Groups[3].Value -match '0x[0-9a-fA-F]+') { $eggs.Add("${name}_spawn_egg") }
}
Add-Ids entity $entities
Add-Ids spawn_egg $eggs

# worldgen and data
Add-Ids biome (Get-JavaIds 'init/TFBiomes.java')
Add-Ids structure (Get-JavaIds 'init/TFStructures.java')
Add-Ids structure_set (Get-DataIds (Join-Path $GenRes 'data/twilightforest/worldgen/structure_set'))
Add-Ids configured_feature (Get-DataIds (Join-Path $GenRes 'data/twilightforest/worldgen/configured_feature'))
Add-Ids placed_feature (Get-DataIds (Join-Path $GenRes 'data/twilightforest/worldgen/placed_feature'))
Add-Ids configured_carver (Get-DataIds (Join-Path $GenRes 'data/twilightforest/worldgen/configured_carver'))
Add-Ids density_function (Get-DataIds (Join-Path $GenRes 'data/twilightforest/worldgen/density_function'))
Add-Ids noise_settings (Get-DataIds (Join-Path $GenRes 'data/twilightforest/worldgen/noise_settings'))
Add-Ids dimension (Get-DataIds (Join-Path $GenRes 'data/twilightforest/dimension'))
Add-Ids dimension_type (Get-DataIds (Join-Path $GenRes 'data/twilightforest/dimension_type'))
Add-Ids loot_table (Get-DataIds (Join-Path $GenRes 'data/twilightforest/loot_table'))
Add-Ids damage_type (Get-DataIds (Join-Path $GenRes 'data/twilightforest/damage_type'))
Add-Ids enchantment (Get-DataIds (Join-Path $GenRes 'data/twilightforest/enchantment'))
Add-Ids banner_pattern (Get-DataIds (Join-Path $GenRes 'data/twilightforest/banner_pattern'))
Add-Ids jukebox_song (Get-DataIds (Join-Path $GenRes 'data/twilightforest/jukebox_song'))
Add-Ids trim_material (Get-DataIds (Join-Path $GenRes 'data/twilightforest/trim_material'))
Add-Ids function (Get-DataIds (Join-Path $MainRes 'data/twilightforest/function') '*.mcfunction') manual
Add-Ids particle (Get-JavaIds 'init/TFParticleType.java')
$soundText = Strip-Comments (Read-Text (Join-Path $JavaDir 'init/TFSounds.java')) # GHAST_TRAP_ACTIVE is commented out: 328 events
$sounds = @([regex]::Matches($soundText, 'createEvent\("([^"]+)"\)') | ForEach-Object { $_.Groups[1].Value })
$soundsJson = Get-Content (Join-Path $GenRes 'assets/twilightforest/sounds.json') -Raw | ConvertFrom-Json -AsHashtable
Add-Ids sound_event (@($sounds) + @($soundsJson.Keys) | Sort-Object -Unique)

# recipes and advancements
$recipeFiles = @{}
foreach ($ns in 'twilightforest', 'minecraft') {
    $dir = Join-Path $GenRes "data/$ns/recipe"
    foreach ($r in Get-DataIds $dir) { $recipeFiles["${ns}:$r"] = Join-Path $dir "$r.json" }
    $mdir = Join-Path $MainRes "data/$ns/recipe"
    foreach ($r in Get-DataIds $mdir) { $recipeFiles["${ns}:$r"] = Join-Path $mdir "$r.json" }
}
Add-Ids recipe @($recipeFiles.Keys | Sort-Object)
$advancements = @(Get-DataIds (Join-Path $GenRes 'data/twilightforest/advancement') | ForEach-Object { "twilightforest:$_" }) +
    @(Get-DataIds (Join-Path $GenRes 'data/minecraft/advancement') | ForEach-Object { "minecraft:$_" })
Add-Ids advancement $advancements

# code registries (init/*.java)
$codeKinds = [ordered]@{
    effect = 'TFMobEffects'; attribute = 'TFAttributes'; stat = 'TFStats'; keybind = 'TFKeyBinds'; gamerule = 'TFGameRules'
    map_decoration = 'TFMapDecorations'; armor_material = 'TFArmorMaterials'; trigger = 'TFAdvancements'; item_sub_predicate = 'TFItemSubPredicates'
    recipe_serializer = 'TFRecipes'; loot_modifier = 'TFLootModifiers'; feature_type = 'TFFeatures'; placement_modifier = 'TFFeatureModifiers'
    structure_type = 'TFStructureTypes'; structure_processor = 'TFStructureProcessors'; structure_placement_type = 'TFStructurePlacementTypes'
    block_entity = 'TFBlockEntities'; menu = 'TFMenuTypes'; data_component = 'TFDataComponents'; attachment = 'TFDataAttachments'
    poi = 'TFPOITypes'; creative_tab = 'TFCreativeTabs'; density_function_type = 'TFDensityFunctions'; carver = 'TFCaveCarvers'
    enchantment_effect = 'TFEnchantmentEffects'; data_serializer = 'TFDataSerializers'
}
foreach ($k in $codeKinds.Keys) { Add-Ids $k (Get-JavaIds "init/$($codeKinds[$k]).java") manual }
$lootText = Strip-Comments (Read-Text (Join-Path $JavaDir 'init/TFLoot.java'))
Add-Ids loot_condition @([regex]::Matches($lootText, 'CONDITIONS\.register\("([a-z0-9_]+)"') | ForEach-Object { $_.Groups[1].Value }) manual
Add-Ids loot_function @([regex]::Matches($lootText, 'FUNCTIONS\.register\("([a-z0-9_]+)"') | ForEach-Object { $_.Groups[1].Value }) manual
Add-Ids loot_number_provider @([regex]::Matches($lootText, 'NUMBERS\.register\("([a-z0-9_]+)"') | ForEach-Object { $_.Groups[1].Value }) manual
$pieceText = Strip-Comments (Read-Text (Join-Path $JavaDir 'init/TFStructurePieceTypes.java'))
$pieceClass = @{}
foreach ($m in [regex]::Matches($pieceText, 'registerPieceType\("([A-Za-z0-9_]+)",\s*(\w+)::new')) { $pieceClass[$m.Groups[1].Value] = $m.Groups[2].Value }
Add-Ids structure_piece_type @($pieceClass.Keys | Sort-Object) manual

# data maps, tags, twilight/* registries
$dataMaps = @()
foreach ($ns in 'twilightforest', 'neoforge') {
    $dataMaps += @(Get-DataIds (Join-Path $GenRes "data/$ns/data_maps") | ForEach-Object { "${ns}:" + ($_ -replace '^.*/', '') })
}
Add-Ids data_map ($dataMaps | Sort-Object -Unique)
$tags = [Collections.Generic.List[string]]::new()
foreach ($nsDir in Get-ChildItem (Join-Path $GenRes 'data') -Directory) {
    $tdir = Join-Path $nsDir.FullName 'tags'
    if (-not (Test-Path $tdir)) { continue }
    foreach ($regDir in Get-ChildItem $tdir -Directory) {
        $regs = if ($regDir.Name -eq 'worldgen') { Get-ChildItem $regDir.FullName -Directory | ForEach-Object { @{ name = "worldgen/$($_.Name)"; dir = $_.FullName } } } else { @(@{ name = $regDir.Name; dir = $regDir.FullName }) }
        foreach ($reg in $regs) {
            foreach ($t in Get-DataIds $reg.dir) { $tags.Add("$($reg.name):$($nsDir.Name):$t") }
        }
    }
}
Add-Ids tag $tags
$registries = [Collections.Generic.List[string]]::new()
$registryEntries = [Collections.Generic.List[string]]::new()
foreach ($ns in 'twilightforest', 'minecraft') {
    $tw = Join-Path $GenRes "data/$ns/twilight"
    if (-not (Test-Path $tw)) { continue }
    foreach ($reg in Get-ChildItem $tw -Directory) {
        $registries.Add($reg.Name)
        foreach ($e in Get-DataIds $reg.FullName) { $registryEntries.Add("$($reg.Name):${ns}:$e") }
    }
}
foreach ($f in Get-ChildItem (Join-Path $JavaDir 'init/custom') -Filter '*.java') {
    $t = Read-Text $f.FullName
    foreach ($m in [regex]::Matches($t, 'ResourceKey\.createRegistryKey\([^"]*"([a-z0-9_/]+)"\)|registryKey\("([a-z0-9_/]+)"\)')) {
        $n = if ($m.Groups[1].Success) { $m.Groups[1].Value } else { $m.Groups[2].Value }
        $registries.Add(($n -replace '^twilight/', ''))
    }
}
Add-Ids registry ($registries | Sort-Object -Unique)
Add-Ids registry_entry $registryEntries

# config keys (TFCommonConfig / TFClientConfig): "<common|client>.<Section>.<key>" + translation ids
$configKeys = [ordered]@{}
$configTranslations = @{}
foreach ($pair in @(@('common', 'config/TFCommonConfig.java'), @('client', 'config/TFClientConfig.java'))) {
    $text = Strip-Comments (Read-Text (Join-Path $JavaDir $pair[1]))
    $stack = [Collections.Generic.List[string]]::new()
    $pending = $null
    foreach ($m in [regex]::Matches($text, 'translation\(TFConfig\.CONFIG_ID \+ "([^"]+)"\)|push\("([^"]+)"\)|pop\(\)|\.define\w*\("([^"]+)"')) {
        if ($m.Groups[1].Success) { $pending = $m.Groups[1].Value }
        elseif ($m.Groups[2].Success) {
            $stack.Add($m.Groups[2].Value)
            if ($pending) { $configTranslations[$pending] = "$($pair[0]).$($m.Groups[2].Value)"; $pending = $null }
        } elseif ($m.Value -eq 'pop()') { if ($stack.Count) { $stack.RemoveAt($stack.Count - 1) } }
        else {
            $key = (@($pair[0]) + @($stack) + @($m.Groups[3].Value)) -join '.'
            $configKeys[$key] = $true
            if ($pending) { $configTranslations[$pending] = $key; $pending = $null }
        }
    }
}
Add-Ids config_key @($configKeys.Keys) manual

# en_us lang keys
$lang = Get-Content (Join-Path $GenRes 'assets/twilightforest/lang/en_us.json') -Raw | ConvertFrom-Json -AsHashtable
Add-Ids lang @($lang.Keys | Sort-Object)

# ---- ownership --------------------------------------------------------------------------------------

$IdRules = $rules.ids
foreach ($k in 'block', 'item', 'entity', 'biome', 'structure', 'particle', 'sound_event', 'damage_type', 'effect', 'enchantment',
    'enchantment_effect', 'attribute', 'stat', 'gamerule', 'keybind', 'banner_pattern', 'jukebox_song', 'trim_material', 'armor_material',
    'trigger', 'item_sub_predicate', 'recipe_serializer', 'loot_condition', 'loot_function', 'loot_number_provider', 'loot_modifier',
    'feature_type', 'placement_modifier', 'structure_processor', 'structure_placement_type', 'block_entity', 'menu', 'poi', 'creative_tab',
    'data_serializer', 'data_component', 'attachment', 'density_function_type', 'carver', 'configured_carver', 'density_function',
    'noise_settings', 'dimension', 'dimension_type', 'function') {
    if ($ids.Contains($k)) { Resolve-Rules $k $IdRules[$k] }
}
# map decorations, structure sets and structure types follow their structure
$byStructure = { param($ref, $id) Owner-Of 'structure' $id }
Resolve-Rules 'structure_set' @(, @('*', '$structure')) $byStructure
Resolve-Rules 'structure_type' $IdRules['structure_type'] $byStructure
Resolve-Rules 'map_decoration' $IdRules['map_decoration'] $byStructure
foreach ($id in $ids['block_item']) { Set-Owner block_item $id (Owner-Of block $id) }
foreach ($id in $ids['spawn_egg']) { Set-Owner spawn_egg $id (Owner-Of entity ($id -replace '_spawn_egg$', '')) }
Resolve-Rules 'data_map' ($IdRules['data_map'] | ForEach-Object { , @(("*:" + $_[0]), $_[1]) })
foreach ($k in 'configured_feature', 'placed_feature') { Resolve-Rules $k $IdRules['configured_feature'] }

# structure piece types: by the Java package of the piece class
$pieceOwners = @{
    hollowhill = 'U18'; hedgemaze = 'U18'; mushroomtower = 'U28'; icetower = 'U28'; lichtower = 'U21'; lichtowerrevamp = 'U21'; minotaurmaze = 'U23';
    stronghold = 'U25'; darktower = 'U26'; finalcastle = 'U30'; trollcave = 'U27'; courtyard = 'U20'; hydralair = 'U24'; yeticave = 'U27';
    fallentrunk = 'U18'; hollowtree = 'U18'; quest = 'U19'; questgrove = 'U19'; camp = 'U19'; gianthouse = 'U29'; util = 'F7'; templates = 'F7'; landmarks = 'F6'
}
$classPackage = @{}
foreach ($f in Get-ChildItem (Join-Path $JavaDir 'world/components/structures') -Recurse -Filter '*.java') {
    $classPackage[$f.BaseName] = $f.Directory.Name
}
foreach ($id in $ids['structure_piece_type']) {
    $cls = $pieceClass[$id]
    $pkg = $classPackage[$cls]
    $o = if ($pkg -and $pieceOwners.ContainsKey($pkg)) { $pieceOwners[$pkg] } elseif ($pkg) { $null } else { $null }
    if (-not $o) {
        if ($cls -match 'Hedge') { $o = 'U18' } elseif ($cls -match 'Hill') { $o = 'U18' } elseif ($cls -match 'Hydra') { $o = 'U24' } elseif ($cls -match 'Yeti') { $o = 'U27' }
        elseif ($cls -match 'Quest') { $o = 'U19' } elseif ($cls -match 'Trunk|HollowTree') { $o = 'U18' } elseif ($cls -match 'Camp') { $o = 'U19' }
        elseif ($cls -match 'Template|Jigsaw|Utility|Sortable') { $o = 'F7' }
    }
    if ($o) { Set-Owner structure_piece_type $id $o } else { $unclaimed.Add("structure_piece_type`t$id ($cls in $pkg)") }
}

# loot tables: block / entity tables follow their block / entity
foreach ($id in $ids['loot_table']) {
    $o = $null
    if ($id -match '^blocks/(.+)$') { $o = Owner-Of block $Matches[1] }
    elseif ($id -match '^entities/([a-z0-9_]+)') { $o = Owner-Of entity $Matches[1] }
    if (-not $o) {
        foreach ($r in $rules.lootRules) { if ((Get-GlobRegex $r[0]).IsMatch($id)) { $o = $r[1]; break } }
    }
    if ($o) { Set-Owner loot_table $id $o } else { $unclaimed.Add("loot_table`t$id") }
}

# recipes: custom types follow the serializer (R4), vanilla types the output item
function Get-ItemOwner([string]$full) {
    if ($full -notmatch '^twilightforest:(.+)$') { return $null }
    $p = $Matches[1]
    foreach ($k in 'item', 'block_item', 'block', 'spawn_egg') { $o = Owner-Of $k $p; if ($o) { return $o } }
    return $null
}
foreach ($id in $ids['recipe']) {
    $doc = Get-Content $recipeFiles[$id] -Raw | ConvertFrom-Json -AsHashtable
    $type = [string]$doc['type']
    $o = $null
    foreach ($r in $rules.recipeRules) { if ((Get-GlobRegex $r[0]).IsMatch($id)) { $o = $r[1]; break } }
    if (-not $o -and $type -match '^twilightforest:(.+)$') { $o = Owner-Of recipe_serializer $Matches[1]; if (-not $o) { $o = Owner-Of recipe_serializer ($Matches[1] + '_recipe') } }
    if (-not $o) {
        $res = $doc['result']
        $resultId = if ($res -is [string]) { $res } elseif ($res -is [hashtable]) { if ($res['id']) { $res['id'] } else { $res['item'] } } else { $null }
        if ($resultId) { $o = Get-ItemOwner ([string]$resultId) }
    }
    if (-not $o) {
        foreach ($m in [regex]::Matches((Get-Content $recipeFiles[$id] -Raw), '"(twilightforest:[a-z0-9_/]+)"')) { $o = Get-ItemOwner $m.Groups[1].Value; if ($o) { break } }
    }
    if ($o) { Set-Owner recipe $id $o } else { $unclaimed.Add("recipe`t$id") }
}
# advancements: recipe unlocks follow their recipe (generated by F1, R21), the rest belong to U14
foreach ($id in $ids['advancement']) {
    $o = $null
    if ($id -match '^(\w+):recipes/(?:[^/]+/)?(.+)$') {
        $o = Owner-Of recipe "$($Matches[1]):$($Matches[2])"
        if (-not $o) { $o = Owner-Of recipe "twilightforest:$($Matches[2])" }
        if (-not $o) { $o = 'F1'; $notes.Add("advancement $id unlocks an unknown recipe; owned by F1 (import)") }
    } else { $o = 'U14' }
    Set-Owner advancement $id $o
}

# tags: TF namespace by the tag rules, other namespaces are imported member lists (F1, R6)
foreach ($id in $ids['tag']) {
    $parts = $id.Split(':', 3)  # registry, namespace, path
    $o = $null
    if ($parts[1] -ne 'twilightforest') { $o = 'F1' }
    else {
        $probe = "$($parts[0]):$($parts[2])"
        foreach ($r in $rules.tagRules) { if ((Get-GlobRegex $r[0]).IsMatch($probe)) { $o = $r[1]; break } }
    }
    if ($o) { Set-Owner tag $id $o } else { $unclaimed.Add("tag`t$id") }
}
# twilight/* registries and their entries
Resolve-Rules 'registry' $rules.registryRules
foreach ($id in $ids['registry_entry']) {
    $parts = $id.Split(':', 3)  # registry, namespace, path
    $probe = "$($parts[0]):$($parts[2])"
    $o = $null
    foreach ($r in $rules.registryEntryRules) {
        if ((Get-GlobRegex $r[0]).IsMatch($probe)) { $o = $r[1]; if ($o -eq '$registry') { $o = Owner-Of registry $parts[0] }; break }
    }
    if ($o) { Set-Owner registry_entry $id $o } else { $unclaimed.Add("registry_entry`t$id") }
}
# config keys (docs/spec/config.md)
foreach ($id in $ids['config_key']) {
    $o = $null
    foreach ($r in $rules.configRules) { if ((Get-GlobRegex $r[0]).IsMatch($id)) { $o = $r[1]; break } }
    if ($o) { Set-Owner config_key $id $o } else { $unclaimed.Add("config_key`t$id") }
}

# en_us keys
$subtitleOwner = @{}
foreach ($e in $soundsJson.Keys) {
    $s = $soundsJson[$e]['subtitle']
    if ($s -and -not $subtitleOwner.ContainsKey($s)) { $subtitleOwner[$s] = Owner-Of sound_event $e }
}
function Resolve-LangRef([string]$ref, [string]$capture, [string]$key) {
    switch ($ref) {
        'block' { $o = Owner-Of block $capture; if (-not $o) { $o = Owner-Of block_item $capture }; return $o }
        'item' { foreach ($k in 'item', 'block_item', 'block', 'spawn_egg') { $o = Owner-Of $k $capture; if ($o) { return $o } }; return $null }
        'entity' { return Owner-Of entity $capture }
        'sound_event_subtitle' { return $subtitleOwner[$key] }
        'config_key' {
            $base = $capture -replace '\.(tooltip|button|more_health|more_loot|more_loot_and_health|none)$', ''
            $ck = $configTranslations[$base]
            if (-not $ck) { return $null }
            if ($owner['config_key'].ContainsKey($ck)) { return $owner['config_key'][$ck] }
            # a section title: the owner of its keys (when they agree)
            $os = @($owner['config_key'].Keys | Where-Object { $_.StartsWith("$ck.") } | ForEach-Object { $owner['config_key'][$_] } | Sort-Object -Unique)
            if ($os.Count -eq 1) { return $os[0] }
            return 'F0'
        }
        'stat' { return Owner-Of stat $capture }
        default {
            if ($rules.langGroups.ContainsKey($ref)) {
                foreach ($r in $rules.langGroups[$ref]) { if ((Get-GlobRegex $r[0]).IsMatch($capture)) { return $r[1] } }
            }
            return $null
        }
    }
}
$langRules = @($rules.lang | ForEach-Object { , @([regex]::new("^$($_[0])$"), $_[1]) })
foreach ($key in $ids['lang']) {
    $o = $null
    foreach ($r in $langRules) {
        $m = $r[0].Match($key)
        if (-not $m.Success) { continue }
        $target = $r[1]
        if ($target -match '^\$(\w+):(\d+)$') { $o = Resolve-LangRef $Matches[1] $m.Groups[[int]$Matches[2]].Value $key }
        else { $o = $target }
        break
    }
    if ($o) { Set-Owner lang $key $o } else { $unclaimed.Add("lang`t$key") }
}

# ---- unit-declared rows: paths, net, config sections, subcommands, selftests, world DB, scenarios ----
$pathRows = [Collections.Generic.List[object]]::new()
function Add-Path([string]$glob, [string]$o, [string]$mode = 'manual') { $pathRows.Add([pscustomobject]@{ kind = 'path'; id = $glob; owner = $o; mode = $mode }) }
foreach ($u in $rules.units.Keys) {
    $unit = $rules.units[$u]
    $n = $unit.name
    if ($unit.ContainsKey('paths')) {
        foreach ($p in $unit.paths) { Add-Path $p $u $(if ($p -like 'assets/gen/*') { 'gen' } else { 'manual' }) }
    }
    if ($unit.wave -eq 'W1') {
        $src = if ($unit.ContainsKey('srcPaths')) { $unit.srcPaths } else { @("src/feature/$n/**", "src/client/$n/**") }
        foreach ($p in @($src) + @("src/core/$n/**", "tests/$n/**", "assets/pack/{bp,rp}/**/$n/**", "**/*.d/$n.json", "assets/pack/rp/texts.d/*/$n.lang",
                "assets/data/*/$n.json", "assets/lang/*/$n.json", "assets/tags/**/*.d/$n.json", "tools/import/overrides/$n.json", "tools/sounds/recipes/$n.json",
                "tools/structures/$n/**", "tools/e2e/*/scenarios/$n{,.*}.ps1", "docs/units/$n.md", "src/feature/ext/$n/**")) { Add-Path $p $u }
        $structures = if ($unit.ContainsKey('structurePaths')) { $unit.structurePaths } else { @("assets/structures/$n/**") }
        foreach ($p in $structures) { Add-Path $p $u }
        foreach ($p in @("assets/gen/import/$n/**", "assets/gen/geo/$n/**", "assets/gen/audio/$n/**")) { Add-Path $p $u 'gen' }
        if ($unit.ContainsKey('extraPaths')) { foreach ($p in $unit.extraPaths) { Add-Path $p $u } }
        Set-Owner e2e $n $u
    }
    # W0 units own their generated layers too (the tools write the ids a W0 unit owns there, R3)
    if ($unit.wave -in 'W0a', 'W0b') { foreach ($p in @("assets/gen/import/$n/**", "assets/gen/geo/$n/**", "assets/gen/audio/$n/**")) { Add-Path $p $u 'gen' } }
    if ($unit.ContainsKey('contractHeaders')) { foreach ($p in $unit.contractHeaders) { Add-Path $p $u } }
    if ($unit.ContainsKey('stubs')) { foreach ($p in $unit.stubs) { Set-Owner stub $p $u } }
    # dx range and selftest prefix (R12): U<n> 100n.., F<k> 5000+100k.., W2 9000+
    $lo = $null
    if ($unit.ContainsKey('dx')) { $lo = $unit.dx[0]; $hi = $unit.dx[1] }
    elseif ($u -match '^U(\d+)$') { $lo = 100 * [int]$Matches[1]; $hi = $lo + 99 }
    if ($null -ne $lo) { Set-Owner selftest_dx "$lo-$hi" $u }
    Set-Owner selftest "$n.*" $u
    # net channels "<u>.<name>" (R15) and world DB keys "gf:<u>:" (R16)
    Set-Owner net "$n.*" $u
    $prefixes = if ($unit.ContainsKey('worlddb')) { $unit.worlddb } else { @("gf:${n}:") }
    foreach ($w in $prefixes) { Set-Owner worlddb "$w*" $u }
    if ($unit.ContainsKey('net')) { foreach ($c in $unit.net) { Set-Owner net $c $u } }
    if ($unit.ContainsKey('config')) { foreach ($c in $unit.config) { Set-Owner config_section $c $u } }
    if ($unit.ContainsKey('subcommands')) { foreach ($c in $unit.subcommands) { Set-Owner subcommand $c $u } }
    if ($unit.ContainsKey('e2e')) { foreach ($c in $unit.e2e) { Set-Owner e2e $c $u } }
    # ids outside TF's namespace that a unit declares itself: {kind: [entry]}, an entry being the full
    # id ("<ns>:<path>", mode gen) or {id, mode: gen|manual}. TF's own ids go through the rule tables.
    if ($unit.ContainsKey('ids')) {
        foreach ($k in $unit.ids.Keys) {
            foreach ($e in $unit.ids[$k]) {
                $i = if ($e -is [Collections.IDictionary]) { [string]$e['id'] } else { [string]$e }
                $m = if ($e -is [Collections.IDictionary] -and $e.Contains('mode')) { [string]$e['mode'] } else { 'gen' }
                $problem = if ($i -notmatch '^[a-z0-9_.-]+:[a-z0-9_./-]+$') { "is not a full id <namespace>:<path>" }
                    elseif ($i.StartsWith('twilightforest:')) { "is TF's: the rule tables own TF ids" }
                    elseif ($m -notin 'gen', 'manual') { "has mode '$m' (gen or manual)" }
                    elseif ($other = @($owner.Keys | Where-Object { $owner[$_].ContainsKey($i) -and $owner[$_][$i] -ne $u } | Select-Object -First 1)) { "is owned by $($owner[$other[0]][$i]) already ($($other[0]))" }
                if ($problem) { Write-Host "[owners] $u ids.${k}: '$i' $problem" -ForegroundColor Red; exit 1 }
                Set-Owner $k $i $u
                if (-not $idModes.ContainsKey($k)) { $idModes[$k] = @{} }
                $idModes[$k][$i] = $m
                $declared.Add(@($k, $i, $u))
            }
        }
    }
}
# F-units with several directories own the channel / key prefixes of each (F10: combat, loot, fx, ...)
foreach ($c in $rules.netChannels) { Set-Owner net $c[0] $c[1] }
foreach ($k in 'config_section', 'subcommand', 'net', 'selftest', 'selftest_dx', 'worlddb', 'e2e', 'stub') { if ($ids.Contains($k)) { $modes[$k] = 'manual' } }
# a declared id keeps its unit: no later rule may take it over
foreach ($d in $declared) {
    if ($owner[$d[0]][$d[1]] -ne $d[2]) { Write-Host "[owners] $($d[2]) ids.$($d[0]): '$($d[1])' was taken over by $($owner[$d[0]][$d[1]])" -ForegroundColor Red; exit 1 }
}

# ---- output -------------------------------------------------------------------------------------------
$nsKinds = @('block', 'block_item', 'item', 'spawn_egg', 'entity', 'biome', 'structure', 'structure_set', 'structure_type', 'map_decoration', 'particle',
    'damage_type', 'effect', 'enchantment', 'enchantment_effect', 'attribute', 'stat', 'banner_pattern', 'jukebox_song', 'trim_material', 'armor_material',
    'trigger', 'item_sub_predicate', 'recipe_serializer', 'loot_condition', 'loot_function', 'loot_number_provider', 'loot_modifier', 'feature_type',
    'placement_modifier', 'structure_processor', 'structure_placement_type', 'structure_piece_type', 'block_entity', 'menu', 'poi', 'creative_tab',
    'data_serializer', 'data_component', 'attachment', 'density_function_type', 'carver', 'configured_carver', 'density_function', 'noise_settings',
    'dimension', 'dimension_type', 'function', 'configured_feature', 'placed_feature', 'loot_table', 'keybind')
$lines = [Collections.Generic.List[string]]::new()
$counts = [ordered]@{}
foreach ($kind in $ids.Keys) {
    $mode = if ($modes.ContainsKey($kind)) { $modes[$kind] } else { 'gen' }
    foreach ($id in $ids[$kind]) {
        if (-not $owner[$kind].ContainsKey($id)) { continue }
        $full = if ($nsKinds -contains $kind) { if ($id.Contains(':')) { $id } else { "twilightforest:$id" } } elseif ($kind -eq 'tag') { $p = $id.Split(':', 3); "$($p[0])/#$($p[1]):$($p[2])" } elseif ($kind -eq 'registry_entry') { $p = $id.Split(':', 3); "$($p[0])/$($p[1]):$($p[2])" } else { $id }
        $m = if ($idModes.ContainsKey($kind) -and $idModes[$kind].ContainsKey($id)) { $idModes[$kind][$id] } else { $mode }
        $lines.Add("$kind`t$full`t$($owner[$kind][$id])`t$m")
        $counts[$kind] = 1 + $(if ($counts.Contains($kind)) { $counts[$kind] } else { 0 })
    }
}
foreach ($p in $pathRows) {
    $lines.Add("path`t$($p.id)`t$($p.owner)`t$($p.mode)")
    $counts['path'] = 1 + $(if ($counts.Contains('path')) { $counts['path'] } else { 0 })
}
$sorted = @($lines | Sort-Object -CaseSensitive -Unique)
$header = @(
    '# docs/spec/owners.tsv: generated by tools/spec/owners.ps1 from TF 4.8 (9e40a75721c716b2fdf80c16d1058fe718825e9f) and tools/spec/owners.rules.json. Do not edit.',
    "kind`tid`towner`tmode"
)
$text = (($header + $sorted) -join "`n") + "`n"
$unclaimedText = (@($unclaimed | Sort-Object -Unique) -join "`n") + $(if ($unclaimed.Count) { "`n" } else { '' })

# one owner per id: the rule tables assign each id once and a unit's declared ids (units.<u>.ids)
# are rejected above when another owner has them; ambiguous paths are checked by assemble
Write-Host ("[owners] {0} rows: {1}" -f $sorted.Count, (($counts.Keys | ForEach-Object { "$_=$($counts[$_])" }) -join ', '))
foreach ($n in $notes) { Write-Host "[owners] note: $n" }
if ($unclaimed.Count) {
    Write-Host "[owners] $($unclaimed.Count) id(s) no unit record claims:" -ForegroundColor Yellow
    $unclaimed | Select-Object -First 400 | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
}
if ($Check) {
    $old = if (Test-Path $Out) { [IO.File]::ReadAllText($Out) } else { '' }
    if ($old -ne $text) { Write-Host "[owners] $Out is stale: run pwsh tools/spec/owners.ps1" -ForegroundColor Red; exit 1 }
    Write-Host "[owners] $Out is up to date" -ForegroundColor Green
} else {
    New-Item -ItemType Directory -Force (Split-Path $Out) | Out-Null
    [IO.File]::WriteAllText($Out, $text)
    [IO.File]::WriteAllText($unclaimedFile, $unclaimedText)
    Write-Host "[owners] wrote $Out"
}
if ($Strict -and $unclaimed.Count) { exit 1 }
exit 0
