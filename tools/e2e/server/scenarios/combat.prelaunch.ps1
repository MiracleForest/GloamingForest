# F10 (combat): before the server boots, add a throw-away behavior pack with a probe chest block
# that has the chest type / facing states of TF's chests (gf_combat:chest, "gf_combat:type"
# single / left / right, "gf_combat:facing"), so combat.inventory_chest_rules can pair two halves
# into a double chest. The pack lives only in this run's slot; nothing ships.
$pack = Join-Path $script:SlotDir "plugins\$script:ModName\behavior_packs\GloamingForest_Combat_Probe_BP"
New-Item -ItemType Directory -Force (Join-Path $pack 'blocks') | Out-Null
@'
{
  "format_version": 2,
  "header": {
    "name": "GloamingForest combat probe",
    "description": "F10 probe blocks (e2e only)",
    "uuid": "6d0e1f7a-3c2b-4f1e-9a51-2b8c4d0f1a01",
    "version": [0, 0, 1],
    "min_engine_version": [1, 21, 40]
  },
  "modules": [ { "type": "data", "uuid": "6d0e1f7a-3c2b-4f1e-9a51-2b8c4d0f1a02", "version": [0, 0, 1] } ]
}
'@ | Set-Content -Encoding UTF8 (Join-Path $pack 'manifest.json')
@'
{
  "format_version": "1.26.50",
  "minecraft:block": {
    "description": {
      "identifier": "gf_combat:chest",
      "menu_category": { "category": "none" },
      "states": {
        "gf_combat:type": ["single", "left", "right"],
        "gf_combat:facing": ["north", "south", "west", "east"]
      }
    },
    "components": {
      "minecraft:destructible_by_mining": { "seconds_to_destroy": 0.5 },
      "minecraft:geometry": "minecraft:geometry.full_block",
      "minecraft:material_instances": { "*": { "texture": "gf_test_fixture_block", "render_method": "opaque" } }
    }
  }
}
'@ | Set-Content -Encoding UTF8 (Join-Path $pack 'blocks\chest.json')
Write-Step "combat probe pack written to $pack"
