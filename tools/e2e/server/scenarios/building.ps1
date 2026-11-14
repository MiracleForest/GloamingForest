# Building (U5): the building blocks and boss collectibles on BDS. The building.* selftests check
# every block of the unit is registered (hardness spot checks, the nine unbreakable boss spawners),
# the trophy variant table (pedestal comparator values per variant, the click / redstone sound per
# variant, the alpha yeti's rare roar), the trophy placement (rotation from the yaw, the wall trophy
# against a wall), the powered state following redstone, a dispenser equipping a trophy on the head,
# the nagastone variants for every 0 / 1 / 2 connection layout (and heads facing in), the spiral
# bricks' placement, the rope (support, the one-tick decay dropping one rope per strand, the rope
# item extending a chain), the iron ladder (placement, left / right, breaking without support), the
# knightmetal / fiery / arctic fur blocks, the banner pattern recipes, every stonecutting recipe of
# the unit, every block's hardness / explosion resistance / tool / tier (Appendix A), and upstream's banner
# patterns (registered, unlocked by their items, the loom's result banner, placed banners, and a real
# loom craft: the bytes of a client's CraftLoom request read and validated by the server).
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
Wait-LogLine 'building: installed' 60 | Out-Null

$mark = Get-LogMark
$r = Invoke-SelfTest 'building.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) building selftest(s) failed" }
if ($r.Pass -lt 14) { throw "expected at least 14 building selftests to pass, got $($r.Pass)" }
if (-not (Get-ServerLog -Since 0 | Select-String -Pattern 'building: 9 banner patterns registered' -Quiet)) { throw 'upstream banner patterns were not registered at load' }
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern 'building: trophy gloamingforest:hydra_trophy sounds entity\.gloamingforest\.hydra\.growl' -Quiet)) {
    throw 'the powered / clicked hydra trophy played no sound'
}
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern 'building: dispenser put gloamingforest:minoshroom_trophy on' -Quiet)) { throw 'the dispenser did not equip the trophy' }
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern 'building: rope at .* broke' -Quiet)) { throw 'no rope broke without support' }
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern 'building: the server''s loom crafted a banner with gf_naga' -Quiet)) { throw 'the server did not craft an upstream banner from a loom request' }
Write-Step "building selftests: pass=$($r.Pass)"
