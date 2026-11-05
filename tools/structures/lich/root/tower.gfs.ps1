# U21 (lich): the central lich tower (original GloamingForest designs at TF's template ids, rule R19):
# foyer, base, trim, slice, boss room, boss boundary, boss roof and the foyer shelves.
. (Join-Path $PSScriptRoot '../LichLib.ps1')

$W = 13; $C = 6; $Cut = 2
$NS = 'twilightforest:lich_tower/'
# the templates TF's code names directly (no template definition): GloamingForest ids
$GNS = 'gf:lich_tower/'

# ---- tower_foyer: the entrance hall in front of the tower ---------------------------------------------
$t = New-Gft -Id "${GNS}tower_foyer" -Size 11, 7, 9 -Out 'lich/root/tower_foyer'
Set-GftBox $t 0 0 0 10 0 8 'minecraft:stone_bricks'
for ($y = 1; $y -le 4; $y++) {
    $b = if ($y -eq 4) { 'minecraft:polished_andesite' } else { 'minecraft:stone_bricks' }
    Set-GftBox $t 0 $y 0 10 $y 8 $b -Hollow
    Set-GftBox $t 1 $y 1 9 $y 7 'minecraft:air'
}
# a pitched roof of stone brick stairs
Set-GftBox $t 0 5 0 10 5 8 'minecraft:stone_brick_slab' -Properties @{ type = 'bottom' }
Set-GftBox $t 1 5 1 9 5 7 'minecraft:stone_bricks'
Set-GftBox $t 2 6 2 8 6 6 'minecraft:stone_brick_slab' -Properties @{ type = 'bottom' }
foreach ($x in 0, 10) { foreach ($z in 0, 8) { Set-GftBox $t $x 1 $z $x 5 $z 'minecraft:chiseled_stone_bricks' } }
# windows
foreach ($z in 3, 5) { Set-GftBlock $t 0 2 $z 'minecraft:iron_bars'; Set-GftBlock $t 10 2 $z 'minecraft:iron_bars' }
# the entrance (north) and the door to the tower (south)
Set-LichDoor $t 5 1 0
Set-LichDoor $t 5 1 8
Set-GftBlock $t 4 1 0 'minecraft:stone_brick_wall'; Set-GftBlock $t 6 1 0 'minecraft:stone_brick_wall'
Add-GftJigsaw $t 5 0 4 -Front down -Top south -Name 'gf:lich_tower/vestibule'
Add-GftJigsaw $t 5 1 8 -Front south -Name 'gf:lich_tower/foyer' -Target 'gf:lich_tower/tower_base'
Add-GftJigsaw $t 5 0 0 -Front north -Name 'gf:lich_tower/foyer' -Target 'gf:lich_tower/path'
foreach ($z in 2, 4, 6) {
    Add-GftJigsaw $t 0 1 $z -Front east -Name 'gf:shelf_wall' -Target 'gf:shelf'
    Add-GftJigsaw $t 10 1 $z -Front west -Name 'gf:shelf_wall' -Target 'gf:shelf'
}
Set-GftBlock $t 2 1 7 'minecraft:air'; Add-GftDataMarker $t 2 1 7 'chest_a@north'
Set-GftBlock $t 8 1 7 'minecraft:air'; Add-GftDataMarker $t 8 1 7 'chest_b@north'
Save-Gft $t

# ---- foyer_decor: one shelf niche (the piece places its block in code) ----------------------------------
$t = New-Gft -Id "${GNS}foyer_decor" -Size 1, 2, 1 -Out 'lich/root/foyer_decor'
Set-GftBlock $t 0 0 0 'minecraft:air'
Set-GftBlock $t 0 1 0 'minecraft:air'
Add-GftJigsaw $t 0 0 0 -Front north -Name 'gf:shelf'
Save-Gft $t

# ---- tower_base: the bottom of the central tower -------------------------------------------------------
$H = 12
$t = New-Gft -Id "${GNS}tower_base" -Size $W, $H, $W -Out 'lich/root/tower_base'
Set-LichLayer $t 0 $W $Cut 'minecraft:stone_bricks' 'minecraft:stone_bricks'
for ($y = 1; $y -lt $H; $y++) { Set-LichWallCourse $t $y $W $Cut ($y % 5 -eq 0) }
# an inlaid floor pattern
Set-LichLayer $t 0 7 1 'minecraft:polished_andesite' 'minecraft:chiseled_stone_bricks' 3 3
# corner pillars inside
foreach ($p in @(@(2, 2), @(10, 2), @(2, 10), @(10, 10))) { Set-GftBox $t $p[0] 1 $p[1] $p[0] ($H - 1) $p[1] 'minecraft:stone_bricks' }
Set-LichDoor $t $C 1 0
Add-GftJigsaw $t $C 1 0 -Front north -Name 'gf:lich_tower/tower_base'
foreach ($j in @(@(12, 1, 6, 'east'), @(0, 1, 6, 'west'), @(6, 1, 12, 'south'), @(12, 7, 6, 'east'), @(0, 7, 6, 'west'))) {
    Set-LichDoor $t $j[0] $j[1] $j[2]
    Add-GftJigsaw $t $j[0] $j[1] $j[2] -Front $j[3] -Name 'gf:lich_tower/bridge_out' -Target 'gf:lich_tower/bridge'
}
Add-GftJigsaw $t $C ($H - 1) $C -Front up -Top north -Name 'gf:lich_tower/tower_above' -Target 'gf:lich_tower/tower_below'
Add-GftJigsaw $t $C 0 $C -Front up -Top north -Name 'gf:lich_tower/decor_floor' -Target 'gf:lich_tower/decor'
Add-GftJigsaw $t $C 0 1 -Front down -Top north -Name 'gf:lich_tower/trim_floor' -Target 'gf:lich_tower/tower_trim'
foreach ($p in @(@(3, 1, 3), @(9, 1, 3), @(3, 1, 9), @(9, 1, 9), @(4, 1, 1), @(8, 1, 11))) { Set-GftBlock $t $p[0] $p[1] $p[2] 'minecraft:air'; Add-GftDataMarker $t $p[0] $p[1] $p[2] 'candle:1' }
foreach ($p in @(@(2, 1, 6), @(10, 1, 4))) { Set-GftBlock $t $p[0] $p[1] $p[2] 'minecraft:air'; Add-GftDataMarker $t $p[0] $p[1] $p[2] 'candle:2' }
Save-Gft $t

# ---- central_trim: the stepped foundation under the base -------------------------------------------------
$t = New-Gft -Id "${GNS}central_trim" -Size 15, 2, 15 -Out 'lich/root/central_trim'
Set-LichLayer $t 1 15 3 'minecraft:polished_andesite' 'minecraft:stone_bricks'
for ($x = 0; $x -lt 15; $x++) {
    for ($z = 0; $z -lt 15; $z++) {
        if (-not (Test-LichRing $x $z 15 3)) { continue }
        $f = if ($z -eq 0) { 'north' } elseif ($z -eq 14) { 'south' } elseif ($x -eq 0) { 'west' } elseif ($x -eq 14) { 'east' } else { $null }
        if ($f) { Set-GftBlock $t $x 1 $z 'minecraft:polished_andesite_stairs' @{ facing = @{ north = 'south'; south = 'north'; west = 'east'; east = 'west' }[$f]; half = 'bottom' } }
    }
}
Set-LichLayer $t 0 13 2 'minecraft:stone_bricks' 'minecraft:stone_bricks' 1 1
Add-GftJigsaw $t 7 1 2 -Front up -Top north -Name 'gf:lich_tower/tower_trim'
Save-Gft $t

# ---- tower_slice: one storey of the shaft with its spiral stairs ------------------------------------------
$H = 6
$t = New-Gft -Id "${GNS}tower_slice" -Size $W, $H, $W -Out 'lich/root/tower_slice'
for ($y = 0; $y -lt $H; $y++) { Set-LichWallCourse $t $y $W $Cut ($y -eq 0) }
# the spiral: 12 half steps around the inside of the wall
$path = Get-LichPerimeter $W $Cut
for ($i = 0; $i -lt 12; $i++) {
    $cell = $path[[int][Math]::Floor($i * $path.Count / 12)]
    $y = [int][Math]::Floor($i / 2)
    $type = if ($i % 2 -eq 0) { 'bottom' } else { 'top' }
    Set-GftBlock $t $cell[0] $y $cell[1] 'twilightforest:twilight_oak_slab' @{ type = $type }
    $next = $path[([int][Math]::Floor($i * $path.Count / 12) + 1) % $path.Count]
    Set-GftBlock $t $next[0] $y $next[1] 'twilightforest:twilight_oak_slab' @{ type = $type }
}
# windows and wing doors
foreach ($j in @(@(12, 1, 6, 'east'), @(0, 1, 6, 'west'), @(6, 1, 0, 'north'), @(6, 1, 12, 'south'))) {
    Set-LichDoor $t $j[0] $j[1] $j[2]
    Add-GftJigsaw $t $j[0] $j[1] $j[2] -Front $j[3] -Name 'gf:lich_tower/bridge_out' -Target 'gf:lich_tower/bridge'
}
foreach ($w in @(@(3, 3, 0), @(9, 3, 12), @(0, 3, 9), @(12, 3, 3))) { Set-GftBlock $t $w[0] $w[1] $w[2] 'minecraft:iron_bars' }
Add-GftJigsaw $t 1 3 6 -Front east -Name 'gf:mob_bridge_wall' -Target 'gf:mob_bridge'
Add-GftJigsaw $t $C 0 $C -Front down -Top north -Name 'gf:lich_tower/tower_below'
Add-GftJigsaw $t $C ($H - 1) $C -Front up -Top north -Name 'gf:lich_tower/tower_above' -Target 'gf:lich_tower/tower_below'
Add-GftDataMarker $t 6 3 2 'south:2:2'
Add-GftDataMarker $t 10 3 6 'west:2:2'
Save-Gft $t

# ---- tower_boss_room: the Lich's hall on top of the tower ------------------------------------------------
$B = 25; $BC = 12; $BH = 14
$t = New-Gft -Id "${GNS}tower_boss_room" -Size $B, $BH, $B -Out 'lich/root/tower_boss_room'
Set-LichLayer $t 0 $B 6 'minecraft:stone_bricks' 'minecraft:polished_andesite'
Set-LichLayer $t 0 15 4 'minecraft:chiseled_stone_bricks' 'minecraft:stone_bricks' 5 5
for ($y = 1; $y -lt $BH; $y++) { Set-LichWallCourse $t $y $B 6 ($y % 4 -eq 0) }
foreach ($y in 4, 5, 9, 10) { foreach ($p in @(@(12, 0), @(12, 24), @(0, 12), @(24, 12))) { Set-GftBlock $t $p[0] $y $p[1] 'minecraft:iron_bars' } }
Set-GftBlock $t $BC 5 $BC 'twilightforest:lich_boss_spawner'
Add-GftJigsaw $t $BC 0 $BC -Front down -Top north -Name 'gf:lich_tower/tower_below'
Add-GftJigsaw $t $BC ($BH - 1) $BC -Front up -Top north -Name 'gf:lich_tower/boss_room_top' -Target 'gf:lich_tower/tower_below'
foreach ($m in @(@(12, 3, 1, 'south'), @(12, 3, 23, 'north'), @(1, 3, 12, 'east'), @(23, 3, 12, 'west'))) { Add-GftDataMarker $t $m[0] $m[1] $m[2] "$($m[3]):3:3" }
Save-Gft $t

# ---- tower_boss_boundary: the buttresses around the hall -------------------------------------------------
$t = New-Gft -Id "${GNS}tower_boss_boundary" -Size $B, $BH, $B -Out 'lich/root/tower_boss_boundary'
foreach ($p in @(@(4, 4), @(20, 4), @(4, 20), @(20, 20))) {
    Set-GftBox $t $p[0] 0 $p[1] $p[0] ($BH - 1) $p[1] 'minecraft:chiseled_stone_bricks'
}
Set-LichLayer $t ($BH - 1) $B 6 'minecraft:stone_brick_wall' ''
Add-GftJigsaw $t $BC 0 $BC -Front down -Top north -Name 'gf:lich_tower/tower_below'
Save-Gft $t

# ---- tower_boss_roof: the pointed roof ------------------------------------------------------------------
$t = New-Gft -Id "${GNS}tower_boss_roof" -Size $B, 13, $B -Out 'lich/root/tower_boss_roof'
for ($y = 0; $y -lt 12; $y++) {
    $w = $B - 2 * $y
    if ($w -lt 3) { break }
    $cut = [Math]::Max(1, 6 - $y)
    $ring = if ($y % 3 -eq 2) { 'minecraft:polished_andesite' } else { 'minecraft:stone_bricks' }
    $fill = if ($y -eq 0) { 'minecraft:stone_bricks' } else { 'minecraft:air' }
    Set-LichLayer $t $y $w $cut $ring $fill $y $y
}
Set-GftBlock $t $BC 12 $BC 'minecraft:stone_brick_wall'
Add-GftJigsaw $t $BC 0 $BC -Front down -Top north -Name 'gf:lich_tower/tower_below'
Save-Gft $t
