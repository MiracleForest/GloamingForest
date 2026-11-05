# U21 (lich): the Lich and its company on a real client. The selftest lich.client_showcase puts the
# Lich (shield up: phase 1), a translucent shadow clone with glowing eyes, a Lich Minion, a Death
# Tome and a half-risen zombie in front of the player for 15 seconds, then generates a lich tower 48
# blocks ahead. Screenshots: out/lich/mobs-<mode>.png and tower-<mode>.png; compare them with TF's
# twilightlich64.png / twilightlich64_eyes.png, the zombie and the enchanting table book textures.
# Local mode also runs the lich.* selftests on the integrated server.
Save-Screenshot 'hud' | Out-Null
$remote = $script:Mode -ne 'local'
$hostLine = if ($remote) { '\[host\].*' } else { '' }

if ($remote) { Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null }
else { Wait-LogLine 'GloamingForest world ready' 120 | Out-Null }

$mark = Get-LogMark
$command = Format-SelfTestCommand 'lich.client_showcase'
if ($remote) { Send-HostConsole $command } else { Send-SelfTestCommand $command }
Wait-LogLine "${hostLine}lich\.client_showcase: mobs ready \((\d+) shown\)" 90 -Since $mark | Out-Null
Start-Sleep -Seconds 4
Save-Screenshot "mobs-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}lich\.client_showcase: tower (placed|not placed)" 120 -Since $mark | Out-Null
Start-Sleep -Seconds 8
Save-Screenshot "tower-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}SELFTEST (PASS|FAIL) lich\.client_showcase" 120 -Since $mark | Out-Null

if (-not $remote) {
    $mark = Get-LogMark
    Send-SelfTestCommand (Format-SelfTestCommand 'lich.*')
    $done = Wait-LogLine 'SELFTEST DONE pass=(\d+) fail=(\d+)' 600 -Since $mark
    if ($done -notmatch 'fail=0') { throw "lich selftests on the client build: $done" }
    Write-Step "lich selftests on the integrated server: $done"
}
