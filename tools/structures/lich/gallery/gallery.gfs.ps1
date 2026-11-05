# U21 (lich): the magic galleries off the top slice of the lich tower and their roofs (original
# designs, rule R19). A gallery runs along +z from its junction (facing north at (centre, 1, 0)); the
# "painting" marker is where U22's magic painting of the gallery hangs, the others hang paintings of
# #twilightforest:tower_paintings. Roofs come in odd and even widths (LichTowerUtil#rollGalleryRoof).
. (Join-Path $PSScriptRoot '../LichLib.ps1')
$NS = 'twilightforest:lich_tower/gallery/'
$W = 9; $H = 7; $L = 13; $C = 4

$styles = [ordered]@{
    castaway_paradise    = @('minecraft:stone_bricks', 'twilightforest:twilight_oak_planks', 'minecraft:light_blue_carpet')
    darkness             = @('minecraft:deepslate_bricks', 'twilightforest:canopy_planks', 'minecraft:black_carpet')
    lucid_lands          = @('minecraft:stone_bricks', 'twilightforest:twilight_oak_planks', 'minecraft:lime_carpet')
    music_in_the_mire    = @('minecraft:mossy_stone_bricks', 'twilightforest:canopy_planks', 'minecraft:green_carpet')
    the_hostile_paradise = @('minecraft:stone_bricks', 'twilightforest:canopy_planks', 'minecraft:red_carpet')
}
foreach ($name in $styles.Keys) {
    $s = $styles[$name]
    $t = New-Gft -Id "$NS$name" -Size $W, $H, $L -Out "lich/gallery/$name"
    Set-GftBox $t 0 0 0 ($W - 1) 0 ($L - 1) $s[1]
    Set-GftBox $t 0 1 0 ($W - 1) ($H - 1) ($L - 1) $s[0] -Hollow
    Set-GftBox $t 1 1 1 ($W - 2) ($H - 2) ($L - 2) 'minecraft:air'
    Set-GftBox $t 2 1 2 ($W - 3) 1 ($L - 3) $s[2]
    foreach ($z in 3, 6, 9) { foreach ($x in 0, ($W - 1)) { Set-GftBlock $t $x 4 $z 'minecraft:iron_bars' } }
    foreach ($p in @(@(1, 1), @(7, 1), @(1, 11), @(7, 11))) { Set-GftBox $t $p[0] 1 $p[1] $p[0] 5 $p[1] 'minecraft:chiseled_stone_bricks' }
    Set-LichDoor $t $C 1 0
    Add-GftJigsaw $t $C 1 0 -Front north -Name 'gf:lich_tower/bridge_center'
    Add-GftJigsaw $t $C ($H - 1) 6 -Front up -Top north -Name 'gf:lich_tower/gallery_top' -Target 'gf:lich_tower/roof'
    Set-GftBlock $t $C 3 ($L - 2) 'minecraft:air'; Add-GftDataMarker $t $C 3 ($L - 2) 'painting'
    Add-GftDataMarker $t 1 3 4 'east:2:2'
    Add-GftDataMarker $t 7 3 8 'west:2:2'
    Save-Gft $t
}

# the roofs: (width, style); the junction faces down at the bottom centre
foreach ($odd in $true, $false) {
    $w = if ($odd) { 9 } else { 10 }
    $c = [int][Math]::Floor(($w - 1) / 2)
    $suffix = if ($odd) { 'odd' } else { 'even' }
    foreach ($style in 'fence', 'slabs', 'stairs') {
        $id = "$NS${style}_roof_$suffix"
        $t = New-Gft -Id $id -Size $w, 4, $L -Out "lich/gallery/${style}_roof_$suffix"
        switch ($style) {
            'fence' {
                Set-GftBox $t 0 0 0 ($w - 1) 0 ($L - 1) 'minecraft:stone_bricks'
                Set-GftBox $t 0 1 0 ($w - 1) 1 ($L - 1) 'twilightforest:canopy_fence' -Hollow
                Set-GftBox $t 1 1 1 ($w - 2) 1 ($L - 2) 'minecraft:air'
            }
            'slabs' {
                for ($y = 0; $y -lt 4; $y++) {
                    $x0 = $y; $x1 = $w - 1 - $y
                    if ($x0 -gt $x1) { break }
                    Set-GftBox $t $x0 $y 0 $x1 $y ($L - 1) 'minecraft:stone_brick_slab' @{ type = 'bottom' }
                    if ($x1 - $x0 -ge 2) { Set-GftBox $t ($x0 + 1) $y 0 ($x1 - 1) $y ($L - 1) 'minecraft:stone_bricks' }
                }
            }
            'stairs' {
                for ($y = 0; $y -lt 4; $y++) {
                    $x0 = $y; $x1 = $w - 1 - $y
                    if ($x0 -gt $x1) { break }
                    Set-GftBox $t $x0 $y 0 $x0 $y ($L - 1) 'minecraft:stone_brick_stairs' @{ facing = 'east'; half = 'bottom' }
                    Set-GftBox $t $x1 $y 0 $x1 $y ($L - 1) 'minecraft:stone_brick_stairs' @{ facing = 'west'; half = 'bottom' }
                    if ($x1 - $x0 -ge 2) { Set-GftBox $t ($x0 + 1) $y 0 ($x1 - 1) $y ($L - 1) 'minecraft:stone_bricks' }
                }
            }
        }
        Add-GftJigsaw $t $c 0 6 -Front down -Top north -Name 'gf:lich_tower/roof'
        Save-Gft $t
    }
}
