# F12 (ui): the client frameworks in the real game (spike S5).
#
# local / remote-mod: the fixture (client/ui/Fixture.cpp) draws a HUD layer, a toast, world-space
#   lines and geometry, fog / sky / cloud providers, a TF-style ContainerScreen, item renderers in
#   an item frame (Fixed) and on the head (Head) and TF map decorations on a held map; the fixture
#   key (J) must reach the server and a movement rule must correct a real client's move. All of
#   it twice in local mode: Classic (graphics_mode 1, set by ui.prelaunch.ps1) and Vibrant Visuals
#   (graphics_mode 2, after a game restart).
#   Screenshots: out/ui/<mode>-hud-toast-world.png, <mode>-fog.png, <mode>-screen(-clicked).png,
#   <mode>-chest-skin.png, <mode>-chest-plain.png, <mode>-frame.png, <mode>-map.png,
#   <mode>-map-frame.png, <mode>-head.png.
# remote-vanilla: a vanilla client gets the form and the DDUI fallback of the fixture screen (no
#   companion packets) and the vanilla chests (plain titles). Screenshots: out/ui/vanilla-form.png,
#   vanilla-ddui.png, vanilla-chest-skin.png, vanilla-chest-plain.png.
# $env:GF_UI_SOFT = '1' turns the waits of the item, map and head steps into warnings (calibration
# runs that should collect every screenshot).
. (Join-Path $PSScriptRoot 'ui.graphics.ps1')

$remote = $script:Mode -ne 'local'
$hostRx = if ($remote) { '\[host\].*' } else { '' }
$soft   = $env:GF_UI_SOFT -eq '1'

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

# The vanilla container fallback: a vanilla chest titled with containerTitleSkin (a plain title:
# no JSON-UI route can skin one container on 1.26.51, spike S5), two blocks south of the player
# (the selftest turns the player towards it), opened with a right click; then an unnamed chest at
# the same place. Both keep the vanilla look. Screenshots: out/ui/<tag>-chest-skin.png,
# <tag>-chest-plain.png.
function Open-UiChest([string]$test, [string]$placed, [string]$shot) {
    $mark = Get-LogMark
    Invoke-UiTest $test | Out-Null
    Wait-LogLine "${hostRx}ui fixture: $placed" 20 -Since $mark | Out-Null
    Start-Sleep -Seconds 2
    Focus-Game
    Click 0.5 0.5 -Relative -Right
    Start-Sleep -Seconds 3
    Save-Screenshot $shot | Out-Null
    # the chest screen is open (traced by the client mod; a vanilla client is assumed to have it)
    $open = if (Test-ClientTrace) { (Get-ClientScreen) -match 'chest' } else { $true }
    if ($open) { Close-Screen } else { Write-Step "${shot}: the chest did not open (screen: $(Get-ClientScreen))" }
    Invoke-SelfTest 'ui.fixture_clear' | Out-Null
}
function Invoke-UiChest([string]$tag) {
    Open-UiChest 'ui.fixture_chest' "chest '.*' placed" "$tag-chest-skin"
    Open-UiChest 'ui.fixture_chest_plain' 'plain chest placed' "$tag-chest-plain"
}

if ($script:Mode -eq 'remote-vanilla') {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
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
    return
}

$readyRx = if ($remote) { '\[host\].*GloamingForest world ready' } else { 'GloamingForest world ready' }
Wait-LogLine $readyRx 120 | Out-Null

# Runs the native fixture once and screenshots it; `$tag` names the graphics mode.
function Invoke-UiFixture([string]$tag) {
    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_show' | Out-Null
    $shown = Wait-LogLine 'ui fixture: shown \(' 30 -Since $mark
    Write-Step "fixture shown: $shown"
    Wait-LogLine 'ui fixture: hud layer drawn' 30 -Since $mark | Out-Null
    Wait-LogLine 'ui: toast drawn' 30 -Since $mark | Out-Null
    Wait-LogLine 'ui fixture: world lines drawn' 30 -Since $mark | Out-Null
    Start-Sleep -Seconds 2
    Save-Screenshot "$tag-hud-toast-world" | Out-Null

    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_fog' | Out-Null
    foreach ($line in 'sky: provider fog applied', 'sky: provider volumetric fog applied', 'sky: provider drawn', 'sky: provider cloud height applied') {
        try { Wait-LogLine $line 15 -Since $mark | Out-Null; Write-Step "$tag ok: $line" }
        catch { Write-Step "${tag}: no '$line' line (recorded for S5)" }
    }
    Start-Sleep -Seconds 2
    Save-Screenshot "$tag-fog" | Out-Null

    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_screen' | Out-Null
    $open = Wait-LogLine 'screen opened: GloamingForest UI fixture' 30 -Since $mark
    Start-Sleep -Seconds 2
    Save-Screenshot "$tag-screen" | Out-Null
    # a click on container slot 4 (the compass) reaches the server's screen type
    if ($open -match 'panel=([-\d.]+),([-\d.]+) inv=[-\d.,]+ gui=([\d.]+)x([\d.]+)') {
        $px = [double]$Matches[1]; $py = [double]$Matches[2]; $gw = [double]$Matches[3]; $gh = [double]$Matches[4]
        Click (($px + 8 + 4 * 18 + 8) / $gw) (($py + 18 + 8) / $gh) -Relative
        Wait-LogLine "${hostRx}ui fixture: slot 4 clicked by" 20 -Since $mark | Out-Null
        Start-Sleep -Seconds 1
        Save-Screenshot "$tag-screen-clicked" | Out-Null
    } else { throw "no layout in '$open'" }
    Close-Screen
    Wait-LogLine "${hostRx}ui fixture: screen closed by" 20 -Since $mark | Out-Null

    Invoke-UiTest 'ui.fixture_hide' | Out-Null
}

# Item renderers in an item frame (Fixed) and on the head (Head), TF decorations on a held map.
function Invoke-UiItems([string]$tag) {
    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_frame' | Out-Null
    Wait-LogLine "${hostRx}ui fixture: item frame with gf_test:fixture_item placed" 20 -Since $mark | Out-Null
    Wait-UiLine 'ui fixture: item frame item drawn' 20 $mark | Out-Null
    Start-Sleep -Seconds 2
    Save-Screenshot "$tag-frame" | Out-Null
    Invoke-UiTest 'ui.fixture_clear' | Out-Null

    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_map' | Out-Null
    Wait-UiLine 'ui fixture: map decorations set on map' 30 $mark | Out-Null
    Wait-UiLine 'render: map decorations drawn' 20 0 | Out-Null   # logged once per game session
    Start-Sleep -Seconds 2
    Save-Screenshot "$tag-map" | Out-Null
    # the same map in an item frame
    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_map_frame' | Out-Null
    Wait-LogLine "${hostRx}ui fixture: item frame with minecraft:filled_map placed" 20 -Since $mark | Out-Null
    Start-Sleep -Seconds 2
    Save-Screenshot "$tag-map-frame" | Out-Null
    Invoke-UiTest 'ui.fixture_clear' | Out-Null

    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_head' | Out-Null
    Focus-Game
    Press-VK 0x74; Start-Sleep -Milliseconds 400; Press-VK 0x74   # F5 twice: third person, front
    Wait-UiLine 'ui fixture: head item drawn' 20 $mark | Out-Null
    Start-Sleep -Seconds 2
    Save-Screenshot "$tag-head" | Out-Null
    Press-VK 0x74   # back to first person
    Start-Sleep -Milliseconds 500
    Invoke-UiTest 'ui.fixture_clear' | Out-Null
    Invoke-UiTest 'ui.fixture_hide' | Out-Null
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

# every ui selftest (server halves and, in the client build, ui.client_*); the fixture screen it
# opens is closed again before the next chat command
$r = Invoke-SelfTest 'ui.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) ui selftest(s) failed" }
if (-not $remote -and $r.Pass -lt 26) { throw "expected at least 26 ui selftests in the client build, got $($r.Pass)" }
Start-Sleep -Seconds 1
if ((Get-ClientScreen) -ne 'hud_screen') { Close-Screen }
Invoke-SelfTest 'ui.fixture_hide' | Out-Null
Invoke-SelfTest 'ui.fixture_clear' | Out-Null

Invoke-UiFixture 'classic'
Invoke-UiChest 'classic'
Invoke-UiItems 'classic'

# the fixture key reaches the server (forwarded on input.key)
$mark = Get-LogMark
Focus-Game
Press-Key J
Wait-LogLine "${hostRx}ui fixture: key pressed by" 20 -Since $mark | Out-Null
Write-Step 'the fixture key press reached the server'

Invoke-UiValidate

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
    # the game was closed before the world saved the fixture blocks' removal: clear them again
    Invoke-Chat '/setblock ~ ~ ~2 air'
    Invoke-Chat '/setblock ~ ~1 ~2 air'
    Invoke-Chat '/setblock ~ ~1 ~3 air'
    Invoke-UiFixture 'vibrant'
    Invoke-UiChest 'vibrant'
    Invoke-UiItems 'vibrant'
    $mark = Get-LogMark
    Focus-Game
    Press-Key J
    Wait-LogLine 'ui fixture: key pressed by' 20 -Since $mark | Out-Null
}
