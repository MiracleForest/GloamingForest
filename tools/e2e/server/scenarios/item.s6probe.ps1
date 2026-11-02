# F9 (item), spike S6: the throw-away probe content of the item scenarios' prelaunch steps (server
# and the client's local mode; dot-sourced, not a scenario of its own). Nothing of it ships:
# - <mod>/s6-trim-probe.flag: the run registers the fixture trim material iron_darker at load
#   (src/feature/item/SpikeTests.cpp), as U7 registers TF's;
# - behavior_packs/GloamingForest_S6_Probe_BP: a data-driven disc gf_s6:disc whose minecraft:record
#   names a custom sound event (gf_s6.disc), as TF's discs (U10) will: item.spike_record_sounds
#   reads its record component and plays it in a jukebox.
function Write-S6Probe([string]$ModDir, [string]$Reason) {
    if (-not (Test-Path $ModDir)) { throw "the deployed mod is not at $ModDir" }
    Set-Content -Path (Join-Path $ModDir 's6-trim-probe.flag') -Value $Reason -Encoding UTF8
    $pack = Join-Path $ModDir 'behavior_packs\GloamingForest_S6_Probe_BP'
    New-Item -ItemType Directory -Force (Join-Path $pack 'items') | Out-Null
    @'
{
  "format_version": 2,
  "header": {
    "name": "GloamingForest S6 probe",
    "description": "Spike S6 probe items (e2e only)",
    "uuid": "8e2b6f4a-1c3d-4b5e-9f60-7a1d2c3b4e01",
    "version": [0, 0, 1],
    "min_engine_version": [1, 21, 40]
  },
  "modules": [ { "type": "data", "uuid": "8e2b6f4a-1c3d-4b5e-9f60-7a1d2c3b4e02", "version": [0, 0, 1] } ]
}
'@ | Set-Content -Encoding UTF8 (Join-Path $pack 'manifest.json')
    @'
{
  "format_version": "1.21.60",
  "minecraft:item": {
    "description": {
      "identifier": "gf_s6:disc",
      "menu_category": { "category": "none" }
    },
    "components": {
      "minecraft:icon": "gf_test_fixture_item",
      "minecraft:max_stack_size": 1,
      "minecraft:record": { "comparator_signal": 7, "duration": 5, "sound_event": "gf_s6.disc" }
    }
  }
}
'@ | Set-Content -Encoding UTF8 (Join-Path $pack 'items\disc.json')
    Write-Step "S6 probe (trim flag, probe BP with gf_s6:disc) written to $ModDir"
}
