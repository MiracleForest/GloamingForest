# U24 (hydra): the Hydra, its mortar, the fire jets and smokers on a real client. The selftest
# hydra.client_showcase spawns a hydra 22 blocks in front of the player (its 44 part actors draw
# the heads and necks with TF's hydra4 texture), makes head 0 breathe fire (large_flame), places a
# fire jet (flaming), an encased fire jet and an encased smoker on redstone and a smoker beside
# it, and fires a mortar. Every client draws the same: the parts are ordinary actors whose
# properties (part index, active, mouth, red) drive the geometry and animations, so the vanilla
# client in remote-vanilla sees the host's resource pack do it.
# Screenshots: out/hydra*/hydra-<mode>.png (idle heads), hydra-flame-<mode>.png (fire breath),
# hydra-jets-<mode>.png (jets and smokers), hydra-mortar-<mode>.png (the mortar in flight).
# Local mode also runs the hydra.* selftests on the integrated server.
Save-Screenshot 'hud' | Out-Null
$remote = $script:Mode -ne 'local'
$hostLine = if ($remote) { '\[host\].*' } else { '' }

if ($remote) { Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null }
else { Wait-LogLine 'GloamingForest world ready' 120 | Out-Null }
if ($script:Mode -eq 'remote-mod') { Wait-LogLine '\[host\].*companion joined' 60 | Out-Null }

$mark = Get-LogMark
$command = Format-SelfTestCommand 'hydra.client_showcase'
if ($remote) { Send-HostConsole $command } else { Send-SelfTestCommand $command }
Wait-LogLine "${hostLine}hydra\.client_showcase: hydra ready" 90 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot "hydra-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}hydra\.client_showcase: flame ready" 60 -Since $mark | Out-Null
Save-Screenshot "hydra-flame-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}hydra\.client_showcase: jets ready" 60 -Since $mark | Out-Null
Save-Screenshot "hydra-jets-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}hydra\.client_showcase: mortar ready" 60 -Since $mark | Out-Null
Save-Screenshot "hydra-mortar-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}$script:SelfTestDoneRegex" 60 -Since $mark | Out-Null
$r = ConvertFrom-SelfTestLines (Get-LeviLog -Since $mark) 'hydra.client_showcase'
if (-not (Register-SelfTestResult $r)) { throw 'hydra.client_showcase failed' }

if (-not $remote) {
    $r = Invoke-SelfTest 'hydra.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) hydra selftest(s) failed" }
    if ($r.Pass -lt 12) { throw "expected at least 12 hydra selftests to pass, got $($r.Pass)" }
} elseif (-not (Get-GameProcess)) {
    throw 'the game closed while connected to the host'
}
