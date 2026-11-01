# F12 (ui): the client frameworks in the real game (spike S5).
#
# local / remote-mod: the fixture (client/ui/Fixture.cpp) draws a HUD layer, a toast, world-space
#   lines and geometry, fog / sky / cloud providers and a TF-style ContainerScreen; the fixture key
#   (J) must reach the server. All of it twice in local mode: Classic (graphics_mode 1, set by
#   ui.prelaunch.ps1) and Vibrant Visuals (graphics_mode 2, after a game restart).
#   Screenshots: out/ui/<mode>-hud-toast-world.png, <mode>-fog.png, <mode>-screen.png.
# remote-vanilla: a vanilla client gets the form fallback of the fixture screen (no companion
#   packets). Screenshot: out/ui/vanilla-form.png.
. (Join-Path $PSScriptRoot 'ui.graphics.ps1')

$remote = $script:Mode -ne 'local'
$hostRx = if ($remote) { '\[host\].*' } else { '' }

# The vanilla container fallback: a vanilla chest titled with containerTitleSkin (a plain title,
# spike S5), two blocks south of the player (the selftest turns the player towards it), opened
# with a right click. Screenshot: out/ui/<tag>-chest-skin.png.
function Invoke-UiChest([string]$tag) {
    $mark = Get-LogMark
    $r = Invoke-SelfTest 'ui.fixture_chest'
    if ($r.Fail -gt 0) { throw 'ui.fixture_chest failed' }
    Wait-LogLine "${hostRx}ui fixture: chest '.*' placed" 20 -Since $mark | Out-Null
    Start-Sleep -Seconds 2
    Focus-Game
    Click 0.5 0.5 -Relative -Right
    Start-Sleep -Seconds 3
    Save-Screenshot "$tag-chest-skin" | Out-Null
    # the chest screen is open (traced by the client mod; a vanilla client is assumed to have it)
    $open = if (Test-ClientTrace) { (Get-ClientScreen) -match 'chest' } else { $true }
    if ($open) { Close-Screen } else { Write-Step "${tag}: the chest did not open (screen: $(Get-ClientScreen))" }
    Invoke-SelfTest 'ui.fixture_chest_clear' | Out-Null
}

if ($script:Mode -eq 'remote-vanilla') {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
    $mark = Get-LogMark
    $r = Invoke-SelfTest 'ui.form_fallback'
    if ($r.Fail -gt 0) { throw 'ui.form_fallback failed on the host' }
    Wait-LogLine "\[host\].*ui form 'GloamingForest UI fixture' sent to" 30 -Since $mark | Out-Null
    Start-Sleep -Seconds 3
    Save-Screenshot 'vanilla-form' | Out-Null
    Press-Key ESC
    Wait-LogLine "\[host\].*ui form 'GloamingForest UI fixture' closed by" 30 -Since $mark | Out-Null
    Write-Step 'the vanilla client got the form fallback and closed it'
    if (Get-LeviLog | Select-String -Pattern '\[host\].*companion joined' -Quiet) { throw 'a vanilla client was accepted as a companion' }
    Invoke-UiChest 'vanilla'
    return
}

$readyRx = if ($remote) { '\[host\].*GloamingForest world ready' } else { 'GloamingForest world ready' }
Wait-LogLine $readyRx 120 | Out-Null

# Runs the native fixture once and screenshots it; `$tag` names the graphics mode.
function Invoke-UiFixture([string]$tag) {
    $mark = Get-LogMark
    $r = Invoke-SelfTest 'ui.fixture_show'
    if ($r.Fail -gt 0) { throw 'ui.fixture_show failed' }
    $shown = Wait-LogLine 'ui fixture: shown \(' 30 -Since $mark
    Write-Step "fixture shown: $shown"
    Wait-LogLine 'ui fixture: hud layer drawn' 30 -Since $mark | Out-Null
    Wait-LogLine 'ui: toast drawn' 30 -Since $mark | Out-Null
    Wait-LogLine 'ui fixture: world lines drawn' 30 -Since $mark | Out-Null
    Start-Sleep -Seconds 2
    Save-Screenshot "$tag-hud-toast-world" | Out-Null

    $mark = Get-LogMark
    $r = Invoke-SelfTest 'ui.fixture_fog'
    if ($r.Fail -gt 0) { throw 'ui.fixture_fog failed' }
    foreach ($line in 'sky: provider fog applied', 'sky: provider drawn', 'sky: provider cloud height applied') {
        try { Wait-LogLine $line 15 -Since $mark | Out-Null; Write-Step "$tag ok: $line" }
        catch { Write-Step "${tag}: no '$line' line (recorded for S5)" }
    }
    Start-Sleep -Seconds 2
    Save-Screenshot "$tag-fog" | Out-Null

    $mark = Get-LogMark
    $r = Invoke-SelfTest 'ui.fixture_screen'
    if ($r.Fail -gt 0) { throw 'ui.fixture_screen failed' }
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

    $r = Invoke-SelfTest 'ui.fixture_hide'
    if ($r.Fail -gt 0) { throw 'ui.fixture_hide failed' }
}

# every ui selftest (server halves and, in the client build, ui.client_*); the fixture screen it
# opens is closed again before the next chat command
$r = Invoke-SelfTest 'ui.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) ui selftest(s) failed" }
if (-not $remote -and $r.Pass -lt 18) { throw "expected at least 18 ui selftests in the client build, got $($r.Pass)" }
Start-Sleep -Seconds 1
if ((Get-ClientScreen) -ne 'hud_screen') { Close-Screen }
Invoke-SelfTest 'ui.fixture_hide' | Out-Null

Invoke-UiFixture 'classic'
Invoke-UiChest 'classic'

# the fixture key reaches the server (forwarded on input.key)
$mark = Get-LogMark
Focus-Game
Press-Key J
Wait-LogLine "${hostRx}ui fixture: key pressed by" 20 -Since $mark | Out-Null
Write-Step 'the fixture key press reached the server'

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
    # the game was closed before the world saved the chest's removal: clear it again
    Invoke-Chat '/setblock ~ ~ ~2 air'
    Invoke-UiFixture 'vibrant'
    $mark = Get-LogMark
    Focus-Game
    Press-Key J
    Wait-LogLine 'ui fixture: key pressed by' 20 -Since $mark | Out-Null
}
