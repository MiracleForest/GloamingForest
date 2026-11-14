# Woodwork (U4): the wood sets and wooden furniture on BDS. The woodwork.* selftests: log stripping
# for every upstream log and wood, banister placement / axe cycle / un-extension, drying racks (add and
# remove, the upstream drying times, coral after 40 ticks, the default 6000 ticks, no drying while
# waterlogged, the item dropped when the water comes or goes and when the rack breaks, automation
# refused while drying), hollow logs (placement by face, moss / grass / snow / shovel / shears,
# vine / ladder / shears, the climbable variant), the Java shapes of racks, banisters and hollow
# logs, chest pairs, the trapped chest's signal, signs, every recipe with its unlock, fuel, the
# boats (placed from the item, ridden, broken: the upstream item; a crash landing: planks and sticks), the
# client mod's rack sync and the display entity (one, no drops); then a rack keeps drying across a
# server restart.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
Wait-LogLine 'woodwork: installed' 60 | Out-Null

$r = Invoke-SelfTest 'woodwork.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) woodwork selftest(s) failed" }
if ($r.Pass -lt 18) { throw "expected at least 18 woodwork selftests to pass, got $($r.Pass)" }
Write-Step "woodwork selftests: $($r.Pass) passed"

# A drying rack's item and its drying time survive a restart (block data + the rack index), and it
# keeps drying afterwards.
$r = Invoke-SelfTest 'woodwork.persist_a_write'
if ($r.Pass -ne 1) { throw 'woodwork.persist_a_write did not pass' }
Restart-Server
Wait-LogLine 'woodwork: installed' 60 | Out-Null
$mark = Get-LogMark
$r = Invoke-SelfTest 'woodwork.persist_b_read'
if ($r.Pass -ne 1) { throw 'woodwork.persist_b_read did not pass after the restart' }
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern 'drying rack verified after the reload' -Quiet)) { throw 'the drying rack was not checked after the restart' }
Write-Step 'a drying rack kept its item and kept drying across the restart'
