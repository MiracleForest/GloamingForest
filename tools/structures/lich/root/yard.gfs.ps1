# U21 (lich): the lich tower's yard (original designs, rule R19): the graves beside the paths
# (upstream's template pool gloamingforest:lich_tower/grave: grave, grave_ajar, grave_open) and the
# perimeter fence pieces LichPerimeterFence strings from the end of the path to the wing trims
# (outer_fence_1 .. outer_fence_7; upstream's code names them, so they carry GloamingForest ids).
. (Join-Path $PSScriptRoot '../LichLib.ps1')
$NS  = 'gloamingforest:lich_tower/'
$GNS = 'gf:lich_tower/'

# ---- graves -----------------------------------------------------------------------------------------
# 3 wide, 3 high, 5 long. The junction sits at the path end of the plot on the ground layer (y 1)
# and faces the path (north); y 0 lies under the ground, y 2 above it. The headstone stands at the
# far end (z 4).
function New-LichGrave([string]$id, [scriptblock]$shape) {
    $t = New-Gft -Id "$NS$id" -Size 3, 3, 5 -Out "lich/root/$id"
    Set-GftBox $t 0 0 0 2 0 4 'minecraft:dirt'
    Set-GftBox $t 0 1 0 2 1 0 'minecraft:coarse_dirt'
    Set-GftBox $t 0 1 1 0 1 3 'minecraft:mossy_cobblestone'
    Set-GftBox $t 2 1 1 2 1 3 'minecraft:mossy_cobblestone'
    Set-GftBox $t 1 1 1 1 1 3 'minecraft:coarse_dirt'
    Set-GftBox $t 0 1 4 2 1 4 'minecraft:stone_bricks'
    Set-GftBlock $t 0 2 4 'minecraft:stone_brick_slab' @{ type = 'bottom' }
    Set-GftBlock $t 1 2 4 'minecraft:mossy_stone_brick_wall'
    Set-GftBlock $t 2 2 4 'minecraft:stone_brick_slab' @{ type = 'bottom' }
    & $shape $t
    Add-GftJigsaw $t 1 1 0 -Front north -Top up -Name 'gf:lich_tower/grave'
    Save-Gft $t
}

# a closed grave: a slab lid over the plot, a chest buried under it
New-LichGrave 'grave' {
    param($t)
    Set-GftBox $t 1 2 1 1 2 3 'minecraft:stone_brick_slab' @{ type = 'bottom' }
    Set-GftBlock $t 1 0 2 'minecraft:chest' @{ facing = 'north' }
    Add-GftMarker $t 1 0 2 'loot' @{ table = 'gloamingforest:chests/tower_grave_lower' }
}
# the lid pushed back: the hole at the foot shows the chest
New-LichGrave 'grave_ajar' {
    param($t)
    Set-GftBox $t 1 2 2 1 2 3 'minecraft:mossy_stone_brick_slab' @{ type = 'bottom' }
    Set-GftBlock $t 1 2 1 'minecraft:air'
    Set-GftBlock $t 1 1 1 'minecraft:air'
    Set-GftBlock $t 1 0 1 'minecraft:chest' @{ facing = 'north' }
    Add-GftMarker $t 1 0 1 'loot' @{ table = 'gloamingforest:chests/tower_grave_upper' }
    Set-GftBlock $t 0 2 1 'minecraft:cobblestone_slab' @{ type = 'bottom' }
}
# dug up: an open pit with the earth heaped beside it
New-LichGrave 'grave_open' {
    param($t)
    Set-GftBox $t 1 1 1 1 1 3 'minecraft:air'
    Set-GftBox $t 1 2 1 1 2 3 'minecraft:air'
    Set-GftBlock $t 1 0 3 'minecraft:chest' @{ facing = 'north' }
    Add-GftMarker $t 1 0 3 'loot' @{ table = 'gloamingforest:chests/tower_grave_lower' }
    Set-GftBlock $t 0 2 2 'minecraft:coarse_dirt'
    Set-GftBlock $t 2 2 1 'minecraft:coarse_dirt'
    Set-GftBlock $t 2 2 3 'minecraft:dirt'
}

# ---- the perimeter fence ---------------------------------------------------------------------------
# outer_fence_<n>: n + 1 blocks along x between two stone brick pillars (x 0 and x n), one block
# deep. y 0 lies under the ground, y 1 is the ground layer, the fence stands on y 2 .. 5. Outward is
# north. The junctions are on the ground layer at the pillars, facing down with their top outward:
# fence_edge_left at x n and fence_edge_right at x 0 (left and right as seen from outside, looking
# at the tower); a fence continues another by the edge the parent's edge targets. outer_fence_7 also
# has the fence_source in the middle, where the first fence meets the end of the path.
function New-LichFence([int]$n) {
    $t = New-Gft -Id "${GNS}outer_fence_$n" -Size ($n + 1), 6, 1 -Out "lich/root/outer_fence_$n"
    Set-GftBox $t 0 0 0 $n 0 0 'minecraft:stone_bricks'
    for ($x = 0; $x -le $n; $x++) {
        $b = if (($x % 3) -eq 1) { 'minecraft:mossy_stone_bricks' } else { 'minecraft:stone_bricks' }
        Set-GftBlock $t $x 1 0 $b
    }
    foreach ($x in 0, $n) {
        Set-GftBox $t $x 2 0 $x 4 0 'minecraft:stone_bricks'
        Set-GftBlock $t $x 5 0 'minecraft:chiseled_stone_bricks'
    }
    for ($x = 1; $x -lt $n; $x++) {
        Set-GftBlock $t $x 2 0 'minecraft:stone_brick_wall'
        # the iron fence: posts (a zombie may be leashed to one) on the middle course
        $post = if (($x % 2) -eq 1) { 'post' } else { 'none' }
        Set-GftBlock $t $x 3 0 'gloamingforest:wrought_iron_fence' @{ post = $post }
        Set-GftBlock $t $x 4 0 'gloamingforest:wrought_iron_fence' @{ post = 'capped' }
    }
    Add-GftJigsaw $t $n 1 0 -Front down -Top north -Name 'gf:lich_tower/fence_edge_left' -Target 'gf:lich_tower/fence_edge_right'
    Add-GftJigsaw $t 0 1 0 -Front down -Top north -Name 'gf:lich_tower/fence_edge_right' -Target 'gf:lich_tower/fence_edge_left'
    if ($n -eq 7) { Add-GftJigsaw $t 3 1 0 -Front down -Top north -Name 'gf:lich_tower/fence_source' }
    Save-Gft $t
}
for ($n = 1; $n -le 7; $n++) { New-LichFence $n }
