# Biomes (U1): TF's biome source in the Twilight Forest. The selftests compare the layer stack with
# F13's oracle samples (three seeds: main and wide main-biome grids, noise biomes; the full grids and
# BiomeManager's zoom are the gtests of tests/biomes), check the key biome quadrants, companions,
# thornlands border, streams, vertical layers, the 17 valid_* tags and in_twilight_forest, the 22
# behavior pack biomes registered with the game, BiomeHelper's spawn tables against TF's biome
# JSON, the terrain blend extension, and a freshly generated Twilight Forest chunk, written on the
# generation worker threads, storing the source's zoomed biomes.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
Wait-LogLine 'biomes: Twilight Forest biome source installed \(2 layer stacks, 22 biomes\)' 30 | Out-Null

$r = Invoke-SelfTest 'biomes.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) biomes selftest(s) failed" }
if ($r.Pass -lt 12) { throw "expected at least 12 biomes selftests to pass, got $($r.Pass)" }

# no biome the generator could not resolve (F5 warns once per unknown biome id)
$unknown = @(Get-ServerLog | Select-String -Pattern 'unknown biome twilightforest:')
if ($unknown.Count -gt 0) { throw "the game does not know a Twilight Forest biome: $($unknown[0].Line)" }
Write-Step "biomes: $($r.Pass) selftests passed"
