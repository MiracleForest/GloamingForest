# Landmark (F6): the landmark grid and TF's structure records, forceStart + progression protection
# (break / use / multi-place refused for a survival player without the lock advancement, the boxes
# and packets shown, allowed after the grant once the progression store exists), conquest, the
# start store (written, read back, kept over a server restart), hint books, decoration clearance;
# then /twilightforest center | info | conquer | locate | place | genbook from the console.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$r = Invoke-SelfTest 'landmark.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) landmark selftest(s) failed" }
if ($r.Pass -lt 9) { throw "expected at least 9 landmark selftests to pass, got $($r.Pass)" }

# The commands (console origin: the overworld at 0 0 0)
$mark = Get-LogMark
Send-Console 'twilightforest center'
$center = Wait-LogLine 'landmark: center 0 0 -> twilightforest:\S+ at 8 8 \(in center chunk: true\)' 30 -Since $mark
Write-Step "center: $center"
$mark = Get-LogMark
Send-Console 'tf info'
Wait-LogLine 'landmark: info at' 30 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'tffeature locate lich_tower'
Wait-LogLine 'landmark: locate twilightforest:lich_tower from' 30 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'twilightforest place lich_tower'
Wait-LogLine 'landmark: place twilightforest:lich_tower at .* failed|landmark: placed twilightforest:lich_tower' 30 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'twilightforest genbook'
Wait-LogLine 'commands.tffeature.not_player|must be run by a valid player' 30 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'twilightforest conquer'
Wait-LogLine 'commands.tffeature.structure.required|You are not in a structure' 30 -Since $mark | Out-Null

# The start store survives a restart: a forced, conquered start written to the world DB
$mark = Get-LogMark
$r = Invoke-SelfTest 'landmark.persist_write'
if ($r.Pass -ne 1) { throw 'landmark.persist_write did not pass' }
Wait-LogLine 'landmark\.persist_write: start written at' 30 -Since $mark | Out-Null
Restart-Server
$mark = Get-LogMark
Wait-LogLine 'landmark: \d+ starts loaded' 60 | Out-Null
$r = Invoke-SelfTest 'landmark.persist_read'
if ($r.Pass -ne 1) { throw 'landmark.persist_read did not pass after the restart' }
Wait-LogLine 'landmark\.persist_read: start verified at' 30 -Since $mark | Out-Null
Write-Step 'the forced start and its conquered flag survived the restart'
$mark = Get-LogMark
$r = Invoke-SelfTest 'landmark.persist_read'
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern 'landmark\.persist_read: no start' -Quiet)) { throw 'landmark.persist_read did not remove its start' }
