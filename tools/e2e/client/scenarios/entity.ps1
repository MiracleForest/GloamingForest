# F8 (entity): the boss bar on a real client (spike S4). The selftest entity.client_fixture shows
# the fixture boss gf_test:boss next to the real player: an orange bar (0xFF8000), a colour none of
# Bedrock's 8 BossBarColor values has, with the Naga's NOTCHED_10 overlay, then a second fixture
# boss with 3 linked parts, then a boss that dies with a 100-tick death animation:
#   local / remote-mod   the client mod paints the bar in the exact RGB (TFBossBarPacket through
#                        ClientInstanceScreenModel::getBossBarColor) and draws its notches; the
#                        client logs "client boss bar <id> coloured #FF8000" and "... notched 10",
#                        the synced string list ("client entity data ... gf_test:heads") and the
#                        death time ("client boss <id> dying"): required.
#   remote-vanilla       the vanilla client shows the nearest vanilla colour (red) and never gets a
#                        companion packet.
# Screenshots: out/entity/boss-bar-<mode>.png, boss-parts-<mode>.png (the parts render on every
# client as ordinary actors) and boss-dying-<mode>.png (the dead body 2 s after the death, where
# Bedrock alone removes it after 1 s). Local mode also runs every entity selftest on the
# integrated server (the client build's hooks).
Save-Screenshot 'hud' | Out-Null
$remote = $script:Mode -ne 'local'
$hostLine = if ($remote) { '\[host\].*' } else { '' }
$clientLine = if ($remote) { '^(?!\[host\]).*' } else { '' }

if ($remote) { Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null }
else { Wait-LogLine 'GloamingForest world ready' 120 | Out-Null }
if ($script:Mode -eq 'remote-mod') { Wait-LogLine '\[host\].*companion joined' 60 | Out-Null }

# The fixture runs on the server (the integrated one in local mode, the host otherwise) and holds
# each boss for 20 seconds; the screenshots are taken meanwhile.
$mark = Get-LogMark
$command = Format-SelfTestCommand 'entity.client_fixture'
if ($remote) { Send-HostConsole $command } else { Send-SelfTestCommand $command }
Wait-LogLine "${hostLine}entity\.client_fixture: bar boss ready" 90 -Since $mark | Out-Null
if ($script:Mode -eq 'remote-vanilla') {
    Start-Sleep -Seconds 4
    if (Get-LeviLog | Select-String -Pattern '\[host\].*companion joined' -Quiet) { throw 'a vanilla client was accepted as a companion' }
    if (Get-LeviLog -Since $mark | Select-String -Pattern '^(?!\[host\]).*client boss bar' -Quiet) { throw 'the vanilla client applied a companion colour' }
} else {
    Wait-LogLine "${clientLine}client boss bar -?\d+ received \(#FF8000\)" 30 -Since $mark | Out-Null
    $hit = Wait-LogLine "${clientLine}client boss bar -?\d+ coloured #FF8000" 30 -Since $mark
    Write-Step "exact RGB applied: $hit"
    # the bar's overlay (NOTCHED_10, the Naga's) drawn over the HUD's bar
    $notch = Wait-LogLine "${clientLine}client boss bar -?\d+ notched 10 at" 30 -Since $mark
    Write-Step "notches drawn: $notch"
    # a synced value (TFDataSerializers.STRING_LIST) reached the client mod's readers
    Wait-LogLine "${clientLine}client entity data -?\d+ gf_test:heads = \[`"left`",`"center`",`"right`"\]" 30 -Since $mark | Out-Null
}
Start-Sleep -Seconds 2
Save-Screenshot "boss-bar-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}entity\.client_fixture: parts boss ready" 60 -Since $mark | Out-Null
Start-Sleep -Seconds 3
Save-Screenshot "boss-parts-$($script:Mode)" | Out-Null
# a boss with a 100-tick death animation: its body is still there 2 seconds after its death
Wait-LogLine "${hostLine}entity\.client_fixture: dying boss ready" 60 -Since $mark | Out-Null
Save-Screenshot "boss-dying-$($script:Mode)" | Out-Null
if ($script:Mode -ne 'remote-vanilla') { Wait-LogLine "${clientLine}client boss -?\d+ dying \(death time" 10 -Since $mark | Out-Null }
Wait-LogLine "${hostLine}boss: gf_test:boss left \(peaceful\)" 60 -Since $mark | Out-Null
Wait-LogLine "${hostLine}$script:SelfTestDoneRegex" 60 -Since $mark | Out-Null
$r = ConvertFrom-SelfTestLines (Get-LeviLog -Since $mark) 'entity.client_fixture'
if (-not (Register-SelfTestResult $r)) { throw 'entity.client_fixture failed' }

if (-not $remote) {
    $r = Invoke-SelfTest 'entity.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) entity selftest(s) failed" }
    if ($r.Pass -lt 27) { throw "expected at least 27 entity selftests to pass, got $($r.Pass)" }
} elseif (-not (Get-GameProcess)) {
    throw 'the game closed while connected to the host'
}
