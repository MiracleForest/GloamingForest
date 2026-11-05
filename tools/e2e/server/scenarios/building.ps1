# Building (U5): the building blocks and boss collectibles on BDS. The building.* selftests check
# every block of the unit is registered (hardness spot checks, the nine unbreakable boss spawners),
# the trophy variant table (pedestal comparator values per variant, the click / redstone sound per
# variant, the alpha yeti's rare roar), the trophy placement (rotation from the yaw, the wall trophy
# against a wall), the powered state following redstone, a dispenser equipping a trophy on the head,
# the nagastone variants for every 0 / 1 / 2 connection layout (and heads facing in), the spiral
# bricks' placement, the rope (support, the one-tick decay dropping one rope per strand, the rope
# item extending a chain), the iron ladder (placement, left / right, breaking without support), the
# knightmetal / fiery / arctic fur blocks, and the banner pattern and stonecutting recipes.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
Wait-LogLine 'building: installed' 60 | Out-Null

$mark = Get-LogMark
$r = Invoke-SelfTest 'building.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) building selftest(s) failed" }
if ($r.Pass -lt 12) { throw "expected at least 12 building selftests to pass, got $($r.Pass)" }
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern 'building: trophy twilightforest:hydra_trophy sounds entity\.twilightforest\.hydra\.growl' -Quiet)) {
    throw 'the powered / clicked hydra trophy played no sound'
}
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern 'building: dispenser put twilightforest:minoshroom_trophy on' -Quiet)) { throw 'the dispenser did not equip the trophy' }
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern 'building: rope at .* broke' -Quiet)) { throw 'no rope broke without support' }
Write-Step "building selftests: pass=$($r.Pass)"
