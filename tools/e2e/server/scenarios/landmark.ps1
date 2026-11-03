# Landmark (F6): the landmark grid and TF's structure records, forceStart + progression protection
# (break / use / multi-place refused for a survival player without the lock advancement, the boxes
# and packets shown, allowed after progression::grant and locked again after revoke), conquest,
# the start store (written, read back, kept over a server restart), hint books and the hint mob's
# guaranteed book drop, the hint timer's game time, decoration clearance (reserved sites without
# pieces block nothing), the controlled spawns and spawn overrides of the fixtures and a hollow
# hill's dome, the starts' piece types and saved tags, the start evaluation threading and locate
# probe, the descriptors' full TF records, Java's decoration slots and seeds of the landmark pieces,
# the biome checks against TF's biome tags and TF's command literals and texts; then
# /twilightforest center | info | conquer [reactivate] | locate <structure or #tag> | place |
# genbook [<structure>] (+ the generatebook alias) from the console.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$r = Invoke-SelfTest 'landmark.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) landmark selftest(s) failed" }
if ($r.Pass -lt 18) { throw "expected at least 18 landmark selftests to pass, got $($r.Pass)" }

# The commands (console origin: the overworld at 0 0 0)
$mark = Get-LogMark
Send-Console 'twilightforest center'
$center = Wait-LogLine 'landmark: center 0 0 -> twilightforest:\S+ at 8 8 \(in center chunk: true\)' 30 -Since $mark
Write-Step "center: $center"
$mark = Get-LogMark
Send-Console 'tf info'
Wait-LogLine 'landmark: info at' 30 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'tffeature locate twilightforest:lich_tower'
Wait-LogLine 'landmark: locate twilightforest:lich_tower from' 30 -Since $mark | Out-Null
# a structure tag (LocateCommand#getHolders: Registry#getTag) and one of Minecraft's own structures
# (Bedrock generates and locates those itself)
$mark = Get-LogMark
Send-Console 'tf locate #twilightforest:landmark'
Wait-LogLine 'landmark: locate #twilightforest:landmark from' 30 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'tf locate minecraft:village_plains'
Wait-LogLine "landmark: locate minecraft:village_plains: not a Twilight Forest structure" 30 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'twilightforest place twilightforest:lich_tower'
Wait-LogLine 'landmark: place twilightforest:lich_tower at .* failed|landmark: placed twilightforest:lich_tower' 30 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'twilightforest genbook'
Wait-LogLine 'landmark: genbook \(all\): not run by a player' 30 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'twilightforest conquer'
Wait-LogLine 'landmark: conquer at -?\d+ -?\d+ -?\d+: not in a structure' 30 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'twilightforest conquer reactivate'
Wait-LogLine 'landmark: conquer reactivate at -?\d+ -?\d+ -?\d+: not in a structure' 30 -Since $mark | Out-Null
# genbook's ResourceKeyArgument is resolved before the player check: an unknown structure (no namespace:
# minecraft) is commands.place.structure.invalid, a registered one reaches not_player; the GloamingForest
# alias generatebook behaves the same
$mark = Get-LogMark
Send-Console 'twilightforest genbook lich_tower'
Wait-LogLine 'landmark: genbook lich_tower: no structure minecraft:lich_tower' 30 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'tf generatebook twilightforest:lich_tower'
Wait-LogLine 'landmark: genbook twilightforest:lich_tower: not run by a player' 30 -Since $mark | Out-Null

# The start store survives a restart: a forced, conquered start written to the world DB
$mark = Get-LogMark
$r = Invoke-SelfTest 'landmark.persist_1_write'
if ($r.Pass -ne 1) { throw 'landmark.persist_1_write did not pass' }
Wait-LogLine 'landmark\.persist_1_write: start written at' 30 -Since $mark | Out-Null
Restart-Server
$mark = Get-LogMark
Wait-LogLine 'landmark: \d+ starts loaded' 60 | Out-Null
$r = Invoke-SelfTest 'landmark.persist_2_read'
if ($r.Pass -ne 1) { throw 'landmark.persist_2_read did not pass after the restart' }
Wait-LogLine 'landmark\.persist_2_read: start verified at' 30 -Since $mark | Out-Null
Write-Step 'the forced start and its conquered flag survived the restart'
$mark = Get-LogMark
$r = Invoke-SelfTest 'landmark.persist_2_read'
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern 'landmark\.persist_2_read: no start' -Quiet)) { throw 'landmark.persist_2_read did not remove its start' }
