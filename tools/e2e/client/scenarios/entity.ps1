# F8 (entity): the boss bar on a real client (spike S4). The fixture boss gf_test:boss has an
# orange bar (0xFF8000), a colour none of Bedrock's 8 BossBarColor values has:
#   local / remote-mod   the client mod paints the bar in the exact RGB (TFBossBarPacket); the
#                        client logs "client boss bar <id> coloured #FF8000" when it applies it.
#   remote-vanilla       the vanilla client shows the nearest vanilla colour (red) and never gets a
#                        companion packet.
# Screenshots: out/entity/boss-bar-<mode>.png. Local mode also runs the entity selftests on the
# integrated server (the client build's hooks).
Save-Screenshot 'hud' | Out-Null
# the name tag gf_parts makes the fixture behaviour add 3 linked parts (spike S4: they render on
# every client as ordinary actors)
$summon = 'summon gf_test:boss gf_parts ~4 ~ ~'
if ($script:Mode -eq 'local') {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $mark = Get-LogMark
    Invoke-Chat '/difficulty easy'
    Invoke-Chat "/$summon"
    Wait-LogLine 'client boss bar -?\d+ coloured #FF8000' 30 -Since $mark | Out-Null
    Start-Sleep -Seconds 2
    Save-Screenshot 'boss-bar-local' | Out-Null
    # peaceful: a boss without a home just leaves (BaseTFBoss#checkDespawn)
    Invoke-Chat '/difficulty peaceful'
    Wait-LogLine 'boss: gf_test:boss left \(peaceful\)' 30 -Since $mark | Out-Null
    $r = Invoke-SelfTest 'entity.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) entity selftest(s) failed" }
    if ($r.Pass -lt 12) { throw "expected at least 12 entity selftests to pass, got $($r.Pass)" }
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
    $mark = Get-LogMark
    Send-HostConsole 'difficulty easy'
    Send-HostConsole "execute as @a at @s run $summon"
    if ($script:Mode -eq 'remote-mod') {
        Wait-LogLine '\[host\].*companion joined' 60 | Out-Null
        Wait-LogLine '^(?!\[host\]).*client boss bar -?\d+ coloured #FF8000' 30 -Since $mark | Out-Null
    } else {
        Start-Sleep -Seconds 6
        if (Get-LeviLog | Select-String -Pattern '\[host\].*companion joined' -Quiet) { throw 'a vanilla client was accepted as a companion' }
        if (Get-LeviLog -Since $mark | Select-String -Pattern '^(?!\[host\]).*client boss bar' -Quiet) { throw 'the vanilla client applied a companion colour' }
    }
    Start-Sleep -Seconds 2
    Save-Screenshot "boss-bar-$($script:Mode)" | Out-Null
    Send-HostConsole 'difficulty peaceful'
    Wait-LogLine '\[host\].*boss: gf_test:boss left \(peaceful\)' 30 -Since $mark | Out-Null
    if (-not (Get-GameProcess)) { throw 'the game closed while connected to the host' }
}
