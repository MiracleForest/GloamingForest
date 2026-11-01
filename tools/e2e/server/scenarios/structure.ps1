# Structure kit (F7): the structure selftests (piece framework, templates, the 17 TF processors on
# blocks they act on, waterlogging, flower pots, paintings, TF's marker handlers, jigsaw assembly
# with terrain height adjustment, the template definitions API, wood palettes, block selectors,
# speleothems, TFMaze, HollowHill, the old lich tower bases, spawner mob scaling) pass with their
# golden volume hashes, the kit loaded its data, /twilightforest structure_util answers, and work
# queued for an unloaded chunk survives a restart and runs once the chunk is loaded.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
Wait-LogLine 'structure kit: \d+ wood palettes, \d+ speleothem configs' 60 | Out-Null

$r = Invoke-SelfTest 'structure.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) structure selftest(s) failed" }
if ($r.Pass -lt 20) { throw "expected at least 20 structure selftests to pass, got $($r.Pass)" }

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

# the kit still places in the world after the reload
$r = Invoke-SelfTest 'structure.template_world'
if ($r.Pass -ne 1) { throw 'structure.template_world did not pass after the restart' }
