# Woodwork (U4): every wood set and furniture block as the player sees it, a drying rack at work and
# a TF boat ride. Built in rows (one per TF wood, then the vanilla-wood furniture) and photographed
# from the north (the camera looks south: east is on the left):
#   out/woodwork/woodwork-wood-<wood>.png   one row per TF wood: log (y, x), stripped log, wood,
#                                           stripped wood, planks, stairs, slab, fences, fence gate,
#                                           door, trapdoor, button, pressure plate, sign, wall sign,
#                                           hanging sign, wall hanging sign, chest, trapped chest
#                                           double, banister (tall, short, connected), drying rack,
#                                           hollow logs (horizontal, vertical, ladder, vine); canopy
#                                           also its bookshelf
#   out/woodwork/woodwork-vanilla-*.png     the 11 vanilla-wood banisters and drying racks, the 10
#                                           vanilla-wood hollow logs
#   out/woodwork/woodwork-hollow-close.png  hollow logs close up: moss, moss and grass, snow, a ladder
#   out/woodwork/woodwork-rack-*.png        drying racks holding items (kelp drying with its
#                                           particles), close up
#   out/woodwork/woodwork-boat-*.png        a canopy boat and a sorting chest boat on water, then a ride
#                                           (first and third person)
# Local mode also runs the woodwork selftests on the integrated server (the climbable hollow log is
# climbed there through the client mod's BlockProperty::Climbable); remote modes build through the
# host console (the vanilla client sees the downloaded resource pack and rides the boat).

function Invoke-Build([string]$command) {
    if ($script:Mode -eq 'local') { Invoke-Chat "/$command" } else { Send-HostConsole $command }
}

if ($script:Mode -eq 'local') {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'woodwork.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) woodwork selftest(s) failed" }
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
}
Invoke-Build 'gamerule sendcommandfeedback false'
Invoke-Build 'gamerule dodaylightcycle false'
Invoke-Build 'time set 6000'

$y = -60 # the flat world's first air layer
# the hollow log items in the hotbar (TF's item model: the horizontal hollow log)
Invoke-Chat '/clear @s'
foreach ($it in 'hollow_oak_log', 'hollow_canopy_log', 'hollow_twilight_oak_log', 'hollow_crimson_stem', 'oak_banister', 'canopy_drying_rack', 'canopy_boat', 'sorting_chest_boat') { Invoke-Chat "/give @s twilightforest:$it" }
Start-Sleep -Seconds 2
Save-Screenshot 'woodwork-items-hotbar' | Out-Null
$z = 46
$f = 'twilightforest:facing'
$tf = 'twilight_oak', 'canopy', 'mangrove', 'dark', 'time', 'transformation', 'mining', 'sorting'

function Get-WoodRow([string]$w, [int]$x0) {
    $wall = if ($w -eq 'twilight_oak') { 'twilight_wall_sign' } else { "${w}_wall_sign" }
    $hollow = "hollow_${w}_log"
    $b = @(
        @(0, 0, 0, "${w}_log", '["twilightforest:axis"="y"]'),
        @(1, 0, 0, "${w}_log", '["twilightforest:axis"="x"]'),
        @(2, 0, 0, "stripped_${w}_log", '["twilightforest:axis"="y"]'),
        @(3, 0, 0, "${w}_wood", ''),
        @(4, 0, 0, "stripped_${w}_wood", ''),
        @(5, 0, 0, "${w}_planks", ''),
        @(6, 0, 0, "${w}_stairs", "[`"$f`"=`"north`",`"twilightforest:half`"=`"bottom`"]"),
        @(7, 0, 0, "${w}_slab", '["twilightforest:type"="bottom"]'),
        @(8, 0, 0, "${w}_fence", ''),
        @(9, 0, 0, "${w}_fence", ''),
        @(10, 0, 0, "${w}_fence_gate", "[`"$f`"=`"north`"]"),
        @(12, 0, 0, "${w}_door", "[`"$f`"=`"north`",`"twilightforest:half`"=`"lower`"]"),
        @(12, 1, 0, "${w}_door", "[`"$f`"=`"north`",`"twilightforest:half`"=`"upper`"]"),
        @(13, 0, 0, "${w}_trapdoor", "[`"$f`"=`"north`",`"twilightforest:half`"=`"bottom`"]"),
        @(14, 0, 0, "${w}_planks", ''),
        @(14, 1, 0, "${w}_button", '["twilightforest:face"="floor"]'),
        @(15, 0, 0, "${w}_pressure_plate", ''),
        @(16, 0, 0, "${w}_sign", '["twilightforest:rotation"=8]'),
        @(17, 0, 1, "${w}_planks", ''),
        @(17, 0, 0, $wall, "[`"$f`"=`"north`"]"),
        @(18, 2, 0, "${w}_planks", ''),
        @(18, 1, 0, "${w}_hanging_sign", '["twilightforest:rotation"=8]'),
        @(19, 1, 1, "${w}_planks", ''),
        @(19, 1, 0, "${w}_wall_hanging_sign", "[`"$f`"=`"east`"]"),
        @(21, 0, 0, "${w}_chest", "[`"$f`"=`"north`"]"),
        @(23, 0, 0, "${w}_trapped_chest", "[`"$f`"=`"north`",`"twilightforest:type`"=`"left`"]"),
        @(24, 0, 0, "${w}_trapped_chest", "[`"$f`"=`"north`",`"twilightforest:type`"=`"right`"]"),
        @(26, 0, 0, "${w}_banister", "[`"$f`"=`"north`",`"twilightforest:shape`"=`"tall`"]"),
        @(27, 0, 0, "${w}_banister", "[`"$f`"=`"north`",`"twilightforest:shape`"=`"short`"]"),
        @(28, 1, 0, "${w}_banister", "[`"$f`"=`"north`",`"twilightforest:shape`"=`"tall`",`"twilightforest:extended`"=true]"),
        @(28, 0, 0, "${w}_banister", "[`"$f`"=`"north`",`"twilightforest:shape`"=`"connected`"]"),
        @(30, 0, 0, "${w}_drying_rack", "[`"$f`"=`"north`"]"),
        @(32, 0, 0, "${hollow}_horizontal", '["twilightforest:axis"="z"]'),
        @(33, 0, 0, "${hollow}_horizontal", '["twilightforest:axis"="x","twilightforest:variant"="moss_and_grass"]'),
        @(34, 0, 0, "${hollow}_vertical", ''),
        @(35, 0, 0, "${hollow}_climbable", "[`"$f`"=`"north`",`"twilightforest:variant`"=`"ladder`"]"),
        @(36, 0, 0, "${hollow}_climbable", "[`"$f`"=`"north`",`"twilightforest:variant`"=`"vine`"]")
    )
    if ($w -eq 'canopy') { $b += , @(38, 0, 0, 'canopy_bookshelf', '') }
    @{ name = "woodwork-wood-$w"; x = $x0; blocks = $b; width = 39 }
}

$rows = @()
for ($i = 0; $i -lt $tf.Count; $i++) { $rows += Get-WoodRow $tf[$i] (300 + 50 * $i) }
$vf = 'oak', 'spruce', 'birch', 'jungle', 'acacia', 'dark_oak', 'crimson', 'warped', 'vangrove', 'bamboo', 'cherry'
$vb = @()
for ($i = 0; $i -lt $vf.Count; $i++) {
    $vb += , @((2 * $i), 0, 0, "$($vf[$i])_banister", "[`"$f`"=`"north`"]")
    $vb += , @((2 * $i), 0, -2, "$($vf[$i])_drying_rack", "[`"$f`"=`"north`"]")
}
$rows += @{ name = 'woodwork-vanilla-furniture'; x = 720; blocks = $vb; width = 22 }
$vh = 'oak', 'spruce', 'birch', 'jungle', 'acacia', 'dark_oak', 'crimson', 'warped', 'vangrove', 'cherry'
$hb = @()
for ($i = 0; $i -lt $vh.Count; $i++) {
    $base = if ($vh[$i] -in 'crimson', 'warped') { "hollow_$($vh[$i])_stem" } else { "hollow_$($vh[$i])_log" }
    $hb += , @((2 * $i), 0, 0, "${base}_horizontal", '["twilightforest:axis"="z"]')
    $hb += , @((2 * $i), 0, -2, "${base}_vertical", '')
    $hb += , @((2 * $i), 0, -4, "${base}_climbable", "[`"$f`"=`"north`",`"twilightforest:variant`"=`"ladder`"]")
}
$rows += @{ name = 'woodwork-vanilla-hollow'; x = 760; blocks = $hb; width = 20 }

foreach ($row in $rows) {
    Invoke-Teleport "/tp @s $($row.x + [int]($row.width / 2)) $($y + 2) $($z - 12) 0 15"
    Start-Sleep -Seconds 4
    foreach ($b in $row.blocks) {
        $x = $row.x + $b[0]
        $states = if ($b[4]) { " $($b[4])" } else { '' }
        Invoke-Build "setblock $x $($y + $b[1]) $($z + $b[2]) twilightforest:$($b[3])$states"
    }
}
Start-Sleep -Seconds 12 # the chat fades
foreach ($row in $rows) {
    # far enough back for the whole row (about 40 blocks wide)
    $back = if ($row.width -gt 30) { 22 } else { 13 }
    Invoke-Teleport "/tp @s $($row.x + [int]($row.width / 2)) $($y + 4) $($z - $back) 0 15"
    Start-Sleep -Seconds 3
    Save-Screenshot $row.name | Out-Null
    # the near half close up
    Invoke-Teleport "/tp @s $($row.x + 7) $($y + 2) $($z - 7) 0 15"
    Start-Sleep -Seconds 2
    Save-Screenshot "$($row.name)-a" | Out-Null
    if ($row.width -gt 30) {
        Invoke-Teleport "/tp @s $($row.x + 21) $($y + 2) $($z - 7) 0 15"
        Start-Sleep -Seconds 2
        Save-Screenshot "$($row.name)-b" | Out-Null
        Invoke-Teleport "/tp @s $($row.x + 33) $($y + 2) $($z - 6) 0 15"
        Start-Sleep -Seconds 2
        Save-Screenshot "$($row.name)-c" | Out-Null
    }
}

# hollow logs close up: empty, moss, moss and grass, snow, waterlogged; vertical, vine, ladder
$hx = 820
Invoke-Teleport "/tp @s $($hx + 4) $($y + 2) $($z - 6) 0 15"
Start-Sleep -Seconds 3
$i = 0
foreach ($v in 'empty', 'moss', 'moss_and_grass', 'snow') {
    Invoke-Build "setblock $($hx + 2 * $i) $y $z twilightforest:hollow_canopy_log_horizontal [`"twilightforest:axis`"=`"z`",`"twilightforest:variant`"=`"$v`"]"
    $i++
}
Invoke-Build "setblock $($hx + 8) $y $z twilightforest:hollow_canopy_log_climbable [`"$f`"=`"south`",`"twilightforest:variant`"=`"ladder`"]"
Start-Sleep -Seconds 8
Invoke-Teleport "/tp @s $($hx + 4) $($y + 1) $($z - 3.5) 0 20"
Start-Sleep -Seconds 3
Save-Screenshot 'woodwork-hollow-close' | Out-Null

# drying racks holding items (the item drawn on the rack), kelp drying (its particles): the
# selftest woodwork.fixture_showcase fills six racks in the unit's selftest row and logs where
$mark = Get-LogMark
if ($script:Mode -eq 'local') { Invoke-SelfTest 'woodwork.fixture_showcase' | Out-Null } else { Send-HostConsole 'tf selftest woodwork.fixture_showcase' }
$line = Wait-LogLine 'woodwork: showcase racks at -?\d+ -?\d+ -?\d+' 60 -Since $mark
if ("$line" -notmatch 'showcase racks at (-?\d+) (-?\d+) (-?\d+)') { throw 'no showcase racks' }
$rx = [int]$Matches[1]; $ry = [int]$Matches[2]; $rz = [int]$Matches[3]
Invoke-Teleport "/tp @s $($rx + 2.5) $($ry + 0.5) $($rz - 2.5) 0 10"
Start-Sleep -Seconds 6
Save-Screenshot 'woodwork-rack-items' | Out-Null
Invoke-Teleport "/tp @s $($rx + 0.5) $($ry + 0.5) $($rz - 1.4) 0 20"
Start-Sleep -Seconds 3
Save-Screenshot 'woodwork-rack-kelp-close' | Out-Null
# the side view: the item hangs below the bar, flat against it (DryingRackRenderer, FIXED)
Invoke-Teleport "/tp @s $($rx + 6.5) $($ry - 0.4) $($rz + 0.5) 90 5"
Start-Sleep -Seconds 3
Save-Screenshot 'woodwork-rack-side' | Out-Null
if ($script:Mode -ne 'remote-vanilla') {
    # the client mod draws the rack items itself (the display entities hidden)
    Wait-LogLine '^(?!\[host\]).*woodwork: a drying rack item drawn by the client mod' 20 | Out-Null
    if ($env:GF_WOODWORK_RACK_CAL) {
        # development: the item transform's calibration, one picture per entry of the JSON array
        $calFile = Join-Path (Join-Path $script:ModsDir $script:ModName) 'woodwork_rack_calibration.json'
        $cals = $env:GF_WOODWORK_RACK_CAL | ConvertFrom-Json
        Invoke-Teleport "/tp @s $($rx + 2.5) $($ry + 0.2) $($rz - 2.2) 0 10"
        for ($i = 0; $i -lt @($cals).Count; $i++) {
            @($cals)[$i] | ConvertTo-Json -Depth 5 | Set-Content -Path $calFile -Encoding UTF8
            Start-Sleep -Seconds 2
            Save-Screenshot "woodwork-rackcal-$i" | Out-Null
        }
        Remove-Item $calFile -ErrorAction SilentlyContinue
    }
}

# climbing a climbable hollow log (the client mod: the block is climbable, BlockProperty::Climbable)
if ($script:Mode -ne 'remote-vanilla') {
    $cx = 950
    Invoke-Teleport "/tp @s $($cx + 0.5) $($y + 1) $($z - 4.5) 0 10"
    for ($i = 0; $i -lt 4; $i++) { Invoke-Build "setblock $cx $($y + $i) $z twilightforest:hollow_oak_log_climbable [`"$f`"=`"north`",`"twilightforest:variant`"=`"ladder`"]" }
    Invoke-Build "setblock $($cx + 1) $y $z twilightforest:hollow_oak_log_climbable [`"$f`"=`"north`",`"twilightforest:variant`"=`"vine`"]"
    Start-Sleep -Seconds 2
    Save-Screenshot 'woodwork-climb-logs' | Out-Null
    $mark = Get-LogMark
    Invoke-Teleport "/tp @s $($cx + 0.5) $y $($z + 0.5) 0 0"
    Start-Sleep -Seconds 1
    Focus-Game
    Press-Key W 2500
    Wait-LogLine '^(?!\[host\]).*woodwork: the local player climbs a climbable hollow log' 15 -Since $mark | Out-Null
    Save-Screenshot 'woodwork-climb-inside' | Out-Null
    Write-Step 'the local player climbed the laddered hollow log'
}

# crawling into a horizontal hollow log: sneaking with an empty hand, a right-click into the tunnel
# at its end (HorizontalHollowLogBlock.useWithoutItem) moves the player into it, where the game
# crawls under the 14 px ceiling (Java: Pose.SWIMMING); the client mod reports the crawl
$kx = 960
Invoke-Build "setblock $kx $y $z twilightforest:hollow_oak_log_horizontal [`"twilightforest:axis`"=`"z`"]"
Invoke-Build "setblock $kx $y $($z + 1) twilightforest:hollow_oak_log_horizontal [`"twilightforest:axis`"=`"z`"]"
Invoke-Chat '/clear @s'
Invoke-Teleport "/tp @s $($kx + 0.5) $y $($z - 0.31) 0 12"
Start-Sleep -Seconds 1
$mark = Get-LogMark
Focus-Game
[BsE2E.Native]::Key([uint16]0x10, $false)
Start-Sleep -Milliseconds 300
[BsE2E.Native]::MouseButton(1, $false); Start-Sleep -Milliseconds 80; [BsE2E.Native]::MouseButton(1, $true)
Start-Sleep -Milliseconds 300
[BsE2E.Native]::Key([uint16]0x57, $false)
Start-Sleep -Milliseconds 1500
Save-Screenshot 'woodwork-crawl' | Out-Null
[BsE2E.Native]::Key([uint16]0x57, $true)
[BsE2E.Native]::Key([uint16]0x10, $true)
try { Wait-LogLine 'woodwork: \S+ crawls into twilightforest:hollow_oak_log_horizontal' 10 -Since $mark | Out-Null; Write-Step 'the sneak-click moved the player into the hollow log' }
catch { Write-Step 'warning: no crawl click seen by the server' }
if ($script:Mode -ne 'remote-vanilla') {
    try { Wait-LogLine '^(?!\[host\]).*woodwork: the local player crawls in a horizontal hollow log' 5 -Since $mark | Out-Null; Write-Step 'the local player crawls in the hollow log' }
    catch { Write-Step 'warning: the local player did not crawl in the hollow log (the game crawls only where it allows it)' }
}
# boats on a pond, then a ride
$bx = 900
Invoke-Teleport "/tp @s $($bx + 3) $($y + 3) $($z - 8) 0 25"
Start-Sleep -Seconds 3
Invoke-Build "fill $($bx - 1) $($y - 1) $($z - 1) $($bx + 9) $($y - 1) $($z + 6) minecraft:stone"
Invoke-Build "fill $($bx) $($y - 1) $($z) $($bx + 8) $($y - 1) $($z + 5) minecraft:water"
Start-Sleep -Seconds 2
Invoke-Build "summon twilightforest:boat $($bx + 2) $($y - 0.5) $($z + 2) 0 0 twilightforest:canopy"
Invoke-Build "summon twilightforest:chest_boat $($bx + 6) $($y - 0.5) $($z + 2) 0 0 twilightforest:sorting"
Start-Sleep -Seconds 4
Invoke-Teleport "/tp @s $($bx + 4) $($y + 2) $($z - 3) 0 30"
Start-Sleep -Seconds 3
Save-Screenshot 'woodwork-boat-pond' | Out-Null
if ($script:Mode -ne 'remote-vanilla') {
    # the client mod draws the TF boats with their client entities (TF texture, the vanilla chest)
    Wait-LogLine '^(?!\[host\]).*woodwork: twilightforest:(chest_)?boat drawn with its client entity' 20 | Out-Null
}
Invoke-Chat "/ride @s start_riding @e[type=twilightforest:boat,c=1] teleport_rider"
Start-Sleep -Seconds 3
Save-Screenshot 'woodwork-boat-ride' | Out-Null
# paddling (W): the boat moves, the paddles row
Focus-Game
[BsE2E.Native]::Key([uint16]0x57, $false)
Start-Sleep -Milliseconds 700
Save-Screenshot 'woodwork-boat-paddling' | Out-Null
Start-Sleep -Milliseconds 400
[BsE2E.Native]::Key([uint16]0x57, $true)
# third person (F5): the rider in the boat
function Press-F5 {
    Focus-Game
    [BsE2E.Native]::Key([uint16]0x74, $false); Start-Sleep -Milliseconds 60; [BsE2E.Native]::Key([uint16]0x74, $true)
    Start-Sleep -Milliseconds 400
}
Press-F5
Start-Sleep -Seconds 2
Save-Screenshot 'woodwork-boat-ride-third' | Out-Null
Focus-Game
[BsE2E.Native]::Key([uint16]0x57, $false)
Start-Sleep -Milliseconds 600
Save-Screenshot 'woodwork-boat-paddling-third' | Out-Null
[BsE2E.Native]::Key([uint16]0x57, $true)
Press-F5
Press-F5
Reset-CameraPerspective
Invoke-Chat "/ride @s stop_riding"
Start-Sleep -Seconds 1
