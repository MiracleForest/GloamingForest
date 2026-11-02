# F10 (combat): the hurt hook, damage types and death messages, the death pipeline (lethal hook,
# keepers, drops, respawn), the loot interpreter with TF's functions, loot containers, the
# container backend, the progression store / trigger bus / events, gamerules and fx run as
# selftests ("combat.*"), with the other W0 units' real implementations: TF's chests as F11's
# families declare them (combat.tf_*), F9's stats and tags, the missing advancement toast. The
# scenario adds the /twilightforest gamerule command and containers that keep their items over a
# world reload (combat.container_persist_write and combat.tf_chest_persist_write, a TF double
# chest, before the restart; the _read tests after it, same counts).
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

# /twilightforest gamerule <rule> <value> (Bedrock's /gamerule only knows vanilla rules)
$mark = Get-LogMark
Send-Console 'tf gamerule tfEnforcedProgression false'
Wait-LogLine 'gamerule tfEnforcedProgression = false' 30 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'tf gamerule playersTfPortalDefaultDelay 75'
Wait-LogLine 'gamerule playersTfPortalDefaultDelay = 75' 30 -Since $mark | Out-Null
Send-Console 'tf gamerule tfEnforcedProgression true'
Send-Console 'tf gamerule playersTfPortalDefaultDelay 60'

$r = Invoke-SelfTest 'combat.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) combat selftest(s) failed" }
if ($r.Pass -lt 45) { throw "expected at least 45 combat selftests to pass, got $($r.Pass)" }
# the double chest probe of combat.prelaunch.ps1 was there (pairing checked, not skipped)
if (Get-ServerLog | Select-String -Pattern 'no gf_combat:chest probe block' -Quiet) { throw 'the gf_combat:chest probe block was not loaded' }
# TF's chests are F11's declared containers (data/gf/containers/families.json read at mod load)
if (-not (Get-ServerLog | Select-String -Pattern 'inventory: \d+ data-declared container' -Quiet)) { throw 'no data-declared containers were loaded' }

# Containers keep their items over a world reload: the fixture block and a TF double chest.
$mark = Get-LogMark
$r = Invoke-SelfTest 'combat.container_persist_write'
if ($r.Pass -ne 1) { throw 'combat.container_persist_write did not pass' }
$written = Get-ServerLog -Since $mark | Select-String -Pattern 'container marker written count=(\d+)' | Select-Object -Last 1
if (-not $written) { throw 'combat.container_persist_write logged no count' }
$count = $written.Matches[0].Groups[1].Value
Write-Step "container marker count $count"
$mark = Get-LogMark
$r = Invoke-SelfTest 'combat.tf_chest_persist_write'
if ($r.Pass -ne 1) { throw 'combat.tf_chest_persist_write did not pass' }
$tfWritten = Get-ServerLog -Since $mark | Select-String -Pattern 'tf chest marker written gold=(\d+) emeralds=(\d+)' | Select-Object -Last 1
if (-not $tfWritten) { throw 'combat.tf_chest_persist_write logged no counts' }
$gold = $tfWritten.Matches[0].Groups[1].Value
$emeralds = $tfWritten.Matches[0].Groups[2].Value
Write-Step "tf chest marker gold $gold emeralds $emeralds"

Restart-Server
$mark = Get-LogMark
$r = Invoke-SelfTest 'combat.container_persist_read'
if ($r.Pass -ne 1) { throw 'combat.container_persist_read did not pass after the reload' }
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern "container marker verified count=$count\b" -Quiet)) {
    throw "the container did not keep its $count gold ingots over the reload"
}
Write-Step 'the container kept its items over the reload'
$mark = Get-LogMark
$r = Invoke-SelfTest 'combat.tf_chest_persist_read'
if ($r.Pass -ne 1) { throw 'combat.tf_chest_persist_read did not pass after the reload' }
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern "tf chest marker verified gold=$gold emeralds=$emeralds\b" -Quiet)) {
    throw "the TF double chest did not keep its $gold gold ingots and $emeralds emeralds over the reload"
}
Write-Step 'the TF double chest kept both halves over the reload'
