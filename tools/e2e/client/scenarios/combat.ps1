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
#   upsrc-double-chest.png    upstream's gloaming oak double chest (F11's families declare upstream's chests as this
#                          contract's containers), right-clicked: the client mod's 54-slot "Large
#                          Chest", the right half's slots first (local, remote-mod); on a vanilla
#                          client the game's large chest screen with the same slots (remote-vanilla:
#                          the chest minecart proxy behind a chest pair of the client)
#   upsrc-double-chest-taken.png  remote-vanilla: the large chest screen after two clicks moved the
#                          naga scales from screen slot 0 to the first hotbar slot (the host's
#                          combat.ui_gf_double_chest_taken checks the right half and the inventory)
#   upsrc-trapped-chest.png   upstream's canopy trapped chest, right-clicked: the client mod's chest screen, the
#                          game's chest screen through the proxy on a vanilla client
#   fx-particles.png       upstream's large_flame through gf::fx in front of the player: the client mod gets
#                          fx.particle (F12's native sink first, else the resource pack effect), a
#                          vanilla client the game's particle packets
#   fx-portal.png          upstream's portal particles (GloamingPortalBlock#animateTick, minecraft:portal) over a
#                          3-block row ahead: specks rising above it and falling back onto it
#                          (Java's PortalParticle: F10's own effect gf:portal with variable.direction,
#                          Java's blue-violet (0.9f, 0.3f, 1) * f, f = 0.4..1, not Bedrock's pink
#                          portal_directional)
#   fx-vanilla.png         every Java particle type gf::fx maps, 3 blocks ahead; the content log must
#                          hold no particle error (variables the effects read are all set)
#   fx-legacy.png          left to right: red dust (DustParticleOptions.REDSTONE) and purple dust
#                          (TrollsteinnBlock's colour), both F10's gf:dust (Java's DustParticle: hovering,
#                          drifting up, 0.4..2 s), gloaming oak log item particles (a block item's IconCrack),
#                          still crits scattering, poofs rising (explosion_particle's -velocity * 10)
#   missing-advancement-toast.png  upstream's MissingAdvancementToast for progress_lich: the client mod's
#                          toast through F12 (UPPER_TEXT and the title in white, the lich tower
#                          miniature), the game's toast on a vanilla client
# combat.ui_cleanup then removes the blocks the UI tests placed beside the player.
# Open every screenshot and compare it with upstream's chest GUI (vanilla's generic_54 layout) and upstream's
# MissingAdvancementToast (the advancement toast frame, item at 6,8, text at 25,7 and 25,18).
$ready = if ($script:Mode -eq 'local') { 'GloamingForest world ready' } else { '\[host\].*GloamingForest world ready' }
Wait-LogLine $ready 120 | Out-Null
Save-Screenshot 'hud' | Out-Null

foreach ($t in 'combat.loot_gf_functions', 'combat.loot_number_rounding', 'combat.loot_set_contents', 'combat.fx', 'combat.fx_effect_variables', 'combat.progression',
    'combat.missing_advancement', 'combat.damage_type_tags', 'combat.loot_tags') {
    $r = Invoke-SelfTest $t
    if ($r.Fail -gt 0) { throw "$t failed" }
}
if ($script:Mode -eq 'local') {
    # the client mod's own side (client build only): the toast F12 queues for showMissing, and
    # fx.particle through F12's native particle sink
    foreach ($t in 'combat.client_missing_toast', 'combat.client_fx_native') {
        $r = Invoke-SelfTest $t
        if ($r.Fail -gt 0) { throw "$t failed" }
        if ($r.Pass -ne 1) { throw "$t did not run" }
    }
}

# Every screen is shown and photographed even when an earlier one failed; the failures are thrown
# at the end. Esc is pressed only when a screen came up (Esc on the HUD opens the pause menu).
#   -Opened  the server's log line once the test opened the screen
#   -Screen  the client mod's trace of the screen that must come up (client mod runs only)
$screenFailures = [System.Collections.Generic.List[string]]::new()
#   -Interact  runs on the open screen after the screenshot (clicks, a check), before it closes
function Show-ContainerTest([string]$Test, [string]$Shot, [string]$Opened, [string]$Screen, [switch]$Spike, [scriptblock]$Interact) {
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
    if ($Interact) {
        try { & $Interact } catch { $screenFailures.Add("$Test interaction: $_") }
    }
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
# upstream's chests (F11's declared containers) opened by a right click
if ($script:Mode -eq 'remote-vanilla') {
    # the game's large chest screen: the minecart proxy behind a chest pair of the client. Its item
    # requests reach the halves: a click takes the right half's first stack (screen slot 0, the
    # naga scales), a click puts it in the first hotbar slot; the host checks both sides (a host
    # console selftest: the chat would cover the screen)
    $takeStack = {
        foreach ($at in @(@(0.3664, 0.2333), @(0.3664, 0.8194))) {
            Move-Mouse $at[0] $at[1] -Relative
            Start-Sleep -Milliseconds 300
            Click $at[0] $at[1] -Relative
            Start-Sleep -Milliseconds 700
        }
        Save-Screenshot 'upsrc-double-chest-taken' | Out-Null
        $m = Get-LogMark
        Send-HostConsole 'gloamingforest selftest "combat.ui_gf_double_chest_taken"'
        $line = Wait-LogLine 'SELFTEST (PASS|FAIL) combat\.ui_gf_double_chest_taken.*' 30 -Since $m
        Write-Step "large chest item request: $line"
        if ($line -match 'SELFTEST FAIL') { throw $line }
    }
    Show-ContainerTest 'combat.ui_gf_double_chest' 'upsrc-double-chest' 'inventory: \S+ opened the container at .* \(vanilla screen, large chest\)' -Interact $takeStack
    Show-ContainerTest 'combat.ui_gf_trapped_chest' 'upsrc-trapped-chest' 'inventory: \S+ opened the container at .* \(vanilla screen\)'
} else {
    Show-ContainerTest 'combat.ui_gf_double_chest' 'upsrc-double-chest' 'combat ui: upsrc double chest opened for' 'inventory\.chest'
    Show-ContainerTest 'combat.ui_gf_trapped_chest' 'upsrc-trapped-chest' 'combat ui: upsrc trapped chest opened for' 'inventory\.chest'
}

# fx particles in front of the player (5 s)
$mark = Get-LogMark
$r = Invoke-SelfTest 'combat.ui_fx_particles'
if ($r.Fail -gt 0) { $screenFailures.Add('combat.ui_fx_particles failed') }
if (Test-ClientTrace) {
    # the client mod's fx.particle handler: F12's native factory or the resource pack effect (one
    # line per particle id, route and probe: this test's particles carry the probe
    # combat.ui_fx_particles, so the line is this test's own)
    try { Write-Step "fx: $(Wait-LogLine 'fx: client particle gloamingforest:large_flame .*\(probe combat\.ui_fx_particles\)' 20 -Since $mark)" }
    catch { $screenFailures.Add('the client mod did not handle fx.particle for gloamingforest:large_flame of combat.ui_fx_particles') }
}
Start-Sleep -Seconds 2 # the emitter runs from the next tick for 5 s; the chat that ran the test closes
Save-Screenshot 'fx-particles' | Out-Null

# upstream's portal particles (5 s) and every mapped vanilla particle type (2 s). The game writes its
# content log only as it closes, so a missing molang variable ("unable to find member variable",
# "unhandled request for unknown variable": one error per particle frame) cannot be seen here; the
# run checks the log after the game closed (Get-ContentLogProblems: any error fails the run), and
# the selftest combat.fx_effect_variables checks the variables every mapped effect gets.
$r = Invoke-SelfTest 'combat.ui_fx_portal'
if ($r.Fail -gt 0) { $screenFailures.Add('combat.ui_fx_portal failed') }
Start-Sleep -Seconds 2
Save-Screenshot 'fx-portal' | Out-Null
Start-Sleep -Seconds 4 # the portal emitter ends before the next one
$r = Invoke-SelfTest 'combat.ui_fx_vanilla'
if ($r.Fail -gt 0) { $screenFailures.Add('combat.ui_fx_vanilla failed') }
Start-Sleep -Seconds 1
Save-Screenshot 'fx-vanilla' | Out-Null
Start-Sleep -Seconds 2
$r = Invoke-SelfTest 'combat.ui_fx_legacy'
if ($r.Fail -gt 0) { $screenFailures.Add('combat.ui_fx_legacy failed') }
Start-Sleep -Seconds 1
Save-Screenshot 'fx-legacy' | Out-Null
Start-Sleep -Seconds 2

# the missing advancement toast (shown 10 s; 600 ms slide in)
$mark = Get-LogMark
$r = Invoke-SelfTest 'combat.ui_missing_toast'
if ($r.Fail -gt 0) { $screenFailures.Add('combat.ui_missing_toast failed') }
if (Test-ClientTrace) {
    try {
        Write-Step "toast: $(Wait-LogLine 'client toast: missing advancement gloamingforest:progress_lich' 20 -Since $mark)"
        Wait-LogLine 'ui: toast drawn: ' 20 -Since $mark | Out-Null
    } catch { $screenFailures.Add('the client mod did not show the missing advancement toast') }
} else {
    Wait-LogLine 'combat ui: missing advancement toast sent to' 20 -Since $mark | Out-Null
}
Start-Sleep -Seconds 2
Save-Screenshot 'missing-advancement-toast' | Out-Null

# the UI tests' blocks and the fixture's container declaration go (nothing stays for later tests)
$r = Invoke-SelfTest 'combat.ui_cleanup'
if ($r.Fail -gt 0) { $screenFailures.Add('combat.ui_cleanup failed') }
if ($screenFailures.Count -gt 0) { throw ($screenFailures -join '; ') }
