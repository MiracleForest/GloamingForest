# U22 (lichdecor): the roofs and beards of the lich tower rooms, all four sizes, and the room decor
# (original designs, rule R11/R19). Roofs sit on a room's ceiling (junction facing down at their
# bottom centre); beards hang under a room's floor (junction facing up at their top centre); decor
# stands on a room's floor centre.
. (Join-Path $PSScriptRoot '../RoomLib.ps1')

$sb   = 'minecraft:stone_bricks'
$bslab = 'minecraft:stone_brick_slab'
$st   = 'minecraft:stone_brick_stairs'

foreach ($n in 3, 5, 7, 9) {
    $w = $n + 2; $c = [int][Math]::Floor($w / 2)

    # flat: a slab deck in a brick rim, low posts at the corners
    $t = New-LichRoof $n 'flat_roof' 2
    Set-GftBox $t 0 0 0 ($w - 1) 0 ($w - 1) $bslab $script:Slab
    Set-SquareRing $t 0 0 $sb
    foreach ($p in @(@(0, 0), @(0, ($w - 1)), @(($w - 1), 0), @(($w - 1), ($w - 1)))) { Set-GftBlock $t $p[0] 1 $p[1] 'minecraft:stone_brick_wall' }
    Save-Gft $t

    # flat side: a brick deck behind a wall parapet with crenels
    $t = New-LichRoof $n 'flat_side_roof' 2
    Set-GftBox $t 0 0 0 ($w - 1) 0 ($w - 1) $sb
    Set-GftBox $t 1 0 1 ($w - 2) 0 ($w - 2) 'minecraft:polished_andesite'
    for ($i = 0; $i -lt $w; $i += 2) {
        foreach ($p in @(@($i, 0), @($i, ($w - 1)), @(0, $i), @(($w - 1), $i))) { Set-GftBlock $t $p[0] 1 $p[1] 'minecraft:stone_brick_wall' }
    }
    Save-Gft $t

    # slabs: stepped courses of slabs over brick fill
    $t = New-LichRoof $n 'slabs_roof' ($c + 1)
    for ($k = 0; $k -le $c; $k++) {
        Set-SquareRing $t $k $k $bslab $script:Slab
        if ($k + 1 -le $w - 2 - $k) { Set-GftBox $t ($k + 1) $k ($k + 1) ($w - 2 - $k) $k ($w - 2 - $k) $sb }
    }
    Save-Gft $t

    # pyramid: stone brick stairs all the way up, a chiseled cap
    $t = New-LichRoof $n 'pyramid_roof' ($c + 2)
    for ($k = 0; $k -lt $c; $k++) {
        Set-StairRing $t $k $k $st
        if ($k + 1 -le $w - 2 - $k) { Set-GftBox $t ($k + 1) $k ($k + 1) ($w - 2 - $k) $k ($w - 2 - $k) $sb }
    }
    Set-GftBlock $t $c $c $c 'minecraft:chiseled_stone_bricks'
    Set-GftBlock $t $c ($c + 1) $c 'minecraft:stone_brick_wall'
    Save-Gft $t

    # pointed: a steep spire of canopy wood (two courses per step) with an iron finial
    $ph = 2 * $c + 3
    $t = New-LichRoof $n 'pointed_roof' $ph
    for ($k = 0; $k -lt $c; $k++) {
        Set-StairRing $t (2 * $k) $k 'twilightforest:canopy_stairs'
        Set-SquareRing $t (2 * $k + 1) ($k + 1) 'twilightforest:canopy_planks'
    }
    Set-GftBlock $t $c (2 * $c) $c 'twilightforest:canopy_planks'
    Set-GftBlock $t $c (2 * $c + 1) $c 'twilightforest:twisted_stone_pillar'
    Set-GftBlock $t $c (2 * $c + 2) $c 'twilightforest:wrought_iron_fence' @{ post = 'capped' }
    Save-Gft $t

    # curved: a dome of twilight oak, slow at the eaves and steep at the top
    $insets = [System.Collections.Generic.List[int]]::new()
    $insets.Add(0); $insets.Add(0)
    for ($k = 1; $k -le $c; $k++) { $insets.Add($k) }
    $t = New-LichRoof $n 'curved_roof' ($insets.Count + 1)
    for ($y = 0; $y -lt $insets.Count; $y++) {
        $k = $insets[$y]
        $next = if ($y + 1 -lt $insets.Count) { $insets[$y + 1] } else { $k + 1 }
        if ($next -gt $k) { Set-StairRing $t $y $k 'twilightforest:twilight_oak_stairs' }
        else { Set-SquareRing $t $y $k 'twilightforest:twilight_oak_planks' }
    }
    Set-GftBox $t 1 0 1 ($w - 2) 0 ($w - 2) 'twilightforest:twilight_oak_planks'
    Set-GftBlock $t $c $insets.Count $c 'minecraft:lantern'
    Save-Gft $t

    # fence: a railed terrace with iron posts at the corners and a brazier in the middle
    $t = New-LichRoof $n 'fence_roof' 3
    Set-GftBox $t 0 0 0 ($w - 1) 0 ($w - 1) $sb
    Set-GftBox $t 1 0 1 ($w - 2) 0 ($w - 2) 'twilightforest:canopy_planks'
    Set-SquareRing $t 1 0 'twilightforest:canopy_fence'
    foreach ($p in @(@(0, 0), @(0, ($w - 1)), @(($w - 1), 0), @(($w - 1), ($w - 1)))) {
        Set-GftBlock $t $p[0] 1 $p[1] 'twilightforest:wrought_iron_fence' @{ post = 'post' }
        Set-GftBlock $t $p[0] 2 $p[1] 'twilightforest:wrought_iron_fence' @{ post = 'capped' }
    }
    if ($n -ge 5) {
        Set-GftBlock $t $c 1 $c 'twilightforest:brazier' @{ half = 'lower'; light = 'off' }
        Set-GftBlock $t $c 2 $c 'twilightforest:brazier' @{ half = 'upper'; light = 'off' }
    }
    Save-Gft $t

    if ($n -lt 5) { continue }
    $deep = $c - 1 # inverted pyramid depth: insets 1 .. c-1 under the floor

    # flat (fallback): one course of upside-down slabs inside the walls
    $t = New-LichBeard $n 'beard_flat' 1
    Set-GftBox $t 1 0 1 ($w - 2) 0 ($w - 2) $bslab $script:TopSlab
    Save-Gft $t

    # checkered: an inverted pyramid of bricks and andesite in a checkerboard
    $t = New-LichBeard $n 'beard_checkered' $deep
    for ($i = 0; $i -lt $deep; $i++) {
        $y = $deep - 1 - $i; $k = $i + 1
        for ($x = $k; $x -le $w - 1 - $k; $x++) {
            for ($z = $k; $z -le $w - 1 - $k; $z++) {
                $b = if (($x + $z + $y) % 2 -eq 0) { $sb } else { 'minecraft:polished_andesite' }
                Set-GftBlock $t $x $y $z $b
            }
        }
    }
    Save-Gft $t

    # chiseled: an inverted pyramid of upside-down stairs over a chiseled pendant
    $t = New-LichBeard $n 'beard_chiseled' ($deep + 1)
    for ($i = 0; $i -lt $deep; $i++) {
        $y = $deep - $i; $k = $i + 1
        Set-StairRing $t $y $k $st 'top'
        if ($k + 1 -le $w - 2 - $k) { Set-GftBox $t ($k + 1) $y ($k + 1) ($w - 2 - $k) $y ($w - 2 - $k) $sb }
    }
    Set-GftBlock $t $c 0 $c 'minecraft:chiseled_stone_bricks'
    Save-Gft $t

    # chunks: brick corbels under the corners and the wall middles
    $t = New-LichBeard $n 'beard_chunks' ($deep + 1)
    foreach ($p in @(@(1, 1), @(1, ($w - 2)), @(($w - 2), 1), @(($w - 2), ($w - 2)))) { Set-GftBox $t $p[0] 0 $p[1] $p[0] $deep $p[1] $sb }
    foreach ($p in @(@($c, 1), @($c, ($w - 2)), @(1, $c), @(($w - 2), $c))) {
        Set-GftBox $t $p[0] 1 $p[1] $p[0] $deep $p[1] 'minecraft:cobblestone'
        Set-GftBlock $t $p[0] 0 $p[1] 'minecraft:cobblestone_wall'
    }
    Set-GftBox $t 1 $deep 1 ($w - 2) $deep ($w - 2) $sb
    Save-Gft $t

    # staggered: rings of upside-down slabs and stairs alternating, stepping in
    $t = New-LichBeard $n 'beard_staggered' $deep
    for ($i = 0; $i -lt $deep; $i++) {
        $y = $deep - 1 - $i; $k = $i + 1
        if ($i % 2 -eq 0) { Set-SquareRing $t $y $k $bslab $script:TopSlab } else { Set-StairRing $t $y $k 'minecraft:mossy_stone_brick_stairs' 'top' }
        if ($k + 1 -le $w - 2 - $k) { Set-GftBox $t ($k + 1) $y ($k + 1) ($w - 2 - $k) $y ($w - 2 - $k) 'minecraft:cobblestone' }
    }
    Save-Gft $t

    # trim (on the ground): a plinth of brick and cobble the terraformer buries
    $t = New-LichBeard $n 'beard_trim' 3
    Set-GftBox $t 0 0 0 ($w - 1) 2 ($w - 1) 'minecraft:cobblestone'
    Set-SquareRing $t 2 0 $sb
    Set-SquareRing $t 1 0 'minecraft:mossy_cobblestone'
    Save-Gft $t
}

# ---- decor (3 x 3, standing on a room floor or the foyer) ----------------------------------------------
function New-LichDecor([string]$Name, [int]$Height) {
    $t = New-Gft -Id "$($script:NS)3x3/$Name" -Size 3, $Height, 3 -Out "lich/rooms/3x3/$Name"
    Add-GftJigsaw $t 1 0 1 -Front down -Top north -Name 'gf:lich_tower/decor'
    return $t
}

$t = New-LichDecor 'lava_well' 2
Set-GftBox $t 0 0 0 2 0 2 $sb
Set-GftBlock $t 1 0 1 'minecraft:lava'
foreach ($p in @(@(0, 0), @(0, 2), @(2, 0), @(2, 2))) { Set-GftBlock $t $p[0] 1 $p[1] 'minecraft:stone_brick_wall' }
Set-GftBox $t 1 1 0 1 1 0 'minecraft:iron_bars'; Set-GftBlock $t 1 1 2 'minecraft:iron_bars'; Set-GftBlock $t 0 1 1 'minecraft:iron_bars'; Set-GftBlock $t 2 1 1 'minecraft:iron_bars'
Save-Gft $t

$t = New-LichDecor 'water_well' 4
Set-GftBox $t 0 0 0 2 0 2 'minecraft:cobblestone'
Set-GftBlock $t 1 0 1 'minecraft:water'
foreach ($p in @(@(0, 0), @(0, 2), @(2, 0), @(2, 2))) { Set-GftBox $t $p[0] 1 $p[1] $p[0] 2 $p[1] 'twilightforest:canopy_fence' }
Set-GftBox $t 0 3 0 2 3 2 'twilightforest:canopy_slab' $script:Slab
Set-GftBlock $t 1 2 1 'minecraft:chain'
Save-Gft $t

$t = New-LichDecor 'water_fountain' 3
Set-GftBox $t 0 0 0 2 0 2 $bslab $script:Slab
Set-GftBlock $t 1 0 1 'minecraft:chiseled_stone_bricks'
Set-GftBlock $t 1 1 1 'minecraft:stone_brick_wall'
Set-GftBlock $t 1 2 1 'minecraft:water'
Save-Gft $t

$t = New-LichDecor 'sapling' 2
Set-GftBox $t 0 0 0 2 0 2 'minecraft:polished_andesite'
Set-GftBlock $t 1 0 1 'minecraft:grass_block'
Set-GftBlock $t 1 1 1 'minecraft:air'
Add-GftDataMarker $t 1 1 1 'sapling'
Save-Gft $t

$t = New-LichDecor 'tree' 2
Set-GftBox $t 0 0 0 2 0 2 'minecraft:mossy_stone_bricks'
Set-GftBlock $t 1 0 1 'minecraft:grass_block'
Set-GftBlock $t 1 1 1 'minecraft:air'
Add-GftDataMarker $t 1 1 1 'tree'
Save-Gft $t

$t = New-LichDecor 'wither_rose' 2
Set-GftBox $t 0 0 0 2 0 2 'minecraft:polished_blackstone_bricks'
Set-GftBlock $t 1 0 1 'minecraft:soul_soil'
Set-GftBlock $t 1 1 1 'minecraft:wither_rose'
foreach ($p in @(@(0, 0), @(0, 2), @(2, 0), @(2, 2))) { Set-GftBlock $t $p[0] 1 $p[1] 'minecraft:soul_lantern' }
Save-Gft $t
