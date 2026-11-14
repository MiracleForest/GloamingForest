# U21 (lich): the Lich and its company on a real client. The selftest lich.client_showcase shows, in
# stages, each facing the player (out/lich/<stage>-<mode>.png):
#   mobs        the Lich (phase 1, shield up), a translucent shadow clone with glowing eyes, two Lich
#               Minions (green: red and blue x0.5; the one with Strength x0.25), a Death Tome resting on a
#               lectern, rising zombies at 20, 60 and 100 ticks of their rise and a zombie tied to an iron
#               post by a lead;
#   projectiles a lich bolt, a lich bomb and a tome bolt in the air;
#   phase3      a Lich without shield or summons: the angry_lich particles and the phase-3 arms;
#   death60, death140  the death animation (shaking; the crown sinking);
#   tower_far, tower_yard, tower_side  a generated lich tower from afar, its yard (paths, graves,
#               lights, perimeter fence) and the shaft from the base.
# Compare them with upstream's gloaminglich64.png / gloaminglich64_eyes.png, the zombie and enchanting
# table book textures, the particle sheet and upstream's lich tower. Local mode also runs the lich.*
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
$showcase = Wait-LogLine "${hostLine}SELFTEST (PASS|FAIL) lich\.client_showcase" 180 -Since $mark
if ($showcase -match 'SELFTEST FAIL') { throw "lich.client_showcase failed: $showcase" }

if (-not $remote) {
    $mark = Get-LogMark
    Send-SelfTestCommand (Format-SelfTestCommand 'lich.*')
    $done = Wait-LogLine 'SELFTEST DONE pass=(\d+) fail=(\d+)' 900 -Since $mark
    if ($done -notmatch 'fail=0') { throw "lich selftests on the client build: $done" }
    Write-Step "lich selftests on the integrated server: $done"
}
