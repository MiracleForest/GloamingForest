# F12 (ui): the client frameworks' server halves on BDS. Every "ui.*" selftest: vanilla
# fallbacks (title skins, forms, DDUI refusal and sessions), native screens and the fixture for
# test companions (screen, HUD, chest, item frame, head, map, gf::fx::particle's fx.particle), the
# undo of the real-player steps (stage blocks with item frames emptied first, position, every
# inventory slot a step changed, helmet; a stage that cannot be put back keeps its snapshot), key
# forwarding (input.key), input edges and the ground state of an auth input packet, motion, movement
# validation (registration, rejection, removal) and its fresh start after server teleports,
# respawns and dimension changes (a client that never arrives is validated from the server's
# place). The client
# scenario (tools/e2e/client/scenarios/ui.ps1) covers what needs a real client. Every test runs
# twice: the second run checks that nothing the first left behind (handlers, rules, the stage)
# changes the result.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

foreach ($round in 1, 2) {
    $r = Invoke-SelfTest 'ui.*'
    if ($r.Fail -gt 0) { throw "round ${round}: $($r.Fail) ui selftest(s) failed" }
    if ($r.Pass -lt 29) { throw "round ${round}: expected at least 29 ui selftests to pass, got $($r.Pass)" }
}

# no F12 contract stub is called any more
$mark = Get-LogMark
Send-Console 'gloamingforest stubs'
$line = Wait-LogLine 'contract stubs called: ' 30 -Since $mark
foreach ($stub in 'ui.form', 'ui.ddui', 'ui.containerTitleSkin', 'movement.onInputEdge', 'movement.applyMotion', 'movement.validate') {
    if ($line -match [regex]::Escape($stub)) { throw "contract stub $stub is still called: $line" }
}
Write-Step 'no F12 contract stub was called'
