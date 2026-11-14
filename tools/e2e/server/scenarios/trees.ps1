# Trees (U3): upstream's tree features, placers and decorators, saplings and the magic log cores. The
# selftests grow every sapling from bone meal (the real click), place every owned tree and mushroom
# feature, compare seeded canopy / mega canopy / darkwood / mangrove trees with their golden volume
# hashes, and run each core on a fixture (time: a crop grows; transformation: a column becomes
# enchanted forest; mining: an ore vein moves toward the core; sorting: a chest item moves into the
# chest holding more of it); a core toggles with a block in hand without placing it, and a core
# disabled by config stays off and names itself in the message.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
Wait-LogLine 'trees: installed' 60 | Out-Null

$mark = Get-LogMark
$r = Invoke-SelfTest 'trees.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) trees selftest(s) failed" }
if ($r.Pass -lt 16) { throw "expected at least 16 trees selftests to pass, got $($r.Pass)" }
foreach ($t in 'trees.sapling_growers', 'trees.mega_2x2', 'trees.mega_2x2_java', 'trees.time_core_ticks', 'trees.hardened_dark_leaves', 'trees.mining_core_magnet', 'trees.sorting_core_moves_item', 'trees.core_toggle_held_item') {
    if (-not (Get-ServerLog -Since $mark | Select-String -Pattern "SELFTEST PASS $([regex]::Escape($t))$" -Quiet)) { throw "$t did not pass" }
}
# the features contract knows every type and placer this unit registers (no unknown-id warnings)
$unknown = @(Get-ServerLog -Since $mark | Select-String -Pattern 'unknown (trunk placer|foliage placer|tree decorator|feature type|block state provider) gloamingforest:' | ForEach-Object { $_.Line.Trim() })
if ($unknown.Count -gt 0) { throw "unknown tree parts: $($unknown -join ' | ')" }
# every tree was compared with upstream 4.8 on Java block for block (the oracle's boxes are this world's):
# 3 feature grids, 4 golden trees, 9 saplings and 3 seeded 2x2 saplings
$parity = @(Get-ServerLog -Since $mark | Select-String -Pattern 'JAVA PARITY (\S+): ' | ForEach-Object { $_.Line.Trim() })
$notCompared = @($parity | Where-Object { $_ -match 'not compared' })
if ($notCompared.Count -gt 0) { throw "Java parity not compared: $($notCompared -join ' | ')" }
$cases = @('features_a', 'features_b', 'features_c', 'golden_canopy_tree', 'golden_mega_canopy_tree', 'golden_darkwood_tree', 'golden_mangrove_tree') +
    @('gloaming_oak', 'canopy', 'mangrove', 'darkwood', 'time', 'transformation', 'mining', 'sorting', 'rainbow_oak' | ForEach-Object { "sapling_$($_)_sapling" }) +
    @('mega_canopy_sapling', 'mega_gloaming_oak_sapling_forest', 'mega_gloaming_oak_sapling_savannah')
$missing = @($cases | Where-Object { $c = $_; -not ($parity | Where-Object { $_ -match "JAVA PARITY $([regex]::Escape($c)): " }) })
if ($missing.Count -gt 0) { throw "Java parity comparisons missing: $($missing -join ', ')" }
Write-Step "every trees selftest passed; $($parity.Count) Java parity comparisons ran"
