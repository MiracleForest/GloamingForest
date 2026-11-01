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

# Every screen is shown and photographed even when an earlier one failed; the failures are thrown
# at the end. Esc is pressed only when a screen came up (Esc on the HUD opens the pause menu).
$screenFailures = [System.Collections.Generic.List[string]]::new()
function Show-ContainerTest([string]$Test, [string]$Shot, [string]$Opened, [switch]$Spike) {
    $mark = Get-LogMark
    $r    = Invoke-SelfTest $Test
    if ($r.Fail -gt 0) { $screenFailures.Add("$Test failed"); return }
    if ($Opened) { Wait-LogLine $Opened 30 -Since $mark | Out-Null }
    $shown = $true
    if (Test-ClientTrace) {
        # the client mod traces the top screen: a container / form / mod screen must come up
        try {
            $line = Wait-LogLine 'client screen: (?!hud_screen|chat_screen)\S+' 20 -Since $mark
            Write-Step "screen: $line"
        } catch {
            $shown = $false
            # the spike's experiment records what the client does; it is not a failure of the unit
            if ($Spike) { Write-Step "S3: $Test opened no screen on the client (spike evidence)" }
            else { $screenFailures.Add("$Test opened no screen on the client") }
        }
    } else {
        Start-Sleep -Seconds 3 # vanilla client: no trace
    }
    Start-Sleep -Seconds 2
    Save-Screenshot $Shot | Out-Null
    if (-not $shown) { return }
    $closed = Get-LogMark
    Close-Screen
    if (Test-ClientTrace) {
        try { Wait-LogLine 'client screen: hud_screen' 10 -Since $closed | Out-Null } catch { Write-Step "no hud screen after closing $Shot" }
    }
    Start-Sleep -Seconds 1
}

Show-ContainerTest 'combat.ui_loot_chest' 'loot-chest' 'loot: unpacked gf_test:chests/fixture_chest'
if ($script:Mode -ne 'remote-vanilla') {
    Show-ContainerTest 'combat.ui_native_container' 'native-container' 'combat ui: native container opened' -Spike
}
Show-ContainerTest 'combat.ui_chest_screen' 'chest-screen' 'combat ui: chest screen opened'
if ($screenFailures.Count -gt 0) { throw ($screenFailures -join '; ') }
