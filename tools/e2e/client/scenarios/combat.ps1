# F10 (combat): the logic runs as selftests on the server side (local: the integrated server;
# remote: the host); the client shows the container screens of the inventory backend (spike S3):
#   loot-chest.png         a vanilla chest placed with a loot table, rolled from the Java table when
#                          opened (every client)
#   native-container.png   the S3 probe block (data-driven block actor) with the injected game
#                          container: the game's container screen for a custom block (local only:
#                          the probe pack is the client mod's, a BDS host does not have it)
#   vanilla-container.png  a 27-slot container through the chest minecart proxy: the game's chest
#                          screen, what vanilla clients get (every client)
#   chest-screen.png       a container opened as a player opens it: the client mod's chest screen
#                          (local, remote-mod: Java's generic_54 layout) or, on a vanilla client,
#                          the game's chest screen through the proxy (remote-vanilla)
# combat.ui_cleanup then removes the blocks the UI tests placed beside the player.
# Open every screenshot and compare it with TF's chest GUI (vanilla's generic_54 layout).
$ready = if ($script:Mode -eq 'local') { 'GloamingForest world ready' } else { '\[host\].*GloamingForest world ready' }
Wait-LogLine $ready 120 | Out-Null
Save-Screenshot 'hud' | Out-Null

foreach ($t in 'combat.loot_tf_functions', 'combat.loot_number_rounding', 'combat.loot_set_contents', 'combat.fx', 'combat.progression') {
    $r = Invoke-SelfTest $t
    if ($r.Fail -gt 0) { throw "$t failed" }
}

# Every screen is shown and photographed even when an earlier one failed; the failures are thrown
# at the end. Esc is pressed only when a screen came up (Esc on the HUD opens the pause menu).
#   -Opened  the server's log line once the test opened the screen
#   -Screen  the client mod's trace of the screen that must come up (client mod runs only)
$screenFailures = [System.Collections.Generic.List[string]]::new()
function Show-ContainerTest([string]$Test, [string]$Shot, [string]$Opened, [string]$Screen, [switch]$Spike) {
    $mark = Get-LogMark
    $r    = Invoke-SelfTest $Test
    if ($r.Fail -gt 0) { $screenFailures.Add("$Test failed"); return }
    if ($Opened) {
        try { Wait-LogLine $Opened 30 -Since $mark | Out-Null }
        catch { $screenFailures.Add("$Test did not open its screen ($Opened)"); return }
    }
    $shown = $true
    if (Test-ClientTrace) {
        # the client mod traces the top screen: the expected one must come up
        $pattern = if ($Screen) { "client screen: $Screen" } else { 'client screen: (?!hud_screen|chat_screen)\S+' }
        try {
            $line = Wait-LogLine $pattern 20 -Since $mark
            Write-Step "screen: $line"
        } catch {
            $shown = $false
            # the spike's experiment records what the client does; it is not a failure of the unit
            if ($Spike) { Write-Step "S3: $Test opened no screen on the client (spike evidence)" }
            else { $screenFailures.Add("$Test opened no screen on the client (expected '$pattern')") }
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

Show-ContainerTest 'combat.ui_loot_chest' 'loot-chest' 'loot: unpacked gf_test:chests/fixture_chest' 'small_chest_screen'
if ($script:Mode -eq 'local') {
    # spike S3: the probe pack exists only in the client mod's folder (combat.prelaunch.ps1)
    Show-ContainerTest 'combat.ui_native_container' 'native-container' 'combat ui: native container opened' -Spike
}
Show-ContainerTest 'combat.ui_vanilla_container' 'vanilla-container' 'inventory: \S+ opened the container at .* \(vanilla screen\)' '(?!hud_screen|chat_screen|inventory\.chest)\S+'
if ($script:Mode -eq 'remote-vanilla') {
    # a vanilla client gets the game's chest screen through the proxy
    Show-ContainerTest 'combat.ui_chest_screen' 'chest-screen' 'inventory: \S+ opened the container at .* \(vanilla screen\)'
} else {
    Show-ContainerTest 'combat.ui_chest_screen' 'chest-screen' 'combat ui: chest screen opened' 'inventory\.chest'
}
# the UI tests' blocks and the fixture's container declaration go (nothing stays for later tests)
$r = Invoke-SelfTest 'combat.ui_cleanup'
if ($r.Fail -gt 0) { $screenFailures.Add('combat.ui_cleanup failed') }
if ($screenFailures.Count -gt 0) { throw ($screenFailures -join '; ') }
