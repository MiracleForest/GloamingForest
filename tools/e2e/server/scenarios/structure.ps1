# Structure kit (F7): the structure selftests (piece framework, templates, the 17 TF processors on
# blocks they act on, update_marking shape updates, waterlogging, flower pots, paintings, TF's
# marker handlers, jigsaw assembly with terrain height adjustment, the template definitions API,
# wood palettes, block selectors, speleothems, TFMaze, HollowHill, the old lich tower bases, spawner
# mob scaling and custom spawn rules) pass with their golden volume hashes, the kit loaded its data,
# /twilightforest structure_util answers, an outline expires without harm, and across a restart
# work queued for an unloaded chunk runs once the chunk is loaded, a forced landmark start reloads
# its pieces through F6's piece types (then is removed), a scaled mob keeps its size and
# structure.count_loot passes again in the same world (its forced landmark start is removed when it ends).
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
Wait-LogLine 'structure kit: \d+ wood palettes, \d+ speleothem configs' 60 | Out-Null

# the next structure.scale_probe leaves a scaled calf for the run after the restart (plain runs
# leave nothing behind)
$mark = Get-LogMark
Send-Console 'twilightforest structure_util scale_probe'
Wait-LogLine 'structure_util scale_probe armed' 30 -Since $mark | Out-Null

$r = Invoke-SelfTest 'structure.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) structure selftest(s) failed" }
if ($r.Pass -lt 31) { throw "expected at least 31 structure selftests to pass, got $($r.Pass)" }
$probeWritten = Get-ServerLog | Select-String -Pattern 'scale probe written id=(-?\d+)' | Select-Object -Last 1
if (-not $probeWritten) { throw 'structure.scale_probe did not leave its scaled mob for the restart' }

# structure.count_loot removes the landmark start it forced (F6's ScopedStart): a second run in the
# same world, without a restart, passes again
$r = Invoke-SelfTest 'structure.count_loot'
if ($r.Pass -ne 1) { throw 'structure.count_loot did not pass a second time in the same world (without a restart)' }
Write-Step 'structure.count_loot passed again in the same world'

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
# structure.pending_read loads that chunk after the reload; the probe also forces a fixture
# landmark start (unplaced, 512 blocks further south) whose pieces must load through F6's piece
# factories after the reload
$mark = Get-LogMark
Send-Console 'twilightforest structure_util pending_probe'
$queued = Wait-LogLine 'structure_util pending_probe queued nonce=(\d+)' 30 -Since $mark
if ($queued -notmatch 'nonce=(\d+)') { throw "no probe nonce: $queued" }
$nonce = $Matches[1]
if ($queued -notmatch 'start probe stored') { throw "the probe did not store its fixture landmark start: $queued" }
Write-Step "pending probe nonce $nonce queued, fixture landmark start stored"

Restart-Server
Wait-LogLine 'structure kit: \d+ wood palettes' 60 | Out-Null
$mark = Get-LogMark
$r = Invoke-SelfTest 'structure.pending_read'
if ($r.Pass -ne 1) { throw 'structure.pending_read did not pass after the restart' }
$verified = Get-ServerLog -Since $mark | Select-String -Pattern "pending probe verified nonce=$nonce\b" | Select-Object -First 1
if (-not $verified) { throw "the probe job with nonce $nonce did not run after the reload (no 'verified' line)" }
Write-Step 'the queued job survived the restart and ran when its chunk was loaded'
$startVerified = Get-ServerLog -Since $mark | Select-String -Pattern 'start probe verified gf_test:landmark_fixture_open pieces=gf_test:landmark_fixture_shell gf_test:landmark_fixture_porch' | Select-Object -First 1
if (-not $startVerified) { throw "the probe's fixture landmark start did not reload with its pieces (no 'start probe verified' line)" }
# the start was reloaded from the world DB with the restart: none of its pieces met an unknown piece type
$unknown = Get-ServerLog | Select-String -Pattern "unknown piece type 'gf_test:landmark_fixture" | Select-Object -First 1
if ($unknown) { throw "a reloaded fixture landmark start has an unknown piece type: $($unknown.Line.Trim())" }
Write-Step 'the forced fixture landmark start reloaded through F6''s piece types and was removed'

# a mob scaled by a structure spawner (entity_width_max) keeps its size after it was saved and
# loaded, and the kit puts the factor on the size the game sets from its definition afterwards
# (the calf grows up)
$mark = Get-LogMark
$r = Invoke-SelfTest 'structure.scale_probe'
if ($r.Pass -ne 1) { throw 'structure.scale_probe did not pass after the restart' }
$scaled = Get-ServerLog -Since $mark | Select-String -Pattern 'scale probe verified width=' | Select-Object -First 1
if (-not $scaled) { throw 'the scaled mob was not measured after the reload (no "scale probe verified" line)' }
Write-Step "the scaled mob kept its size across the restart ($($scaled.Line.Trim()))"

# the kit still places in the world after the reload
$r = Invoke-SelfTest 'structure.template_world'
if ($r.Pass -ne 1) { throw 'structure.template_world did not pass after the restart' }

# structure.count_loot passes after the restart too
$r = Invoke-SelfTest 'structure.count_loot'
if ($r.Pass -ne 1) { throw 'structure.count_loot did not pass a second time in the same world' }
Write-Step 'structure.count_loot passed after the restart'
