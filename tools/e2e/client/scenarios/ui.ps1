# F12 (ui): the client frameworks in the real game (spike S5).
#
# Every `ui.*` selftest runs first, with the player untagged: it must leave the real player alone
# (no fixture shown, no screen, no stage, nothing in hand or on the head). Then the player gets the
# tag gf_ui_fixture, which opts it into the fixture's real-player steps (fixture::kPlayerTag), and:
#
# local / remote-mod: the fixture (client/ui/Fixture.cpp) draws a HUD layer, two toasts
#   (LockedBiomeToast's and MissingAdvancementToast's colours; hidden with the GUI, F1), world-space
#   lines and geometry, fog / sky / cloud providers, a TF-style ContainerScreen, item renderers in
#   an item frame (Fixed) and on the head (Head) and TF map decorations on a held and a framed map
#   (a conquered icon with TF's red X, the player icon on top); the fixture keys J (bound at mod load)
#   and K (registered late) must reach the server, a /particle and a gf::fx::particle (F10's
#   fx.particle, through F12's native sink: velocity and count) with the fixture's native particle
#   must run its factory, a jump must give JumpPressed / LeftGround / Landed edges, a movement rule
#   must correct a real client's move and a distance rule must not correct a respawn (the server
#   turns doimmediaterespawn / keepinventory on and puts their earlier values back). The framed map
#   leaves the player icon out (Java's showOnItemFrame). Blocks go on the stage in the ui selftest
#   row; ui.fixture_clear puts the stage and the player back after each step. All of it twice in
#   local mode: Classic (graphics_mode 1, set by ui.prelaunch.ps1) and Vibrant Visuals (graphics_mode
#   2, after a game restart; the restarted world may still carry the tag and the fixture's leftovers
#   from before the restart: ui.fixture_clear and /tag remove first).
#   Screenshots: out/ui/<mode>-hud-toast-world.png, <mode>-gui-hidden.png, <mode>-fog.png,
#   <mode>-screen(-clicked).png, <mode>-chest-skin.png, <mode>-chest-plain.png, <mode>-frame.png,
#   <mode>-map.png, <mode>-map-frame.png, <mode>-head.png.
# remote-vanilla: a vanilla client gets the form and the DDUI fallback of the fixture screen (no
#   companion packets) and the vanilla chests (plain titles). Screenshots: out/ui/vanilla-form.png,
#   vanilla-ddui.png, vanilla-chest-skin.png, vanilla-chest-plain.png.
# $env:GF_UI_SOFT = '1' turns the waits of the item, map and head steps into warnings (calibration
# runs that should collect every screenshot).
. (Join-Path $PSScriptRoot 'ui.graphics.ps1')

$remote = $script:Mode -ne 'local'
$hostRx = if ($remote) { '\[host\].*' } else { '' }
$soft   = $env:GF_UI_SOFT -eq '1'
$tag    = 'gf_ui_fixture'

# A wait that only warns in soft runs.
function Wait-UiLine([string]$pattern, [double]$seconds, [int]$Since) {
    try { return Wait-LogLine $pattern $seconds -Since $Since }
    catch {
        if (-not $soft) { throw }
        Write-Step "SOFT: no '$pattern' within $seconds s"
        return $null
    }
}

# Runs one fixture selftest and fails on a failing test.
function Invoke-UiTest([string]$name) {
    $r = Invoke-SelfTest $name
    if ($r.Fail -gt 0) { throw "$name failed" }
    return $r
}

# The number of tagged players the server sees (ui.fixture_players).
function Get-UiFixturePlayers {
    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_players' | Out-Null
    $line = Get-LeviLog -Since $mark | Where-Object { $_ -match 'SELFTEST INFO ui\.fixture_players: fixture players: (\d+)' } | Select-Object -Last 1
    if ($line -match 'fixture players: (\d+)') { return [int]$Matches[1] }
    return 0
}

# Opts the player into the fixture's real-player steps (the tag), checked on the server.
function Enable-UiFixturePlayer {
    for ($i = 1; $i -le 3; $i++) {
        Invoke-Chat "/tag @s add $tag"
        Start-Sleep -Milliseconds 800
        $n = Get-UiFixturePlayers
        if ($n -ge 1) { Write-Step "the player carries $tag ($n fixture player(s))"; return }
        Write-Step "the tag did not arrive (attempt $i/3)"
    }
    throw "the player never got the tag $tag"
}

function Disable-UiFixturePlayer {
    Invoke-UiTest 'ui.fixture_clear' | Out-Null
    Invoke-UiTest 'ui.fixture_hide' | Out-Null
    Invoke-Chat "/tag @s remove $tag"
    Start-Sleep -Milliseconds 800
    if ((Get-UiFixturePlayers) -ne 0) { Write-Step "warning: the player still carries $tag" }
}

# After a fixture step teleported the player to the stage: the teleport arrived and the chunks there
# are drawn (fixed waits for a vanilla client).
function Wait-UiStage([int]$Since) {
    if (Test-ClientTrace) {
        Wait-LogLine '^(?!\[host\]).*client teleported to' 20 -Since $Since | Out-Null
        Start-Sleep -Seconds 3
    } else { Start-Sleep -Seconds 6 }
}

# The `ui.*` run must not touch the untagged real player.
function Assert-UiUntouched([int]$Since) {
    $lines = @(Get-LeviLog -Since $Since)
    # (simulated test players are named gf_test_<n>; only the real player matters here)
    foreach ($rx in 'ui fixture: shown', 'screen opened: GloamingForest UI fixture', 'ui fixture: .* put back', "ui form 'GloamingForest UI fixture' sent to (?!gf_test_)", "ui ddui 'ui.fixture' sent to (?!gf_test_)") {
        $hit = $lines | Where-Object { $_ -match $rx } | Select-Object -First 1
        if ($hit) { throw "the ui.* run acted on the untagged player: $hit" }
    }
    $info = $lines | Where-Object { $_ -match 'SELFTEST INFO ui\.fixture_players: fixture players: (\d+)' } | Select-Object -Last 1
    if ($info -and $info -notmatch 'fixture players: 0') { throw "the ui.* run saw tagged players: $info" }
    if ((Test-ClientTrace) -and (Get-ClientScreen) -ne 'hud_screen') { throw "the ui.* run left a screen open: $(Get-ClientScreen)" }
    Write-Step 'the ui.* run left the untagged player alone'
}

# The vanilla container fallback: a vanilla chest titled with containerTitleSkin (a plain title:
# no JSON-UI route can skin one container on 1.26.51, spike S5) on the stage two blocks south of the
# player (the selftest teleports the player there, facing it), opened with a right click; then an
# unnamed chest at the same place. Both keep the vanilla look. Screenshots:
# out/ui/<tag>-chest-skin.png, <tag>-chest-plain.png.
function Open-UiChest([string]$test, [string]$placed, [string]$shot) {
    $mark = Get-LogMark
    Invoke-UiTest $test | Out-Null
    Wait-LogLine "${hostRx}ui fixture: $placed" 20 -Since $mark | Out-Null
    Wait-UiStage $mark
    Focus-Game
    Click 0.5 0.5 -Relative -Right
    Start-Sleep -Seconds 3
    Save-Screenshot $shot | Out-Null
    # the chest screen is open (traced by the client mod; a vanilla client is assumed to have it)
    $open = if (Test-ClientTrace) { (Get-ClientScreen) -match 'chest' } else { $true }
    if ($open) { Close-Screen } else { Write-Step "${shot}: the chest did not open (screen: $(Get-ClientScreen))" }
    Invoke-UiTest 'ui.fixture_clear' | Out-Null
}
function Invoke-UiChest([string]$mode) {
    Open-UiChest 'ui.fixture_chest' "chest '.*' placed" "$mode-chest-skin"
    Open-UiChest 'ui.fixture_chest_plain' 'plain chest placed' "$mode-chest-plain"
}

# every ui selftest (server halves and, in the client build, ui.client_*), the player untagged
Wait-LogLine "${hostRx}GloamingForest world ready" 120 | Out-Null

Invoke-Chat "/tag @s remove $tag"   # a world that kept it from an earlier run
$untagged = Get-LogMark
$r = Invoke-SelfTest 'ui.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) ui selftest(s) failed" }
if ($script:Mode -eq 'local' -and $r.Pass -lt 34) { throw "expected at least 34 ui selftests in the client build, got $($r.Pass)" }
Start-Sleep -Seconds 1
Assert-UiUntouched $untagged

Enable-UiFixturePlayer

if ($script:Mode -eq 'remote-vanilla') {
    $mark = Get-LogMark
    Invoke-UiTest 'ui.form_fallback' | Out-Null
    Wait-LogLine "\[host\].*ui form 'GloamingForest UI fixture' sent to" 30 -Since $mark | Out-Null
    Start-Sleep -Seconds 3
    Save-Screenshot 'vanilla-form' | Out-Null
    Press-Key ESC
    Wait-LogLine "\[host\].*ui form 'GloamingForest UI fixture' closed by" 30 -Since $mark | Out-Null
    Write-Step 'the vanilla client got the form fallback and closed it'
    Start-Sleep -Seconds 1

    # the DDUI fallback: shown, then closed with Esc (the server gets the close and drops the document)
    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_ddui' | Out-Null
    Wait-LogLine "\[host\].*ui ddui 'ui.fixture' sent to" 30 -Since $mark | Out-Null
    Start-Sleep -Seconds 3
    Save-Screenshot 'vanilla-ddui' | Out-Null
    Press-Key ESC
    $answer = Wait-LogLine "\[host\].*ui ddui 'ui.fixture' answered by" 30 -Since $mark
    Write-Step "the vanilla client got the DDUI fallback and closed it: $answer"
    Start-Sleep -Seconds 1

    if (Get-LeviLog | Select-String -Pattern '\[host\].*companion joined' -Quiet) { throw 'a vanilla client was accepted as a companion' }
    Invoke-UiChest 'vanilla'
    Disable-UiFixturePlayer
    return
}

# Runs the native fixture once and screenshots it; `$mode` names the graphics mode.
function Invoke-UiFixture([string]$mode) {
    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_show' | Out-Null
    $shown = Wait-LogLine 'ui fixture: shown \(' 30 -Since $mark
    Write-Step "fixture shown: $shown"
    Wait-LogLine 'ui fixture: hud layer drawn' 30 -Since $mark | Out-Null
    Wait-LogLine 'ui: toast drawn' 30 -Since $mark | Out-Null
    Wait-LogLine 'ui fixture: world lines drawn' 30 -Since $mark | Out-Null
    Start-Sleep -Seconds 2
    Save-Screenshot "$mode-hud-toast-world" | Out-Null

    # Java's ToastComponent draws nothing while the GUI is hidden (F1)
    $mark = Get-LogMark
    Focus-Game
    Press-Key F1
    Wait-LogLine 'ui: toasts not drawn while the GUI is hidden' 15 -Since $mark | Out-Null
    Start-Sleep -Seconds 1
    Save-Screenshot "$mode-gui-hidden" | Out-Null
    Press-Key F1
    Start-Sleep -Seconds 1
    Write-Step "${mode}: the toasts hide with the GUI"

    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_fog' | Out-Null
    foreach ($line in 'sky: provider fog applied', 'sky: provider volumetric fog applied', 'sky: provider drawn', 'sky: provider cloud height applied') {
        try { Wait-LogLine $line 15 -Since $mark | Out-Null; Write-Step "$mode ok: $line" }
        catch { Write-Step "${mode}: no '$line' line (recorded for S5)" }
    }
    Start-Sleep -Seconds 2
    Save-Screenshot "$mode-fog" | Out-Null

    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_screen' | Out-Null
    $open = Wait-LogLine 'screen opened: GloamingForest UI fixture' 30 -Since $mark
    Start-Sleep -Seconds 2
    Save-Screenshot "$mode-screen" | Out-Null
    # a click on container slot 4 (the compass) reaches the server's screen type
    if ($open -match 'panel=([-\d.]+),([-\d.]+) inv=[-\d.,]+ gui=([\d.]+)x([\d.]+)') {
        $px = [double]$Matches[1]; $py = [double]$Matches[2]; $gw = [double]$Matches[3]; $gh = [double]$Matches[4]
        Click (($px + 8 + 4 * 18 + 8) / $gw) (($py + 18 + 8) / $gh) -Relative
        Wait-LogLine "${hostRx}ui fixture: slot 4 clicked by" 20 -Since $mark | Out-Null
        Start-Sleep -Seconds 1
        Save-Screenshot "$mode-screen-clicked" | Out-Null
    } else { throw "no layout in '$open'" }
    Close-Screen
    Wait-LogLine "${hostRx}ui fixture: screen closed by" 20 -Since $mark | Out-Null

    Invoke-UiTest 'ui.fixture_hide' | Out-Null
}

# Item renderers in an item frame (Fixed) and on the head (Head), TF decorations on a held map.
function Invoke-UiItems([string]$mode) {
    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_frame' | Out-Null
    Wait-LogLine "${hostRx}ui fixture: item frame with gf_test:fixture_item placed" 20 -Since $mark | Out-Null
    Wait-UiStage $mark
    Wait-UiLine 'ui fixture: item frame item drawn' 20 $mark | Out-Null
    Start-Sleep -Seconds 1
    Save-Screenshot "$mode-frame" | Out-Null
    Invoke-UiTest 'ui.fixture_clear' | Out-Null

    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_map' | Out-Null
    Wait-UiStage $mark
    Wait-UiLine 'ui fixture: map decorations set on map' 30 $mark | Out-Null
    Wait-UiLine 'render: map decorations drawn' 20 0 | Out-Null       # logged once per game session
    Wait-UiLine 'render: map red X drawn over a conquered icon' 20 0 | Out-Null
    Wait-UiLine 'render: map icon drawn on top' 20 0 | Out-Null
    Start-Sleep -Seconds 2
    Save-Screenshot "$mode-map" | Out-Null
    # the same map in an item frame
    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_map_frame' | Out-Null
    Wait-LogLine "${hostRx}ui fixture: item frame with minecraft:filled_map placed" 20 -Since $mark | Out-Null
    Wait-UiStage $mark
    # Java's MapRenderer leaves the player marker (showOnItemFrame false) out on a framed map
    Wait-UiLine 'render: map decoration minecraft:player not drawn on an item frame' 20 0 | Out-Null   # once per game session
    Save-Screenshot "$mode-map-frame" | Out-Null
    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_clear' | Out-Null
    # the stack the player held before the map is back (the fixture keeps it)
    Wait-LogLine "${hostRx}ui fixture: .* put back" 20 -Since $mark | Out-Null

    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_head' | Out-Null
    Wait-UiStage $mark
    Focus-Game
    Press-VK 0x74; Start-Sleep -Milliseconds 400; Press-VK 0x74   # F5 twice: third person, front
    Wait-UiLine 'ui fixture: head item drawn' 20 $mark | Out-Null
    Start-Sleep -Seconds 2
    Save-Screenshot "$mode-head" | Out-Null
    Press-VK 0x74   # back to first person
    Start-Sleep -Milliseconds 500
    Invoke-UiTest 'ui.fixture_clear' | Out-Null
    Invoke-UiTest 'ui.fixture_hide' | Out-Null
}

# The fixture keys reach the server: J (bound at mod load, in the controls) and K (registered after
# the game built its input handler: delivered from the raw key events).
function Invoke-UiKeys {
    $mark = Get-LogMark
    Focus-Game
    Press-Key J
    Wait-LogLine "${hostRx}ui fixture: key pressed by" 20 -Since $mark | Out-Null
    Press-Key K
    Wait-LogLine "${hostRx}ui fixture: late key pressed by" 20 -Since $mark | Out-Null
    Write-Step 'the fixture keys (bound and late) reached the server'
}

# A native particle factory runs when the client spawns the effect: /particle on the server sends a
# SpawnParticleEffectPacket like every server-spawned effect.
function Invoke-UiParticle {
    $mark = Get-LogMark
    Invoke-Chat '/particle gf_test:fixture_particle ~ ~1 ~'
    $line = Wait-LogLine '^(?!\[host\]).*particles: native factory of gf_test:fixture_particle ran \(engine' 20 -Since $mark
    Write-Step "the native particle factory ran from the engine: $line"
    # gf::fx::particle (F10) for a companion: fx.particle reaches the factory through F12's native
    # sink with the call's velocity and count
    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_fx' | Out-Null
    $line = Wait-LogLine '^(?!\[host\]).*particles: native factory of gf_test:fixture_particle ran \(fx\.particle, x2 vel 0\.250 0\.500 -0\.125\)' 20 -Since $mark
    Write-Step "the native particle factory ran from gf::fx::particle: $line"
}

# The real client's auth input gives the input edges of a jump, the ground state from the packet.
function Invoke-UiEdges {
    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_edges' | Out-Null
    Focus-Game
    Press-Key SPACE 120
    $jump = Wait-LogLine "${hostRx}ui fixture: edge JumpPressed of" 20 -Since $mark
    $left = Wait-LogLine "${hostRx}ui fixture: edge LeftGround of" 20 -Since $mark
    $land = Wait-LogLine "${hostRx}ui fixture: edge Landed of" 20 -Since $mark
    Write-Step "jump edges: $jump / $left / $land"
}

# A movement rule that rejects every horizontal move of the real player: walking forward must be
# rejected on the server (the move the client claims) and corrected back.
function Invoke-UiValidate {
    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_validate' | Out-Null
    Focus-Game
    Press-Key W 1500
    Wait-LogLine "${hostRx}movement: ui.fixture_rule rejected a move of" 20 -Since $mark | Out-Null
    $corrected = Wait-LogLine "${hostRx}movement: corrected .* \(ui.fixture_rule\)" 20 -Since $mark
    Write-Step "the server rejected and corrected the client's move: $corrected"
    Start-Sleep -Seconds 2
    Invoke-UiTest 'ui.fixture_validate_off' | Out-Null
}

# A distance rule (moves of at most 4 blocks) and a respawn far from the death place: the respawn is
# a move of the server, so the rule must neither reject it nor correct the player back.
# The server turns doimmediaterespawn and keepinventory on for the step (ui.fixture_far keeps their
# values before it in the world DB) and ui.fixture_far_off puts those values back.
function Invoke-UiRespawn {
    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_far' | Out-Null
    $before = Wait-LogLine "${hostRx}ui fixture: gamerules before the respawn step: " 20 -Since $mark
    Wait-UiStage $mark
    Start-Sleep -Seconds 1
    Invoke-Chat '/kill @s'
    Start-Sleep -Seconds 5
    Focus-Game
    Press-Key W 800
    Start-Sleep -Seconds 3
    Invoke-UiTest 'ui.fixture_far_off' | Out-Null
    $after = Wait-LogLine "${hostRx}ui fixture: gamerules put back: " 20 -Since $mark
    $bad = Get-LeviLog -Since $mark | Where-Object { $_ -match 'movement: ui\.fixture_far_rule rejected|movement: corrected .*ui\.fixture_far_rule' } | Select-Object -First 1
    if ($bad) { throw "the respawn was treated as a move: $bad" }
    $was = if ($before -match 'step: (\{.*\})') { $Matches[1] } else { '?' }
    $now = if ($after -match 'put back: (\{.*\})') { $Matches[1] } else { '!' }
    if ($was -ne $now) { throw "the gamerules were not put back as they were: before $was, put back $now" }
    Invoke-UiTest 'ui.fixture_clear' | Out-Null
    Write-Step "a respawn under a distance rule caused no rejection and no correction; gamerules back to $was"
}

Invoke-UiFixture 'classic'
Invoke-UiChest 'classic'
Invoke-UiItems 'classic'
Invoke-UiKeys
Invoke-UiParticle
Invoke-UiEdges
Invoke-UiValidate
Invoke-UiRespawn
Disable-UiFixturePlayer

if (-not $remote) {
    # Vibrant Visuals: the same fixture after a restart with graphics_mode 2
    Stop-Game
    Set-UiGraphicsMode 2
    Start-Sleep -Seconds 3
    Start-Game | Out-Null
    Wait-GameReady
    Enter-TestWorld -Attempts 2
    Wait-HudReady
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    # the game was closed without a final save: the world may come back with the tag of the first
    # half (an autosave while it was on) and what the world DB kept (stage, players, gamerules); the
    # clear puts those back, then the tag goes before the fixture opts the player in again
    Invoke-UiTest 'ui.fixture_clear' | Out-Null
    Invoke-Chat "/tag @s remove $tag"
    Start-Sleep -Milliseconds 800
    if ((Get-UiFixturePlayers) -ne 0) { throw "the restarted world still carries $tag after /tag remove" }
    Write-Step 'the restarted world starts untagged'
    Enable-UiFixturePlayer
    Invoke-UiFixture 'vibrant'
    Invoke-UiChest 'vibrant'
    Invoke-UiItems 'vibrant'
    Invoke-UiKeys
    Disable-UiFixturePlayer
}
