# Worldgen (F5) on the client build: the same selftests as the server (the fixture feature and the
# in-memory pipeline hash identically on both builds), the 200-chunk timing logged by the client's
# integrated server, and one screenshot. Screenshots: out/worldgen/{world,selftest}.png
Save-Screenshot 'world' | Out-Null
if ($script:Mode -eq 'local') {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'worldgen.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) worldgen selftest(s) failed" }
    if ($r.Pass -lt 10) { throw "expected 10 worldgen selftests to pass, got $($r.Pass)" }
    if (-not (Get-LeviLog | Select-String -Pattern 'worldgen timing: 200 chunks' -Quiet)) { throw 'no 200-chunk timing line logged' }
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'worldgen.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) worldgen selftest(s) failed on the host" }
    Start-Sleep -Seconds 5
    if (-not (Get-GameProcess)) { throw 'the game closed while connected to the host' }
}
Save-Screenshot 'selftest' | Out-Null
