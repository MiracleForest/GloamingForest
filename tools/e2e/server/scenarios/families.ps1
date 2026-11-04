# Families (F11): the block family behaviours and the block event dispatch on the gf_test fixture
# families (families.prelaunch.ps1 generated them): placement, slab doubling, the door pair, fence /
# wall / pane connections and their 24 px collision, buttons and plates powering redstone, leaf
# decay on the game's random ticks, double chests (the inventory contract's containers: the 54-slot
# screen, the lids, the trapped chest's signal, the comparator), signs and their editor, the other
# shapes and interactions, saplings, the block events contract, waterlogging, the loot of a broken
# unopened chest, hanging signs chained, TF's own blocks through the kit, TF's chests as block actor
# containers (hoppers on, beside and under single and double chests, the comparator of a single and
# of a double chest, 0 while blocked, a dropper, a crafter, a hopper minecart, a hopper unpacking a
# loot table, a chest hidden from hoppers or redeclared, open client mod chest screens refreshed by
# a hopper, another player's click and code under the context the click opened them with (none
# taken from a click that opened nothing, a double chest screen opened by code left open), block
# data stacks moved before a hopper, a comparator or a click reaches them (a click on a chest without
# a block actor gives it one on the next tick), at the end of the tick their chunk loaded or by a
# neighbouring chunk's first tick, and dropped when the chest breaks before that), and vanilla
# viewers shown only the sign side facing them; then what a world reload keeps: the
# sign text (and its display entity), the release of switches saved while pressed, the chests'
# stacks in their block actors, and a chest saved without a block actor (its stacks in block data, as
# the BlockData backend kept them) moving its stacks into one.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
Wait-LogLine 'families: installed' 60 | Out-Null

$mark = Get-LogMark
$r = Invoke-SelfTest 'families.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) families selftest(s) failed" }
if ($r.Pass -lt 35) { throw "expected at least 35 families selftests to pass, got $($r.Pass)" }
foreach ($t in 'families.chest_hoppers', 'families.chest_hoppers_more', 'families.chest_hoppers_screen', 'families.chest_migrate_lazy', 'families.chest_migrate_click', 'families.chest_migrate_removed', 'families.chest_screen_context', 'families.chest_crafter', 'families.sign_facing_vanilla', 'families.chest_persist_a_write', 'families.chest_persist_b_read') {
    if (-not (Get-ServerLog -Since $mark | Select-String -Pattern "SELFTEST PASS $([regex]::Escape($t))$" -Quiet)) { throw "$t did not pass" }
}
if (Get-ServerLog -Since $mark | Select-String -Pattern 'no TF chests in this build' -Quiet) { throw "TF's chests are not in this build" }
if (Get-ServerLog -Since $mark | Select-String -Pattern 'fixture families missing|no gf_test packs' -Quiet) { throw 'the fixture families were not generated (tools/families)' }
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern 'families fixtures present: \d+ \(0 missing\)' -Quiet)) { throw 'not every fixture family is registered in the game' }

# Sign text lives in block data and switches saved while pressed are released after a reload.
$r = Invoke-SelfTest 'families.sign_a_write'
if ($r.Pass -ne 1) { throw 'families.sign_a_write did not pass' }
$r = Invoke-SelfTest 'families.switch_a_save'
if ($r.Pass -ne 1) { throw 'families.switch_a_save did not pass' }
$r = Invoke-SelfTest 'families.chest_persist_a_write'
if ($r.Pass -ne 1) { throw 'families.chest_persist_a_write did not pass' }
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
$r = Invoke-SelfTest 'families.chest_persist_b_read'
if ($r.Pass -ne 1) { throw 'families.chest_persist_b_read did not pass after the reload' }
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern 'chests verified after the reload' -Quiet)) { throw "the chests' stacks were not checked after the reload" }
# (the chunk may load and take its first tick before the mark: the whole boot's log)
if (-not (Get-ServerLog | Select-String -Pattern 'inventory: moved 1 stack\(s\) of the container at' -Quiet)) { throw 'the chest saved with its stacks in block data did not move them into its block actor' }
Write-Step "the chests' stacks survived the reload in their block actors; the block data chest moved its stacks into one"

# The doors of a reloaded world are watched again (their chunk's first tick).
$r = Invoke-SelfTest 'families.door_pair'
if ($r.Pass -ne 1) { throw 'families.door_pair failed after the reload' }
