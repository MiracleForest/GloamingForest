# Woodwork (U4): the wood sets and wooden furniture on BDS. The woodwork.* selftests: log stripping
# for every TF log and wood, banister placement / axe cycle / un-extension, drying racks (add and
# remove, the TF drying times, coral after 40 ticks, the default 6000 ticks, no drying while
# waterlogged, the item dropped when the water comes or goes and when the rack breaks, automation
# refused while drying), hollow logs (placement by face, moss / grass / snow / shovel / shears,
# vine / ladder / shears, the climbable variant), the Java shapes of racks, banisters and hollow
# logs, chest pairs, the drying recipes; then a rack keeps drying across a server restart, and a
# boat placed on water from its item and broken drops its TF item.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
Wait-LogLine 'woodwork: installed' 60 | Out-Null

$r = Invoke-SelfTest 'woodwork.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) woodwork selftest(s) failed" }
if ($r.Pass -lt 10) { throw "expected at least 10 woodwork selftests to pass, got $($r.Pass)" }
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
