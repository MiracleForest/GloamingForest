# Worldgen (F5, spike S2): the custom probe biome gf_test:worldgen_probe. Dot-sourced by the
# worldgen prelaunch scripts of both harnesses; never shipped (it only goes into the deployed copy
# of the gf_test fixture packs of this run). The behavior pack biome registers the custom id with
# the game; the resource pack part gives it a client look that a screenshot cannot miss (magenta
# fog and water, the hot / dry grass tint of its climate).

function Install-WorldgenProbe([string]$ModDir) {
    $bp = Join-Path $ModDir 'behavior_packs/GloamingForest_Test_BP'
    $rp = Join-Path $ModDir 'resource_packs/GloamingForest_Test_RP'
    if (-not (Test-Path $bp)) { throw "the test behavior pack is missing: $bp (a development build is needed)" }

    $biome = [ordered]@{
        format_version   = '1.21.110'
        'minecraft:biome' = [ordered]@{
            description = @{ identifier = 'gf_test:worldgen_probe' }
            components  = [ordered]@{
                'minecraft:climate'   = [ordered]@{ downfall = 0.0; snow_accumulation = @(0.0, 0.125); temperature = 2.0 }
                # the server's map colours of its grass and foliage (worldgen.custom_biome_map)
                'minecraft:map_tints' = [ordered]@{ foliage = '#FF00FF'; grass = '#FF00FF' }
            }
        }
    }
    New-Item -ItemType Directory -Force (Join-Path $bp 'biomes') | Out-Null
    $biome | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $bp 'biomes/worldgen_probe.biome.json')

    if (Test-Path $rp) {
        $client = @{ biomes = @{ 'gf_test:worldgen_probe' = [ordered]@{
                    water_surface_color        = '#FF00FF'
                    water_surface_transparency = 0.65
                    water_fog_color            = '#FF00FF'
                    water_fog_distance         = 15
                    fog_identifier             = 'gf_test:worldgen_probe_fog'
                } } }
        $client | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $rp 'biomes_client.json')
        $fog = [ordered]@{
            format_version          = '1.16.100'
            'minecraft:fog_settings' = [ordered]@{
                description = @{ identifier = 'gf_test:worldgen_probe_fog' }
                distance    = @{ air = [ordered]@{ fog_start = 0.0; fog_end = 0.5; fog_color = '#FF00FF'; render_distance_type = 'render' } }
            }
        }
        New-Item -ItemType Directory -Force (Join-Path $rp 'fogs') | Out-Null
        $fog | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $rp 'fogs/worldgen_probe_fog.json')
    }
    Write-Step "installed the probe biome gf_test:worldgen_probe into the test packs of $ModDir"
}
