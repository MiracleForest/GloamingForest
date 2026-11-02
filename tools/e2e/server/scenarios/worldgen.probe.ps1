# Worldgen (F5, spike S2): the custom probe biome gf_test:worldgen_probe. Dot-sourced by the
# worldgen prelaunch scripts of both harnesses; never shipped (it only goes into the deployed copy
# of the gf_test fixture packs of this run). The behavior pack biome registers the custom id with
# the game; the resource pack part gives it a client look that a screenshot cannot miss (a magenta
# sky, grass, foliage, water and fog in the current client biome format, the legacy
# biomes_client.json for comparison).

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
                'minecraft:map_tints' = [ordered]@{ foliage = '#FF00FF'; grass = [ordered]@{ type = 'tint'; tint = '#FF00FF' } }
            }
        }
    }
    New-Item -ItemType Directory -Force (Join-Path $bp 'biomes') | Out-Null
    $biome | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $bp 'biomes/worldgen_probe.biome.json')

    if (Test-Path $rp) {
        # the current client biome format (the game's own resource pack ships its biomes as
        # biomes/<name>.client_biome.json, format 1.21.60 / 1.21.70): magenta sky, grass, foliage,
        # water and fog, so a screenshot shows which parts a resource pack can give a custom biome
        $clientBiome = [ordered]@{
            format_version          = '1.21.70'
            'minecraft:client_biome' = [ordered]@{
                description = @{ identifier = 'gf_test:worldgen_probe' }
                components  = [ordered]@{
                    'minecraft:sky_color'          = @{ sky_color = '#FF00FF' }
                    'minecraft:grass_appearance'   = @{ color = '#FF00FF' }
                    'minecraft:foliage_appearance' = @{ color = '#FF00FF' }
                    'minecraft:water_appearance'   = [ordered]@{ surface_color = '#FF00FF'; surface_opacity = 0.65 }
                    'minecraft:fog_appearance'     = @{ fog_identifier = 'gf_test:worldgen_probe_fog' }
                }
            }
        }
        New-Item -ItemType Directory -Force (Join-Path $rp 'biomes') | Out-Null
        $clientBiome | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $rp 'biomes/worldgen_probe.client_biome.json')
        # the legacy file too (it did not apply on 1.26.51; kept as the comparison)
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
