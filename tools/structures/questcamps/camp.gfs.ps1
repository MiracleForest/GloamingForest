# U19 (questcamps): the camp's 35 templates (CampStructure / CampPieces jigsaw, docs/spec/questcamps.md
# 3.1). Original GloamingForest designs (rule R19) keeping TF's pools, weights, placements and marker
# names (twilightforest:camp_marker_handlers: birch_drying_rack, camp_armor_rack, painting, tent_pot,
# twilight_oak_slab, each with "@<facing>"). The jigsaw convention is in QuestcampsLib.ps1.
. (Join-Path $PSScriptRoot 'QuestcampsLib.ps1')

$log   = 'twilightforest:twilight_oak_log'
$logX  = Get-QcState $log @{ axis = 'x' }
$logZ  = Get-QcState $log @{ axis = 'z' }
$logY  = Get-QcState $log @{ axis = 'y' }
$fence = 'twilightforest:twilight_oak_fence'
$planks = 'twilightforest:twilight_oak_planks'
$wool  = 'minecraft:light_gray_wool'
$fire  = Get-QcState 'minecraft:campfire' @{ lit = 'true'; signal_fire = 'false'; facing = 'north'; waterlogged = 'false' }

# ---- starts: a campfire ring 9 x 3 x 9 ----------------------------------------------------------------------
# exits: north, south, west, east (front out); the main path leaves on the side the name gives
function New-CampStart([string]$name, [hashtable]$exits) {
    $t = New-Gft -Id "twilightforest:camp/$name" -Size 9, 3, 9
    Clear-QcBox $t 0 0 0 8 2 8
    Set-QcRows $t 0 @{ 'c' = $fire; 'x' = $logX; 'z' = $logZ; 'g' = 'minecraft:coarse_dirt'; 's' = 'minecraft:cobblestone_slab' } @(
        '.........',
        '...xxx...',
        '..s...s..',
        '.z.....z.',
        '.z..c..z.',
        '.z.....z.',
        '..s...s..',
        '...xxx...',
        '.........')
    # the fire pit's stones
    Set-GftBlock $t 3 0 4 'minecraft:cobblestone_slab' @{ type = 'bottom'; waterlogged = 'false' }
    Set-GftBlock $t 5 0 4 'minecraft:cobblestone_slab' @{ type = 'bottom'; waterlogged = 'false' }
    Set-GftBlock $t 4 0 3 'minecraft:cobblestone_slab' @{ type = 'bottom'; waterlogged = 'false' }
    Set-GftBlock $t 4 0 5 'minecraft:cobblestone_slab' @{ type = 'bottom'; waterlogged = 'false' }
    foreach ($p in @(@(2, 0, 2), @(6, 0, 2), @(2, 0, 6), @(6, 0, 6))) {
        Set-GftBlock $t $p[0] $p[1] $p[2] 'minecraft:cobblestone_slab' @{ type = 'bottom'; waterlogged = 'false' }
    }
    # stools around the fire (25 % slabs) and a drying rack by the seats
    Add-QcData $t 3 0 2 'twilight_oak_slab@south'
    Add-QcData $t 5 0 6 'twilight_oak_slab@north'
    Add-QcData $t 1 0 1 'birch_drying_rack@south'
    Add-QcData $t 7 0 7 'birch_drying_rack@north'
    Add-QcSeed $t 4 0 4 'down' $script:QcName.start
    $t.Jigsaws[$t.Jigsaws.Count - 1].top = 'south'
    if ($exits.north) { Add-QcExit $t 4 0 0 'north' $exits.north }
    if ($exits.south) { Add-QcExit $t 4 0 8 'south' $exits.south }
    if ($exits.west) { Add-QcExit $t 0 0 4 'west' $exits.west }
    if ($exits.east) { Add-QcExit $t 8 0 4 'east' $exits.east }
    Save-Gft $t
}
New-CampStart 'campfire_south' @{ south = 'main'; east = 'tent'; west = 'tent'; north = 'rackPath' }
New-CampStart 'campfire_east' @{ east = 'main'; north = 'tent'; south = 'tent'; west = 'tent' }
New-CampStart 'campfire_west' @{ west = 'main'; north = 'tent'; east = 'tent'; south = 'rackPath' }

# ---- tents: light grey wool (swapped per piece), a red bed (swapped), entrance at z 0 -------------------------
function Set-TentFrame($t, [int]$w, [int]$h, [int]$d) {
    # an A-frame along z: row y holds wool at x = y and x = w - 1 - y
    for ($y = 0; $y -lt $h; $y++) {
        $a = $y; $b = $w - 1 - $y
        if ($a -gt $b) { break }
        for ($z = 0; $z -lt $d; $z++) {
            Set-GftBlock $t $a $y $z $wool
            Set-GftBlock $t $b $y $z $wool
        }
        # the back gable
        for ($x = $a; $x -le $b; $x++) { Set-GftBlock $t $x $y ($d - 1) $wool }
    }
}
function Set-Bed($t, [int]$x, [int]$z, [string]$facing) {
    $dz = switch ($facing) { 'south' { 1 } 'north' { -1 } default { 0 } }
    $dx = switch ($facing) { 'east' { 1 } 'west' { -1 } default { 0 } }
    Set-GftBlock $t $x 0 $z 'minecraft:red_bed' @{ facing = $facing; occupied = 'false'; part = 'foot' }
    Set-GftBlock $t ($x + $dx) 0 ($z + $dz) 'minecraft:red_bed' @{ facing = $facing; occupied = 'false'; part = 'head' }
}
function Set-TentChest($t, [int]$x, [int]$z, [string]$facing) {
    Set-GftBlock $t $x 0 $z 'minecraft:chest' @{ facing = $facing; type = 'single'; waterlogged = 'false' }
    Add-GftMarker $t $x 0 $z 'loot' @{ table = 'twilightforest:camp_tent' }
}

# solo tent 5 x 4 x 6
$t = New-Gft -Id 'twilightforest:camp/tent/solo_tent' -Size 5, 4, 6
Clear-QcBox $t 0 0 0 4 3 5
Set-TentFrame $t 5 3 6
Set-GftBlock $t 0 3 0 $fence; Set-GftBlock $t 4 3 0 $fence
Set-GftBlock $t 2 3 0 $fence; Set-GftBlock $t 2 3 5 $fence
Set-Bed $t 2 3 'south'
Set-TentChest $t 1 4 'east'
Add-QcData $t 3 0 4 'tent_pot@west'
Add-QcData $t 1 0 1 'twilight_oak_slab@south'
Add-QcSeed $t 2 0 0 'north' $script:QcName.tent
Save-Gft $t

# duo tent 7 x 5 x 7
$t = New-Gft -Id 'twilightforest:camp/tent/duo_tent' -Size 7, 5, 7
Clear-QcBox $t 0 0 0 6 4 6
Set-TentFrame $t 7 4 7
Set-Bed $t 1 3 'south'
Set-Bed $t 5 3 'south'
Set-TentChest $t 3 5 'north'
Add-QcData $t 2 0 5 'tent_pot@north'
Add-QcData $t 4 0 5 'camp_armor_rack@north'
Add-QcData $t 3 1 4 'painting@north'
Add-QcSeed $t 3 0 0 'north' $script:QcName.tent
Save-Gft $t

# open tent 5 x 4 x 5: a wool canopy on four posts
$t = New-Gft -Id 'twilightforest:camp/tent/open_tent' -Size 5, 4, 5
Clear-QcBox $t 0 0 0 4 3 4
foreach ($p in @(@(0, 0), @(4, 0), @(0, 4), @(4, 4))) { Set-GftBox $t $p[0] 0 $p[1] $p[0] 1 $p[1] $fence }
Set-GftBox $t 0 2 0 4 2 4 $wool
Set-GftBox $t 1 3 0 3 3 4 $wool
Set-GftBox $t 1 2 1 3 2 3 'minecraft:air'
Set-Bed $t 1 2 'south'
Add-QcData $t 3 0 3 'birch_drying_rack@west'
Add-QcData $t 3 0 1 'tent_pot@west'
Add-QcSeed $t 2 0 0 'north' $script:QcName.tent
Save-Gft $t

# luxury tent 9 x 5 x 8: a wide wool hall with a carpet, two beds, a chest, a pot, an armour rack and a painting
$t = New-Gft -Id 'twilightforest:camp/tent/luxury_tent' -Size 9, 5, 8
Clear-QcBox $t 0 0 0 8 4 7
Set-TentFrame $t 9 5 8
# raise the side walls one more block for headroom
for ($z = 0; $z -lt 8; $z++) { Set-GftBlock $t 0 1 $z $wool; Set-GftBlock $t 8 1 $z $wool }
for ($z = 0; $z -lt 8; $z++) { Set-GftBlock $t 1 1 $z 'minecraft:air'; Set-GftBlock $t 7 1 $z 'minecraft:air' }
Set-GftBox $t 0 1 7 8 1 7 $wool
Set-GftBox $t 3 0 1 5 0 5 'minecraft:red_carpet'
Set-Bed $t 2 4 'south'
Set-Bed $t 6 4 'south'
Set-TentChest $t 4 6 'north'
Add-QcData $t 3 0 6 'tent_pot@north'
Add-QcData $t 5 0 6 'camp_armor_rack@north'
Add-QcData $t 4 1 6 'painting@north'
Add-QcData $t 1 0 1 'twilight_oak_slab@east'
Set-GftBlock $t 0 4 0 $fence; Set-GftBlock $t 8 4 0 $fence
Add-QcSeed $t 4 0 0 'north' $script:QcName.tent
Save-Gft $t

# ---- paths: layer 0 under the surface, layer 1 the surface (gravity), layer 2 the ground air level ---------
$pathKey = @{ 'p' = 'minecraft:dirt_path'; 'c' = 'minecraft:coarse_dirt'; 'd' = 'minecraft:dirt'; 'g' = 'minecraft:gravel' }
function Set-PathCells($t, [string[]]$rows) {
    # rows: '#' path cell (surface mixed), ' ' untouched
    for ($z = 0; $z -lt $rows.Count; $z++) {
        for ($x = 0; $x -lt $rows[$z].Length; $x++) {
            if ($rows[$z][$x] -ne '#') { continue }
            Set-GftBlock $t $x 0 $z 'minecraft:dirt'
            $surface = if ((($x * 7 + $z * 3) % 5) -eq 0) { 'minecraft:coarse_dirt' } else { 'minecraft:dirt_path' }
            Set-GftBlock $t $x 1 $z $surface
        }
    }
}
function New-Path([string]$name, [int]$w, [int]$len) {
    $t = New-Gft -Id "twilightforest:camp/path/$name" -Size $w, 3, $len
    $rows = @(1..$len | ForEach-Object { '#' * $w })
    Set-PathCells $t $rows
    Add-QcSeed $t 0 2 0 'north' $script:QcName.path
    $mid = [int][Math]::Floor($len / 2)
    Add-QcExit $t 0 2 ($len - 1) 'south' 'deco'
    Add-QcExit $t 0 2 $mid 'west' 'deco'
    Add-QcExit $t ($w - 1) 2 ($mid - 1) 'east' 'deco'
    Save-Gft $t
}
New-Path 'path_2x4' 2 4
New-Path 'path_3x4' 3 4
New-Path 'path_2x6' 2 6
New-Path 'path_2x7' 2 7

# intersection_left 5 x 3 x 7: a lane at x 3..4 with a branch west at z 2..3
$t = New-Gft -Id 'twilightforest:camp/path/intersection_left' -Size 5, 3, 7
Set-PathCells $t @('   ##', '   ##', '#####', '#####', '   ##', '   ##', '   ##')
Add-QcSeed $t 3 2 0 'north' $script:QcName.path
Add-QcExit $t 3 2 6 'south' 'path'
Add-QcExit $t 0 2 2 'west' 'path'
Add-QcExit $t 4 2 4 'east' 'deco'
Save-Gft $t
# intersection_right 5 x 3 x 7: the mirror image
$t = New-Gft -Id 'twilightforest:camp/path/intersection_right' -Size 5, 3, 7
Set-PathCells $t @('##   ', '##   ', '#####', '#####', '##   ', '##   ', '##   ')
Add-QcSeed $t 0 2 0 'north' $script:QcName.path
Add-QcExit $t 0 2 6 'south' 'path'
Add-QcExit $t 4 2 2 'east' 'path'
Add-QcExit $t 0 2 4 'west' 'deco'
Save-Gft $t
# intersection_short 6 x 3 x 4: a T
$t = New-Gft -Id 'twilightforest:camp/path/intersection_short' -Size 6, 3, 4
Set-PathCells $t @('  ##  ', '  ##  ', '######', '######')
Add-QcSeed $t 2 2 0 'north' $script:QcName.path
Add-QcExit $t 0 2 2 'west' 'path'
Add-QcExit $t 5 2 3 'east' 'path'
Add-QcExit $t 3 2 3 'south' 'tent'
Save-Gft $t
# j_path 5 x 3 x 6: south, then east
$t = New-Gft -Id 'twilightforest:camp/path/j_path' -Size 5, 3, 6
Set-PathCells $t @('##   ', '##   ', '##   ', '##   ', '#####', '#####')
Add-QcSeed $t 0 2 0 'north' $script:QcName.path
Add-QcExit $t 4 2 4 'east' 'path'
Add-QcExit $t 0 2 2 'west' 'deco'
Add-QcExit $t 1 2 5 'south' 'deco'
Save-Gft $t
# l_path 5 x 3 x 6: south, then west
$t = New-Gft -Id 'twilightforest:camp/path/l_path' -Size 5, 3, 6
Set-PathCells $t @('   ##', '   ##', '   ##', '   ##', '#####', '#####')
Add-QcSeed $t 3 2 0 'north' $script:QcName.path
Add-QcExit $t 0 2 4 'west' 'path'
Add-QcExit $t 4 2 2 'east' 'tent'
Add-QcExit $t 3 2 5 'south' 'deco'
Save-Gft $t

# ---- decorations: the front row (z 0) stays open, the seed connector in its middle ------------------------
function New-Deco([string]$name, [int]$w, [int]$h, [int]$d) {
    $t = New-Gft -Id "twilightforest:camp/deco/$name" -Size $w, $h, $d
    Clear-QcBox $t 0 0 0 ($w - 1) ($h - 1) ($d - 1)
    Add-QcSeed $t ([int][Math]::Floor(($w - 1) / 2)) 0 0 'north' $script:QcName.deco
    return $t
}
$composter = Get-QcState 'minecraft:composter' @{ level = '4' }
$composterFull = Get-QcState 'minecraft:composter' @{ level = '8' }

$t = New-Deco 'compost_large' 3 2 4
Set-QcRows $t 0 @{ 'C' = $composter; 'X' = $composterFull; 'h' = (Get-QcState 'minecraft:hay_block' @{ axis = 'y' }); 'f' = $fence } @(
    '...', 'CXC', 'ChC', 'fff')
Set-GftBlock $t 1 1 2 'minecraft:hay_block' @{ axis = 'y' }
Save-Gft $t

$t = New-Deco 'compost_small' 2 2 3
Set-QcRows $t 0 @{ 'C' = $composter; 'F' = $composterFull } @('..', 'CF', '..')
Save-Gft $t

function Set-RaisedBed($t, [int]$x1, [int]$z1, [int]$x2, [int]$z2, $crop) {
    # a log-framed bed: farmland at layer 0 inside, the frame one block around, the crop on top
    for ($x = $x1 - 1; $x -le $x2 + 1; $x++) {
        for ($z = $z1 - 1; $z -le $z2 + 1; $z++) {
            $inner = $x -ge $x1 -and $x -le $x2 -and $z -ge $z1 -and $z -le $z2
            if ($inner) {
                Set-GftBlock $t $x 0 $z 'minecraft:farmland' @{ moisture = '7' }
                Set-GftBlock $t $x 1 $z $crop.Name $crop.Properties
            } else {
                $along = if ($x -eq $x1 - 1 -or $x -eq $x2 + 1) { 'z' } else { 'x' }
                Set-GftBlock $t $x 0 $z $log @{ axis = $along }
            }
        }
    }
}
$wheat    = Get-QcState 'minecraft:wheat' @{ age = '7' }
$carrots  = Get-QcState 'minecraft:carrots' @{ age = '5' }
$potatoes = Get-QcState 'minecraft:potatoes' @{ age = '7' }
$beets    = Get-QcState 'minecraft:beetroots' @{ age = '3' }

$t = New-Deco 'garden_1x3' 3 2 6
Set-RaisedBed $t 1 2 1 4 $wheat
Save-Gft $t
$t = New-Deco 'garden_2x4' 4 2 7
Set-RaisedBed $t 1 2 2 5 $carrots
Save-Gft $t
$t = New-Deco 'garden_straight' 3 2 9
Set-RaisedBed $t 1 2 1 7 $beets
Save-Gft $t
$t = New-Deco 'garden_u' 7 2 7
Set-RaisedBed $t 1 2 1 5 $potatoes
Set-RaisedBed $t 5 2 5 5 $potatoes
Set-RaisedBed $t 2 5 4 5 $wheat
Set-GftBlock $t 1 0 5 'minecraft:farmland' @{ moisture = '7' }; Set-GftBlock $t 1 1 5 $potatoes.Name $potatoes.Properties
Set-GftBlock $t 5 0 5 'minecraft:farmland' @{ moisture = '7' }; Set-GftBlock $t 5 1 5 $potatoes.Name $potatoes.Properties
Set-GftBlock $t 3 0 3 'minecraft:water'
Save-Gft $t

$t = New-Deco 'pen' 7 2 8
Set-GftBox $t 0 0 1 6 0 7 $fence
Set-GftBox $t 1 0 2 5 0 6 'minecraft:air'
Set-GftBlock $t 3 0 1 'minecraft:oak_fence_gate' @{ facing = 'north'; in_wall = 'false'; open = 'false'; powered = 'false' }
Set-GftBlock $t 5 0 6 'minecraft:hay_block' @{ axis = 'x' }
Set-GftBlock $t 4 0 6 'minecraft:hay_block' @{ axis = 'x' }
Set-GftBlock $t 1 0 6 'minecraft:water_cauldron' @{ level = '3' }
Save-Gft $t

$t = New-Deco 'repair_station' 4 3 3
Set-QcRows $t 0 @{
    'a' = Get-QcState 'minecraft:anvil' @{ facing = 'east' }
    'g' = Get-QcState 'minecraft:grindstone' @{ face = 'floor'; facing = 'north' }
    't' = 'minecraft:crafting_table'
    's' = 'minecraft:smithing_table'
} @('....', 'a..g', 'ts..')
Add-QcData $t 3 0 2 'camp_armor_rack@north'
Save-Gft $t

function Get-Bush([string]$kind) { return Get-QcState "twilightforest:${kind}_bush" @{ age = '3'; layers = '0' } }
$t = New-Deco 'berries_staked' 3 3 3
Set-QcRows $t 0 @{ 'b' = (Get-Bush 'raspberry'); 'f' = $fence } @('...', 'bfb', 'bfb')
Set-GftBlock $t 1 1 1 $fence; Set-GftBlock $t 1 1 2 $fence
Save-Gft $t
$t = New-Deco 'berries_support' 3 3 4
Set-QcRows $t 0 @{ 'b' = (Get-Bush 'blueberry'); 'l' = $logY } @('...', 'lbl', 'bbb', 'lbl')
Set-GftBlock $t 0 1 1 $logY.Name $logY.Properties; Set-GftBlock $t 2 1 1 $logY.Name $logY.Properties
Set-GftBlock $t 0 1 3 $logY.Name $logY.Properties; Set-GftBlock $t 2 1 3 $logY.Name $logY.Properties
Save-Gft $t
$t = New-Deco 'berries_trellis' 3 4 3
Set-QcRows $t 0 @{ 'b' = (Get-Bush 'blackberry'); 'f' = $fence } @('...', 'bbb', 'fff')
Set-GftBox $t 0 1 2 2 2 2 $fence
Set-GftBlock $t 1 2 2 $planks
Save-Gft $t
foreach ($kind in 'blackberry', 'blueberry', 'raspberry') {
    $t = New-Deco "${kind}_wall" 5 3 3
    Set-QcRows $t 0 @{ 'b' = (Get-Bush $kind); 'f' = $fence } @('.....', 'bbbbb', 'fffff')
    Set-GftBox $t 0 1 2 4 1 2 $fence
    Save-Gft $t
}

$t = New-Deco 'double_drying_rack' 3 2 3
Set-QcRows $t 0 @{ 'l' = $logY } @('...', 'l.l', '...')
Add-QcData $t 1 0 1 'birch_drying_rack@north'
Add-QcData $t 1 0 2 'birch_drying_rack@south'
Save-Gft $t
$t = New-Deco 'long_drying_rack' 5 2 3
Set-QcRows $t 0 @{ 'l' = $logY } @('.....', 'l...l', '.....')
Add-QcData $t 1 0 1 'birch_drying_rack@north'
Add-QcData $t 2 0 1 'birch_drying_rack@north'
Add-QcData $t 3 0 1 'birch_drying_rack@north'
Save-Gft $t

# lumber: chopped logs (the pool's processor turns every twilight oak log upright and into one wood)
$t = New-Deco 'lumber1' 3 3 3
Set-QcRows $t 0 @{ 'l' = $logZ } @('...', 'l.l', 'lll')
Set-GftBlock $t 1 1 2 $logZ.Name $logZ.Properties
Save-Gft $t
$t = New-Deco 'lumber2' 4 3 4
Set-QcRows $t 0 @{ 'l' = $logX } @('....', 'llll', 'll.l', '.l..')
Set-GftBlock $t 1 1 1 $logX.Name $logX.Properties; Set-GftBlock $t 2 1 1 $logX.Name $logX.Properties
Set-GftBlock $t 0 1 2 $logX.Name $logX.Properties
Save-Gft $t
$t = New-Deco 'lumber3' 2 2 3
Set-QcRows $t 0 @{ 'l' = $logY } @('..', 'l.', 'll')
Set-GftBlock $t 0 1 2 $logY.Name $logY.Properties
Save-Gft $t
