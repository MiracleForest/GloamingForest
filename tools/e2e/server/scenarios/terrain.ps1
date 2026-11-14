# Terrain (U2): upstream's density types, the noise heights and the surface (rules + chunk blankets) of
# the F13 goldens' three seeds (fingerprint grids, the goldens' own biomes), the surface rules per
# biome, the canopy and glacier blankets (and the canopy's avoid_structures against a forced dark
# tower start), the Skylight preset's floating islands, the noise -> surface -> blanket chunk
# pipeline, upstream's caves (no water exposure, dirt roofs), the chunk_blanketing placement modifier, the
# legacy ores, NeoForge's per-piece beardifier, and the per-chunk timing p95 against the 'terrain'
# budget, in memory and on chunks the game generates in the Gloaming Forest (selftests "terrain.*").
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$mark = Get-LogMark
$r = Invoke-SelfTest 'terrain.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) terrain selftest(s) failed" }
if ($r.Pass -lt 18) { throw "expected 18 terrain selftests to pass, got $($r.Pass)" }
$log = @(Get-ServerLog -Since $mark)

$heights = $log | Select-String -Pattern 'heights checked (\d+), mismatches (\d+)' | Select-Object -Last 1
if (-not $heights) { throw 'terrain.height_golden logged no fingerprint result' }
Write-Step "height fingerprint (3 seeds): $($heights.Matches[0].Groups[1].Value) columns, $($heights.Matches[0].Groups[2].Value) mismatches"
$surface = $log | Select-String -Pattern 'surface cells checked (\d+), mismatches (\d+)' | Select-Object -Last 1
if (-not $surface) { throw 'terrain.surface_golden logged no fingerprint result' }
Write-Step "surface fingerprint (3 seeds): $($surface.Matches[0].Groups[1].Value) cells, $($surface.Matches[0].Groups[2].Value) mismatches"
$timing = $log | Select-String -Pattern 'terrain: terrain chunk p50 ([\d.]+) ms, p95 ([\d.]+) ms \(budget (\d+) ms\)' | Select-Object -Last 1
if (-not $timing) { throw 'terrain.timing logged no p95' }
Write-Step "in-memory chunk terrain p50 $($timing.Matches[0].Groups[1].Value) ms, p95 $($timing.Matches[0].Groups[2].Value) ms (budget $($timing.Matches[0].Groups[3].Value) ms)"
$world = $log | Select-String -Pattern 'terrain: world chunk p50 ([\d.]+) ms, p95 ([\d.]+) ms over (\d+) chunks \(budget (\d+) ms\)' | Select-Object -Last 1
if (-not $world) { throw 'terrain.timing_world logged no p95 of generated chunks' }
Write-Step "generated Gloaming Forest chunks ($($world.Matches[0].Groups[3].Value)): p50 $($world.Matches[0].Groups[1].Value) ms, p95 $($world.Matches[0].Groups[2].Value) ms (budget $($world.Matches[0].Groups[4].Value) ms)"
$sky = $log | Select-String -Pattern 'terrain: skylight islands in (\d+) of (\d+) columns, (\d+) chunks checked' | Select-Object -Last 1
if (-not $sky) { throw 'terrain.skylight_option logged no island count' }
Write-Step "skylight: islands in $($sky.Matches[0].Groups[1].Value) of $($sky.Matches[0].Groups[2].Value) columns, $($sky.Matches[0].Groups[3].Value) island chunks checked"
$caves = $log | Select-String -Pattern 'gf_caves carved (\d+) blocks, dirt roof blocks (\d+)' | Select-Object -Last 1
if ($caves) { Write-Step "caves: $($caves.Matches[0].Groups[1].Value) carved, $($caves.Matches[0].Groups[2].Value) dirt roof blocks" }
$preset = Get-ServerLog | Select-String -Pattern 'terrain: world preset (\w+)' | Select-Object -Last 1
if (-not $preset) { throw 'no terrain world preset line' }
Write-Step "world preset: $($preset.Matches[0].Groups[1].Value)"
