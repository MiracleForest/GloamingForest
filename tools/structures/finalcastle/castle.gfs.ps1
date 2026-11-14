# U30 (finalcastle): the final castle's three template pieces (original GloamingForest designs at upstream's
# template ids, rule R11 / R19; upstream's .nbt files are All Rights Reserved and never opened).
#
# Every template is drawn in the local frame of the piece that places it (TFStructureComponentOld,
# orientation SOUTH: local x / z = world x / z from the piece's minimum corner), with its jigsaw
# connector one block inside the host's junction, facing back at it:
#
#   gazebo       21 x 12 x 21  host FinalCastleBossGazeboComponent, junction local (10, -1, 10), UP_SOUTH;
#                              connector "gloamingforest:final_castle/final_boss" at (10, 0, 10), front down,
#                              top north. The violet force-field cage of upstream's commented-out procedural
#                              code (walls 0..10, roof at y 11) and the "final_castle_wip" marker at the
#                              connector's block (U30's handler puts the four WIP text lines and the
#                              punch box there, docs/spec/finalcastle.md 1.2).
#   large_tower  13 x 61 x 13  host FinalCastleLargeTowerComponent, junction local (0, 1, 2), WEST_UP;
#                              connector "gloamingforest:final_castle/large_tower" at (1, 1, 2), front west.
#                              A spiral quartz stair along the walls from the yellow door (x 0, y 1..4)
#                              to the roof deck, two floors (y 20, 40) and the sign upstream's comment names.
#   bell_tower   21 x 65 x 21  host FinalCastleBellTower21Component, junction local (0, 9, 10), WEST_UP;
#                              connector "gloamingforest:final_castle/room" at (1, 9, 10), front west. The
#                              entrance floor at y 8, a spiral stair up to the bell room floor at y 47
#                              (under the blue force-field windows, y 48..62) with its bell, and upstream's sign.
#
# Connector names: upstream's are "gloamingforest:final_castle/<name>"; assemble's dangling-reference check
# refuses gloamingforest: strings that are no registry id, so the pair (template and host) uses
# "gf:final_castle/<name>" (the U21 lich tower does the same). The pool logic is unchanged.
#
# Cells a template leaves alone are structure_void (the tower walls, door and windows stay the host's).

$brick   = 'gloamingforest:castle_brick'
$thick   = 'gloamingforest:thick_castle_brick'
$pillar  = 'gloamingforest:bold_castle_brick_pillar'
$field   = 'gloamingforest:violet_force_field'
$quartz  = 'minecraft:chiseled_quartz_block'

function Set-Stair($t, [int]$x, [int]$y, [int]$z, [string]$facing) {
    Set-GftBlock $t $x $y $z 'minecraft:quartz_stairs' @{ facing = $facing; half = 'bottom'; shape = 'straight'; waterlogged = 'false' }
}

# The cells of the ring one block inside the walls of a tower of `size`, in walking order from
# (1, startZ) towards +z, then +x, -z, -x (clockwise seen from above), with the facing of a step there.
function Get-Ring([int]$size, [int]$startZ, [int]$count) {
    $lo = 1; $hi = $size - 2
    $cells = [System.Collections.Generic.List[object]]::new()
    for ($z = $lo; $z -le $hi; $z++) { $cells.Add(@($lo, $z, 'south')) }
    for ($x = $lo + 1; $x -le $hi; $x++) { $cells.Add(@($x, $hi, 'east')) }
    for ($z = $hi - 1; $z -ge $lo; $z--) { $cells.Add(@($hi, $z, 'north')) }
    for ($x = $hi - 1; $x -gt $lo; $x--) { $cells.Add(@($x, $lo, 'west')) }
    $start = 0
    for ($i = 0; $i -lt $cells.Count; $i++) { if ($cells[$i][0] -eq $lo -and $cells[$i][1] -eq $startZ) { $start = $i; break } }
    $out = [System.Collections.Generic.List[object]]::new()
    for ($k = 0; $k -lt $count; $k++) { $out.Add($cells[($start + $k) % $cells.Count]) }
    return , $out
}

# A spiral stair: step k at ring cell k, y = y0 + k, a castle brick under it.
function Add-Spiral($t, [int]$size, [int]$startZ, [int]$y0, [int]$count) {
    $ring = Get-Ring $size $startZ $count
    for ($k = 0; $k -lt $count; $k++) {
        $c = $ring[$k]
        $y = $y0 + $k
        Set-Stair $t $c[0] $y $c[1] $c[2]
        if ($y - 1 -gt 0) { Set-GftBlock $t $c[0] ($y - 1) $c[1] $brick }
        # head room over the step (two blocks)
        foreach ($h in 1, 2) { if ($y + $h -lt $t.Size[1] - 1) { Set-GftBlock $t $c[0] ($y + $h) $c[1] 'minecraft:air' } }
    }
    return , $ring
}

# A floor of castle brick bordered by thick castle brick.
function Set-Floor($t, [int]$x1, [int]$y, [int]$z1, [int]$x2, [int]$z2) {
    Set-GftBox $t $x1 $y $z1 $x2 $y $z2 $brick
    Set-Ring $t $x1 $y $z1 $x2 $z2 $thick
}

# The border of a flat rectangle.
function Set-Ring($t, [int]$x1, [int]$y, [int]$z1, [int]$x2, [int]$z2, [string]$block) {
    Set-GftBox $t $x1 $y $z1 $x2 $y $z1 $block
    Set-GftBox $t $x1 $y $z2 $x2 $y $z2 $block
    Set-GftBox $t $x1 $y $z1 $x1 $y $z2 $block
    Set-GftBox $t $x2 $y $z1 $x2 $y $z2 $block
}

# ---- the boss gazebo ---------------------------------------------------------------------------------------------
$t = New-Gft -Id 'gloamingforest:final_castle/temp/gazebo' -Size 21, 12, 21
# inside: cleared
Set-GftBox $t 1 0 1 19 10 19 'minecraft:air'
# four walls 0..10 and the roof at y 11 (the procedural code upstream 4.8 left commented out)
Set-GftBox $t 0 0 0 20 10 0 $field
Set-GftBox $t 0 0 20 20 10 20 $field
Set-GftBox $t 0 0 0 0 10 20 $field
Set-GftBox $t 20 0 0 20 10 20 $field
Set-GftBox $t 0 11 0 20 11 20 $field
# the connector and the WIP marker share the centre block (air: the texts stand above it)
Add-GftJigsaw $t 10 0 10 -Front down -Top north -Pool 'minecraft:empty' -Name 'gf:final_castle/final_boss' -Target 'minecraft:empty'
Add-GftMarker $t 10 0 10 'final_castle_wip'
Save-Gft $t

# ---- the large tower ---------------------------------------------------------------------------------------------
$t = New-Gft -Id 'gloamingforest:final_castle/temp/large_tower' -Size 13, 61, 13
# a spiral from the door (the first step just inside it, going south) to the ceiling at y 59: the last
# step replaces a ceiling block, so the stair comes out on the roof deck
$ring = Add-Spiral $t 13 4 1 59
# two floors inside the ring, reached from the stair where it passes their height
foreach ($fy in 20, 40) {
    Set-Floor $t 2 $fy 2 10 10
}
# a bold pillar through the middle of the tower
Set-GftBox $t 6 1 6 6 58 6 $pillar @{ axis = 'y' }
Set-GftBox $t 6 20 6 6 20 6 $quartz
Set-GftBox $t 6 40 6 6 40 6 $quartz
# the sign of upstream's comment, by the pillar at the bottom
Set-GftBlock $t 6 1 7 'minecraft:oak_sign' @{ rotation = '0'; waterlogged = 'false' }
Add-GftMarker $t 6 1 7 'sign' @{ lines = @('', 'Parkour area 1', 'Unique monster?', '') }
# the connector inside the door
Set-GftBlock $t 1 1 2 'minecraft:air'
Add-GftJigsaw $t 1 1 2 -Front west -Top up -Pool 'minecraft:empty' -Name 'gf:final_castle/large_tower' -Target 'minecraft:empty'
Save-Gft $t

# ---- the bell tower ----------------------------------------------------------------------------------------------
$t = New-Gft -Id 'gloamingforest:final_castle/temp/bell_tower' -Size 21, 65, 21
# the entrance floor under the door (x 0, y 9..12)
Set-Floor $t 1 8 1 19 19
# the bell room floor under the windows
Set-Floor $t 1 47 1 19 19
# a spiral from the entrance floor up to the bell room floor (y 9 .. 47)
$ring = Add-Spiral $t 21 13 9 39
# the hole in the bell room floor over the last steps
for ($k = 33; $k -lt 38; $k++) { $c = $ring[$k]; Set-GftBlock $t $c[0] 47 $c[1] 'minecraft:air' }
# a gallery at the lower windows (y 24..38), next to the stair where it passes y 23
Set-Ring $t 2 23 2 18 18 $thick
# the bell room: four bold pillars to the ceiling, a beam across, the bell on its pedestal
foreach ($p in @(@(5, 5), @(15, 5), @(5, 15), @(15, 15))) { Set-GftBox $t $p[0] 48 $p[1] $p[0] 63 $p[1] $pillar @{ axis = 'y' } }
Set-GftBox $t 6 63 5 14 63 5 $pillar @{ axis = 'x' }
Set-GftBox $t 6 63 15 14 63 15 $pillar @{ axis = 'x' }
Set-GftBox $t 9 48 9 11 48 11 $thick
Set-GftBlock $t 10 49 10 $quartz
Set-GftBlock $t 10 50 10 'minecraft:bell' @{ attachment = 'floor'; facing = 'north'; powered = 'false' }
# the sign of upstream's comment, on the entrance floor
Set-GftBlock $t 7 9 8 'minecraft:oak_sign' @{ rotation = '4'; waterlogged = 'false' }
Add-GftMarker $t 7 9 8 'sign' @{ lines = @('', 'Parkour area 2', 'mini-boss 1', '') }
# the connector inside the door
Set-GftBlock $t 1 9 10 'minecraft:air'
Add-GftJigsaw $t 1 9 10 -Front west -Top up -Pool 'minecraft:empty' -Name 'gf:final_castle/room' -Target 'minecraft:empty'
Save-Gft $t
