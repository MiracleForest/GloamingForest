# F10 (combat): the logic runs as selftests on the server side (local: the integrated server;
# remote: the host); the client shows the container screens of the inventory backend (spike S3):
#   loot-chest.png        a vanilla chest placed with a loot table, rolled from the Java table when
#                         opened (every client)
#   native-container.png  the S3 probe block (data-driven block actor) with the injected game
#                         container: the game's container screen for a custom block (client mod runs)
#   chest-screen.png      a BlockData container: the client mod's chest screen (local, remote-mod)
#                         or the vanilla form (remote-vanilla)
# Open every screenshot and compare it with TF's chest GUI (vanilla's generic_54 layout).
$ready = if ($script:Mode -eq 'local') { 'GloamingForest world ready' } else { '\[host\].*GloamingForest world ready' }
Wait-LogLine $ready 120 | Out-Null
Save-Screenshot 'hud' | Out-Null

foreach ($t in 'combat.loot_tf_functions', 'combat.fx', 'combat.progression') {
    $r = Invoke-SelfTest $t
    if ($r.Fail -gt 0) { throw "$t failed" }
}

function Show-ContainerTest([string]$Test, [string]$Shot, [string]$Opened) {
    $mark = Get-LogMark
    $r    = Invoke-SelfTest $Test
    if ($r.Fail -gt 0) { throw "$Test failed" }
    if ($Opened) { Wait-LogLine $Opened 20 -Since $mark | Out-Null }
    Start-Sleep -Seconds 2
    Save-Screenshot $Shot | Out-Null
    Close-Screen
    Start-Sleep -Seconds 1
}

Show-ContainerTest 'combat.ui_loot_chest' 'loot-chest' 'combat ui: loot chest opened'
if ($script:Mode -ne 'remote-vanilla') {
    Show-ContainerTest 'combat.ui_native_container' 'native-container' 'combat ui: native container opened'
}
Show-ContainerTest 'combat.ui_chest_screen' 'chest-screen' 'combat ui: chest screen opened'
