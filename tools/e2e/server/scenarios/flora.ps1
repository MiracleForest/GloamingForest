# Flora (U6): upstream's small plants, berry bushes, critters and jars. The selftests grow and pick the
# bushes (random ticks with scripted draws, bone meal, the harvest into the inventory, dark tower
# bushes dying on nylium, oreberry thorns), harvest torchberries (stat), grow root strands, strip a
# liveroot block, layer fallen leaves, pop unsupported plants, break a huge lily pad as a whole,
# squish bugs (a direct squish with the stat and the kill_bug trigger, and a real arrow falling
# into a cicada), place critters against faces, catch a firefly in a mason jar, release a jarred
# firefly, fill and empty a mason jar (comparator), swap a jar lid, tune the firefly particle
# spawner and place the berry bush, fallen leaves and underground plant features.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
Wait-LogLine 'flora: installed' 60 | Out-Null

$mark = Get-LogMark
$r = Invoke-SelfTest 'flora.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) flora selftest(s) failed" }
if ($r.Pass -lt 11) { throw "expected at least 11 flora selftests to pass, got $($r.Pass)" }
foreach ($t in 'flora.bush_growth', 'flora.bush_harvest', 'flora.torchberries', 'flora.squish_bug', 'flora.jars', 'flora.features') {
    if (-not (Get-ServerLog -Since $mark | Select-String -Pattern "SELFTEST PASS $([regex]::Escape($t))$" -Quiet)) { throw "$t did not pass" }
}
# the features contract knows every flora feature type (no unknown-id warnings)
$unknown = @(Get-ServerLog -Since $mark | Select-String -Pattern 'unknown feature type gloamingforest:(berry_bush|enchanted_forest_vines|fallen_leaves|huge_lily_pad|huge_water_lily|underground_plants|underground_plants_in_structure|oreberry_bushes)' | ForEach-Object { $_.Line.Trim() })
if ($unknown.Count -gt 0) { throw "unknown flora feature types: $($unknown -join ' | ')" }
# the user-visible actions logged one line each
foreach ($p in 'flora: \S+ picked gloamingforest:raspberry_bush', 'flora: \S+ harvested torchberries', 'flora: a firefly was squished', 'flora: \S+ caught a firefly in a mason jar') {
    if (-not (Get-ServerLog -Since $mark | Select-String -Pattern $p -Quiet)) { throw "no log line '$p'" }
}
Write-Step "every flora selftest passed ($($r.Pass))"
