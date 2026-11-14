# U19 (questcamps): the quest grove (QuestGrove, docs/spec/questcamps.md 2). An original 27 x 10 x 27
# design (rule R19): layers 0 (foundation) and 1 (floor) are flush with the terrain, layer 2 is the
# first open-air layer. A ring of mossy stone brick pillars crowned with leaves around a grassy
# clearing, a brick cross leading to a raised dais where the Questing Ram waits (the quest_ram data
# marker, an air cell above the floor), and four offering droppers on the south side (dispenser
# markers: each becomes a dropper facing north holding four random wool blocks).
. (Join-Path $PSScriptRoot 'QuestcampsLib.ps1')

$size = 27
$c    = 13
$t    = New-Gft -Id 'gloamingforest:quest_grove' -Size $size, 10, $size
$leaves = @{ Name = 'minecraft:oak_leaves'; Properties = @{ persistent = 'true'; distance = '1'; waterlogged = 'false' } }

for ($x = 0; $x -lt $size; $x++) {
    for ($z = 0; $z -lt $size; $z++) {
        $dx = $x - $c; $dz = $z - $c
        $d  = [Math]::Sqrt($dx * $dx + $dz * $dz)
        if ($d -gt 13.2) { continue }
        # foundation and floor
        Set-GftBlock $t $x 0 $z 'minecraft:dirt'
        $floor = 'minecraft:grass_block'
        if ($d -ge 11.6) { $floor = 'minecraft:mossy_stone_bricks' }
        elseif ([Math]::Abs($dx) -le 1 -or [Math]::Abs($dz) -le 1) { $floor = if ((($x + $z) % 3) -eq 0) { 'minecraft:mossy_stone_bricks' } else { 'minecraft:stone_bricks' } }
        if ([Math]::Abs($dx) -le 2 -and [Math]::Abs($dz) -le 2) { $floor = 'minecraft:chiseled_stone_bricks' }
        if ($floor -eq 'minecraft:grass_block') { Set-GftBlock $t $x 1 $z $floor @{ snowy = 'false' } } else { Set-GftBlock $t $x 1 $z $floor }
        # the open air of the clearing
        for ($y = 2; $y -lt 10; $y++) { Set-GftBlock $t $x $y $z 'minecraft:air' }
        # a low ring wall with four gaps on the axes
        if ($d -ge 12.1 -and [Math]::Abs($dx) -gt 1 -and [Math]::Abs($dz) -gt 1) {
            Set-GftBlock $t $x 2 $z 'minecraft:mossy_stone_brick_wall' @{ east = 'none'; north = 'none'; south = 'none'; west = 'none'; up = 'true'; waterlogged = 'false' }
        }
    }
}

# eight pillars on the ring, each 5 high with a leaf crown
$angles = 0..7 | ForEach-Object { ($_ * 45 + 22.5) * [Math]::PI / 180 }
foreach ($a in $angles) {
    $px = $c + [int][Math]::Round([Math]::Cos($a) * 11)
    $pz = $c + [int][Math]::Round([Math]::Sin($a) * 11)
    for ($y = 2; $y -le 6; $y++) {
        $b = if ($y -eq 2 -or $y -eq 6) { 'minecraft:chiseled_stone_bricks' } elseif ($y -eq 4) { 'minecraft:mossy_stone_bricks' } else { 'minecraft:stone_bricks' }
        Set-GftBlock $t $px $y $pz $b
    }
    foreach ($o in @(@(0, 0), @(1, 0), @(-1, 0), @(0, 1), @(0, -1))) {
        Set-GftBlock $t ($px + $o[0]) 7 ($pz + $o[1]) $leaves.Name $leaves.Properties
    }
    Set-GftBlock $t $px 8 $pz $leaves.Name $leaves.Properties
}

# the dais: a raised 3 x 3 platform of slabs around the ram's spot, lanterns on the corners
foreach ($o in @(@(-1, -1), @(1, -1), @(-1, 1), @(1, 1))) {
    Set-GftBlock $t ($c + $o[0] * 2) 2 ($c + $o[1] * 2) 'minecraft:stone_brick_wall' @{ east = 'none'; north = 'none'; south = 'none'; west = 'none'; up = 'true'; waterlogged = 'false' }
    Set-GftBlock $t ($c + $o[0] * 2) 3 ($c + $o[1] * 2) 'minecraft:lantern' @{ hanging = 'false'; waterlogged = 'false' }
}
Add-QcData $t $c 2 $c 'quest_ram'

# the offering droppers south of the dais (markers on air cells; each becomes a dropper facing north)
foreach ($x in @(10, 12, 14, 16)) {
    Set-GftBlock $t $x 2 18 'minecraft:mossy_stone_bricks'
    Add-QcData $t $x 3 18 'dispenser'
}
Save-Gft $t
