# Smoke (F0): BDS loads the mod (run.ps1 waited for "GloamingForest enabled"), the world is ready,
# the root command and its aliases answer, the foundation selftests ("core.*") pass, and the world
# reloads after a restart with its data intact.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

# /twilightforest and its aliases /tf and /tffeature
$mark = Get-LogMark
Send-Console 'twilightforest stubs'
Wait-LogLine 'contract stubs called: ' 30 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'tf stubs'
Wait-LogLine 'contract stubs called: ' 30 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'tffeature stubs'
Wait-LogLine 'contract stubs called: ' 30 -Since $mark | Out-Null

$r = Invoke-SelfTest 'core.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) core selftest(s) failed" }
if ($r.Pass -lt 15) { throw "expected at least 15 core selftests to pass, got $($r.Pass)" }

# The world reloads: stop, boot again on the same world, and the foundation still works there.
Restart-Server
foreach ($t in 'core.world_db', 'core.block_data') {
    $r = Invoke-SelfTest $t
    if ($r.Pass -ne 1) { throw "$t did not pass after the reload" }
}