# U21 (lich): placeholder rooms of the lich tower wings. The room templates (lich_tower/3x3 .. 9x9,
# their roofs, beards and decor) are U22's; while one is missing the wing ends in a plain room of the
# same size from here (twilightforest:lich_tower/placeholder/<size>), so the tower still assembles.
. (Join-Path $PSScriptRoot '../LichLib.ps1')
$NS = 'twilightforest:lich_tower/'

foreach ($n in 3, 5, 7, 9) {
    $w = $n + 2; $h = $n + 2; $c = [int]($w / 2) - ($(if ($w % 2 -eq 0) { 1 } else { 0 }))
    $c = [int][Math]::Floor($w / 2)
    $t = New-Gft -Id "gf:lich_tower/placeholder/${n}x${n}" -Size $w, $h, $w -Out "lich/root/placeholder_${n}x${n}"
    Set-GftBox $t 0 0 0 ($w - 1) 0 ($w - 1) 'minecraft:stone_bricks'
    Set-GftBox $t 0 1 0 ($w - 1) ($h - 2) ($w - 1) 'minecraft:stone_bricks' -Hollow
    Set-GftBox $t 1 1 1 ($w - 2) ($h - 2) ($w - 2) 'minecraft:air'
    Set-GftBox $t 0 ($h - 1) 0 ($w - 1) ($h - 1) ($w - 1) 'minecraft:stone_brick_slab' @{ type = 'bottom' }
    Set-GftBox $t 1 ($h - 1) 1 ($w - 2) ($h - 1) ($w - 2) 'minecraft:stone_bricks'
    foreach ($x in 0, ($w - 1)) { Set-GftBox $t $x 1 0 $x ($h - 2) 0 'minecraft:chiseled_stone_bricks'; Set-GftBox $t $x 1 ($w - 1) $x ($h - 2) ($w - 1) 'minecraft:chiseled_stone_bricks' }
    Set-GftBlock $t $c 2 ($w - 1) 'minecraft:iron_bars'
    Set-GftBlock $t 0 2 $c 'minecraft:iron_bars'
    Set-GftBlock $t ($w - 1) 2 $c 'minecraft:iron_bars'
    Set-LichDoor $t $c 1 0
    Add-GftJigsaw $t $c 1 0 -Front north -Name 'gf:lich_tower/room'
    Save-Gft $t
}
