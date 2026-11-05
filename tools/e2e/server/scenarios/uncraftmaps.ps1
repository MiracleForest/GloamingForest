# Uncrafting table and maps (U13): the uncrafting menu (a reversed crafting recipe, broken parts,
# costs and levels, the config bans, the uncrafting recipes, recrafting with enchantment transfer),
# the screen path for companions and the config sync, the powered table, the magic map (centre,
# biome colours, landmark icons), the maze / ore maps (ore colours, the labyrinth snap, cloning) and
# the commands (map_locator; biomepng does nothing on a dedicated server).
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$mark = Get-LogMark
$r = Invoke-SelfTest 'uncraftmaps.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) uncraftmaps selftest(s) failed" }
if ($r.Pass -lt 12) { throw "expected 12 uncraftmaps selftests to pass, got $($r.Pass)" }
foreach ($t in 'uncraftmaps.iron_pickaxe', 'uncraftmaps.damaged_parts', 'uncraftmaps.config_bans', 'uncraftmaps.uncrafting_recipes', 'uncraftmaps.recraft',
               'uncraftmaps.screen', 'uncraftmaps.sync_config', 'uncraftmaps.powered', 'uncraftmaps.magic_map', 'uncraftmaps.maze_map', 'uncraftmaps.magic_cloning', 'uncraftmaps.commands') {
    if (-not (Get-ServerLog -Since $mark | Select-String -Pattern "SELFTEST PASS $([regex]::Escape($t))$" -Quiet)) { throw "$t did not pass" }
}
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern 'uncraftmaps: magic map \d+ for .* centred at' -Quiet)) { throw 'no magic map was made' }
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern 'uncraftmaps: ore map \d+ for' -Quiet)) { throw 'no ore map was made' }
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern 'uncraftmaps: biomepng on a dedicated server' -Quiet)) { throw 'biomepng did not take the dedicated server path' }
Write-Step 'uncrafting, recrafting, the config, the maps and the commands work'
