# U21 (lich): the spawner bridges that cross the central tower's shaft (original designs, rule R19).
# Each runs along +x across the shaft. The junction is a vertical one in the middle of the deck
# (front up, top east) that meets the slice's mob bridge jigsaw above it (front down); TF's
# LichTowerSegment keeps or spins that jigsaw's top by 180, which turns the bridge end for end.
. (Join-Path $PSScriptRoot '../LichLib.ps1')
$NS = 'twilightforest:lich_tower/'
$L  = 10

function New-LichMobBridge([string]$id, [scriptblock]$shape) {
    $t = New-Gft -Id "$NS$id" -Size $L, 3, 3 -Out "lich/root/$id"
    & $shape $t
    Add-GftJigsaw $t 5 0 1 -Front up -Top east -Name 'gf:mob_bridge'
    Save-Gft $t
}

New-LichMobBridge 'bridge_spawner' {
    param($t)
    Set-GftBox $t 0 0 1 ($L - 1) 0 1 'twilightforest:twilight_oak_planks'
    Set-GftBox $t 0 1 0 ($L - 1) 1 0 'twilightforest:canopy_fence'
    Set-GftBox $t 0 1 2 ($L - 1) 1 2 'twilightforest:canopy_fence'
    Set-GftBlock $t 5 1 1 'minecraft:spawner'
}
New-LichMobBridge 'bridge_spawner_bend' {
    param($t)
    Set-GftBox $t 0 0 1 4 0 1 'twilightforest:twilight_oak_planks'
    Set-GftBox $t 4 0 0 5 0 2 'twilightforest:twilight_oak_planks'
    Set-GftBox $t 5 0 1 ($L - 1) 0 1 'twilightforest:twilight_oak_planks'
    Set-GftBlock $t 4 1 2 'minecraft:spawner'
    Set-GftBlock $t 5 1 0 'twilightforest:canopy_fence'
}
New-LichMobBridge 'bridge_spawner_ropes' {
    param($t)
    Set-GftBox $t 0 0 1 ($L - 1) 0 1 'twilightforest:twilight_oak_slab' @{ type = 'top' }
    foreach ($x in 0, 3, 6, 9) { Set-GftBox $t $x 1 1 $x 2 1 'twilightforest:rope' }
    Set-GftBlock $t 5 1 1 'minecraft:spawner'
}
New-LichMobBridge 'bridge_spawner_wide' {
    param($t)
    Set-GftBox $t 0 0 0 ($L - 1) 0 2 'twilightforest:canopy_planks'
    Set-GftBox $t 0 0 1 ($L - 1) 0 1 'twilightforest:twilight_oak_planks'
    Set-GftBlock $t 3 1 0 'minecraft:spawner'
    Set-GftBlock $t 6 1 2 'minecraft:spawner'
}
New-LichMobBridge 'bridge_spawner_zag' {
    param($t)
    for ($x = 0; $x -lt $L; $x++) { $z = if (($x -shr 1) % 2 -eq 0) { 1 } else { 2 }; Set-GftBlock $t $x 0 $z 'twilightforest:twilight_oak_planks' }
    Set-GftBox $t 0 0 1 0 0 1 'twilightforest:twilight_oak_planks'
    Set-GftBox $t ($L - 1) 0 1 ($L - 1) 0 1 'twilightforest:twilight_oak_planks'
    Set-GftBlock $t 5 1 1 'minecraft:spawner'
}
New-LichMobBridge 'bridge_spawner_zig' {
    param($t)
    for ($x = 0; $x -lt $L; $x++) { $z = if (($x -shr 1) % 2 -eq 0) { 1 } else { 0 }; Set-GftBlock $t $x 0 $z 'twilightforest:canopy_planks' }
    Set-GftBox $t 0 0 1 0 0 1 'twilightforest:canopy_planks'
    Set-GftBox $t ($L - 1) 0 1 ($L - 1) 0 1 'twilightforest:canopy_planks'
    Set-GftBlock $t 4 1 1 'minecraft:spawner'
}
