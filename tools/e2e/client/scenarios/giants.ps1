# U29 (giants): giant blocks, clouds, giants and the beanstalk on a real client. The selftest
# giants.client_showcase works in front of the player:
#   giants-blocks-<mode>.png     the four giant blocks (cobblestone, log, leaves, obsidian) side by
#                                side: each 4 x 4 x 4 cell shows one big texture (GiantBlockModel)
#   giants-clouds-<mode>.png     wispy / fluffy / rainy / snowy cloud, and rain and snow falling from
#                                a row of rainy and snowy clouds (client mod: drops and splashes;
#                                vanilla: the server's fallback drops)
#   giants-mobs-<mode>.png       the giant miner (giant pickaxe) and the armored giant (giant sword,
#                                iron armour), 4x player model
#   giants-rumble-<mode>.png     the beanstalk grower's dust (the camera shakes for the client mod)
#   giants-beanstalk-<mode>.png  the grown beanstalk with its leaves
# Then (with U27's cloud pieces merged) the cloud castle above a forced giant_house: one-time review
# of giants.cloud_castle_hash (rule R19). Local mode also runs the giants.* selftests.
Save-Screenshot 'hud' | Out-Null
$remote = $script:Mode -ne 'local'
$hostLine = if ($remote) { '\[host\].*' } else { '' }

if ($remote) { Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null }
else { Wait-LogLine 'GloamingForest world ready' 120 | Out-Null }
if ($script:Mode -eq 'remote-mod') { Wait-LogLine '\[host\].*companion joined' 60 | Out-Null }

$mark = Get-LogMark
$command = Format-SelfTestCommand 'giants.client_showcase'
if ($remote) { Send-HostConsole $command } else { Send-SelfTestCommand $command }
Wait-LogLine "${hostLine}giants\.client_showcase: blocks ready" 90 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot "giants-blocks-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}giants\.client_showcase: clouds ready" 60 -Since $mark | Out-Null
Save-Screenshot "giants-clouds-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}giants\.client_showcase: giants ready" 60 -Since $mark | Out-Null
Save-Screenshot "giants-mobs-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}giants\.client_showcase: rumble ready" 60 -Since $mark | Out-Null
Save-Screenshot "giants-rumble-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}giants\.client_showcase: beanstalk ready" 120 -Since $mark | Out-Null
Save-Screenshot "giants-beanstalk-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}$script:SelfTestDoneRegex" 90 -Since $mark | Out-Null
$r = ConvertFrom-SelfTestLines (Get-LeviLog -Since $mark) 'giants.client_showcase'
if (-not (Register-SelfTestResult $r)) { throw 'giants.client_showcase failed' }

if (-not $remote) {
    $r = Invoke-SelfTest 'giants.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) giants selftest(s) failed" }
    if ($r.Pass -lt 11) { throw "expected at least 11 giants selftests to pass, got $($r.Pass)" }
} elseif (-not (Get-GameProcess)) {
    throw 'the game closed while connected to the host'
}
