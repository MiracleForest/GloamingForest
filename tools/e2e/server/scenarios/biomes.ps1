# Biomes (U1): upstream's biome source in the Gloaming Forest. The selftests compare the layer stack with
# F13's oracle samples (three seeds: main and wide main-biome grids, noise biomes; the full grids and
# BiomeManager's zoom are the gtests of tests/biomes), check the key biome quadrants, companions,
# thornlands border, streams, vertical layers, the 17 valid_* tags and in_gloaming_forest, the 22
# behavior pack biomes registered with the game, BiomeHelper's spawn tables against upstream's biome
# JSON, the terrain blend extension, a freshly generated Gloaming Forest chunk, written on the
# generation worker threads, storing the source's zoomed biomes, and the natural spawns of the
# vanilla mobs of upstream's tables in the Gloaming Forest.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
Wait-LogLine 'biomes: Gloaming Forest biome source installed \(2 layer stacks, 22 biomes\)' 30 | Out-Null

$r = Invoke-SelfTest 'biomes.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) biomes selftest(s) failed" }
if ($r.Pass -lt 15) { throw "expected at least 15 biomes selftests to pass, got $($r.Pass)" }
# the vanilla mobs of upstream's tables spawn naturally in the Gloaming Forest (biomes.vanilla_spawns)
Wait-LogLine 'biomes: the natural spawn of minecraft:\S+ in the Gloaming Forest goes through' 5 | Out-Null

# no biome the generator could not resolve (F5 warns once per unknown biome id)
$unknown = @(Get-ServerLog | Select-String -Pattern 'unknown biome gloamingforest:')
if ($unknown.Count -gt 0) { throw "the game does not know a Gloaming Forest biome: $($unknown[0].Line)" }
Write-Step "biomes: $($r.Pass) selftests passed"
