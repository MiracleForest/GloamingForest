# F10 (combat), spike S3: the client mod gets the throw-away probe pack of combat.s3 (a block with a
# data-driven block actor, gf_s3:entity) and the flag that makes the mod declare it a 27-slot
# container, so combat.ui_native_container can open the injected game container for the real
# player. Nothing ships; only the local world has it (a BDS host of the remote modes has no probe).
if ($script:Mode -eq 'local') {
    $modDir = Join-Path $script:ModsDir $script:ModName
    $s3Pack = Join-Path $modDir 'behavior_packs\GloamingForest_S3_BP'
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
    @'
{
  "format_version": "1.26.50",
  "minecraft:block": {
    "description": { "identifier": "gf_s3:entity", "menu_category": { "category": "none" } },
    "components": {
      "minecraft:destructible_by_mining": { "seconds_to_destroy": 0.5 },
      "minecraft:geometry": "minecraft:geometry.full_block",
      "minecraft:material_instances": { "*": { "texture": "gf_test_fixture_block", "render_method": "opaque" } },
      "minecraft:block_entity": {}
    }
  }
}
'@ | Set-Content -Encoding UTF8 (Join-Path $s3Pack 'blocks\entity.json')
    Set-Content -Path (Join-Path $modDir 's3-probe.flag') -Value 'combat.ui_native_container' -Encoding UTF8
    Write-Step "S3 probe pack written to $s3Pack"
}
