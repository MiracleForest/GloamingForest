# Synth (F3): the client plays every TF sound event once. Local: synth.play_all runs in the client
# process and plays each event through the game's own sound engine: an event the engine does not
# know fails, the engine's subtitle must be TF's, a long synthesized sound must still be playing a
# quarter second later (a file the engine cannot decode does not play) and the engine's volume and
# pitch must be TF's; synth.definitions also checks the vanilla sounds the definitions name (packs
# vanilla and vanilla_music). Remote: the BDS host runs the tests and sends every event to this
# client as PlaySoundPacket (remote-vanilla: the client only has the packs the server sent).
# Content-log errors (a sound file that does not load) fail the run. Screenshots: out/synth/{hud,played}.png
Save-Screenshot 'hud' | Out-Null
if ($script:Mode -eq 'local') {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'synth.*' -TimeoutSeconds 300
    if ($r.Fail -gt 0) { throw "$($r.Fail) synth selftest(s) failed" }
    if ($r.Pass -lt 2) { throw "expected 2 synth selftests to pass, got $($r.Pass)" }
    Wait-LogLine 'synth: played \d+ sound events on the client' 30 | Out-Null
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'synth.*' -TimeoutSeconds 300
    if ($r.Fail -gt 0) { throw "$($r.Fail) synth selftest(s) failed on the host" }
    if ($r.Pass -lt 2) { throw "expected 2 synth selftests to pass on the host, got $($r.Pass)" }
    Wait-LogLine '\[host\].*synth: played \d+ sound events to [1-9]\d* player' 30 | Out-Null
    if (-not (Get-GameProcess)) { throw 'the game closed while sounds were played' }
}
Save-Screenshot 'played' | Out-Null
