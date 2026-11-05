# Terrain (U2): TF's density types, the noise heights and the surface (rules + chunk blankets) of
# the F13 goldens' three seeds (fingerprint grids, the goldens' own biomes), the surface rules per
# biome, the canopy and glacier blankets, the Skylight preset, the noise -> surface -> blanket chunk
# pipeline, TF's caves (no water exposure, dirt roofs), the chunk_blanketing placement modifier, the
# legacy ores and the per-chunk timing p95 against the 'terrain' budget (selftests "terrain.*").
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$mark = Get-LogMark
$r = Invoke-SelfTest 'terrain.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) terrain selftest(s) failed" }
if ($r.Pass -lt 13) { throw "expected 13 terrain selftests to pass, got $($r.Pass)" }
$log = @(Get-ServerLog -Since $mark)

$heights = $log | Select-String -Pattern 'heights checked (\d+), mismatches (\d+)' | Select-Object -Last 1
if (-not $heights) { throw 'terrain.height_golden logged no fingerprint result' }
Write-Step "height fingerprint (3 seeds): $($heights.Matches[0].Groups[1].Value) columns, $($heights.Matches[0].Groups[2].Value) mismatches"
$surface = $log | Select-String -Pattern 'surface cells checked (\d+), mismatches (\d+)' | Select-Object -Last 1
if (-not $surface) { throw 'terrain.surface_golden logged no fingerprint result' }
Write-Step "surface fingerprint (3 seeds): $($surface.Matches[0].Groups[1].Value) cells, $($surface.Matches[0].Groups[2].Value) mismatches"
$timing = $log | Select-String -Pattern 'terrain: terrain chunk p50 ([\d.]+) ms, p95 ([\d.]+) ms \(budget (\d+) ms\)' | Select-Object -Last 1
if (-not $timing) { throw 'terrain.timing logged no p95' }
Write-Step "chunk terrain p50 $($timing.Matches[0].Groups[1].Value) ms, p95 $($timing.Matches[0].Groups[2].Value) ms (budget $($timing.Matches[0].Groups[3].Value) ms)"
$caves = $log | Select-String -Pattern 'tf_caves carved (\d+) blocks, dirt roof blocks (\d+)' | Select-Object -Last 1
if ($caves) { Write-Step "caves: $($caves.Matches[0].Groups[1].Value) carved, $($caves.Matches[0].Groups[2].Value) dirt roof blocks" }
$preset = Get-ServerLog | Select-String -Pattern 'terrain: world preset (\w+)' | Select-Object -Last 1
if (-not $preset) { throw 'no terrain world preset line' }
Write-Step "world preset: $($preset.Matches[0].Groups[1].Value)"
