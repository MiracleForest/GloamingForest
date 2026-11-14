# U21 (lich): the wing bridges and door covers of the lich tower (original designs, rule R19). Every
# bridge runs along +z: its junction faces north at (centre, 1, 0) and the room junction south at the
# far end; the walkway is at y 1 over a floor at y 0.
. (Join-Path $PSScriptRoot '../LichLib.ps1')
$NS = 'gloamingforest:lich_tower/'

# A bridge of length L (3 wide). Style: open (fences), enclosed (walls and a slab roof), planks.
function New-LichBridge([string]$id, [string]$out, [int]$len, [string]$style, [string]$name, [string]$floor = 'minecraft:stone_bricks') {
    $h = if ($style -eq 'enclosed') { 4 } else { 3 }
    $t = New-Gft -Id "$NS$id" -Size 3, $h, $len -Out "lich/root/$out"
    Set-GftBox $t 0 0 0 2 0 ($len - 1) $floor
    Set-GftBox $t 1 0 0 1 0 ($len - 1) 'minecraft:polished_andesite'
    Set-GftBox $t 1 1 0 1 2 ($len - 1) 'minecraft:air'
    switch ($style) {
        'enclosed' {
            Set-GftBox $t 0 1 0 0 2 ($len - 1) 'minecraft:stone_bricks'
            Set-GftBox $t 2 1 0 2 2 ($len - 1) 'minecraft:stone_bricks'
            for ($z = 1; $z -lt $len - 1; $z += 2) { Set-GftBlock $t 0 2 $z 'minecraft:iron_bars'; Set-GftBlock $t 2 2 $z 'minecraft:iron_bars' }
            Set-GftBox $t 0 3 0 2 3 ($len - 1) 'minecraft:stone_brick_slab' @{ type = 'bottom' }
        }
        'open' {
            Set-GftBox $t 0 1 0 0 1 ($len - 1) 'gloamingforest:canopy_fence'
            Set-GftBox $t 2 1 0 2 1 ($len - 1) 'gloamingforest:canopy_fence'
            Set-GftBox $t 0 2 0 0 2 ($len - 1) 'minecraft:air'
            Set-GftBox $t 2 2 0 2 2 ($len - 1) 'minecraft:air'
        }
        'planks' {
            Set-GftBox $t 0 0 0 2 0 ($len - 1) 'gloamingforest:gloaming_oak_planks'
            Set-GftBox $t 0 1 0 0 1 ($len - 1) 'minecraft:stone_brick_wall'
            Set-GftBox $t 2 1 0 2 1 ($len - 1) 'minecraft:stone_brick_wall'
            Set-GftBox $t 0 2 0 0 2 ($len - 1) 'minecraft:air'
            Set-GftBox $t 2 2 0 2 2 ($len - 1) 'minecraft:air'
        }
        'slabs' {
            Set-GftBox $t 0 0 0 2 0 ($len - 1) 'gloamingforest:canopy_slab' @{ type = 'top' }
            Set-GftBox $t 0 1 0 0 2 ($len - 1) 'minecraft:air'
            Set-GftBox $t 2 1 0 2 2 ($len - 1) 'minecraft:air'
            for ($z = 0; $z -lt $len; $z += 2) { Set-GftBlock $t 0 1 $z 'gloamingforest:canopy_fence'; Set-GftBlock $t 2 1 $z 'gloamingforest:canopy_fence' }
        }
    }
    Add-GftJigsaw $t 1 1 0 -Front north -Name $name
    Add-GftJigsaw $t 1 1 ($len - 1) -Front south -Name 'gf:lich_tower/bridge_end' -Target 'gf:lich_tower/room'
    Save-Gft $t
}

New-LichBridge 'central_bridge' 'central_bridge' 4 'enclosed' 'gf:lich_tower/bridge_center'
New-LichBridge 'central_bridge_open' 'central_bridge_open' 4 'open' 'gf:lich_tower/bridge_center'
New-LichBridge 'room_bridge_1' 'room_bridge_1' 3 'open' 'gf:lich_tower/bridge'
New-LichBridge 'room_bridge_2' 'room_bridge_2' 4 'planks' 'gf:lich_tower/bridge'
New-LichBridge 'room_bridge_3' 'room_bridge_3' 5 'enclosed' 'gf:lich_tower/bridge'
New-LichBridge 'room_bridge_4' 'room_bridge_4' 3 'slabs' 'gf:lich_tower/bridge'
New-LichBridge 'room_bridge_5' 'room_bridge_5' 6 'open' 'gf:lich_tower/bridge' 'minecraft:cobblestone'
New-LichBridge 'end_bridge_1' 'end_bridge_1' 2 'open' 'gf:lich_tower/bridge'
New-LichBridge 'end_bridge_2' 'end_bridge_2' 2 'planks' 'gf:lich_tower/bridge'
New-LichBridge 'end_bridge_3' 'end_bridge_3' 3 'enclosed' 'gf:lich_tower/bridge'
New-LichBridge 'end_bridge_4' 'end_bridge_4' 2 'slabs' 'gf:lich_tower/bridge'
New-LichBridge 'end_bridge_5' 'end_bridge_5' 3 'planks' 'gf:lich_tower/bridge' 'minecraft:cobblestone'

# no_bridge: a room right at the door (a one-block threshold)
$t = New-Gft -Id "${NS}no_bridge" -Size 1, 3, 1 -Out 'lich/root/no_bridge'
Set-GftBlock $t 0 0 0 'minecraft:stone_bricks'
Set-GftBlock $t 0 1 0 'minecraft:air'
Set-GftBlock $t 0 2 0 'minecraft:air'
Add-GftJigsaw $t 0 1 0 -Front north -Name 'gf:lich_tower/bridge'
Add-GftJigsaw $t 0 1 0 -Front south -Name 'gf:lich_tower/bridge_end' -Target 'gf:lich_tower/room'
Save-Gft $t

# the covers of a door that leads nowhere
foreach ($cover in @(@('wall_bars', 'minecraft:iron_bars'), @('wall_cobble', 'minecraft:cobblestone_wall'))) {
    $t = New-Gft -Id "$NS$($cover[0])" -Size 3, 3, 1 -Out "lich/root/$($cover[0])"
    Set-GftBox $t 0 0 0 2 0 0 'minecraft:stone_bricks'
    Set-GftBox $t 0 1 0 2 2 0 $cover[1]
    Set-GftBlock $t 0 1 0 'minecraft:stone_bricks'; Set-GftBlock $t 2 1 0 'minecraft:stone_bricks'
    Add-GftJigsaw $t 1 1 0 -Front north -Name 'gf:lich_tower/bridge'
    Save-Gft $t
}
