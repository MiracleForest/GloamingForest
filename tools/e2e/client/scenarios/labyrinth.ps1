# U23 (labyrinth): the labyrinth, the Minoshroom, the minotaurs and the maze slime on a real
# client. The selftest labyrinth.client_showcase generates a level 1 maze (rooms, dead ends,
# corridors: mazestone walls, mosaic floors, fences, iron bars, roots, fountains, chests) in a flat
# area open to the sky, the boss room (the minoshroom spawner between four mushroom chest shelves)
# and the vault beside it, and shows the Minoshroom (TF's minoshroomtaur texture, the axe in hand),
# a minotaur, three maze slimes of size 1, 2 and 4 (the translucent outer layer) and the maze slime
# block; then the Minoshroom's ground slam pose and the items and spawn eggs in the hotbar.
# Screenshots: out/labyrinth*/labyrinth-maze-<mode>.png (the maze from above),
# labyrinth-corridor-<mode>.png (a corridor), labyrinth-rooms-<mode>.png (boss room and vault),
# labyrinth-mobs-<mode>.png (the mobs and the block), labyrinth-slam-<mode>.png (the slam pose),
# labyrinth-items-<mode>.png (hotbar). Every client draws the same: the mobs' animations read the
# synced entity properties (charging, ground_attack, ground_charge), so a vanilla client in
# remote-vanilla sees the host's resource pack do it.
# Local mode also runs the labyrinth.* selftests on the integrated server.
Save-Screenshot 'hud' | Out-Null
$remote = $script:Mode -ne 'local'
$hostLine = if ($remote) { '\[host\].*' } else { '' }

if ($remote) { Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null }
else { Wait-LogLine 'GloamingForest world ready' 120 | Out-Null }
if ($script:Mode -eq 'remote-mod') { Wait-LogLine '\[host\].*companion joined' 60 | Out-Null }

$mark = Get-LogMark
$command = Format-SelfTestCommand 'labyrinth.client_showcase'
if ($remote) { Send-HostConsole $command } else { Send-SelfTestCommand $command }
foreach ($step in 'maze', 'corridor', 'rooms', 'mobs', 'slam', 'items') {
    Wait-LogLine "${hostLine}labyrinth\.client_showcase: $step ready" 240 -Since $mark | Out-Null
    Start-Sleep -Seconds 1
    Save-Screenshot "labyrinth-$step-$($script:Mode)" | Out-Null
}
Wait-LogLine "${hostLine}$script:SelfTestDoneRegex" 90 -Since $mark | Out-Null
$r = ConvertFrom-SelfTestLines (Get-LeviLog -Since $mark) 'labyrinth.client_showcase'
if (-not (Register-SelfTestResult $r)) { throw 'labyrinth.client_showcase failed' }

if (-not $remote) {
    $r = Invoke-SelfTest 'labyrinth.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) labyrinth selftest(s) failed" }
    if ($r.Pass -lt 11) { throw "expected at least 11 labyrinth selftests to pass, got $($r.Pass)" }
} elseif (-not (Get-GameProcess)) {
    throw 'the game closed while connected to the host'
}
