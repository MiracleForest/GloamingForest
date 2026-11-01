# F12 (ui): the client frameworks' server halves on BDS. Every "ui.*" selftest: vanilla
# fallbacks (title skins, forms, DDUI refusal and sessions), native screens and the fixture for
# test companions (screen, HUD, chest, item frame, head, map), key forwarding (input.key), input
# edges, motion and movement validation (registration, rejection, removal). The client scenario
# (tools/e2e/client/scenarios/ui.ps1) covers what needs a real client. Every test runs twice: the
# second run checks that nothing the first left behind (handlers, rules) changes the result.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

foreach ($round in 1, 2) {
    $r = Invoke-SelfTest 'ui.*'
    if ($r.Fail -gt 0) { throw "round ${round}: $($r.Fail) ui selftest(s) failed" }
    if ($r.Pass -lt 22) { throw "round ${round}: expected at least 22 ui selftests to pass, got $($r.Pass)" }
}

# no F12 contract stub is called any more
$mark = Get-LogMark
Send-Console 'twilightforest stubs'
$line = Wait-LogLine 'contract stubs called: ' 30 -Since $mark
foreach ($stub in 'ui.form', 'ui.ddui', 'ui.containerTitleSkin', 'movement.onInputEdge', 'movement.applyMotion', 'movement.validate') {
    if ($line -match [regex]::Escape($stub)) { throw "contract stub $stub is still called: $line" }
}
Write-Step 'no F12 contract stub was called'
