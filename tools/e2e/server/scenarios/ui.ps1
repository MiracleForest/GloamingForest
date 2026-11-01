# F12 (ui): the client frameworks' server halves on BDS. Every "ui.*" selftest: vanilla
# fallbacks (title skins, forms, DDUI refusal), native screens and the fixture for test
# companions, key forwarding (input.key), input edges, motion and movement validation. The
# client scenario (tools/e2e/client/scenarios/ui.ps1) covers what needs a real client.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$r = Invoke-SelfTest 'ui.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) ui selftest(s) failed" }
if ($r.Pass -lt 12) { throw "expected at least 12 ui selftests to pass, got $($r.Pass)" }

# the fixture screen type and the fixture key are registered on the server
$mark = Get-LogMark
Send-Console 'twilightforest stubs'
$line = Wait-LogLine 'contract stubs called: ' 30 -Since $mark
foreach ($stub in 'ui.form', 'ui.ddui', 'ui.containerTitleSkin', 'movement.onInputEdge', 'movement.applyMotion', 'movement.validate') {
    if ($line -match [regex]::Escape($stub)) { throw "contract stub $stub is still called: $line" }
}
Write-Step 'no F12 contract stub was called'
