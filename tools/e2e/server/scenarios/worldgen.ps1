# Worldgen (F5): the Java-parity core, the feature interpreter (the gf_test fixture feature placed
# identically in memory and in the world, vanilla features, trees, scattered ores), LargeWriter,
# biome writes (the custom probe biome of worldgen.prelaunch.ps1), carvers, the spawn engine and
# the landmark spawn rules, the dev functions run through /function, the 200-chunk generation
# timing of the whole pipeline and, when F4's dimension exists, the in-dimension generator run
# (selftests "worldgen.*"). The timing lines are the budget record of the run.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$mark = Get-LogMark
$r = Invoke-SelfTest 'worldgen.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) worldgen selftest(s) failed" }
if ($r.Pass -lt 15) { throw "expected 15 worldgen selftests to pass, got $($r.Pass)" }
$log = @(Get-ServerLog -Since $mark)

$timing = $log | Select-String -Pattern 'worldgen timing: 200 chunks in ([\d.]+) ms \(([\d.]+) ms/chunk' | Select-Object -Last 1
if (-not $timing) { throw 'no 200-chunk timing line logged' }
Write-Step "200-chunk in-memory pipeline: $($timing.Matches[0].Groups[1].Value) ms ($($timing.Matches[0].Groups[2].Value) ms/chunk)"
$fixture = $log | Select-String -Pattern 'fixture feature hash ([0-9a-f]{16})' | Select-Object -Last 1
if ($fixture) { Write-Step "fixture feature hash $($fixture.Matches[0].Groups[1].Value)" }

# The generator in the Twilight Forest: required once the dimension exists (F4 installs it, and the
# generator hook logs that it took over the dimension's generator); otherwise a distinct skip line.
$gen = $log | Select-String -Pattern 'worldgen generator: (in-dimension run: .*|skipped, .*)' | Select-Object -Last 1
if (-not $gen) { throw 'no worldgen generator line (neither an in-dimension run nor the skip)' }
$dimension = [bool](Get-ServerLog | Select-String -Pattern 'Twilight Forest generator installed' -Quiet)
if ($dimension -and $gen.Line -notmatch 'in-dimension run') { throw 'the Twilight Forest generator is installed but worldgen.generator did not run in the dimension' }
Write-Step $gen.Matches[0].Value
$genBiome = $log | Select-String -Pattern 'worldgen generator: generated chunk biome (\S+)' | Select-Object -Last 1
if ($genBiome) { Write-Step "biome of a chunk the generator made: $($genBiome.Matches[0].Groups[1].Value)" }

# Spike S2: the custom probe biome was registered from the test pack and written into a chunk.
$probe = $log | Select-String -Pattern 'worldgen custom biome: (\S+) written and read back' | Select-Object -Last 1
if (-not $probe) { throw 'the custom probe biome was not written into a chunk (not registered by the game?)' }
Write-Step "custom biome in a chunk: $($probe.Matches[0].Groups[1].Value)"

# The dev functions were loaded by the world and ran (/function, including dev_new_world's scriptevent).
if (-not ($log | Select-String -Pattern 'worldgen functions: dev_powerwash_chunk and dev_new_world ran' -Quiet)) { throw 'the dev functions did not run' }
# The data the interpreter loaded (configured / placed features, biomes).
if (-not (Get-ServerLog | Select-String -Pattern 'worldgen data: \d+ configured features' -Quiet)) { throw 'the worldgen data was not loaded' }
