# U19 (questcamps): the 21 templates of upstream's template features (docs/spec/questcamps.md 4), original
# GloamingForest designs (rule R19). upstream's feature templates have no template definition; their ids are
# upstream's own gloamingforest:feature/<path> (R1). Every TemplateFeature template is at most 15 x 15 (the snap's
# nextInt(16 - size) must stay positive for any rotation); the graveyard's base is not limited.
#   druid hut   layer 0 replaces the averaged ground block; the basement goes 12 below, one block north
#               and one east of the hut's origin, its top layer reaching the hut's floor (a trapdoor)
#               markers: spawner, loot<F><K>[J|T], barrel<F>, painting<F><w>
#   wells       the top sits one above the ground (simple) or on it (fancy); the bottom right below it
#               markers: loot<F> (simple: barrel or cobblestone, maybe a hopper), loot (fancy: barrel up)
#   graveyard   layer 0 one below the averaged ground, layer 1 the ground line, layer 2 the open floor;
#               the graves share one 4 x 4 x 1 footprint; spawner markers in the base and the graves
#   ruins       layer 0 replaces the ground block
. (Join-Path $PSScriptRoot 'QuestcampsLib.ps1')

function S([string]$n, [hashtable]$p) { return @{ Name = $n; Properties = $p } }
$cobble   = 'minecraft:cobblestone'
$planks   = 'minecraft:oak_planks'
$logY     = S 'minecraft:oak_log' @{ axis = 'y' }
$logX     = S 'minecraft:oak_log' @{ axis = 'x' }
$logZ     = S 'minecraft:oak_log' @{ axis = 'z' }
$spruce   = 'minecraft:spruce_planks'
$birch    = 'minecraft:birch_planks'
function Stair([string]$wood, [string]$facing, [string]$half = 'bottom') { return S "minecraft:${wood}_stairs" @{ facing = $facing; half = $half; shape = 'straight'; waterlogged = 'false' } }
function Slab([string]$wood, [string]$type = 'bottom') { return S "minecraft:${wood}_slab" @{ type = $type; waterlogged = 'false' } }
function Put($t, [int]$x, [int]$y, [int]$z, $b) { if ($b -is [string]) { Set-GftBlock $t $x $y $z $b } else { Set-GftBlock $t $x $y $z $b.Name $b.Properties } }
function Box($t, [int]$x1, [int]$y1, [int]$z1, [int]$x2, [int]$y2, [int]$z2, $b) {
    if ($b -is [string]) { Set-GftBox $t $x1 $y1 $z1 $x2 $y2 $z2 $b } else { Set-GftBox $t $x1 $y1 $z1 $x2 $y2 $z2 $b.Name $b.Properties }
}
$pane = S 'minecraft:glass_pane' @{ east = 'false'; north = 'false'; south = 'false'; west = 'false'; waterlogged = 'false' }

# ---- druid huts ----------------------------------------------------------------------------------------------
# A hut room: cobblestone footing (layer 0), a cobblestone course, plank walls with log corners, glass
# panes, a door on the south wall and a stair roof. (x0, z0) .. (x1, z1) is the outline, y0 the floor.
function Set-HutRoom($t, [int]$x0, [int]$z0, [int]$x1, [int]$z1, [int]$y0, [int]$wallH, [string]$wood, [string]$plank, [switch]$NoDoor, [switch]$NoRoof) {
    Box $t $x0 $y0 $z0 $x1 $y0 $z1 $cobble
    Box $t ($x0 + 1) $y0 ($z0 + 1) ($x1 - 1) $y0 ($z1 - 1) $plank
    for ($y = $y0 + 1; $y -le $y0 + $wallH; $y++) {
        $course = if ($y -eq $y0 + 1) { $cobble } else { $plank }
        Box $t $x0 $y $z0 $x1 $y $z1 $course
        Box $t ($x0 + 1) $y ($z0 + 1) ($x1 - 1) $y ($z1 - 1) 'minecraft:air'
        foreach ($cx in $x0, $x1) { foreach ($cz in $z0, $z1) { Put $t $cx $y $cz (S "minecraft:${wood}_log" @{ axis = 'y' }) } }
    }
    # windows in the middle of each wall
    $mx = [int](($x0 + $x1) / 2); $mz = [int](($z0 + $z1) / 2)
    if ($wallH -ge 3) {
        Put $t $mx ($y0 + 2) $z0 $pane; Put $t $x0 ($y0 + 2) $mz $pane; Put $t $x1 ($y0 + 2) $mz $pane
        if ($NoDoor) { Put $t $mx ($y0 + 2) $z1 $pane }
    }
    if (-not $NoDoor) {
        Put $t $mx ($y0 + 1) $z1 (S "minecraft:${wood}_door" @{ facing = 'north'; half = 'lower'; hinge = 'left'; open = 'false'; powered = 'false' })
        Put $t $mx ($y0 + 2) $z1 (S "minecraft:${wood}_door" @{ facing = 'north'; half = 'upper'; hinge = 'left'; open = 'false'; powered = 'false' })
    }
    if (-not $NoRoof) {
        # a stepped stair roof along x
        $y = $y0 + $wallH + 1
        $a = $z0 - 1; $b = $z1 + 1
        while ($a -le $b) {
            for ($x = $x0 - 1; $x -le $x1 + 1; $x++) {
                if ($x -lt 0 -or $x -ge $t.Size[0]) { continue }
                if ($a -ge 0 -and $a -lt $t.Size[2]) { Put $t $x $y $a (Stair $wood 'south') }
                if ($b -ge 0 -and $b -lt $t.Size[2] -and $b -ne $a) { Put $t $x $y $b (Stair $wood 'north') }
                for ($z = $a + 1; $z -lt $b; $z++) { if ($z -ge 0 -and $z -lt $t.Size[2] -and $y -lt $t.Size[1]) { if ($z -eq $a + 1 -or $z -eq $b - 1) { Put $t $x $y $z $plank } } }
            }
            $a++; $b--; $y++
            if ($y -ge $t.Size[1]) { break }
        }
    }
}
function New-Hut([string]$name, [int[]]$size) {
    $t = New-Gft -Id "gloamingforest:feature/druid_hut/$name" -Size $size
    Box $t 0 1 0 ($size[0] - 1) ($size[1] - 1) ($size[2] - 1) 'minecraft:air'
    return $t
}

# druid_hut 9 x 9 x 9: one oak room, a bed nook, a hut-junk chest, a barrel and a painting; the
# basement trapdoor at (2, 0, 1) (above the basement shaft)
$t = New-Hut 'druid_hut' @(9, 9, 9)
Set-HutRoom $t 1 1 7 7 0 3 'oak' $planks
Put $t 2 0 1 'minecraft:cobblestone'
Put $t 2 1 2 'minecraft:crafting_table'
Put $t 6 1 2 'minecraft:cauldron'
Add-QcData $t 6 1 6 'lootNSJ'
Add-QcData $t 2 1 6 'barrelU'
Add-QcData $t 4 2 2 'paintingS1'
Put $t 3 0 4 'minecraft:moss_carpet'; Put $t 4 0 4 'minecraft:moss_carpet'
Put $t 4 4 4 (S 'minecraft:lantern' @{ hanging = 'true'; waterlogged = 'false' })
Save-Gft $t

# druid_sideways 13 x 8 x 7: a long spruce hall with the door on the long side
$t = New-Hut 'druid_sideways' @(13, 8, 7)
Set-HutRoom $t 1 1 11 5 0 3 'spruce' $spruce
Put $t 2 1 2 'minecraft:crafting_table'
Put $t 3 1 2 'minecraft:composter'
Put $t 10 1 2 (S 'minecraft:brewing_stand' @{ has_bottle_0 = 'false'; has_bottle_1 = 'false'; has_bottle_2 = 'false' })
Add-QcData $t 9 1 4 'lootNLJ'
Add-QcData $t 10 1 4 'lootNRJ'
Add-QcData $t 2 1 4 'barrelE'
Add-QcData $t 4 2 2 'paintingS2'
Put $t 6 4 3 (S 'minecraft:lantern' @{ hanging = 'true'; waterlogged = 'false' })
Save-Gft $t

# druid_doubledeck 9 x 13 x 9: a birch room with a spruce loft reached by a ladder
$t = New-Hut 'druid_doubledeck' @(9, 13, 9)
Set-HutRoom $t 1 1 7 7 0 3 'birch' $birch -NoRoof
Set-HutRoom $t 1 1 7 7 4 3 'spruce' $spruce -NoDoor
# the loft floor has a ladder hole
Put $t 6 4 6 'minecraft:air'
for ($y = 1; $y -le 4; $y++) { Put $t 6 $y 6 (S 'minecraft:ladder' @{ facing = 'north'; waterlogged = 'false' }) }
Put $t 6 1 7 $birch
Add-QcData $t 2 5 6 'lootNSJ'
Add-QcData $t 2 1 2 'barrelS'
Add-QcData $t 4 6 2 'paintingS1'
Put $t 3 1 2 'minecraft:crafting_table'
Put $t 4 5 4 'minecraft:white_carpet'
Save-Gft $t

# basements: 8 x 13 x 8 (x 1..8, z -1..6 of the hut); a ladder shaft from the trapdoor at the top layer
# (the hut's floor cell (2, 0, 1), template (1, 12, 2)) down to a 6 x 4 x 6 cellar room
function New-Basement([string]$name, [switch]$Trap) {
    $t = New-Gft -Id "gloamingforest:feature/druid_hut/$name" -Size 8, 13, 8
    Box $t 0 0 0 7 5 7 'minecraft:stone_bricks'
    Box $t 1 1 1 6 4 6 'minecraft:air'
    Box $t 1 1 1 6 1 6 'minecraft:spruce_planks'
    Box $t 1 1 1 6 1 6 'minecraft:air'
    Box $t 1 0 1 6 0 6 'minecraft:spruce_planks'
    # the shaft
    for ($y = 1; $y -le 11; $y++) {
        if ($y -gt 4) { Box $t 0 $y 1 2 $y 3 'minecraft:dirt' }
        Put $t 1 $y 2 (S 'minecraft:ladder' @{ facing = 'south'; waterlogged = 'false' })
    }
    Put $t 1 12 2 (S 'minecraft:spruce_trapdoor' @{ facing = 'south'; half = 'top'; open = 'false'; powered = 'false'; waterlogged = 'false' })
    Put $t 1 5 1 'minecraft:stone_bricks'
    Put $t 6 4 6 (S 'minecraft:lantern' @{ hanging = 'true'; waterlogged = 'false' })
    Put $t 6 4 1 (S 'minecraft:lantern' @{ hanging = 'true'; waterlogged = 'false' })
    if ($Trap) {
        Add-QcData $t 4 1 4 'spawner'
        Add-QcData $t 5 1 6 'lootNST'
    } else {
        Add-QcData $t 5 1 6 'lootNS'
    }
    return $t
}
foreach ($trap in $false, $true) {
    $suffix = if ($trap) { '_trap' } else { '' }
    # the study: a lectern, bookshelves and a writing desk
    $t = New-Basement "basement_study$suffix" -Trap:$trap
    Box $t 6 1 2 6 3 4 'minecraft:bookshelf'
    Put $t 3 1 6 (S 'minecraft:lectern' @{ facing = 'north'; has_book = 'false'; powered = 'false' })
    Put $t 2 1 5 (Stair 'spruce' 'east')
    Put $t 3 1 5 (Slab 'spruce' 'top')
    Save-Gft $t
    # the shelves: rows of barrels and bookshelves along the walls
    $t = New-Basement "basement_shelves$suffix" -Trap:$trap
    Box $t 3 1 1 6 3 1 'minecraft:bookshelf'
    Box $t 6 1 2 6 2 5 'minecraft:bookshelf'
    Add-QcData $t 2 1 6 'barrelN'
    Add-QcData $t 3 1 6 'barrelU'
    Save-Gft $t
    # the gallery: paintings on the walls between brick pillars
    $t = New-Basement "basement_gallery$suffix" -Trap:$trap
    Box $t 3 1 1 3 4 1 'minecraft:chiseled_stone_bricks'
    Box $t 6 1 3 6 4 3 'minecraft:chiseled_stone_bricks'
    Add-QcData $t 4 2 1 'paintingS1'
    Add-QcData $t 6 2 4 'paintingW1'
    Add-QcData $t 2 2 6 'paintingN1'
    Save-Gft $t
}

# ---- wells ---------------------------------------------------------------------------------------------------
# simple_well_top 5 x 4 x 5: a cobblestone rim, two posts and an oak roof (placed one above the ground)
$t = New-Gft -Id 'gloamingforest:feature/well/simple_well_top' -Size 5, 4, 5
Box $t 0 0 0 4 3 4 'minecraft:air'
Box $t 1 0 1 3 0 3 $cobble
Put $t 2 0 2 'minecraft:air'
Put $t 1 1 2 'minecraft:oak_fence'; Put $t 3 1 2 'minecraft:oak_fence'
Put $t 1 2 2 'minecraft:oak_fence'; Put $t 3 2 2 'minecraft:oak_fence'
Box $t 1 3 1 3 3 3 (Slab 'oak')
Put $t 2 3 2 $planks
Save-Gft $t
# simple_well_bottom 5 x 9 x 5: the shaft down to water; loot markers in its walls
$t = New-Gft -Id 'gloamingforest:feature/well/simple_well_bottom' -Size 5, 9, 5
Box $t 1 0 1 3 8 3 $cobble
Box $t 2 0 2 2 8 2 'minecraft:air'
Box $t 2 0 2 2 2 2 'minecraft:water'
Put $t 1 8 1 'minecraft:mossy_cobblestone'
Add-QcData $t 2 4 1 'lootS'
Add-QcData $t 3 3 2 'lootW'
Save-Gft $t
# fancy_well_top 7 x 6 x 7: a stone brick basin of water with four pillars and a stair roof
$t = New-Gft -Id 'gloamingforest:feature/well/fancy_well_top' -Size 7, 6, 7
Box $t 0 0 0 6 5 6 'minecraft:air'
Box $t 0 0 0 6 0 6 'minecraft:stone_bricks'
Box $t 1 1 1 5 1 5 'minecraft:stone_brick_wall'
Box $t 2 1 2 4 1 4 'minecraft:air'
Box $t 2 0 2 4 0 4 'minecraft:water'
Put $t 3 0 3 'minecraft:air'
foreach ($p in @(@(1, 1), @(5, 1), @(1, 5), @(5, 5))) { Box $t $p[0] 1 $p[1] $p[0] 3 $p[1] 'minecraft:cobblestone' }
Box $t 0 4 0 6 4 6 (Slab 'stone_brick')
Box $t 1 4 1 5 4 5 'minecraft:stone_bricks'
Box $t 2 4 2 4 4 4 'minecraft:air'
Box $t 2 5 2 4 5 4 (Slab 'stone_brick')
Put $t 3 3 3 (S 'minecraft:chain' @{ axis = 'y'; waterlogged = 'false' })
Put $t 3 2 3 (S 'minecraft:lantern' @{ hanging = 'true'; waterlogged = 'false' })
Save-Gft $t
# fancy_well_bottom 7 x 12 x 7: a wide shaft of mossy stones; the treasure barrel at the bottom
$t = New-Gft -Id 'gloamingforest:feature/well/fancy_well_bottom' -Size 7, 12, 7
Box $t 1 0 1 5 11 5 'minecraft:stone_bricks'
Box $t 2 1 2 4 11 4 'minecraft:air'
Put $t 3 11 3 'minecraft:air'
Box $t 2 1 2 4 4 4 'minecraft:water'
Box $t 2 0 2 4 0 4 'minecraft:gravel'
Add-QcData $t 3 0 3 'loot'
Save-Gft $t

# ---- graveyard -----------------------------------------------------------------------------------------------
$headstone = 'minecraft:stone_brick_wall'
$mossyWall = S 'minecraft:mossy_cobblestone_wall' @{ east = 'none'; north = 'none'; south = 'none'; west = 'none'; up = 'true'; waterlogged = 'false' }
# base 17 x 5 x 14: a fenced podzol yard with an iron gate in the north wall and dead trees
$t = New-Gft -Id 'gloamingforest:feature/graveyard/graveyard' -Size 17, 5, 14
Box $t 0 0 0 16 0 13 'minecraft:dirt'
Box $t 0 1 0 16 1 13 'minecraft:podzol'
Box $t 0 2 0 16 4 13 'minecraft:air'
for ($x = 0; $x -le 16; $x++) { Put $t $x 2 0 $mossyWall; Put $t $x 2 13 $mossyWall }
for ($z = 0; $z -le 13; $z++) { Put $t 0 2 $z $mossyWall; Put $t 16 2 $z $mossyWall }
Put $t 8 2 0 (S 'minecraft:iron_bars' @{ east = 'false'; north = 'false'; south = 'false'; west = 'false'; waterlogged = 'false' })
Put $t 7 2 0 'minecraft:air'
foreach ($p in @(@(0, 0), @(16, 0), @(0, 13), @(16, 13))) { Box $t $p[0] 2 $p[1] $p[0] 3 $p[1] 'minecraft:mossy_cobblestone'; Put $t $p[0] 4 $p[1] (S 'minecraft:lantern' @{ hanging = 'false'; waterlogged = 'false' }) }
foreach ($p in @(@(3, 2), @(13, 11), @(1, 7))) { Put $t $p[0] 2 $p[1] 'minecraft:dead_bush' }
Box $t 15 2 6 15 4 6 (S 'minecraft:dark_oak_log' @{ axis = 'y' })
Put $t 14 4 6 (S 'minecraft:dark_oak_fence' @{ east = 'true'; north = 'false'; south = 'false'; west = 'false'; waterlogged = 'false' })
Put $t 1 1 1 'minecraft:coarse_dirt'; Put $t 15 1 12 'minecraft:coarse_dirt'
Add-QcData $t 1 2 12 'spawner'
Add-QcData $t 15 2 1 'spawner'
Save-Gft $t
# graves 4 x 4 x 1 (x: the headstone at 0, the mound 1..3; layer 0 under the ground, 1 the ground line)
function New-Grave([string]$name, [bool]$stone, [bool]$mound, [bool]$spawner) {
    $t = New-Gft -Id "gloamingforest:feature/graveyard/$name" -Size 4, 4, 1
    Box $t 0 0 0 3 0 0 'minecraft:dirt'
    Box $t 0 1 0 3 1 0 'minecraft:coarse_dirt'
    Box $t 1 2 0 3 3 0 'minecraft:air'
    if ($stone) {
        Put $t 0 2 0 $headstone
        Put $t 0 3 0 (Slab 'stone_brick')
    } else {
        Put $t 0 2 0 (S 'minecraft:cobblestone_slab' @{ type = 'bottom'; waterlogged = 'false' })
    }
    if ($mound) {
        Box $t 1 1 0 3 1 0 'minecraft:rooted_dirt'
        Put $t 2 2 0 (S 'minecraft:moss_carpet' @{})
    }
    if ($spawner) { Add-QcData $t 2 0 0 'spawner' }
    Save-Gft $t
}
New-Grave 'grave_full' $true $true $true
New-Grave 'grave_upper' $true $false $false
New-Grave 'grave_lower' $false $true $false
# the trap under a full grave: a hollow full of cobwebs (no data markers: the trap's filterBlocks quirk
# then adds nothing)
$t = New-Gft -Id 'gloamingforest:feature/graveyard/grave_trap' -Size 3, 2, 3
Box $t 0 0 0 2 0 2 'minecraft:cobweb'
Put $t 1 0 1 'minecraft:soul_sand'
Box $t 0 1 0 2 1 2 'minecraft:coarse_dirt'
Save-Gft $t

# ---- ruins ---------------------------------------------------------------------------------------------------
# grove_pillar 3 x 8 x 3: a broken stone brick column with vines
$t = New-Gft -Id 'gloamingforest:feature/ruins/grove_pillar' -Size 3, 8, 3
Box $t 0 0 0 2 0 2 'minecraft:stone_bricks'
Box $t 1 1 1 1 6 1 'minecraft:stone_bricks'
Put $t 1 3 1 'minecraft:chiseled_stone_bricks'
Put $t 0 1 1 (Stair 'stone_brick' 'east'); Put $t 2 1 1 (Stair 'stone_brick' 'west')
Put $t 1 1 0 (Stair 'stone_brick' 'south'); Put $t 1 1 2 (Stair 'stone_brick' 'north')
Put $t 1 7 1 (Slab 'stone_brick')
Put $t 0 5 1 (S 'minecraft:vine' @{ east = 'true'; north = 'false'; south = 'false'; up = 'false'; west = 'false' })
Put $t 0 4 1 (S 'minecraft:vine' @{ east = 'true'; north = 'false'; south = 'false'; up = 'false'; west = 'false' })
Save-Gft $t
# grove_arch 7 x 8 x 3: two columns and a lintel, one side fallen
$t = New-Gft -Id 'gloamingforest:feature/ruins/grove_arch' -Size 7, 8, 3
Box $t 0 0 0 6 0 2 'minecraft:stone_bricks'
Box $t 1 1 1 1 6 1 'minecraft:stone_bricks'
Box $t 5 1 1 5 5 1 'minecraft:stone_bricks'
Box $t 1 7 1 4 7 1 'minecraft:stone_bricks'
Put $t 2 6 1 (Stair 'stone_brick' 'west' 'top'); Put $t 4 6 1 (Stair 'stone_brick' 'east' 'top')
Put $t 1 4 1 'minecraft:chiseled_stone_bricks'; Put $t 5 4 1 'minecraft:chiseled_stone_bricks'
Put $t 6 1 0 'minecraft:stone_bricks'; Put $t 6 1 2 (Slab 'stone_brick')
Put $t 2 7 1 'minecraft:mossy_stone_bricks'
Save-Gft $t
# stone_circle 11 x 5 x 11: a ring of mossy cobblestone menhirs around a flat altar stone
$t = New-Gft -Id 'gloamingforest:feature/ruins/stone_circle' -Size 11, 5, 11
$heights = @(4, 3, 4, 2, 4, 3, 4, 1)
for ($i = 0; $i -lt 8; $i++) {
    $a = $i * [Math]::PI / 4
    $x = 5 + [int][Math]::Round([Math]::Cos($a) * 5)
    $z = 5 + [int][Math]::Round([Math]::Sin($a) * 5)
    Put $t $x 0 $z $cobble
    for ($y = 1; $y -le $heights[$i]; $y++) { Put $t $x $y $z $(if ($y % 2) { 'minecraft:mossy_cobblestone' } else { $cobble }) }
}
Box $t 4 0 4 6 0 6 $cobble
Box $t 4 1 5 6 1 5 (S 'minecraft:cobblestone_slab' @{ type = 'bottom'; waterlogged = 'false' })
Save-Gft $t
