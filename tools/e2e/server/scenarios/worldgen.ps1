# Worldgen (F5): the Java-parity core (BiomeManager's zoom included), the feature interpreter (the
# gf_test fixture feature placed identically in memory and in the world, vanilla features, trees,
# scattered ores, count_on_every_layer over flowing fluids, the carving mask placement), LargeWriter,
# biome writes (the custom probe biome of worldgen.prelaunch.ps1, a height-varying layout zoomed
# per block into a live chunk, timed), the stage order (blankets at surface time, structure steps
# at every decoration step with Java's structure registry seeds, the plain Pieces stage once),
# carvers, the spawn engine (Java's default spawn checks), upstream's landmark rules (controlled spawns
# and no_structure over a start F6 forced) and the block / biome tags (gf::tags of F9), the dev functions run
# through /function, a server map over custom / vanilla biome columns, the 200-chunk generation
# timing of the whole pipeline and, when F4's dimension exists, the in-dimension generator run
# (selftests "worldgen.*"). The timing lines are the budget record of the run.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$mark = Get-LogMark
$r = Invoke-SelfTest 'worldgen.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) worldgen selftest(s) failed" }
if ($r.Pass -lt 26) { throw "expected 26 worldgen selftests to pass, got $($r.Pass)" }
$log = @(Get-ServerLog -Since $mark)

$timing = $log | Select-String -Pattern 'worldgen timing: 200 chunks in ([\d.]+) ms \(([\d.]+) ms/chunk' | Select-Object -Last 1
if (-not $timing) { throw 'no 200-chunk timing line logged' }
Write-Step "200-chunk in-memory pipeline: $($timing.Matches[0].Groups[1].Value) ms ($($timing.Matches[0].Groups[2].Value) ms/chunk)"
$varying = $log | Select-String -Pattern 'worldgen timing: 200 chunks with a height-varying biome source in ([\d.]+) ms \(([\d.]+) ms/chunk' | Select-Object -Last 1
if (-not $varying) { throw 'no height-varying biome timing line logged' }
Write-Step "200 chunks with height-varying biomes: $($varying.Matches[0].Groups[1].Value) ms ($($varying.Matches[0].Groups[2].Value) ms/chunk)"
$write = $log | Select-String -Pattern 'worldgen biomes: height-varying biomes written into a live chunk in ([\d.]+) ms \((.*)\)' | Select-Object -Last 1
if (-not $write) { throw 'no live-chunk biome write timing logged' }
Write-Step "height-varying biome write into a live chunk: $($write.Matches[0].Groups[1].Value) ms ($($write.Matches[0].Groups[2].Value))"
$fixture = $log | Select-String -Pattern 'fixture feature hash ([0-9a-f]{16})' | Select-Object -Last 1
if ($fixture) { Write-Step "fixture feature hash $($fixture.Matches[0].Groups[1].Value)" }
$blanket = $log | Select-String -Pattern 'worldgen blanket: (.*)' | Select-Object -Last 1
if (-not $blanket) { throw 'the blanket stage order was not checked' }
Write-Step "blanket stage: $($blanket.Matches[0].Groups[1].Value)"
$zoom = $log | Select-String -Pattern 'worldgen biome zoom: (.*)' | Select-Object -Last 1
if ($zoom) { Write-Step "biome zoom: $($zoom.Matches[0].Groups[1].Value)" }
$surface = $log | Select-String -Pattern 'worldgen spawn: (WORLD_SURFACE .*)' | Select-Object -Last 1
if ($surface) { Write-Step "spawn heights: $($surface.Matches[0].Groups[1].Value)" }
$tags = $log | Select-String -Pattern 'worldgen tags: (.*)' | Select-Object -Last 1
if (-not $tags) { throw 'the block and biome tags were not checked against gf::tags' }
Write-Step "tags: $($tags.Matches[0].Groups[1].Value)"
# worldgen.landmark_rules forces F6's starts (removed again when it ends) in every world
$rules = $log | Select-String -Pattern 'worldgen landmark rules: (forced starts checked .*)' | Select-Object -Last 1
if (-not $rules) { throw 'worldgen.landmark_rules did not check the forced starts' }
Write-Step "landmark rules: $($rules.Matches[0].Groups[1].Value)"

# The generator in the Gloaming Forest: required once the dimension exists (F4 installs it, and the
# generator hook logs that it took over the dimension's generator); otherwise a distinct skip line.
$gen = $log | Select-String -Pattern 'worldgen generator: (in-dimension run: .*|skipped, .*)' | Select-Object -Last 1
if (-not $gen) { throw 'no worldgen generator line (neither an in-dimension run nor the skip)' }
$dimension = [bool](Get-ServerLog | Select-String -Pattern 'Gloaming Forest generator installed' -Quiet)
if ($dimension -and $gen.Line -notmatch 'in-dimension run') { throw 'the Gloaming Forest generator is installed but worldgen.generator did not run in the dimension' }
Write-Step $gen.Matches[0].Value
# in the dimension the generator writes the biome source's biome (the probe, which the prelaunch
# installed) into the chunks it makes: the regression guard of spike S2's server half
$genBiome = $log | Select-String -Pattern 'worldgen generator: generated chunk biome (\S+)' | Select-Object -Last 1
if ($gen.Line -match 'in-dimension run') {
    if (-not $genBiome) { throw 'the in-dimension run logged no generated chunk biome (probe not installed?)' }
    if ($genBiome.Matches[0].Groups[1].Value -ne 'gf_test:worldgen_probe') { throw "a generated chunk holds $($genBiome.Matches[0].Groups[1].Value), not the probe gf_test:worldgen_probe" }
}
if ($genBiome) { Write-Step "biome of a chunk the generator made: $($genBiome.Matches[0].Groups[1].Value)" }

# Spike S2: the custom probe biome was registered from the test pack and written into a chunk, and
# a server map draws it with its own colours.
$probe = $log | Select-String -Pattern 'worldgen custom biome: (\S+) written and read back' | Select-Object -Last 1
if (-not $probe) { throw 'the custom probe biome was not written into a chunk (not registered by the game?)' }
Write-Step "custom biome in a chunk: $($probe.Matches[0].Groups[1].Value)"
$map = $log | Select-String -Pattern 'worldgen custom biome map: (.*)' | Select-Object -Last 1
if (-not $map) { throw 'no server map of the custom biome' }
Write-Step "server map: $($map.Matches[0].Groups[1].Value)"

# The dev functions were loaded by the world and ran (/function, including dev_new_world's scriptevent).
if (-not ($log | Select-String -Pattern 'worldgen functions: dev_powerwash_chunk and dev_new_world ran' -Quiet)) { throw 'the dev functions did not run' }
# The data the interpreter loaded (configured / placed features, biomes).
if (-not (Get-ServerLog | Select-String -Pattern 'worldgen data: \d+ configured features' -Quiet)) { throw 'the worldgen data was not loaded' }
