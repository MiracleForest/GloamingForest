# U22 (lichdecor): the 3x3 rooms of the lich tower wings (original designs, rule R11/R19): the
# smallest rooms, dead ends with no bridges of their own (LichTowerWingRoom ignores bridge junctions
# on size 0).
. (Join-Path $PSScriptRoot '../RoomLib.ps1')

$ladderN = @{ facing = 'north' }

# double: two storeys joined by a ladder in the back corner
$t = New-LichRoom -Name 'double' -N 3 -Height 9
Set-GftBox $t 1 4 1 3 4 3 'gloamingforest:canopy_planks'
Set-GftBlock $t 3 4 3 'minecraft:air'
for ($y = 1; $y -le 5; $y++) { Set-GftBlock $t 3 $y 3 'minecraft:ladder' $ladderN }
Add-RoomMarker $t 1 1 3 'candle:1-2'
Add-RoomMarker $t 1 5 3 'chest:room@east'
Add-RoomMarker $t 1 5 1 'skeleton_candle:1-2'
foreach ($s in 2) { Set-GftBlock $t $s 6 4 'minecraft:iron_bars'; Set-GftBlock $t 0 6 $s 'minecraft:iron_bars'; Set-GftBlock $t 4 6 $s 'minecraft:iron_bars' }
Save-Gft $t

# taller double: three levels, a study at the top
$t = New-LichRoom -Name 'taller_double' -N 3 -Height 11
foreach ($y in 4, 7) {
    Set-GftBox $t 1 $y 1 3 $y 3 'gloamingforest:gloaming_oak_planks'
    Set-GftBlock $t 3 $y 3 'minecraft:air'
}
for ($y = 1; $y -le 8; $y++) { Set-GftBlock $t 3 $y 3 'minecraft:ladder' $ladderN }
Add-RoomMarker $t 1 1 3 'bookshelf|canopy_shelf'
Add-RoomMarker $t 1 5 3 'mason_jar:jar'
Add-RoomMarker $t 1 8 3 'chest:library@east'
Add-RoomMarker $t 1 8 1 'candle:1-3'
Add-RoomMarker $t 2 8 1 'bookshelf|canopy_shelf'
foreach ($y in 5, 8) { Set-GftBlock $t 2 $y 4 'minecraft:iron_bars'; Set-GftBlock $t 0 $y 2 'minecraft:iron_bars'; Set-GftBlock $t 4 $y 2 'minecraft:iron_bars' }
Save-Gft $t

# lookout: tall windows on three sides, a lone lectern to read by
$t = New-LichRoom -Name 'lookout' -N 3 -Height 6 -NoWindows
foreach ($i in 1..3) {
    foreach ($y in 2, 3) {
        Set-GftBlock $t $i $y 4 'minecraft:glass_pane'
        Set-GftBlock $t 0 $y $i 'minecraft:glass_pane'
        Set-GftBlock $t 4 $y $i 'minecraft:glass_pane'
    }
}
Add-RoomMarker $t 2 1 3 'empty_lectern@north'
Add-RoomMarker $t 1 1 3 'candle:1-3'
Add-RoomMarker $t 3 4 1 'rope50%1>candle:1'
Save-Gft $t

# short lookout: a low watch post with arrow slits and a skull on the sill
$t = New-LichRoom -Name 'short_lookout' -N 3 -Height 5 -NoWindows
foreach ($i in 1, 3) { Set-GftBlock $t $i 2 4 'minecraft:iron_bars'; Set-GftBlock $t 0 2 $i 'minecraft:iron_bars'; Set-GftBlock $t 4 2 $i 'minecraft:iron_bars' }
Set-GftBlock $t 2 1 3 'minecraft:stone_brick_slab' @{ type = 'bottom' }
Add-RoomMarker $t 1 1 3 'skeleton_skull'
Add-RoomMarker $t 3 1 3 'candle:1-2'
Save-Gft $t
