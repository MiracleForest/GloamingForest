# F10 (combat): the hurt hook, damage types and death messages, the death pipeline (lethal hook,
# keepers, drops, respawn), the loot interpreter with TF's functions, loot containers, the
# container backend, the progression store / trigger bus / events, gamerules and fx run as
# selftests ("combat.*"); the scenario adds the /twilightforest gamerule command and a container
# that keeps its items over a world reload (combat.container_persist_write before the restart,
# combat.container_persist_read after it, same count).
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
if ($r.Pass -lt 15) { throw "expected at least 15 combat selftests to pass, got $($r.Pass)" }

# A container keeps its items over a world reload.
$mark = Get-LogMark
$r = Invoke-SelfTest 'combat.container_persist_write'
if ($r.Pass -ne 1) { throw 'combat.container_persist_write did not pass' }
$written = Get-ServerLog -Since $mark | Select-String -Pattern 'container marker written count=(\d+)' | Select-Object -Last 1
if (-not $written) { throw 'combat.container_persist_write logged no count' }
$count = $written.Matches[0].Groups[1].Value
Write-Step "container marker count $count"

Restart-Server
$mark = Get-LogMark
$r = Invoke-SelfTest 'combat.container_persist_read'
if ($r.Pass -ne 1) { throw 'combat.container_persist_read did not pass after the reload' }
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern "container marker verified count=$count\b" -Quiet)) {
    throw "the container did not keep its $count gold ingots over the reload"
}
Write-Step 'the container kept its items over the reload'
