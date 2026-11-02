# Spike S7 follow-up (F1): before the server boots, add a throw-away behavior pack of probe blocks that declare
# block tags in each form, in each block format_version from the import's 1.21.80 to 1.26.50, so import.s7_probe
# can read what the game made of them (docs/spikes/S7.md). The pack lives only in this run's slot; nothing ships.
# Forms: "legacy" ("tag:<id>": {} components), "object" ("minecraft:tags": {"tags": [...]}, the item form), "array"
# ("minecraft:tags": [...]); "array_entity" / "legacy_entity" add minecraft:block_entity (F10's native container
# backend). A form a format rejects logs a content error, which fails the run: the run is the evidence, not a gate.
#
# The family blocks: F11's families kit rewrites them at assemble in its format 1.26.0 from the import's skeleton,
# which therefore keeps the pre-1.26.20 "tag:<id>" form for them. To show that the import's whole skeleton loads at
# 1.26.50, this run replaces the kit's file of every family block in the slot by the import's own skeleton converted
# to 1.26.50 as the import writes the other blocks ("tag:<id>" components -> the minecraft:tags array).
$mod = Join-Path $script:SlotDir "$(Get-ModsDirName $script:SlotDir)\$script:ModName"
$familyIds = @(Get-ChildItem (Join-Path $script:RepoRoot 'assets/gen/import') -Recurse -Filter '*.json' |
    Where-Object { $_.FullName -match '[\\/]data[\\/]gf[\\/]families[\\/]' } |
    ForEach-Object { (Get-Content $_.FullName -Raw | ConvertFrom-Json) } | ForEach-Object { $_.id })
$skeletons = @{}
Get-ChildItem (Join-Path $script:RepoRoot 'assets/gen/import') -Recurse -Filter '*.json' |
    Where-Object { $_.FullName -match '[\\/]bp[\\/]blocks[\\/]' } | ForEach-Object { $skeletons[$_.BaseName] = $_.FullName }
$converted = 0
foreach ($id in $familyIds) {
    $name = $id.Substring($id.IndexOf(':') + 1)
    $dest = Join-Path $mod "behavior_packs\GloamingForest_BP\blocks\$name.json"
    if (-not $skeletons.ContainsKey($name) -or -not (Test-Path $dest)) { continue }
    $doc = Get-Content $skeletons[$name] -Raw | ConvertFrom-Json -AsHashtable
    $doc['format_version'] = '1.26.50'
    $block = $doc['minecraft:block']
    $lists = @($block['components']) + @($block['permutations'] | Where-Object { $_ } | ForEach-Object { $_['components'] })
    foreach ($c in $lists) {
        if ($null -eq $c) { continue }
        $tags = @($c.Keys | Where-Object { $_ -like 'tag:*' })
        if ($tags.Count -eq 0) { continue }
        foreach ($t in $tags) { $c.Remove($t) }
        $c['minecraft:tags'] = @($tags | ForEach-Object { $_.Substring(4) })
    }
    $doc | ConvertTo-Json -Depth 64 | Set-Content -Encoding UTF8 $dest
    $converted++
}
Write-Step "S7: $converted of $($familyIds.Count) family block files replaced by the import's skeleton at 1.26.50"

$s7Pack = Join-Path $mod 'behavior_packs\GloamingForest_S7_BP'
New-Item -ItemType Directory -Force (Join-Path $s7Pack 'blocks') | Out-Null
@'
{
  "format_version": 2,
  "header": {
    "name": "GloamingForest S7 probe",
    "description": "Spike S7 block tag probes (e2e only)",
    "uuid": "6f0c2b8e-4d1a-4f57-9a3e-7c51d2e0a701",
    "version": [0, 0, 1],
    "min_engine_version": [1, 21, 80]
  },
  "modules": [ { "type": "data", "uuid": "6f0c2b8e-4d1a-4f57-9a3e-7c51d2e0a702", "version": [0, 0, 1] } ]
}
'@ | Set-Content -Encoding UTF8 (Join-Path $s7Pack 'manifest.json')

# keep in step with kS7Formats / kS7Forms in src/feature/ext/import/S7Probe.cpp
$formats = @('1.21.80', '1.26.0', '1.26.10', '1.26.20', '1.26.30', '1.26.40', '1.26.50')
$tags = '"minecraft:is_pickaxe_item_destructible", "gf_s7:probe"'
$forms = [ordered]@{
    legacy        = '"tag:minecraft:is_pickaxe_item_destructible": {}, "tag:gf_s7:probe": {}'
    object        = "`"minecraft:tags`": { `"tags`": [$tags] }"
    array         = "`"minecraft:tags`": [$tags]"
    array_entity  = "`"minecraft:tags`": [$tags], `"minecraft:block_entity`": {}"
    legacy_entity = "$('"tag:minecraft:is_pickaxe_item_destructible": {}, "tag:gf_s7:probe": {}'), `"minecraft:block_entity`": {}"
}
$written = 0
foreach ($fv in $formats) {
    foreach ($form in $forms.Keys) {
        $name = "${form}_$($fv -replace '\.', '_')"
        @"
{
  "format_version": "$fv",
  "minecraft:block": {
    "description": { "identifier": "gf_s7:$name", "menu_category": { "category": "none" } },
    "components": {
      "minecraft:destructible_by_mining": { "seconds_to_destroy": 1.5 },
      "minecraft:geometry": "minecraft:geometry.full_block",
      "minecraft:material_instances": { "*": { "texture": "gf_test_fixture_block", "render_method": "opaque" } },
      $($forms[$form])
    }
  }
}
"@ | Set-Content -Encoding UTF8 (Join-Path $s7Pack "blocks\$name.json")
        $written++
    }
}
Write-Step "S7 probe pack: $written blocks written to $s7Pack"
