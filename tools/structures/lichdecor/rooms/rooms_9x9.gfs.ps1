# U22 (lichdecor): the 9x9 rooms of the lich tower wings and the keepsake casket room of the
# special pool (original designs, rule R11/R19).
. (Join-Path $PSScriptRoot '../RoomLib.ps1')

$N = 9

function Add-Cells($t, [string]$side, [int]$depth = 2) {
    # iron-barred cells along a side wall (east or west), partitions every 3 cells
    $xWall = if ($side -eq 'east') { 9 } else { 1 }
    $dir = if ($side -eq 'east') { -1 } else { 1 }
    $front = $xWall + $dir * $depth
    for ($z = 1; $z -le 8; $z++) { foreach ($y in 1, 2, 3) { Set-RoomBlock $t $front $y $z 'minecraft:iron_bars' } }
    foreach ($z in 3, 6) { for ($k = 0; $k -lt $depth; $k++) { foreach ($y in 1, 2, 3) { Set-RoomBlock $t ($xWall + $dir * $k) $y $z 'minecraft:stone_bricks' } } }
}

function Add-ShelfRows($t, [int[]]$rows, [int]$top = 3, [string]$label = 'bookshelf|canopy_shelf') {
    foreach ($z in $rows) { for ($x = 2; $x -le 8; $x++) { if ($x -eq 5) { continue }; for ($y = 1; $y -le $top; $y++) { Add-RoomMarker $t $x $y $z $label } } }
}

$rooms = [ordered]@{
    altar             = @{ e = @('east'); l = @(1, 5); f = {
            Add-RoomCarpet $t 'twilightforest:coronation_carpet' 2
            for ($x = 3; $x -le 7; $x++) { Set-RoomBlock $t $x 1 8 'minecraft:chiseled_stone_bricks'; Add-RoomMarker $t $x 2 8 'candle:2-4' }
            Add-RoomMarker $t 5 1 7 'sinister_spawner:zombie,skeleton:4:8'
            foreach ($p in @(@(2, 6), @(8, 6), @(2, 3), @(8, 3))) { Add-RoomMarker $t $p[0] 1 $p[1] 'pedestal>skeleton_candle:2-3|zombie_candle:2-3' }
            Add-RoomHangers $t 'chain' 'candle:2-3'
        }
    }
    archives          = @{ e = @('south'); l = @(1, 5); f = {
            Add-ShelfRows $t @(3, 5, 7) 4
            Add-RoomShelves $t 2 'bookshelf|canopy_shelf' 2
            Add-RoomMarker $t 1 1 2 'chest:library@east'
            Add-RoomMarker $t 9 1 2 'chest:library@west'
            Add-RoomMarker $t 5 7 5 'chain50%1-2>candle:2-3'
        }
    }
    cauldron_keep     = @{ e = @('west'); l = @(1, 2); f = {
            foreach ($p in @(@(4, 4), @(6, 4), @(4, 6), @(6, 6))) { Add-RoomMarker $t $p[0] 1 $p[1] 'water_cauldron:1-3' }
            Set-RoomBlock $t 5 0 5 'minecraft:magma_block'
            Add-RoomMarker $t 5 1 5 'water_cauldron:3'
            Add-RoomCounter $t 'west' @('brewing_stand', 'mason_jar:potion')
            Add-RoomMarker $t 9 1 1 'chest:potion@west'
            Add-RoomHangers $t 'chain' 'candle:1-2'
        }
    }
    center_decor      = @{ e = @('east', 'west'); d = $true; f = {
            foreach ($p in (Get-RoomCorners $t 1)) { Add-RoomPillar $t $p[0] $p[1] }
            Add-RoomCandles $t 1 'candle:1-3'
            Add-RoomCarpet $t 'minecraft:moss_carpet' 2
        }
    }
    centerpiece       = @{ e = @('south'); l = @(1, 5); f = {
            Add-RoomPillar $t 5 5 'twilightforest:twisted_stone_pillar'
            foreach ($p in @(@(4, 5), @(6, 5), @(5, 4), @(5, 6))) { Set-RoomBlock $t $p[0] 1 $p[1] 'minecraft:chiseled_stone_bricks'; Add-RoomMarker $t $p[0] 2 $p[1] 'candle:2-4' }
            Add-RoomCarpet $t 'twilightforest:coronation_carpet' 2
        }
    }
    classic_library   = @{ e = @('east'); l = @(2); f = {
            Add-RoomShelves $t 5
            Add-ShelfRows $t @(4, 6) 3
            Add-RoomMarker $t 5 1 8 'lectern@north'
            Add-RoomMarker $t 4 1 8 'candle:1-3'
            Add-RoomMarker $t 6 1 8 'chest:library@north'
            Add-RoomMarker $t 5 7 5 'chain50%1-2>candle:2-4'
        }
    }
    elbow_junction    = @{ e = @('east'); f = {
            foreach ($p in @(@(3, 3), @(7, 7), @(3, 7))) { Add-RoomPillar $t $p[0] $p[1] }
            Add-RoomHangers $t 'rope' 'candle:1-2'
        }
    }
    enchanting_prison = @{ e = @('west'); l = @(1, 5); f = {
            for ($x = 4; $x -le 6; $x++) { for ($z = 4; $z -le 6; $z++) { if ($x -ne 5 -or $z -ne 5) { foreach ($y in 1, 2, 3) { Set-RoomBlock $t $x $y $z 'minecraft:iron_bars' } } } }
            Set-RoomBlock $t 5 1 5 'minecraft:enchanting_table'
            for ($x = 4; $x -le 6; $x++) { for ($z = 4; $z -le 6; $z++) { Set-RoomBlock $t $x 4 $z 'minecraft:stone_brick_slab' $script:Slab } }
            Add-RoomShelves $t 2
            Add-RoomMarker $t 8 1 2 'chest:enchanting@west'
            Add-RoomMarker $t 2 1 7 'spawner:skeleton:3'
        }
    }
    full_junction     = @{ e = @('east', 'south', 'west'); f = {
            foreach ($p in (Get-RoomCorners $t 1)) { Add-RoomPillar $t $p[0] $p[1] }
            Add-RoomHangers $t 'chain' 'candle:1-3'
            Add-RoomCandles $t 1 'candle:1-2'
        }
    }
    holding           = @{ e = @('south'); f = {
            Add-Cells $t 'east'; Add-Cells $t 'west'
            Add-RoomMarker $t 8 1 2 'zombie_trap'
            Add-RoomMarker $t 2 1 5 'zombie_trap'
            Add-RoomMarker $t 9 1 7 'skeleton_skull'
            Add-RoomMarker $t 1 1 8 'chest:room@east'
        }
    }
    keepsake_casket   = @{ e = @(); NoLadder = $true; f = {
            Add-RoomCarpet $t 'twilightforest:coronation_carpet' 1
            Set-RoomBlock $t 5 1 7 'minecraft:chiseled_stone_bricks'
            Set-RoomBlock $t 5 2 7 'twilightforest:keepsake_casket'
            foreach ($x in 3, 7) { Set-RoomBlock $t $x 1 7 'minecraft:chiseled_stone_bricks'; Add-RoomMarker $t $x 2 7 'candle:3-4' }
            foreach ($p in (Get-RoomCorners $t 1)) { Add-RoomPillar $t $p[0] $p[1] 'twilightforest:twisted_stone_pillar' }
            Add-RoomMarker $t 1 1 1 'skeleton_candle:2-4'
            Add-RoomMarker $t 9 1 1 'skeleton_candle:2-4'
            Add-RoomMarker $t 5 1 9 'chest:room@north'
            Add-RoomHangers $t 'chain' 'candle:2-3'
        }
    }
    large_study       = @{ e = @('east'); l = @(1, 5); f = {
            foreach ($z in 3, 6) { for ($x = 3; $x -le 4; $x++) { Set-RoomBlock $t $x 1 $z 'twilightforest:canopy_slab' $script:TopSlab }; Add-RoomMarker $t 3 2 $z 'candle:1-2'; Add-RoomMarker $t 4 2 $z 'mason_jar:library'; Add-RoomMarker $t 5 1 $z 'canopy_stairs@west' }
            Add-RoomShelves $t 3 'bookshelf|canopy_shelf' 2
            Add-RoomMarker $t 8 1 8 'chest:library@north'
            Add-RoomMarker $t 7 1 5 'empty_lectern@west'
        }
    }
    lectern_hall      = @{ e = @('south'); l = @(1, 5); f = {
            foreach ($z in 3, 5, 7) { foreach ($x in 3, 7) { Add-RoomMarker $t $x 1 $z 'lectern@north' } }
            Add-RoomChiseledShelves $t 'east' 2; Add-RoomChiseledShelves $t 'west' 2
            Add-RoomMarker $t 5 7 5 'chain50%1>candle:2-3'
        }
    }
    library_junction  = @{ e = @('east', 'south', 'west'); l = @(2, 4); f = {
            foreach ($p in (Get-RoomCorners $t 0)) { foreach ($dx in 0, 1) { foreach ($dz in 0, 1) { $x = if ($p[0] -eq 1) { 1 + $dx } else { 9 - $dx }; $z = if ($p[1] -eq 1) { 1 + $dz } else { 9 - $dz }; foreach ($y in 1, 2, 3) { Add-RoomMarker $t $x $y $z 'bookshelf|canopy_shelf' } } } }
            Add-RoomMarker $t 5 1 6 'candled_lectern@north'
        }
    }
    lockup            = @{ e = @('east'); f = {
            Add-Cells $t 'west' 3
            Add-RoomMarker $t 2 1 2 'zombie_trap'
            Add-RoomMarker $t 1 1 5 'chest:room@east'
            Add-RoomMarker $t 2 1 8 'skeleton_skull|zombie_head'
            Add-RoomMarker $t 7 1 7 'spawner:zombie:3'
        }
    }
    mossy_junction    = @{ e = @('east', 'south', 'west'); l = @(1, 5); Floor = 'minecraft:mossy_stone_bricks'; f = {
            Add-RoomCarpet $t 'minecraft:moss_carpet' 1
            foreach ($p in (Get-RoomCorners $t 1)) { Add-RoomPillar $t $p[0] $p[1] 'minecraft:mossy_stone_bricks' }
            Add-RoomMarker $t 1 1 1 'firefly_jar'
            Add-RoomHangers $t 'rope' 'air'
        }
    }
    seven             = @{ e = @('west'); l = @(2, 4); f = {
            foreach ($p in @(@(5, 3), @(7, 3), @(8, 5), @(7, 7), @(3, 7), @(2, 5), @(3, 3))) { Add-RoomPillar $t $p[0] $p[1] 'twilightforest:twisted_stone_pillar' }
            Add-RoomMarker $t 5 1 5 'sinister_spawner:skeleton:3:7'
            foreach ($p in @(@(6, 2), @(8, 4), @(8, 6), @(5, 7), @(2, 6), @(2, 4), @(4, 2))) { Add-RoomMarker $t $p[0] 1 $p[1] 'candle:1-3' }
        }
    }
    straight_junction = @{ e = @('south'); f = {
            foreach ($z in 3, 7) { Add-RoomPillar $t 3 $z; Add-RoomPillar $t 7 $z }
            Add-RoomCandles $t 1 'candle:1-3'
        }
    }
    study             = @{ e = @('east'); l = @(1, 2, 4, 5); f = {
            for ($x = 3; $x -le 7; $x++) { Set-RoomBlock $t $x 1 4 'twilightforest:canopy_slab' $script:TopSlab }
            Add-RoomMarker $t 4 2 4 'candle:1-3'; Add-RoomMarker $t 6 2 4 'mason_jar:library'
            Add-RoomMarker $t 5 1 3 'canopy_stairs@south'
            Add-RoomChiseledShelves $t 'west' 2
            Add-RoomMarker $t 1 1 2 'chest:library@east'
        }
    }
    t_junction        = @{ e = @('east', 'west'); f = {
            Add-RoomPillar $t 5 8 'minecraft:chiseled_stone_bricks'
            Add-RoomMarker $t 4 1 8 'candle:1-3'; Add-RoomMarker $t 6 1 8 'candle:1-3'
            Add-RoomHangers $t 'chain' 'air'
        }
    }
    tiered_library    = @{ e = @('west'); f = {
            Add-RoomMezzanine $t 5 2
            Add-RoomShelves $t 4
            foreach ($cell in (Get-RoomRing $t)) { foreach ($y in 6, 7, 8) { Add-RoomMarker $t $cell[0] $y $cell[1] 'bookshelf|canopy_shelf' } }
            for ($y = 1; $y -le 5; $y++) { Set-RoomBlock $t 4 $y 9 'minecraft:ladder' @{ facing = 'north' } }
            Add-RoomMarker $t 5 1 5 'lectern@north'
            Add-RoomMarker $t 2 6 2 'chest:library@south'
        }
    }
    tiered_spawner    = @{ e = @('east'); f = {
            Add-RoomMezzanine $t 5 2
            for ($y = 1; $y -le 5; $y++) { Set-RoomBlock $t 4 $y 9 'minecraft:ladder' @{ facing = 'north' } }
            Add-RoomMarker $t 2 6 2 'spawner:skeleton:3'
            Add-RoomMarker $t 8 6 8 'spawner:zombie:3'
            Add-RoomMarker $t 5 1 5 'spawner'
            Add-RoomMarker $t 8 6 2 'chest:room@south'
        }
    }
    tiered_study      = @{ e = @('south'); f = {
            Add-RoomMezzanine $t 5 2
            for ($y = 1; $y -le 5; $y++) { Set-RoomBlock $t 4 $y 9 'minecraft:ladder' @{ facing = 'north' } }
            foreach ($x in 2, 3) { Add-RoomMarker $t $x 6 1 'bookshelf|canopy_shelf' }
            Add-RoomMarker $t 7 6 1 'chest:library@south'
            Add-RoomMarker $t 8 6 2 'candle:1-3'
            Add-RoomMarker $t 5 1 5 'candled_lectern@north'
            Add-RoomMarker $t 2 1 2 'mason_jar:library'
        }
    }
    winding_ways      = @{ e = @('east', 'west'); f = {
            # low maze walls of stone brick with lit candles on the ends
            foreach ($seg in @(@(2, 3, 4, 3), @(6, 3, 8, 3), @(4, 5, 6, 5), @(2, 7, 3, 7), @(7, 7, 8, 7), @(3, 4, 3, 5), @(7, 4, 7, 6))) {
                for ($x = $seg[0]; $x -le $seg[2]; $x++) { for ($z = $seg[1]; $z -le $seg[3]; $z++) { foreach ($y in 1, 2) { Set-RoomBlock $t $x $y $z 'minecraft:stone_bricks' } } }
            }
            foreach ($p in @(@(4, 3), @(6, 3), @(5, 5))) { Add-RoomMarker $t $p[0] 4 $p[1] 'below>candle:1-2' }
            Add-RoomMarker $t 5 1 8 'chest:room@north'
        }
    }
}

foreach ($name in $rooms.Keys) {
    $r = $rooms[$name]
    $lb = if ($r.ContainsKey('l')) { $r.l } else { @() }
    $args2 = @{ Name = $name; N = $N; Exits = $r.e; LadderBelow = $lb; Decor = [bool]$r.d }
    if ($r.ContainsKey('Floor')) { $args2.Floor = $r.Floor }
    if ($r.NoLadder) { $args2.NoLadderUp = $true }
    $t = New-LichRoom @args2
    & $r.f
    Save-Gft $t
}
