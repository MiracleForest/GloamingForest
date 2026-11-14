# U22 (lichdecor): the 5x5 rooms of the lich tower wings (original designs, rule R11/R19). The rooms
# of the ladder_0 / ladder_2 pools are entered from a room below as well as by their north door.
. (Join-Path $PSScriptRoot '../RoomLib.ps1')

$N = 5
$rooms = [ordered]@{
    altar             = @{ e = @('east'); l = @(0, 2); f = {
            Add-RoomCarpet $t 'gloamingforest:coronation_carpet' 1
            Add-RoomAltar $t 3 4 'candle:3-4'
            Add-RoomMarker $t 2 1 4 'skeleton_candle:1-3:0+4'
            Add-RoomMarker $t 4 1 4 'zombie_candle:1-3:0+4'
            Add-RoomCandles $t 1 'candle:1-2'
        }
    }
    desk              = @{ e = @('west'); l = @(0, 2); f = {
            foreach ($z in 2, 3, 4) { Set-RoomBlock $t 5 1 $z 'gloamingforest:canopy_slab' $script:TopSlab }
            Add-RoomMarker $t 5 2 3 'candle:1-2'
            Add-RoomMarker $t 4 1 3 'canopy_stairs'
            Add-RoomMarker $t 5 1 1 'chest:library@west'
            Add-RoomMarker $t 1 1 1 'bookshelf|canopy_shelf'
            Add-RoomMarker $t 1 2 1 'bookshelf|canopy_shelf'
            Add-RoomMarker $t 3 1 5 'mason_jar:library'
        }
    }
    elbow_junction    = @{ e = @('east'); l = @(0, 2); f = {
            Add-RoomPillar $t 1 1 'minecraft:chiseled_stone_bricks'
            Add-RoomPillar $t 1 4 'minecraft:chiseled_stone_bricks'
            Add-RoomHangers $t 'chain' 'candle:1-2'
            Add-RoomMarker $t 4 1 1 'skeleton_skull'
        }
    }
    full_junction     = @{ e = @('east', 'south', 'west'); f = {
            foreach ($p in (Get-RoomCorners $t)) { Add-RoomPillar $t $p[0] $p[1] }
            Add-RoomMarker $t 2 5 2 'rope50%1-2>candle:1-2'
        }
    }
    full_junction_2   = @{ e = @('east', 'south', 'west'); f = {
            Add-RoomCarpet $t 'minecraft:red_carpet' 1
            Add-RoomHangers $t 'chain' 'candle:1-3'
            Add-RoomCandles $t 1 'candle:1-2'
        }
    }
    full_junction_3   = @{ e = @('east', 'south', 'west'); f = {
            for ($x = 2; $x -le 4; $x++) { for ($z = 2; $z -le 4; $z++) { Set-GftBlock $t $x 0 $z 'gloamingforest:terrorcotta_curves' @{ facing = @('north', 'east', 'south', 'west')[($x + $z) % 4] } } }
            Add-RoomMarker $t 1 1 1 'creeper_head|zombie_head|skeleton_skull'
            Add-RoomMarker $t 5 1 1 'creeper_head|zombie_head|skeleton_skull'
            Add-RoomMarker $t 3 1 3 'candle:2-4'
        }
    }
    ladder            = @{ e = @('west'); f = {
            foreach ($z in 2, 3, 4) { for ($y = 1; $y -le 5; $y++) { Set-RoomBlock $t 5 $y $z 'minecraft:ladder' @{ facing = 'west' } } }
            Set-RoomBlock $t 5 4 1 'gloamingforest:canopy_slab' $script:TopSlab
            Add-RoomMarker $t 5 5 1 'candle:1-2'
            Add-RoomMarker $t 3 5 3 'rope50%2-3>air'
        }
    }
    lectern           = @{ e = @('east'); l = @(0, 2); f = {
            Add-RoomMarker $t 3 1 4 'lectern@north'
            Add-RoomMarker $t 2 1 4 'candle:1-3'
            Add-RoomMarker $t 4 1 4 'candle:1-3'
            Add-RoomChiseledShelves $t 'east' 2
            Add-RoomMarker $t 1 1 1 'bookshelf|canopy_shelf'
        }
    }
    library           = @{ e = @('south'); f = {
            Add-RoomShelves $t 3
            Add-RoomMarker $t 3 1 3 'lectern@north'
            Add-RoomMarker $t 1 1 3 'chest:library@east'
            Add-RoomMarker $t 3 4 3 'chain50%1>candle:1-3'
        }
    }
    lone_chest        = @{ e = @(); l = @(0, 2); f = {
            Add-RoomMarker $t 3 1 5 'chest:room@north'
            Set-RoomBlock $t 3 2 5 'minecraft:stone_brick_slab' $script:Slab
            Add-RoomMarker $t 2 1 5 'candle:1-2'
            Add-RoomMarker $t 4 1 5 'candle:1-2'
            Add-RoomMarker $t 1 1 1 'cobweb'
            Add-RoomMarker $t 5 4 1 'cobweb'
        }
    }
    potion            = @{ e = @('east'); l = @(0, 2); f = {
            Add-RoomCounter $t 'east' @('brewing_stand', 'mason_jar:potion', 'brewing_stand')
            Add-RoomMarker $t 4 1 4 'water_cauldron:1-3'
            Add-RoomMarker $t 4 1 1 'chest:potion@west'
            Add-RoomMarker $t 3 5 3 'chain50%1>candle:1'
        }
    }
    spawner_1         = @{ e = @('east'); f = {
            Add-RoomMarker $t 3 1 4 'spawner'
            foreach ($p in (Get-RoomCorners $t)) { Add-RoomMarker $t $p[0] 5 $p[1] 'cobweb' }
            Add-RoomMarker $t 1 1 1 'skeleton_skull'
        }
    }
    spawner_2         = @{ e = @('west'); f = {
            Add-RoomMarker $t 2 1 4 'spawner:skeleton:3'
            Add-RoomMarker $t 4 1 4 'spawner:zombie:3'
            Add-RoomMarker $t 3 1 5 'chest:room@north'
            Add-RoomCandles $t 4 'cobweb'
        }
    }
    spawner_3         = @{ e = @('south'); f = {
            Add-RoomMarker $t 2 1 2 'pedestal>spawner:spider,cave_spider'
            Add-RoomMarker $t 4 1 4 'pedestal>candle:2-3'
            Add-RoomMarker $t 2 1 4 'pedestal>candle:1-3'
            Add-RoomMarker $t 4 1 2 'pedestal>skeleton_skull'
        }
    }
    spawner_4         = @{ e = @('east', 'west'); f = {
            Add-RoomMarker $t 3 1 4 'sinister_spawner:zombie,skeleton:3:6'
            Add-RoomCandles $t 1 'candle:2-4'
            Add-RoomMarker $t 3 1 5 'skeleton_candle:2-3'
        }
    }
    spawner_5         = @{ e = @('east'); f = {
            Add-RoomMarker $t 3 1 3 'spawner:swarm_spider,hedge_spider:2'
            for ($x = 1; $x -le 5; $x++) { foreach ($z in 1, 5) { Add-RoomMarker $t $x 5 $z 'cobweb' } }
            Add-RoomMarker $t 1 1 4 'cobweb'
        }
    }
    straight_junction = @{ e = @('south'); l = @(0, 2); f = {
            Add-RoomPillar $t 2 1; Add-RoomPillar $t 4 1
            Add-RoomMarker $t 1 1 3 'candle:1-3'
            Add-RoomMarker $t 5 1 3 'candle:1-3'
        }
    }
    t_junction        = @{ e = @('east', 'west'); l = @(0, 2); f = {
            Add-RoomCarpet $t 'minecraft:gray_carpet' 0
            Add-RoomMarker $t 3 1 5 'skeleton_candle:1-3:0'
            Add-RoomHangers $t 'rope' 'air'
        }
    }
    trinity           = @{ e = @('west'); l = @(0, 2); f = {
            Add-RoomAltar $t 3 4 'candle:3-4'
            Add-RoomAltar $t 2 3 'candle:1-2'
            Add-RoomAltar $t 4 3 'candle:1-2'
        }
    }
    webbed_spawner    = @{ e = @('east'); f = {
            Add-RoomMarker $t 3 1 4 'spawner:cave_spider,spider:3'
            for ($x = 1; $x -le 5; $x++) { for ($z = 1; $z -le 5; $z++) { if ((($x * 3 + $z * 5) % 4) -eq 0) { foreach ($y in 1, 4, 5) { Add-RoomMarker $t $x $y $z 'cobweb' } } } }
        }
    }
    zombie_trap       = @{ e = @('south'); f = {
            Add-RoomMarker $t 2 1 2 'zombie_trap'
            Add-RoomMarker $t 4 1 4 'zombie_trap'
            Add-RoomMarker $t 1 1 5 'chest:room@east'
            Add-RoomMarker $t 5 1 1 'zombie_head'
        }
    }
}

foreach ($name in $rooms.Keys) {
    $r = $rooms[$name]
    $lb = if ($r.ContainsKey('l')) { $r.l } else { @() }
    $t = New-LichRoom -Name $name -N $N -Exits $r.e -LadderBelow $lb -Decor:([bool]$r.d)
    & $r.f
    Save-Gft $t
}
