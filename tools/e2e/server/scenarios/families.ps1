# Families (F11): the block family behaviours and the block event dispatch on the gf_test fixture
# families (families.prelaunch.ps1 generated them): slab doubling, the door pair, fence / wall /
# pane connections, buttons and plates powering redstone, leaf decay, double chests, the other
# shapes and interactions, the block events contract; then the sign text survives a restart.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
Wait-LogLine 'families: installed' 60 | Out-Null

$mark = Get-LogMark
$r = Invoke-SelfTest 'families.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) families selftest(s) failed" }
if ($r.Pass -lt 12) { throw "expected at least 12 families selftests to pass, got $($r.Pass)" }
if (Get-ServerLog -Since $mark | Select-String -Pattern 'fixture families missing' -Quiet) { throw 'the fixture families were not generated (tools/families)' }
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern 'families fixtures present: \d+ \(0 missing\)' -Quiet)) { throw 'not every fixture family is registered in the game' }

# Sign text lives in block data: it survives the world reload.
$r = Invoke-SelfTest 'families.sign_a_write'
if ($r.Pass -ne 1) { throw 'families.sign_a_write did not pass' }
Restart-Server
Wait-LogLine 'families: installed' 60 | Out-Null
$mark = Get-LogMark
$r = Invoke-SelfTest 'families.sign_b_read'
if ($r.Pass -ne 1) { throw 'families.sign_b_read did not pass after the reload' }
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern 'sign text verified' -Quiet)) { throw 'the sign text did not survive the reload' }
Write-Step 'the sign text survived the reload'

# The redstone-tracked doors are persisted too: the door test passes on the reloaded world.
$r = Invoke-SelfTest 'families.door_pair'
if ($r.Pass -ne 1) { throw 'families.door_pair failed after the reload' }
