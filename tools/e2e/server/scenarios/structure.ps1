# Structure kit (F7): the structure selftests (piece framework, templates, the 17 TF processors,
# TF's marker handlers, jigsaw assembly, wood palettes, block selectors, speleothems, TFMaze,
# HollowHill, the old lich tower bases) pass with their golden volume hashes, the kit loaded its
# data, and /twilightforest structure_util answers.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
Wait-LogLine 'structure kit: \d+ wood palettes, \d+ speleothem configs' 60 | Out-Null

$r = Invoke-SelfTest 'structure.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) structure selftest(s) failed" }
if ($r.Pass -lt 13) { throw "expected at least 13 structure selftests to pass, got $($r.Pass)" }

# the dev command lists the kit's registries
$mark = Get-LogMark
Send-Console 'twilightforest structure_util templates'
$line = Wait-LogLine 'structure kit: \d+ templates, \d+ template definitions, (\d+) wood palettes, (\d+) speleothem configs, (\d+) processors' 30 -Since $mark
Write-Step "structure_util templates: $line"
if ($line -notmatch '17 wood palettes, 6 speleothem configs') { throw "the kit did not load the 17 wood palettes and 6 speleothem configs: $line" }

# the queued block entity work of a placement survives a restart (persisted world DB key)
Restart-Server
Wait-LogLine 'structure kit: \d+ wood palettes' 60 | Out-Null
$r = Invoke-SelfTest 'structure.template_world'
if ($r.Pass -ne 1) { throw 'structure.template_world did not pass after the restart' }
