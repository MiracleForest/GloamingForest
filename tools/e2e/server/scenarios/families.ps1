# Families (F11): the block family behaviours and the block event dispatch on the gf_test fixture
# families (families.prelaunch.ps1 generated them): placement, slab doubling, the door pair, fence /
# wall / pane connections and their 24 px collision, buttons and plates powering redstone, leaf
# decay on the game's random ticks, double chests, signs and their editor, the other shapes and
# interactions, saplings, the block events contract; then what a world reload keeps: the sign text
# (and its display entity) and the release of switches saved while pressed.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
Wait-LogLine 'families: installed' 60 | Out-Null

$mark = Get-LogMark
$r = Invoke-SelfTest 'families.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) families selftest(s) failed" }
if ($r.Pass -lt 17) { throw "expected at least 17 families selftests to pass, got $($r.Pass)" }
if (Get-ServerLog -Since $mark | Select-String -Pattern 'fixture families missing|no gf_test packs' -Quiet) { throw 'the fixture families were not generated (tools/families)' }
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern 'families fixtures present: \d+ \(0 missing\)' -Quiet)) { throw 'not every fixture family is registered in the game' }

# Sign text lives in block data and switches saved while pressed are released after a reload.
$r = Invoke-SelfTest 'families.sign_a_write'
if ($r.Pass -ne 1) { throw 'families.sign_a_write did not pass' }
$r = Invoke-SelfTest 'families.switch_a_save'
if ($r.Pass -ne 1) { throw 'families.switch_a_save did not pass' }
Restart-Server
Wait-LogLine 'families: installed' 60 | Out-Null
$mark = Get-LogMark
$r = Invoke-SelfTest 'families.sign_b_read'
if ($r.Pass -ne 1) { throw 'families.sign_b_read did not pass after the reload' }
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern 'sign text verified' -Quiet)) { throw 'the sign text did not survive the reload' }
Write-Step 'the sign text and its display survived the reload'
$r = Invoke-SelfTest 'families.switch_b_reload'
if ($r.Pass -ne 1) { throw 'families.switch_b_reload did not pass after the reload' }
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern 'switches released after the reload' -Quiet)) { throw 'switches saved while pressed stayed pressed' }
Write-Step 'switches saved while pressed are released after the reload'

# The doors of a reloaded world are watched again (their chunk's first tick).
$r = Invoke-SelfTest 'families.door_pair'
if ($r.Pass -ne 1) { throw 'families.door_pair failed after the reload' }
