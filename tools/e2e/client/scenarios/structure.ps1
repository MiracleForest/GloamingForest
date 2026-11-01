# Structure kit (F7) on the client: the selftests pass in the local world (integrated server),
# then a fixture template, a jigsaw assembly and a lich tower wing are placed in front of the
# player for the one-time screenshot review of the golden hashes (rule R19).
# Screenshots: out/structure/{hud,template,jigsaw,tower}.png
Save-Screenshot 'hud' | Out-Null
if ($script:Mode -eq 'local') {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'structure.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) structure selftest(s) failed" }
    if ($r.Pass -lt 13) { throw "expected at least 13 structure selftests to pass, got $($r.Pass)" }

    # a view point above the flat world, looking north-east at the placements
    Invoke-Teleport '/tp @s 0 -55 12 -150 30'
    $mark = Get-LogMark
    Invoke-Chat '/twilightforest structure_util place gf_test:structure/fixture 0 0'
    Wait-LogLine 'structure_util place gf_test:structure/fixture' 30 -Since $mark | Out-Null
    Start-Sleep -Seconds 2
    Save-Screenshot 'template' | Out-Null

    Invoke-Teleport '/tp @s 40 -50 30 -150 35'
    $mark = Get-LogMark
    Invoke-Chat '/twilightforest structure_util jigsaw gf_test:structure/jig/start 4'
    Wait-LogLine 'structure_util jigsaw gf_test:structure/jig/start: \d+ pieces' 30 -Since $mark | Out-Null
    Start-Sleep -Seconds 2
    Save-Screenshot 'jigsaw' | Out-Null
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'structure.block_states'
    if ($r.Fail -gt 0) { throw 'structure.block_states failed on the host' }
}
Save-Screenshot 'selftest' | Out-Null
