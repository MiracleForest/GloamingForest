# Structure kit (F7) on the client: the selftests pass in the local world (integrated server),
# then a fixture template, a jigsaw assembly, an old lich tower (TowerMain and the wings, bridges,
# roofs and beards it grows) and a small hollow hill are built in the flat world for the one-time
# screenshot review of the golden hashes (rule R19).
# Screenshots: out/structure/{hud,template,jigsaw,tower,tower_inside,hill,selftest}.png
Save-Screenshot 'hud' | Out-Null
if ($script:Mode -eq 'local') {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'structure.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) structure selftest(s) failed" }
    if ($r.Pass -lt 23) { throw "expected at least 23 structure selftests to pass, got $($r.Pass)" }

    # placed at the player's feet on the flat world, then looked at from the south-east
    Invoke-Teleport '/tp @s 0 -60 0'
    $mark = Get-LogMark
    Invoke-Chat '/twilightforest structure_util place "gf_test:structure/fixture" 0 0'
    Wait-LogLine 'structure_util place gf_test:structure/fixture' 30 -Since $mark | Out-Null
    Invoke-Teleport '/tp @s 6 -56 10 149 25'
    Start-Sleep -Seconds 2
    Save-Screenshot 'template' | Out-Null

    Invoke-Teleport '/tp @s 40 -60 40'
    $mark = Get-LogMark
    Invoke-Chat '/twilightforest structure_util jigsaw "gf_test:structure/jig/start" 4'
    Wait-LogLine 'structure_util jigsaw gf_test:structure/jig/start: \d+ pieces' 30 -Since $mark | Out-Null
    Invoke-Teleport '/tp @s 56 -53 58 140 18'
    Start-Sleep -Seconds 2
    Save-Screenshot 'jigsaw' | Out-Null

    # the old lich tower (the shared Tower* bases): main tower, wings, bridges, roofs, beards; the
    # camera floats (spectator) so the views from the air and inside the walls do not fall or choke
    Invoke-Teleport '/tp @s 120 -60 -80'
    Start-Sleep -Seconds 2
    $mark = Get-LogMark
    Invoke-Chat '/twilightforest structure_util piece tower'
    Wait-LogLine 'structure_util piece tower: \d+ pieces' 60 -Since $mark | Out-Null
    Invoke-Chat '/gamemode spectator'
    Invoke-Chat '/effect @s night_vision 300 1 true'
    Invoke-Teleport '/tp @s 180 -35 -15 135 -8'
    Start-Sleep -Seconds 4
    Save-Screenshot 'tower' | Out-Null
    # inside the main tower: stairs, floors, the decorated rooms
    Invoke-Teleport '/tp @s 127 -52 -73 200 25'
    Start-Sleep -Seconds 2
    Save-Screenshot 'tower_inside' | Out-Null

    # a small hollow hill dug into a stone dome, looked at from inside (stalactites, spawners, chests)
    Invoke-Teleport '/tp @s 260 -50 -80'
    Start-Sleep -Seconds 2
    $mark = Get-LogMark
    Invoke-Chat '/twilightforest structure_util piece hill'
    Wait-LogLine 'structure_util piece hill: \d+ pieces' 60 -Since $mark | Out-Null
    # (the hill's box is [246,-53,-94 .. 282,-44,-58]: the cave is around its center)
    Invoke-Teleport '/tp @s 264 -48 -64 180 12'
    Start-Sleep -Seconds 3
    Save-Screenshot 'hill' | Out-Null
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'structure.block_states'
    if ($r.Fail -gt 0) { throw 'structure.block_states failed on the host' }
}
Save-Screenshot 'selftest' | Out-Null
