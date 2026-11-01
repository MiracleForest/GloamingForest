# Smoke (F0): the client mod is enabled (run.ps1 waited for "GloamingForest enabled"), the player
# is in the GF-Test world (local), or on the BDS host (remote modes), and the foundation selftests
# pass on the server side. Screenshots: out/smoke/{hud,selftest}.png
Save-Screenshot 'hud' | Out-Null
if ($script:Mode -eq 'local') {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'core.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) core selftest(s) failed" }
    if ($r.Pass -lt 15) { throw "expected at least 15 core selftests to pass, got $($r.Pass)" }
} else {
    # remote: the host runs the mod; a vanilla client (remote-vanilla) must stay connected and
    # must never receive a companion packet (the server logs every accepted companion)
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'core.net'
    if ($r.Fail -gt 0) { throw 'core.net failed on the host' }
    if ($script:Mode -eq 'remote-vanilla') {
        if (Get-LeviLog | Select-String -Pattern '\[host\].*companion joined' -Quiet) { throw 'a vanilla client was accepted as a companion' }
    } else {
        Wait-LogLine '\[host\].*companion joined' 60 | Out-Null
    }
    Start-Sleep -Seconds 5
    if (-not (Get-GameProcess)) { throw 'the game closed while connected to the host' }
    if (Get-LeviLog | Select-String -Pattern 'disconnect|Disconnected' -Quiet) { Write-Step 'warning: a disconnect line was logged' }
}
Save-Screenshot 'selftest' | Out-Null