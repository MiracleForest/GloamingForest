# Worldgen (F5): the Java-parity core, the feature interpreter (the gf_test fixture feature placed
# identically in memory and in the world, vanilla features, trees), LargeWriter, biome writes, the
# spawn engine, the dev functions and the 200-chunk generation timing of the whole pipeline
# (selftests "worldgen.*"). The timing line is the budget record of the run.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$mark = Get-LogMark
$r = Invoke-SelfTest 'worldgen.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) worldgen selftest(s) failed" }
if ($r.Pass -lt 10) { throw "expected 10 worldgen selftests to pass, got $($r.Pass)" }

$timing = Get-ServerLog -Since $mark | Select-String -Pattern 'worldgen timing: 200 chunks in ([\d.]+) ms \(([\d.]+) ms/chunk' | Select-Object -Last 1
if (-not $timing) { throw 'no 200-chunk timing line logged' }
Write-Step "200-chunk pipeline: $($timing.Matches[0].Groups[1].Value) ms ($($timing.Matches[0].Groups[2].Value) ms/chunk)"
$fixture = Get-ServerLog -Since $mark | Select-String -Pattern 'fixture feature hash ([0-9a-f]{16})' | Select-Object -Last 1
if ($fixture) { Write-Step "fixture feature hash $($fixture.Matches[0].Groups[1].Value)" }

# The data the interpreter loaded (configured / placed features, biomes). The dev functions are only
# checked for presence (worldgen.functions): running them would change the shared test world
# (gamerules, cleared ground). The pack loaded them without a content-log error (run.ps1 fails on one).
if (-not (Get-ServerLog | Select-String -Pattern 'worldgen data: \d+ configured features' -Quiet)) { throw 'the worldgen data was not loaded' }
