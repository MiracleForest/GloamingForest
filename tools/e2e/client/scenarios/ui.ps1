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
#   leaves the player icon out (Java's showOnItemFrame). The frame step's stage also holds a block
#   renderer, a tint on the game's foliage and two named simulated players, one of whose name tags the
#   client hides with render::hideNameTag (the other stays drawn). Blocks go on the stage in the ui selftest
#   row; ui.fixture_clear puts the stage and the player back after each step. All of it twice in
#   local mode: Classic (graphics_mode 1, set by ui.prelaunch.ps1) and Vibrant Visuals (graphics_mode
#   2, after a game restart; the restarted world may still carry the tag and the fixture's leftovers
#   from before the restart: ui.fixture_clear and /tag remove first).
#   Screenshots: out/ui/<mode>-hud-toast-world.png, <mode>-gui-hidden.png, <mode>-fog.png,
#   <mode>-screen(-clicked).png, <mode>-chest-skin.png, <mode>-chest-plain.png, <mode>-frame.png,
#   <mode>-map.png, <mode>-map-frame.png, <mode>-head.png (the fixture block on the head, golden boots,
#   the diamond chestplate that render::hideArmor is meant to hide: recorded, see Invoke-UiItems).
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

# Pixel helpers on a screenshot: a 16 x 16 item cell at GUI pixels (`gx`, `gy`) of a `gw` x `gh` GUI
# (the GUI size from the 'screen opened' or 'hud layer drawn' line).
function Get-UiCell([System.Drawing.Bitmap]$bmp, [double]$gx, [double]$gy, [double]$gw, [double]$gh) {
    $sx = $bmp.Width / $gw; $sy = $bmp.Height / $gh
    return @{ sx = $sx; sy = $sy; x0 = $gx * $sx; y0 = $gy * $sy }
}
# The fixture screen's container slot `slot` (Java's (8 + 18 i, 18) of the panel at `px`, `py`).
function Get-UiSlotGeometry([System.Drawing.Bitmap]$bmp, [double]$px, [double]$py, [double]$gw, [double]$gh, [int]$slot) {
    return Get-UiCell $bmp ($px + 8 + 18 * $slot) ($py + 18) $gw $gh
}
# The mean colour of a patch of slot `g` in GUI pixels (x, y, w, h inside the 16 x 16 slot).
function Get-UiPatch([System.Drawing.Bitmap]$bmp, $g, [double]$x, [double]$y, [double]$w, [double]$h) {
    $x0 = [int][Math]::Round($g.x0 + $x * $g.sx); $x1 = [int][Math]::Round($g.x0 + ($x + $w) * $g.sx)
    $y0 = [int][Math]::Round($g.y0 + $y * $g.sy); $y1 = [int][Math]::Round($g.y0 + ($y + $h) * $g.sy)
    $r = 0.0; $gr = 0.0; $b = 0.0; $n = 0
    for ($xx = $x0; $xx -lt [Math]::Max($x1, $x0 + 1); $xx++) {
        for ($yy = $y0; $yy -lt [Math]::Max($y1, $y0 + 1); $yy++) {
            $p = $bmp.GetPixel($xx, $yy); $r += $p.R; $gr += $p.G; $b += $p.B; $n++
        }
    }
    return @{ R = $r / $n; G = $gr / $n; B = $b / $n; L = (0.299 * $r + 0.587 * $gr + 0.114 * $b) / $n }
}
function Get-UiColourDistance($a, $b) { return [Math]::Abs($a.R - $b.R) + [Math]::Abs($a.G - $b.G) + [Math]::Abs($a.B - $b.B) }

# How much of the width of cell `g` is drawn on (0..1), against the empty cell `e` (same background):
# a flat item covers most of its slot, a GUI cube too (an edge-on sliver about a third).
function Get-UiCellCoverage([System.Drawing.Bitmap]$bmp, $g, $e) {
    $w = [int][Math]::Floor(16 * $g.sx); $h = [int][Math]::Floor(16 * $g.sy)
    $cols = 0
    for ($dx = 1; $dx -lt $w - 1; $dx++) {
        $hit = 0
        for ($dy = 1; $dy -lt $h - 1; $dy++) {
            $a = $bmp.GetPixel([int]$g.x0 + $dx, [int]$g.y0 + $dy); $b = $bmp.GetPixel([int]$e.x0 + $dx, [int]$e.y0 + $dy)
            if (([Math]::Abs([int]$a.R - [int]$b.R) + [Math]::Abs([int]$a.G - [int]$b.G) + [Math]::Abs([int]$a.B - [int]$b.B)) -gt 24) { $hit++ }
        }
        if ($hit -ge 2) { $cols++ }
    }
    return [Math]::Round($cols / [double]($w - 2), 2)
}

# A block item drawn with the GUI block transform (Java's block/block.json "gui": rotation 30 225 0,
# scale 0.625) is an isometric cube: a hexagon about 14 x 16 GUI pixels, so the four corners of its
# slot stay empty (the slot's own colour, as in the empty slot), and the top face is lit more than the
# two side faces. A block drawn face on (W0d review) fills the corners and is evenly lit.
# The game's own inventory and hotbar draw block items so (the vanilla look the mod's GUI items match).
function Test-UiCellCube([System.Drawing.Bitmap]$bmp, $g, $e) {
    $corners = @(@(0.6, 0.6), @(13.9, 0.6), @(0.6, 14.4), @(13.9, 14.4))
    $worst = 0
    foreach ($k in $corners) {
        $d = Get-UiColourDistance (Get-UiPatch $bmp $g $k[0] $k[1] 1.5 1.0) (Get-UiPatch $bmp $e $k[0] $k[1] 1.5 1.0)
        $worst = [Math]::Max($worst, $d)
    }
    $top = (Get-UiPatch $bmp $g 6.5 2.8 3 2).L
    $left = (Get-UiPatch $bmp $g 3 8 3 3).L
    $right = (Get-UiPatch $bmp $g 10 8 3 3).L
    $cover = Get-UiCellCoverage $bmp $g $e
    return @{ Corners = [Math]::Round($worst, 1); Top = [Math]::Round($top, 1); Left = [Math]::Round($left, 1); Right = [Math]::Round($right, 1); Cover = $cover
        Cube = ($worst -le 30) -and ($top -gt $left + 3) -and ($top -gt $right + 3) -and ($cover -ge 0.75) }
}
# Throws unless cell `g` (against the empty cell `e`) holds a GUI cube; returns the report.
function Assert-UiCellCube([System.Drawing.Bitmap]$bmp, $g, $e, [string]$what, [string]$png) {
    $t = Test-UiCellCube $bmp $g $e
    $line = "$what corners $($t.Corners) top $($t.Top) left $($t.Left) right $($t.Right) covers $($t.Cover)"
    if (-not $t.Cube) { throw "$what is not drawn as a GUI cube in $(Split-Path $png -Leaf): $line (cube: corners <= 30, top lit more than both sides, covers >= 0.75)" }
    return $line
}

# The fixture screen's flat items (0: map, 4: compass) cover their slots; its block items (2:
# cobblestone, 6: twilight oak planks, a TF block) are isometric cubes.
function Assert-UiSlotItems([string]$png, [string]$layout) {
    if ($layout -notmatch 'panel=([-\d.]+),([-\d.]+) inv=[-\d.,]+ gui=([\d.]+)x([\d.]+)') { throw "no layout in '$layout'" }
    $px = [double]$Matches[1]; $py = [double]$Matches[2]; $gw = [double]$Matches[3]; $gh = [double]$Matches[4]
    Add-Type -AssemblyName System.Drawing
    $bmp = [System.Drawing.Bitmap]::FromFile($png)
    try {
        $report = @()
        foreach ($s in @(@{ slot = 0; empty = 1; what = 'map (flat)' }, @{ slot = 4; empty = 5; what = 'compass (flat)' })) {
            $cover = Get-UiCellCoverage $bmp (Get-UiSlotGeometry $bmp $px $py $gw $gh $s.slot) (Get-UiSlotGeometry $bmp $px $py $gw $gh $s.empty)
            $report += "$($s.what) covers $cover"
            if ($cover -lt 0.6) { throw "slot $($s.slot), $($s.what), covers only $cover of its width in $(Split-Path $png -Leaf)" }
        }
        foreach ($s in @(@{ slot = 2; empty = 1; what = 'cobblestone (block)' }, @{ slot = 6; empty = 5; what = 'twilight oak planks (block)' })) {
            $report += Assert-UiCellCube $bmp (Get-UiSlotGeometry $bmp $px $py $gw $gh $s.slot) (Get-UiSlotGeometry $bmp $px $py $gw $gh $s.empty) "slot $($s.slot), $($s.what)," $png
        }
        Write-Step "slot items: $($report -join '; ')"
    } finally { $bmp.Dispose() }
}
# The fixture HUD layer's cells (from its 'hud layer drawn ... cells=x,y gui=WxH' line): cobblestone,
# an empty cell, twilight oak planks, 18 GUI pixels apart; both blocks are GUI cubes.
function Assert-UiHudCubes([string]$png, [string]$line) {
    if ($line -notmatch 'cells=([-\d.]+),([-\d.]+) gui=([\d.]+)x([\d.]+)') { throw "no cells in '$line'" }
    $cx = [double]$Matches[1]; $cy = [double]$Matches[2]; $gw = [double]$Matches[3]; $gh = [double]$Matches[4]
    Add-Type -AssemblyName System.Drawing
    $bmp = [System.Drawing.Bitmap]::FromFile($png)
    try {
        $empty = Get-UiCell $bmp ($cx + 18) $cy $gw $gh
        $a = Assert-UiCellCube $bmp (Get-UiCell $bmp $cx $cy $gw $gh) $empty 'HUD cobblestone' $png
        $b = Assert-UiCellCube $bmp (Get-UiCell $bmp ($cx + 36) $cy $gw $gh) $empty 'HUD twilight oak planks' $png
        Write-Step "HUD block items: $a; $b"
    } finally { $bmp.Dispose() }
}

# The `ui.*` run must not touch the untagged real player.
function Assert-UiUntouched([int]$Since) {
    $lines = @(Get-LeviLog -Since $Since)
    # (simulated test players are named gf_test_<n>; only the real player matters here)
    foreach ($rx in 'ui fixture: shown', 'screen opened: GloamingForest UI fixture', 'ui fixture: \S+ put back \(position', "ui form 'GloamingForest UI fixture' sent to (?!gf_test_)", "ui ddui 'ui.fixture' sent to (?!gf_test_)") {
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
if ($script:Mode -eq 'local' -and $r.Pass -lt 35) { throw "expected at least 35 ui selftests in the client build, got $($r.Pass)" }
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
    $hud = Wait-LogLine 'ui fixture: hud layer drawn' 30 -Since $mark
    Wait-LogLine 'ui: toast drawn' 30 -Since $mark | Out-Null
    # the toasts' frame is the game's own toast texture (logged once per game session)
    Wait-LogLine 'ui: toast frame drawn with textures/ui/greyBorder' 30 | Out-Null
    # an onWorld callback gets no lightAt (drawGeo lights each geometry at its own place)
    Wait-LogLine 'ui fixture: world lines drawn \(lightAt none\)' 30 -Since $mark | Out-Null
    Start-Sleep -Seconds 2
    # the cell right of the HUD panel: ui::Blend::Invert (the crosshair material) over the world
    Wait-UiLine 'ui: sprite drawn with the inverting crosshair blend' 30 0 | Out-Null   # once per game session
    $shot = Save-Screenshot "$mode-hud-toast-world"
    # the HUD layer's block items (drawn after the game's UI pass): a vanilla and a TF block, cubes
    Assert-UiHudCubes $shot $hud

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
    $shot = Save-Screenshot "$mode-screen"
    # block items (cobblestone, twilight oak planks) cover their slots like the flat ones (map, compass)
    Assert-UiSlotItems $shot $open
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
    # the stage's block renderer (fixture block, left of the frame: the fixture geometry lit by the
    # block above, tracked from its built render chunk) and the twilight oak leaves right of it, tinted
    # from the game's foliage (red and blue swapped): both must run
    if (-not $remote -or (Test-ClientTrace)) {
        $block = Wait-UiLine '^(?!\[host\]).*ui fixture: stage block renderer drawn at' 20 $mark
        $light = Wait-UiLine '^(?!\[host\]).*render: block renderers drawn, each with the light at its block' 20 0   # once per game session
        $tint  = Wait-UiLine '^(?!\[host\]).*ui fixture: stage leaves tinted from the game''s foliage' 20 $mark
        Write-Step "${mode}: stage renderers ran: $block | $light | $tint"
        # render::hideNameTag in a real frame: of the two named simulated players behind the stage
        # blocks (a player's name tag is always drawn), the client fixture's rule (type and predicate)
        # hides one; the name tag pass (ActorRenderDispatcher::extractRenderTextObjects) drops it and
        # draws the other
        Wait-LogLine "${hostRx}ui fixture: 2 named player\(s\) placed on the stage" 20 -Since $mark | Out-Null
        $hidden = Wait-UiLine '^(?!\[host\]).*ui fixture: name tag of ''GF_UI_hidden_tag'' hidden' 20 $mark
        $pass   = Wait-UiLine '^(?!\[host\]).*render: name tag hidden \(minecraft:player\)' 20 0   # once per game session
        $shown  = Wait-UiLine '^(?!\[host\]).*ui fixture: name tag of ''GF_UI_shown_tag'' left drawn' 20 $mark
        Write-Step "${mode}: name tags: $hidden | $pass | $shown"
    }
    Start-Sleep -Seconds 2
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
    Wait-LogLine "${hostRx}ui fixture: \S+ put back \(position" 20 -Since $mark | Out-Null

    $mark = Get-LogMark
    Invoke-UiTest 'ui.fixture_head' | Out-Null
    Wait-UiStage $mark
    Focus-Game
    Press-VK 0x74; Start-Sleep -Milliseconds 400; Press-VK 0x74   # F5 twice: third person, front
    Wait-UiLine 'ui fixture: head item drawn' 20 $mark | Out-Null
    # render::hideArmor (TF's emperor's cloth): the fixture's rule hides the worn chestplate with the
    # fixture's data key. On 1.26.51 the v2 actor renderer does not ask DataDrivenModel::
    # shouldRenderAttachableOnActor; F12 takes the hidden stack out of the wearer's armour for the frame
    # instead (src/client/render/ArmorV2.cpp; docs/spikes/S5.md, Audit round 2): the screenshot below
    # shows the player without the chestplate
    if (-not $remote -or (Test-ClientTrace)) {
        $armorMark = $mark
        try {
            $armor = Wait-LogLine '^(?!\[host\]).*ui fixture: worn minecraft:diamond_chestplate hidden by the fixture''s armour rule' 10 -Since $armorMark
            Write-Step "${mode}: armour hidden: $armor"
            $v2 = Wait-LogLine '^(?!\[host\]).*render: worn armour hidden from the v2 actor renderer' 10 -Since $armorMark
            Write-Step "${mode}: v2 actor renderer: $v2"
        } catch { Write-Step "${mode}: the worn chestplate was not hidden from the v2 actor renderer (recorded)" }
    }
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
