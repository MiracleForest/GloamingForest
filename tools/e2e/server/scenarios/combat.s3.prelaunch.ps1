# Spike S3 (F10): before the server boots, add a throw-away behavior pack with probe blocks that use
# the data-driven block entity / container components ("minecraft:block_entity",
# "minecraft:container"), so combat.s3_* can check what the game builds for them. The pack lives only
# in this run's slot; nothing ships. $env:GF_S3_EXPERIMENTS (e.g. '5,6') makes the probe module
# force those AllExperiments flags into the level data before the packs load.
$s3Pack = Join-Path $script:SlotDir "plugins\$script:ModName\behavior_packs\GloamingForest_S3_BP"
New-Item -ItemType Directory -Force (Join-Path $s3Pack 'blocks') | Out-Null
@'
{
  "format_version": 2,
  "header": {
    "name": "GloamingForest S3 probe",
    "description": "Spike S3 probe blocks (e2e only)",
    "uuid": "0b5c3f0e-6a51-4c39-9a0f-53e3f1d0c301",
    "version": [0, 0, 1],
    "min_engine_version": [1, 21, 40]
  },
  "modules": [ { "type": "data", "uuid": "0b5c3f0e-6a51-4c39-9a0f-53e3f1d0c302", "version": [0, 0, 1] } ]
}
'@ | Set-Content -Encoding UTF8 (Join-Path $s3Pack 'manifest.json')

$fv = if ($env:GF_S3_FORMAT) { $env:GF_S3_FORMAT } else { "1.26.50" }
$env:GF_S3_PROBE = "1"   # the probe module declares gf_s3:entity as a 27-slot container
function Write-ProbeBlock([string]$Name, [string]$Components) {
    @"
{
  "format_version": "$fv",
  "minecraft:block": {
    "description": { "identifier": "gf_s3:$Name", "menu_category": { "category": "none" } },
    "components": {
      "minecraft:destructible_by_mining": { "seconds_to_destroy": 0.5 },
      "minecraft:geometry": "minecraft:geometry.full_block",
      "minecraft:material_instances": { "*": { "texture": "gf_test_fixture_block", "render_method": "opaque" } }$Components
    }
  }
}
"@ | Set-Content -Encoding UTF8 (Join-Path $s3Pack "blocks\$Name.json")
}
# $env:GF_S3_ALL also writes the minecraft:container probes (rejected by the 1.26.51 schema: the
# run then fails on their content log errors, which are the evidence)
if ($env:GF_S3_ALL) {
    Write-ProbeBlock 'container' ",`n      `"minecraft:container`": { `"slot_count`": 27 }"
    Write-ProbeBlock 'entity_container' ",`n      `"minecraft:block_entity`": {},`n      `"minecraft:container`": { `"slot_count`": 27 }"
}
Write-ProbeBlock 'entity' ",`n      `"minecraft:block_entity`": {}"
Write-Step "S3 probe pack written to $s3Pack (format $fv, experiments '$env:GF_S3_EXPERIMENTS')"
