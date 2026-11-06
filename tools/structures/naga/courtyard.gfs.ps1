# The Naga courtyard's 32 templates (U20): original GloamingForest designs (rule R19) on the grid
# CourtyardMain / StructureMazeGenerator fixes (docs/spec/naga.md 3.2):
#   hedge pieces    one 5 x 5 cell (5 thick hedge walls: a 3 x 3 hedge core with 3-wide arms to the
#                   edges of the connected sides), base orientation per the rotation table: cap WEST,
#                   corner SOUTH + EAST, T NORTH + SOUTH + EAST, line EAST + WEST, intersection all;
#                   the padder (hedge_between) is 1 x 5; every "_big" overlay is leaves floof on the
#                   same footprint, placed at 50 % integrity with air ignored
#   pathway         7 x 1 x 7 floor tile one block below the hedges
#   terraces        12 x 5 x 12 basins placed 3 blocks lower; their rim is sandstone slabs, the
#                   markers CourtyardTerraceTemplateProcessor fits to the terrain
#   walls           11 x 6 x 3 runs, 1 x 6 x 3 padding, 3 x 6 x 3 corners; the "_decayed" overlays
#                   are placed at 10 % integrity with cobble variants
#   spawner         the center dais of pool gloamingforest:courtyard/center, its jigsaw
#                   "twilightforest:center" facing down, the Naga's boss spawner on top

$hedge  = 'twilightforest:hedge'
$leaves = @{ Name = 'minecraft:oak_leaves'; Properties = @{ persistent = 'true'; distance = '1' } }
$bricks = 'minecraft:stone_bricks'
$mossy  = 'minecraft:mossy_stone_bricks'
$cracked = 'minecraft:cracked_stone_bricks'

function Get-Arms([string[]]$Sides) {
    # the cells of a 5 x 5 hedge cell: the 3 x 3 core plus an arm to each connected side
    $cells = @{}
    foreach ($x in 1..3) { foreach ($z in 1..3) { $cells["$x,$z"] = $true } }
    foreach ($side in $Sides) {
        foreach ($i in 1..3) {
            switch ($side) {
                'west'  { $cells["0,$i"] = $true }
                'east'  { $cells["4,$i"] = $true }
                'north' { $cells["$i,0"] = $true }
                'south' { $cells["$i,4"] = $true }
            }
        }
    }
    return $cells
}

function New-HedgePair([string]$Name, [string[]]$Sides, [switch]$Pillar) {
    $cells = Get-Arms $Sides
    $t = New-Gft -Id "gloamingforest:courtyard/$Name" -Size 5, 4, 5
    foreach ($key in $cells.Keys) {
        $x, $z = $key.Split(',') | ForEach-Object { [int]$_ }
        Set-GftBox $t $x 0 $z $x 2 $z $hedge
    }
    if ($Pillar) {
        # the cap ends in a naga stone post with an etched band
        Set-GftBox $t 2 0 2 2 3 2 'twilightforest:nagastone_pillar' @{ axis = 'y' }
        Set-GftBlock $t 2 1 2 'twilightforest:etched_nagastone' @{ facing = 'up' }
    }
    Save-Gft $t
    $big = New-Gft -Id "gloamingforest:courtyard/${Name}_big" -Size 5, 5, 5
    foreach ($key in $cells.Keys) {
        $x, $z = $key.Split(',') | ForEach-Object { [int]$_ }
        if (-not ($Pillar -and $x -eq 2 -and $z -eq 2)) { Set-GftBlock $big $x 3 $z $leaves.Name $leaves.Properties }
        # the floof bulges out of the hedge's open sides
        foreach ($n in @(@(-1, 0), @(1, 0), @(0, -1), @(0, 1))) {
            $nx = $x + $n[0]; $nz = $z + $n[1]
            if ($nx -lt 0 -or $nz -lt 0 -or $nx -gt 4 -or $nz -gt 4 -or $cells.ContainsKey("$nx,$nz")) { continue }
            Set-GftBox $big $nx 1 $nz $nx 2 $nz $leaves.Name $leaves.Properties
        }
    }
    Set-GftBlock $big 2 4 2 $leaves.Name $leaves.Properties
    Save-Gft $big
}

New-HedgePair 'hedge_end' @('west')
New-HedgePair 'hedge_end_pillar' @('west') -Pillar
New-HedgePair 'hedge_corner' @('south', 'east')
New-HedgePair 'hedge_t' @('north', 'south', 'east')
New-HedgePair 'hedge_line' @('east', 'west')
New-HedgePair 'hedge_intersection' @('north', 'south', 'east', 'west')

# the 1 x 5 padder between a cell and a line
$t = New-Gft -Id 'gloamingforest:courtyard/hedge_between' -Size 1, 4, 5
Set-GftBox $t 0 0 1 0 2 3 $hedge
Save-Gft $t
$t = New-Gft -Id 'gloamingforest:courtyard/hedge_between_big' -Size 1, 5, 5
Set-GftBox $t 0 3 1 0 3 3 $leaves.Name $leaves.Properties
Set-GftBox $t 0 1 0 0 2 0 $leaves.Name $leaves.Properties
Set-GftBox $t 0 1 4 0 2 4 $leaves.Name $leaves.Properties
Save-Gft $t

# ---- the pathway: a worn stone brick floor tile ----------------------------------------------------------
$t = New-Gft -Id 'gloamingforest:courtyard/pathway' -Size 7, 1, 7
Set-GftLayer $t -Y 0 -Key @{ 'b' = $bricks; 'm' = $mossy; 'c' = $cracked; 'e' = @{ Name = 'twilightforest:etched_nagastone'; Properties = @{ facing = 'up' } }; 'g' = 'minecraft:grass_block' } -Rows @(
    'bbmbbmb',
    'bcbbbbm',
    'mbbebbb',
    'bbeeebb',
    'bbbebcm',
    'mbbbbbb',
    'bmbbmbb')
Save-Gft $t

# ---- terraces: sunken 12 x 12 basins ---------------------------------------------------------------------
$slabDouble = @{ Name = 'minecraft:sandstone_slab'; Properties = @{ type = 'double' } }
$slab       = @{ Name = 'minecraft:sandstone_slab'; Properties = @{ type = 'bottom' } }

function New-Terrace([string]$Name) {
    $t = New-Gft -Id "gloamingforest:courtyard/$Name" -Size 12, 5, 12
    # clear the basin and the air above the rim
    Set-GftBox $t 0 3 0 11 4 11 'minecraft:air'
    Set-GftBox $t 1 1 1 10 2 10 'minecraft:air'
    # the floor and the basin walls
    Set-GftBox $t 1 0 1 10 0 10 $bricks
    Set-GftBox $t 0 0 0 11 1 11 $bricks -Hollow
    # the rim fits the terrain (CourtyardTerraceTemplateProcessor): double slabs on the ring, single
    # slabs as the corner caps above it
    Set-GftBox $t 0 2 0 11 2 11 $slabDouble.Name $slabDouble.Properties -Hollow
    foreach ($c in @(@(0, 0), @(11, 0), @(0, 11), @(11, 11))) { Set-GftBlock $t $c[0] 3 $c[1] $slab.Name $slab.Properties }
    # steps down from the middle of each side
    foreach ($s in @(@(5, 1, 'south'), @(6, 1, 'south'), @(5, 10, 'north'), @(6, 10, 'north'))) {
        Set-GftBlock $t $s[0] 1 $s[1] 'minecraft:stone_brick_stairs' @{ facing = $s[2]; half = 'bottom'; shape = 'straight'; waterlogged = 'false' }
    }
    foreach ($s in @(@(1, 5, 'east'), @(1, 6, 'east'), @(10, 5, 'west'), @(10, 6, 'west'))) {
        Set-GftBlock $t $s[0] 1 $s[1] 'minecraft:stone_brick_stairs' @{ facing = $s[2]; half = 'bottom'; shape = 'straight'; waterlogged = 'false' }
    }
    # mossy patches on the floor
    foreach ($m in @(@(2, 3), @(3, 8), @(8, 2), @(9, 7), @(4, 4), @(7, 9))) { Set-GftBlock $t $m[0] 0 $m[1] $mossy }
    return $t
}

$t = New-Terrace 'terrace_fire'
Set-GftBox $t 4 0 4 7 0 7 'twilightforest:etched_nagastone' @{ facing = 'up' }
Set-GftBox $t 5 0 5 6 0 6 'minecraft:netherrack'
Set-GftBox $t 5 1 5 6 1 6 'minecraft:fire' @{ age = '0'; east = 'false'; north = 'false'; south = 'false'; up = 'false'; west = 'false' }
Save-Gft $t

$t = New-Terrace 'terrace_duct'
Set-GftBox $t 2 0 5 9 0 6 'minecraft:water' @{ level = '0' }
Set-GftBox $t 2 1 4 9 1 4 'minecraft:stone_brick_slab' @{ type = 'bottom'; waterlogged = 'false' }
Set-GftBox $t 2 1 7 9 1 7 'minecraft:stone_brick_slab' @{ type = 'bottom'; waterlogged = 'false' }
Save-Gft $t

$t = New-Terrace 'terrace_channel'
Set-GftBox $t 2 0 5 9 0 6 'minecraft:water' @{ level = '0' }
Set-GftBox $t 5 0 2 6 0 9 'minecraft:water' @{ level = '0' }
foreach ($c in @(@(3, 3), @(8, 3), @(3, 8), @(8, 8))) { Set-GftBlock $t $c[0] 1 $c[1] 'twilightforest:nagastone_pillar' @{ axis = 'y' } }
Save-Gft $t

$t = New-Terrace 'terrace_reservoir'
Set-GftBox $t 3 0 3 8 0 8 'minecraft:water' @{ level = '0' }
Set-GftBox $t 2 0 2 9 0 9 $bricks -Hollow
Set-GftBox $t 4 0 4 7 0 7 'minecraft:water' @{ level = '0' }
Set-GftBlock $t 2 1 2 'minecraft:lily_pad'
Save-Gft $t

$t = New-Terrace 'terrace_statue'
# the rare statue: a coiled naga stone serpent around a pillar, its head looking north
Set-GftBox $t 4 0 4 7 0 7 'twilightforest:etched_nagastone' @{ facing = 'up' }
Set-GftBox $t 5 1 5 5 4 5 'twilightforest:nagastone_pillar' @{ axis = 'y' }
Set-GftBox $t 6 1 6 6 2 6 'twilightforest:nagastone' @{ variant = 'solid' }
Set-GftBox $t 6 1 5 6 1 5 'twilightforest:nagastone' @{ variant = 'axis_z' }
Set-GftBox $t 4 2 5 4 2 6 'twilightforest:nagastone' @{ variant = 'axis_z' }
Set-GftBlock $t 5 3 4 'twilightforest:nagastone' @{ variant = 'axis_y' }
Set-GftBlock $t 5 4 4 'twilightforest:nagastone_head' @{ facing = 'north' }
Save-Gft $t

$t = New-Terrace 'terrace_hedge'
Set-GftBox $t 4 1 4 7 2 7 $hedge
Set-GftBox $t 4 0 4 7 0 7 'minecraft:grass_block'
Save-Gft $t
$t = New-Gft -Id 'gloamingforest:courtyard/terrace_hedge_big' -Size 12, 5, 12
Set-GftBox $t 4 3 4 7 3 7 $leaves.Name $leaves.Properties
Set-GftBox $t 3 1 4 3 2 7 $leaves.Name $leaves.Properties
Set-GftBox $t 8 1 4 8 2 7 $leaves.Name $leaves.Properties
Set-GftBox $t 4 1 3 7 2 3 $leaves.Name $leaves.Properties
Set-GftBox $t 4 1 8 7 2 8 $leaves.Name $leaves.Properties
Save-Gft $t

# ---- the perimeter wall -----------------------------------------------------------------------------------
function New-WallPair([string]$Name, [int]$SizeX, [int]$SizeZ) {
    $t = New-Gft -Id "gloamingforest:courtyard/$Name" -Size $SizeX, 6, $SizeZ
    $mx = $SizeX - 1; $mz = $SizeZ - 1
    Set-GftBox $t 0 0 0 $mx 0 $mz 'minecraft:smooth_stone'
    Set-GftBox $t 0 1 0 $mx 4 $mz $bricks
    Set-GftBox $t 0 5 0 $mx 5 $mz 'minecraft:stone_brick_slab' @{ type = 'bottom'; waterlogged = 'false' }
    return $t
}

$t = New-WallPair 'courtyard_wall' 11 3
# an etched naga stone frieze along both faces, cobbled infill in the middle course
foreach ($x in @(2, 5, 8)) {
    Set-GftBlock $t $x 3 0 'twilightforest:etched_nagastone' @{ facing = 'north' }
    Set-GftBlock $t $x 3 2 'twilightforest:etched_nagastone' @{ facing = 'south' }
}
Set-GftBox $t 0 2 1 10 2 1 'minecraft:cobblestone'
foreach ($x in 0..10) { if ($x % 2 -eq 0) { Set-GftBlock $t $x 5 1 $bricks } }
Save-Gft $t
$t = New-Gft -Id 'gloamingforest:courtyard/courtyard_wall_decayed' -Size 11, 6, 3
Set-GftBox $t 0 1 0 10 4 2 $mossy
Set-GftBox $t 0 2 1 10 2 1 'minecraft:mossy_cobblestone'
Set-GftBox $t 0 5 0 10 5 2 'minecraft:air'
Save-Gft $t

$t = New-WallPair 'courtyard_wall_padding' 1 3
Set-GftBox $t 0 1 0 0 5 2 'twilightforest:nagastone_pillar' @{ axis = 'y' }
Save-Gft $t
$t = New-Gft -Id 'gloamingforest:courtyard/courtyard_wall_padding_decayed' -Size 1, 6, 3
Set-GftBox $t 0 1 0 0 4 2 'twilightforest:mossy_nagastone_pillar' @{ axis = 'y' }
Save-Gft $t

foreach ($corner in @('courtyard_wall_corner', 'courtyard_wall_corner_inner')) {
    $t = New-WallPair $corner 3 3
    Set-GftBox $t 1 1 1 1 5 1 'twilightforest:nagastone_pillar' @{ axis = 'y' }
    Set-GftBlock $t 1 5 1 'twilightforest:etched_nagastone' @{ facing = 'up' }
    if ($corner -eq 'courtyard_wall_corner') {
        foreach ($c in @(@(0, 0), @(2, 0), @(0, 2), @(2, 2))) { Set-GftBlock $t $c[0] 5 $c[1] $bricks }
    }
    Save-Gft $t
    $t = New-Gft -Id "gloamingforest:courtyard/${corner}_decayed" -Size 3, 6, 3
    Set-GftBox $t 0 1 0 2 4 2 'minecraft:mossy_cobblestone' -Hollow
    Set-GftBox $t 0 5 0 2 5 2 'minecraft:air'
    Save-Gft $t
}

# ---- the spawner dais (pool gloamingforest:courtyard/center) ------------------------------------------------
$t = New-Gft -Id 'twilightforest:courtyard/spawner' -Size 5, 3, 5
Set-GftLayer $t -Y 0 -Key @{ 'b' = $bricks; 'm' = $mossy; 'e' = @{ Name = 'twilightforest:etched_nagastone'; Properties = @{ facing = 'up' } } } -Rows @(
    'bmbmb',
    'mbebm',
    'beeeb',
    'mbebm',
    'bmbmb')
Set-GftBlock $t 2 1 2 'minecraft:chiseled_stone_bricks'
foreach ($c in @(@(0, 0), @(4, 0), @(0, 4), @(4, 4))) { Set-GftBlock $t $c[0] 1 $c[1] 'twilightforest:nagastone_pillar' @{ axis = 'y' } }
Set-GftBlock $t 2 2 2 'twilightforest:naga_boss_spawner'
Add-GftJigsaw $t 2 1 2 -Front down -Top north -Pool 'minecraft:empty' -Name 'gloamingforest:courtyard_center' -Target 'minecraft:empty'
Save-Gft $t
