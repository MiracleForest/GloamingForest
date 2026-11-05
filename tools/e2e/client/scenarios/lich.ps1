# U21 (lich): the Lich and its company on a real client. The selftest lich.client_showcase shows, in
# stages, each facing the player (out/lich/<stage>-<mode>.png):
#   mobs        the Lich (phase 1, shield up), a translucent shadow clone with glowing eyes, a Lich
#               Minion (it burns in daylight, a Zombie as in TF), a Death Tome resting on a lectern and
#               rising zombies at 20, 60 and 100 ticks of their rise;
#   projectiles a lich bolt, a lich bomb and a tome bolt in the air;
#   phase3      a Lich without shield or summons: the angry_lich particles and the phase-3 arms;
#   death60, death140  the death animation (shaking; the crown sinking);
#   tower_far, tower_yard, tower_side  a generated lich tower from afar, its yard (paths, graves,
#               lights, perimeter fence) and the shaft from the base.
# Compare them with TF's twilightlich64.png / twilightlich64_eyes.png, the zombie and enchanting
# table book textures, the particle sheet and TF's lich tower. Local mode also runs the lich.*
# selftests on the integrated server.
Save-Screenshot 'hud' | Out-Null
$remote = $script:Mode -ne 'local'
$hostLine = if ($remote) { '\[host\].*' } else { '' }

if ($remote) { Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null }
else { Wait-LogLine 'GloamingForest world ready' 120 | Out-Null }

$mark = Get-LogMark
$command = Format-SelfTestCommand 'lich.client_showcase'
if ($remote) { Send-HostConsole $command } else { Send-SelfTestCommand $command }
foreach ($stage in 'mobs', 'projectiles', 'phase3', 'death60', 'death140', 'tower_far', 'tower_yard', 'tower_side') {
    Wait-LogLine "${hostLine}lich\.client_showcase: stage $stage" 240 -Since $mark | Out-Null
    Start-Sleep -Seconds 2
    Save-Screenshot "$stage-$($script:Mode)" | Out-Null
}
Wait-LogLine "${hostLine}SELFTEST (PASS|FAIL) lich\.client_showcase" 180 -Since $mark | Out-Null

if (-not $remote) {
    $mark = Get-LogMark
    Send-SelfTestCommand (Format-SelfTestCommand 'lich.*')
    $done = Wait-LogLine 'SELFTEST DONE pass=(\d+) fail=(\d+)' 900 -Since $mark
    if ($done -notmatch 'fail=0') { throw "lich selftests on the client build: $done" }
    Write-Step "lich selftests on the integrated server: $done"
}
