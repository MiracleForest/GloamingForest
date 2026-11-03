# Smoke (F0): BDS loads the mod (run.ps1 waited for "GloamingForest enabled"), the world is ready,
# the root command and its aliases answer, the foundation selftests ("core.*") pass, and the world
# reloads after a restart with its data intact: core.persist_write leaves a marker (a world DB
# record, a block and its block data) that only the world's own save path can carry over the
# restart, and core.persist_read verifies it afterwards with the same nonce, then removes it.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

# /twilightforest and its aliases /tf and /tffeature
$mark = Get-LogMark
Send-Console 'twilightforest stubs'
Wait-LogLine 'contract stubs called: ' 30 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'tf stubs'
Wait-LogLine 'contract stubs called: ' 30 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'tffeature stubs'
Wait-LogLine 'contract stubs called: ' 30 -Since $mark | Out-Null

# A failing vanilla command with a parameter on the console: BDS echoes its translated reply as
# "ERR [Server] <text>"; the mod's "Command error text:" line makes the run's error check excuse
# exactly that echo (docs/spec/e2e.md, Error lines), so this run still passes.
$mark = Get-LogMark
Send-Console 'tickingarea remove gf_smoke_no_such_area'
Wait-LogLine 'Command error text: ' 30 -Since $mark | Out-Null

$r = Invoke-SelfTest 'core.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) core selftest(s) failed" }
if ($r.Pass -lt 18) { throw "expected at least 18 core selftests to pass, got $($r.Pass)" }

# A fresh marker right before the restart (the "core.*" run above sorted the read before the write).
$mark = Get-LogMark
$r = Invoke-SelfTest 'core.persist_write'
if ($r.Pass -ne 1) { throw 'core.persist_write did not pass' }
$written = Get-ServerLog -Since $mark | Select-String -Pattern 'core\.persist_write: persist marker written nonce=(\d+)' | Select-Object -Last 1
if (-not $written) { throw 'core.persist_write logged no nonce' }
$nonce = $written.Matches[0].Groups[1].Value
Write-Step "persist marker nonce $nonce"

# The world reloads: stop (the save path writes the marker), boot again on the same world.
Restart-Server
$mark = Get-LogMark
$r = Invoke-SelfTest 'core.persist_read'
if ($r.Pass -ne 1) { throw 'core.persist_read did not pass after the reload' }
$verified = Get-ServerLog -Since $mark | Select-String -Pattern "core\.persist_read: persist marker verified nonce=$nonce\b" | Select-Object -First 1
if (-not $verified) { throw "the persist marker with nonce $nonce did not survive the reload (no 'verified' line)" }
Write-Step 'the world DB record, the block and its block data survived the reload'

# The foundation still works on the reloaded world; the marker is gone.
foreach ($t in 'core.world_db', 'core.block_data') {
    $r = Invoke-SelfTest $t
    if ($r.Pass -ne 1) { throw "$t did not pass after the reload" }
}
$mark = Get-LogMark
$r = Invoke-SelfTest 'core.persist_read'
if ($r.Pass -ne 1) { throw 'core.persist_read failed on the cleaned-up world' }
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern 'core\.persist_read: no persist marker' -Quiet)) { throw 'core.persist_read did not remove the marker' }
