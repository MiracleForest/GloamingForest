# U22 (lichdecor): the 7x7 rooms of the lich tower wings (original designs, rule R11/R19). Rooms of
# the ladder_<n> pools are also entered from a room below; tiered rooms are taller.
. (Join-Path $PSScriptRoot '../RoomLib.ps1')

$N = 7
$ironPost = @{ post = 'post' }

function Add-Cage($t, [int]$x0, [int]$z0, [int]$x1, [int]$z1, [int]$top = 3) {
    for ($x = $x0; $x -le $x1; $x++) {
        for ($z = $z0; $z -le $z1; $z++) {
            if ($x -ne $x0 -and $x -ne $x1 -and $z -ne $z0 -and $z -ne $z1) { continue }
            for ($y = 1; $y -le $top; $y++) { Set-RoomBlock $t $x $y $z 'minecraft:iron_bars' }
        }
    }
    for ($x = $x0; $x -le $x1; $x++) { for ($z = $z0; $z -le $z1; $z++) { Set-RoomBlock $t $x ($top + 1) $z 'minecraft:stone_brick_slab' $script:Slab } }
}

function Add-Planter($t, [int]$x0, [int]$z0, [int]$x1, [int]$z1, [string]$soil, [string]$plant, [int]$every = 1) {
    $i = 0
    for ($x = $x0; $x -le $x1; $x++) {
        for ($z = $z0; $z -le $z1; $z++) {
            if (-not (Test-RoomFree $t $x 1 $z) -or -not (Test-RoomFree $t $x 2 $z)) { continue }
            Set-GftBlock $t $x 1 $z $soil
            if (($i++ % $every) -eq 0) { Set-GftBlock $t $x 2 $z $plant }
        }
    }
}

$rooms = [ordered]@{
    altar             = @{ e = @('east'); l = @(0, 4); f = {
            Add-RoomCarpet $t 'gloamingforest:coronation_carpet' 1
            for ($x = 3; $x -le 5; $x++) { Set-RoomBlock $t $x 1 6 'minecraft:chiseled_stone_bricks'; Add-RoomMarker $t $x 2 6 'candle:2-4' }
            Add-RoomMarker $t 4 1 5 'sinister_spawner:skeleton,zombie:3:6'
            Add-RoomMarker $t 2 1 5 'skeleton_candle:2-3:0+4'
            Add-RoomMarker $t 6 1 5 'zombie_candle:2-3:0+4'
            Add-RoomCandles $t 1 'candle:1-2'
        }
    }
    altars            = @{ e = @('south'); l = @(0, 4); f = {
            foreach ($x in 2, 6) { Add-RoomAltar $t $x 3 'candle:2-4'; Add-RoomAltar $t $x 5 'candle:1-3' }
            Add-RoomMarker $t 1 1 1 'skeleton_candle:1-2'
            Add-RoomMarker $t 7 1 1 'skeleton_candle:1-2'
            Add-RoomHangers $t 'chain' 'candle:1-2'
        }
    }
    barbed_webs       = @{ e = @('east', 'west'); f = {
            Add-RoomMarker $t 4 1 6 'spawner:spider,cave_spider,swarm_spider:4'
            foreach ($p in @(@(2, 2), @(6, 2), @(2, 5), @(6, 5), @(4, 3))) { Add-RoomMarker $t $p[0] 1 $p[1] 'wrought_iron_post' }
            for ($x = 1; $x -le 7; $x++) { for ($z = 1; $z -le 7; $z++) { if ((($x * 5 + $z * 3) % 5) -eq 1) { foreach ($y in 2, 5, 6) { Add-RoomMarker $t $x $y $z 'cobweb' } } } }
        }
    }
    book_staircases   = @{ e = @('west'); f = {
            # two flights of canopy stairs climbing the east and west walls over shelves
            for ($i = 0; $i -lt 5; $i++) {
                $z = 2 + $i; $y = 1 + $i
                foreach ($x in 6, 7) {
                    Set-RoomBlock $t $x $y $z 'gloamingforest:canopy_stairs' @{ facing = 'south'; half = 'bottom' }
                    for ($yy = 1; $yy -lt $y; $yy++) { Add-RoomMarker $t $x $yy $z 'bookshelf|canopy_shelf' }
                }
            }
            for ($z = 2; $z -le 6; $z++) { foreach ($y in 1, 2, 3) { Add-RoomMarker $t 1 $y $z 'bookshelf|canopy_shelf' } }
            Add-RoomMarker $t 4 1 4 'lectern@north'
            Add-RoomMarker $t 4 7 4 'chain50%1-2>candle:1-3'
        }
    }
    cactus            = @{ e = @('east'); f = {
            Add-Planter $t 2 2 3 5 'minecraft:sand' 'minecraft:cactus' 2
            Add-Planter $t 5 2 6 5 'minecraft:red_sand' 'minecraft:dead_bush' 3
            Add-RoomMarker $t 4 1 6 'water_cauldron:1-2'
            Add-RoomMarker $t 1 1 1 'firefly_jar'
        }
    }
    corner_caging     = @{ e = @('south'); f = {
            Add-Cage $t 1 1 2 2
            Add-Cage $t 6 1 7 2
            Add-RoomMarker $t 1 1 1 'zombie_head|skeleton_skull'
            Add-RoomMarker $t 7 1 1 'zombie_head|skeleton_skull'
            Add-Cage $t 3 5 5 6 2
            Add-RoomMarker $t 4 1 6 'chest:room@north'
        }
    }
    desk              = @{ e = @('east'); l = @(0, 4); f = {
            for ($x = 2; $x -le 5; $x++) { Set-RoomBlock $t $x 1 5 'gloamingforest:canopy_slab' $script:TopSlab }
            Add-RoomMarker $t 3 2 5 'candle:1-2'
            Add-RoomMarker $t 5 2 5 'mason_jar:library'
            Add-RoomMarker $t 4 1 4 'canopy_stairs@south'
            Add-RoomShelves $t 2 'bookshelf|canopy_shelf' 2
            Add-RoomMarker $t 1 1 4 'chest:library@east'
        }
    }
    elbow_chest       = @{ e = @('west'); l = @(1, 3); f = {
            Add-RoomMarker $t 7 1 4 'chest:room@west'
            Set-RoomBlock $t 7 1 3 'minecraft:polished_andesite'; Set-RoomBlock $t 7 1 5 'minecraft:polished_andesite'
            Add-RoomMarker $t 7 2 3 'candle:1-3'; Add-RoomMarker $t 7 2 5 'candle:1-3'
            Add-RoomHangers $t 'rope' 'air'
        }
    }
    elbow_junction    = @{ e = @('east'); f = {
            foreach ($p in @(@(2, 2), @(6, 6), @(2, 6))) { Add-RoomPillar $t $p[0] $p[1] }
            Add-RoomMarker $t 6 1 2 'candle:1-3'
        }
    }
    full_junction     = @{ e = @('east', 'south', 'west'); f = {
            foreach ($p in (Get-RoomCorners $t 1)) { Add-RoomPillar $t $p[0] $p[1] }
            Add-RoomCandles $t 1 'candle:1-2'
        }
    }
    full_junction_1   = @{ e = @('east', 'south', 'west'); l = @(0, 4); f = {
            Add-RoomCarpet $t 'gloamingforest:coronation_carpet' 2
            Add-RoomHangers $t 'chain' 'candle:1-3'
        }
    }
    full_junction_2   = @{ e = @('east', 'south', 'west'); f = {
            for ($x = 2; $x -le 6; $x++) { for ($z = 2; $z -le 6; $z++) { Set-GftBlock $t $x 0 $z $(if (($x + $z) % 2) { 'gloamingforest:terrorcotta_lines' } else { 'gloamingforest:terrorcotta_arcs' }) } }
            Add-RoomMarker $t 4 1 4 'pedestal>candle:2-4'
            Add-RoomMarker $t 1 1 1 'skeleton_skull'
        }
    }
    garden_lab        = @{ e = @('west'); l = @(3); f = {
            Add-Planter $t 2 2 3 3 'minecraft:grass_block' 'minecraft:poppy'
            Add-Planter $t 5 2 6 3 'minecraft:podzol' 'minecraft:fern'
            Add-RoomCounter $t 'west' @('brewing_stand', 'mason_jar:potion')
            Add-RoomMarker $t 4 1 5 'water_cauldron:2-3'
            Add-RoomMarker $t 1 1 1 'firefly_jar'
        }
    }
    grave             = @{ e = @('east'); f = {
            for ($x = 3; $x -le 5; $x++) { for ($z = 3; $z -le 6; $z++) { Set-GftBlock $t $x 0 $z 'minecraft:coarse_dirt' } }
            Set-RoomBlock $t 4 1 6 'minecraft:stone_brick_wall'
            Set-RoomBlock $t 4 2 6 'minecraft:stone_brick_slab' $script:Slab
            Add-RoomMarker $t 3 1 6 'skeleton_skull:0+2'
            Add-RoomMarker $t 5 1 6 'wither_skull:0+2'
            Add-RoomMarker $t 4 1 4 'zombie_trap'
            Add-RoomCandles $t 1 'candle:1-2'
        }
    }
    guarded_chest     = @{ e = @('east'); l = @(1, 3); f = {
            Add-Cage $t 3 5 5 6 2
            Add-RoomMarker $t 4 1 6 'chest:room@north'
            Add-RoomMarker $t 4 1 3 'spawner:skeleton:3'
            Add-RoomMarker $t 2 1 2 'skeleton_skull'
        }
    }
    jar_study         = @{ e = @('west'); l = @(1, 3, 4); f = {
            foreach ($z in 2, 4, 6) { foreach ($y in 1, 2) { Set-RoomBlock $t 7 $y $z 'gloamingforest:canopy_bookshelf' } ; Add-RoomMarker $t 7 3 $z 'mason_jar:jar' }
            foreach ($z in 3, 5) { Add-RoomMarker $t 7 1 $z 'mason_jar:jar' }
            for ($x = 3; $x -le 5; $x++) { Set-RoomBlock $t $x 1 3 'gloamingforest:canopy_slab' $script:TopSlab }
            Add-RoomMarker $t 4 2 3 'candle:1-2'
            Add-RoomMarker $t 1 1 1 'chest:library@east'
        }
    }
    library_hall      = @{ e = @('south'); l = @(1, 3); f = {
            Add-RoomShelves $t 4
            foreach ($z in 3, 5) { foreach ($x in 2, 3, 5, 6) { foreach ($y in 1, 2) { Add-RoomMarker $t $x $y $z 'bookshelf|canopy_shelf' } } }
            Add-RoomMarker $t 4 1 4 'lectern@south'
            Add-RoomMarker $t 4 6 4 'chain50%1>candle:2-3'
        }
    }
    nursery           = @{ e = @('east'); f = {
            Add-RoomMarker $t 2 1 3 'spawner:spider:2'
            Add-RoomMarker $t 6 1 3 'spawner:cave_spider:2'
            Add-RoomMarker $t 4 1 5 'spawner:swarm_spider:2'
            for ($x = 1; $x -le 7; $x++) { for ($z = 1; $z -le 7; $z++) { if ((($x + 2 * $z) % 3) -eq 0) { Add-RoomMarker $t $x 6 $z 'cobweb'; Add-RoomMarker $t $x 7 $z 'cobweb' } } }
            Add-RoomMarker $t 1 1 6 'cobweb'
        }
    }
    pedestal_junction = @{ e = @('east', 'south', 'west'); f = {
            foreach ($p in (Get-RoomCorners $t 1)) { Add-RoomMarker $t $p[0] 1 $p[1] 'pedestal>candle:1-3' }
            Add-RoomMarker $t 1 1 1 'skeleton_skull'
        }
    }
    pedestal_library  = @{ e = @('west'); f = {
            Add-RoomShelves $t 3 'bookshelf|canopy_shelf' 1
            Add-RoomMarker $t 3 1 4 'pedestal>candle:1-2'
            Add-RoomMarker $t 5 1 4 'pedestal>candle:1-2'
            Add-RoomMarker $t 4 1 5 'candled_lectern@north'
        }
    }
    potion            = @{ e = @('east'); l = @(1); f = {
            Add-RoomCounter $t 'west' @('brewing_stand', 'mason_jar:potion', 'brewing_stand', 'water_cauldron:1-3')
            Add-RoomMarker $t 6 1 1 'chest:potion@west'
            Add-RoomMarker $t 4 1 5 'water_cauldron:1-3'
        }
    }
    potion_lab        = @{ e = @('west'); l = @(1, 3); f = {
            Add-RoomCounter $t 'east' @('brewing_stand', 'brewing_stand', 'mason_jar:potion')
            foreach ($x in 3, 5) { Add-RoomMarker $t $x 1 4 'water_cauldron:1-3' }
            Add-RoomMarker $t 4 1 4 'brewing_stand'
            Add-RoomMarker $t 1 1 1 'chest:potion@east'
            Add-RoomHangers $t 'chain' 'candle:1'
        }
    }
    ritual            = @{ e = @('south'); f = {
            Add-RoomCarpet $t 'minecraft:black_carpet' 1
            foreach ($p in @(@(4, 2), @(2, 4), @(6, 4), @(3, 6), @(5, 6))) { Add-RoomMarker $t $p[0] 1 $p[1] 'skeleton_candle:2-4|zombie_candle:2-4|creeper_candle:2-4' }
            Add-RoomMarker $t 3 1 4 'sinister_spawner:zombie,skeleton:3:6'
            Add-RoomMarker $t 5 1 4 'candle:3-4'
        }
    }
    shelved_jars      = @{ e = @('east'); f = {
            foreach ($cell in (Get-RoomRing $t)) {
                if ((Get-Inward $t $cell[0] $cell[1]) -eq 'south') { continue }
                Set-RoomBlock $t $cell[0] 2 $cell[1] 'gloamingforest:canopy_slab' $script:TopSlab
                Add-RoomMarker $t $cell[0] 3 $cell[1] 'mason_jar:jar|air'
                Add-RoomMarker $t $cell[0] 1 $cell[1] 'mason_jar:jar|air'
            }
            Add-RoomMarker $t 4 1 4 'candle:2-3'
        }
    }
    straight_junction = @{ e = @('south'); f = {
            foreach ($z in 2, 6) { Add-RoomPillar $t 2 $z; Add-RoomPillar $t 6 $z }
            Add-RoomMarker $t 1 1 4 'candle:1-3'
        }
    }
    t_junction        = @{ e = @('east', 'west'); f = {
            Add-RoomPillar $t 4 6 'minecraft:chiseled_stone_bricks'
            Add-RoomMarker $t 3 1 6 'candle:1-3'; Add-RoomMarker $t 5 1 6 'candle:1-3'
        }
    }
    tiered_library    = @{ e = @('west'); h = 11; f = {
            Add-RoomMezzanine $t 4 1
            Add-RoomShelves $t 3
            foreach ($cell in (Get-RoomRing $t)) { foreach ($y in 5, 6, 7) { Add-RoomMarker $t $cell[0] $y $cell[1] 'bookshelf|canopy_shelf|air' } }
            for ($y = 1; $y -le 4; $y++) { Set-RoomBlock $t 3 $y 7 'minecraft:ladder' @{ facing = 'north' } }
            Add-RoomMarker $t 4 1 4 'lectern@north'
            Add-RoomMarker $t 5 5 1 'chest:library@south'
        }
    }
    tiered_study      = @{ e = @('east'); h = 11; f = {
            Add-RoomMezzanine $t 4 2
            for ($y = 1; $y -le 4; $y++) { Set-RoomBlock $t 3 $y 7 'minecraft:ladder' @{ facing = 'north' } }
            Add-RoomMarker $t 2 5 2 'chest:library@south'
            Add-RoomMarker $t 3 5 1 'candle:1-3'
            Add-RoomMarker $t 6 5 1 'mason_jar:library'
            Add-RoomMarker $t 4 1 4 'empty_lectern@north'
            Add-RoomMarker $t 4 8 4 'chain50%2>candle:2-4'
        }
    }
    walled_library    = @{ e = @('south'); f = {
            for ($x = 2; $x -le 6; $x++) { if ($x -ne 4) { foreach ($y in 1, 2, 3) { Add-RoomMarker $t $x $y 3 'bookshelf|canopy_shelf'; Add-RoomMarker $t $x $y 5 'bookshelf|canopy_shelf' } } }
            Add-RoomChiseledShelves $t 'east' 2
            Add-RoomChiseledShelves $t 'west' 2
            Add-RoomMarker $t 6 1 4 'chest:library@west'
        }
    }
}

foreach ($name in $rooms.Keys) {
    $r = $rooms[$name]
    $lb = if ($r.ContainsKey('l')) { $r.l } else { @() }
    $h = if ($r.ContainsKey('h')) { $r.h } else { 0 }
    $t = New-LichRoom -Name $name -N $N -Height $h -Exits $r.e -LadderBelow $lb -Decor:([bool]$r.d)
    & $r.f
    Save-Gft $t
}
