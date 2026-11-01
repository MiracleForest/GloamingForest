# Structure kit (F7): the structure selftests (piece framework, templates, the 17 TF processors on
# blocks they act on, update_marking shape updates, waterlogging, flower pots, paintings, TF's
# marker handlers, jigsaw assembly with terrain height adjustment, the template definitions API,
# wood palettes, block selectors, speleothems, TFMaze, HollowHill, the old lich tower bases, spawner
# mob scaling and custom spawn rules) pass with their golden volume hashes, the kit loaded its data,
# /twilightforest structure_util answers, an outline expires without harm, and across a restart
# work queued for an unloaded chunk runs once the chunk is loaded and a scaled mob keeps its size.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
Wait-LogLine 'structure kit: \d+ wood palettes, \d+ speleothem configs' 60 | Out-Null

$r = Invoke-SelfTest 'structure.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) structure selftest(s) failed" }
if ($r.Pass -lt 23) { throw "expected at least 23 structure selftests to pass, got $($r.Pass)" }
$probeWritten = Get-ServerLog | Select-String -Pattern 'scale probe written id=(-?\d+)' | Select-Object -Last 1
if (-not $probeWritten) { throw 'structure.scale_probe did not leave its scaled mob for the restart' }

# the dev outlines are drawn by the kit's tick listener and expire on their own: the server keeps
# running and answering after one expired
$mark = Get-LogMark
Send-Console 'twilightforest structure_util outline 0 -60 0 4 -56 4 3'
Wait-LogLine 'structure_util outline: \d+ outlines for 3 s' 30 -Since $mark | Out-Null
Wait-LogLine 'structure_util outlines expired' 30 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'twilightforest structure_util templates'
Wait-LogLine 'structure kit: \d+ templates' 30 -Since $mark | Out-Null
Write-Step 'an outline expired and the server still answers'

# the dev command lists the kit's registries
$mark = Get-LogMark
Send-Console 'twilightforest structure_util templates'
$line = Wait-LogLine 'structure kit: \d+ templates, \d+ template definitions, (\d+) wood palettes, (\d+) speleothem configs, (\d+) processors' 30 -Since $mark
Write-Step "structure_util templates: $line"
if ($line -notmatch '17 wood palettes, 6 speleothem configs') { throw "the kit did not load the 17 wood palettes and 6 speleothem configs: $line" }

# queued block entity work survives a restart: a probe job for an unloaded chunk (4096 blocks
# east of spawn) is persisted with the world (gf:structure:pending) and runs only once
# structure.pending_read loads that chunk after the reload
$mark = Get-LogMark
Send-Console 'twilightforest structure_util pending_probe'
$queued = Wait-LogLine 'structure_util pending_probe queued nonce=(\d+)' 30 -Since $mark
if ($queued -notmatch 'nonce=(\d+)') { throw "no probe nonce: $queued" }
$nonce = $Matches[1]
Write-Step "pending probe nonce $nonce queued"

Restart-Server
Wait-LogLine 'structure kit: \d+ wood palettes' 60 | Out-Null
$mark = Get-LogMark
$r = Invoke-SelfTest 'structure.pending_read'
if ($r.Pass -ne 1) { throw 'structure.pending_read did not pass after the restart' }
$verified = Get-ServerLog -Since $mark | Select-String -Pattern "pending probe verified nonce=$nonce\b" | Select-Object -First 1
if (-not $verified) { throw "the probe job with nonce $nonce did not run after the reload (no 'verified' line)" }
Write-Step 'the queued job survived the restart and ran when its chunk was loaded'

# a mob scaled by a structure spawner (entity_width_max) keeps its size after it was saved and loaded
$mark = Get-LogMark
$r = Invoke-SelfTest 'structure.scale_probe'
if ($r.Pass -ne 1) { throw 'structure.scale_probe did not pass after the restart' }
$scaled = Get-ServerLog -Since $mark | Select-String -Pattern 'scale probe verified width=' | Select-Object -First 1
if (-not $scaled) { throw 'the scaled mob was not measured after the reload (no "scale probe verified" line)' }
Write-Step "the scaled mob kept its size across the restart ($($scaled.Line.Trim()))"

# the kit still places in the world after the reload
$r = Invoke-SelfTest 'structure.template_world'
if ($r.Pass -ne 1) { throw 'structure.template_world did not pass after the restart' }
