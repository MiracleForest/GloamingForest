# U22 (lichdecor): the lich tower decor as the player sees it. Built with commands in rows on the flat
# world and photographed from the north (the camera looks south: east is on the left):
#   out/lichdecor/lichdecor-candelabra.png     standing and wall candelabras with 0..3 candles, unlit,
#                                              normal, dim and ominous (wall ones against stone)
#   out/lichdecor/lichdecor-skull-candles.png  the six skull candles (floor, 1..4 candles, each
#                                              lighting) and their wall forms, turned through 0..15
#   out/lichdecor/lichdecor-ominous-candles.png the 17 ominous candles with 1..4 candles
#   out/lichdecor/lichdecor-misc.png           brazier (off .. full), skull chest (each facing and the
#                                              obsidian / stone / basalt casket forms), sinister spawner,
#                                              ominous fire, wrought iron fences (post, capped, panels),
#                                              terrorcotta arcs / curves / lines, coronation carpet,
#                                              chiseled canopy bookshelf (empty, full, spawner)
#   out/lichdecor/lichdecor-flames-*.png       close ups: dim and ominous flame particles (candelabras,
#                                              skull candles, ominous candles), the sinister spawner's
#                                              particles, ominous fire
#   out/lichdecor/lichdecor-painting-<v>.png   a magic painting of each variant (castaway_paradise,
#                                              darkness, lucid_lands, music_in_the_mire,
#                                              the_hostile_paradise)
#   out/lichdecor/lichdecor-items-hotbar.png   the items: wrought iron bar, magic painting, the six skull
#                                              candles, candelabra, brazier, skull chest, fence
#   out/lichdecor/lichdecor-tower-*.png        a generated lich tower with U22's rooms (wings, roofs,
#                                              beards, interiors)
# Compare them with upstream's textures, models and renderers (CandelabraRenderer, SkullCandleRenderer,
# OminousCandleRenderer, BrazierRenderer, SkullChestRenderer, SinisterSpawnerRenderer,
# MagicPaintingRenderer). Local mode also runs the lichdecor selftests on the integrated server;
# remote modes build through the host console.

function Invoke-Build([string]$command) {
    if ($script:Mode -eq 'local') { Invoke-Chat "/$command" } else { Send-HostConsole $command }
}

if ($script:Mode -eq 'local') {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'lichdecor.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) lichdecor selftest(s) failed" }
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
}
Invoke-Build 'gamerule sendcommandfeedback false'
Invoke-Build 'gamerule dodaylightcycle false'
Invoke-Build 'gamerule domobspawning false'
Invoke-Build 'time set 18000' # night: the lights show

$y = -60 # the flat world's first air layer
$z = 46
$upsrc = 'gloamingforest'

Invoke-Chat '/clear @s'
foreach ($it in 'wrought_iron_bar', 'magic_painting', 'zombie_skull_candle', 'skeleton_skull_candle', 'wither_skeleton_skull_candle', 'creeper_skull_candle', 'player_skull_candle', 'piglin_skull_candle', 'candelabra') {
    Invoke-Chat "/give @s ${upsrc}:$it"
}
Start-Sleep -Seconds 2
Save-Screenshot 'lichdecor-items-hotbar' | Out-Null
Invoke-Chat '/clear @s'
foreach ($it in 'brazier', 'skull_chest', 'wrought_iron_fence', 'sinister_spawner', 'chiseled_canopy_bookshelf', 'coronation_carpet', 'terrorcotta_arcs', 'terrorcotta_curves', 'terrorcotta_lines') {
    Invoke-Chat "/give @s ${upsrc}:$it"
}
Start-Sleep -Seconds 2
Save-Screenshot 'lichdecor-items-hotbar-2' | Out-Null

function S([hashtable]$states) {
    if (-not $states -or $states.Count -eq 0) { return '' }
    $parts = foreach ($k in ($states.Keys | Sort-Object)) {
        $v = $states[$k]
        if ($v -is [bool]) { "`"${upsrc}:$k`"=$($v.ToString().ToLower())" }
        elseif ($v -is [int]) { "`"${upsrc}:$k`"=$v" }
        else { "`"${upsrc}:$k`"=`"$v`"" }
    }
    ' [' + ($parts -join ',') + ']'
}

$lightings = 'none', 'normal', 'dim', 'ominous'
$rows = @()

# candelabras: x = candles (0..3), z rows = lighting; a wall row hangs on stone
$b = @()
for ($l = 0; $l -lt 4; $l++) {
    for ($c = 0; $c -le 3; $c++) {
        $st = @{ facing = 'south'; on_wall = $false; lighting = $lightings[$l]; has_candle_1 = ($c -ge 1); has_candle_2 = ($c -ge 2); has_candle_3 = ($c -ge 3) }
        $b += , @((2 * $c + 9 * $l), 0, 0, "${upsrc}:candelabra", (S $st))
        $b += , @((2 * $c + 9 * $l), 1, 1, 'minecraft:stone', '')
        $st.on_wall = $true; $st.facing = 'south'
        $b += , @((2 * $c + 9 * $l), 1, 0, "${upsrc}:candelabra", (S $st))
    }
}
$rows += @{ name = 'lichdecor-candelabra'; x = 300; blocks = $b; width = 36 }

# skull candles: per type a column of floor (1..4 candles, each lighting) and wall forms
$b = @()
$types = 'zombie', 'skeleton', 'wither_skeleton', 'creeper', 'player', 'piglin'
for ($t = 0; $t -lt $types.Count; $t++) {
    for ($c = 1; $c -le 4; $c++) {
        $b += , @((6 * $t + $c - 1), 0, 0, "${upsrc}:$($types[$t])_skull_candle", (S @{ candles = $c; lighting = $lightings[$c - 1]; rotation = (4 * ($c - 1)) }))
    }
    $b += , @((6 * $t), 1, 3, 'minecraft:stone', '')
    $b += , @((6 * $t), 1, 2, "${upsrc}:$($types[$t])_wall_skull_candle", (S @{ candles = 2; lighting = 'normal'; facing = 'north' }))
    $b += , @((6 * $t + 2), 0, 3, "${upsrc}:$($types[$t])_skull_candle", (S @{ candles = 1; lighting = 'none'; rotation = 2 }))
}
$rows += @{ name = 'lichdecor-skull-candles'; x = 360; blocks = $b; width = 36 }

# ominous candles: the 17 colours with 1..4 candles
$b = @()
$colors = '', 'white_', 'orange_', 'magenta_', 'light_blue_', 'yellow_', 'lime_', 'pink_', 'gray_', 'light_gray_', 'cyan_', 'purple_', 'blue_', 'brown_', 'green_', 'red_', 'black_'
for ($i = 0; $i -lt $colors.Count; $i++) {
    $b += , @((2 * $i), 0, 0, "${upsrc}:ominous_$($colors[$i])candle", (S @{ candles = (($i % 4) + 1) }))
}
$rows += @{ name = 'lichdecor-ominous-candles'; x = 420; blocks = $b; width = 34 }

# the rest
$b = @()
$lights = 'off', 'dim', 'half', 'bright', 'full'
for ($i = 0; $i -lt 5; $i++) {
    $b += , @((2 * $i), 0, 0, "${upsrc}:brazier", (S @{ half = 'lower'; light = $lights[$i] }))
    $b += , @((2 * $i), 1, 0, "${upsrc}:brazier", (S @{ half = 'upper'; light = $lights[$i] }))
}
$i = 0
foreach ($f in 'north', 'east', 'south', 'west') { $b += , @((11 + 2 * $i), 0, 0, "${upsrc}:skull_chest", (S @{ facing = $f; multilogged = 'air' })); $i++ }
$i = 0
foreach ($m in 'obsidian', 'stone', 'basalt', 'water') { $b += , @((11 + 2 * $i), 0, 3, "${upsrc}:skull_chest", (S @{ facing = 'north'; multilogged = $m })); $i++ }
$b += , @(20, 0, 0, "${upsrc}:sinister_spawner", '')
$b += , @(22, -1, 0, 'minecraft:stone', '')
$b += , @(22, 0, 0, "${upsrc}:ominous_fire", '')
$b += , @(24, 0, 0, "${upsrc}:wrought_iron_fence", (S @{ post = 'post' }))
$b += , @(25, 0, 0, "${upsrc}:wrought_iron_fence", (S @{ post = 'capped'; east_fence = 'full' }))
$b += , @(26, 0, 0, "${upsrc}:wrought_iron_fence", (S @{ post = 'none'; east_fence = 'bottom'; west_fence = 'bottom' }))
$b += , @(26, 1, 0, "${upsrc}:wrought_iron_fence", (S @{ post = 'none'; east_fence = 'top'; west_fence = 'top' }))
$b += , @(27, 0, 0, "${upsrc}:wrought_iron_fence", (S @{ post = 'post'; west_fence = 'full' }))
$b += , @(29, 0, 0, "${upsrc}:terrorcotta_arcs", (S @{ axis = 'y' }))
$b += , @(30, 0, 0, "${upsrc}:terrorcotta_arcs", (S @{ axis = 'x' }))
$b += , @(31, 0, 0, "${upsrc}:terrorcotta_curves", (S @{ facing = 'north' }))
$b += , @(32, 0, 0, "${upsrc}:terrorcotta_curves", (S @{ facing = 'east' }))
$b += , @(33, 0, 0, "${upsrc}:terrorcotta_lines", '')
$b += , @(34, 0, 0, "${upsrc}:terrorcotta_lines", (S @{ rotated = $true }))
for ($k = 0; $k -lt 3; $k++) { $b += , @((29 + $k), 0, 2, "${upsrc}:coronation_carpet", '') }
$b += , @(36, 0, 0, "${upsrc}:chiseled_canopy_bookshelf", (S @{ facing = 'north' }))
$b += , @(37, 0, 0, "${upsrc}:chiseled_canopy_bookshelf", (S @{ facing = 'north'; slot_0_occupied = $true; slot_1_occupied = $true; slot_2_occupied = $true; slot_3_occupied = $true; slot_4_occupied = $true; slot_5_occupied = $true }))
$b += , @(38, 0, 0, "${upsrc}:chiseled_canopy_bookshelf", (S @{ facing = 'north'; spawner = $true; slot_0_occupied = $true; slot_4_occupied = $true }))
$rows += @{ name = 'lichdecor-misc'; x = 480; blocks = $b; width = 40 }

foreach ($row in $rows) {
    Invoke-Teleport "/tp @s $($row.x + [int]($row.width / 2)) $($y + 2) $($z - 12) 0 15"
    Start-Sleep -Seconds 4
    foreach ($bk in $row.blocks) {
        Invoke-Build "setblock $($row.x + $bk[0]) $($y + $bk[1]) $($z + $bk[2]) $($bk[3])$($bk[4])"
    }
}
Start-Sleep -Seconds 12 # the chat fades
foreach ($row in $rows) {
    Invoke-Teleport "/tp @s $($row.x + [int]($row.width / 2)) $($y + 4) $($z - 20) 0 15"
    Start-Sleep -Seconds 3
    Save-Screenshot $row.name | Out-Null
    for ($part = 0; $part * 14 -lt $row.width; $part++) {
        Invoke-Teleport "/tp @s $($row.x + 7 + 14 * $part) $($y + 2) $($z - 6) 0 15"
        Start-Sleep -Seconds 2
        Save-Screenshot "$($row.name)-$part" | Out-Null
    }
}

# flames close up (dim / ominous flame particles, the sinister spawner and ominous fire)
Invoke-Teleport "/tp @s $(300 + 21) $($y + 1) $($z - 3) 0 15"
Start-Sleep -Seconds 3
Save-Screenshot 'lichdecor-flames-candelabra' | Out-Null
Invoke-Teleport "/tp @s $(360 + 2) $($y + 1) $($z - 3) 0 15"
Start-Sleep -Seconds 3
Save-Screenshot 'lichdecor-flames-skull-candles' | Out-Null
Invoke-Teleport "/tp @s $(420 + 4) $($y + 1) $($z - 3) 0 15"
Start-Sleep -Seconds 3
Save-Screenshot 'lichdecor-flames-ominous-candles' | Out-Null
Invoke-Teleport "/tp @s $(480 + 21) $($y + 1) $($z - 3) 0 15"
Start-Sleep -Seconds 3
Save-Screenshot 'lichdecor-flames-spawner-fire' | Out-Null

# magic paintings: one of each variant on a stone wall (spawn events set the variant property)
$px = 560
Invoke-Build "fill $px $y $($z + 1) $($px + 40) $($y + 6) $($z + 1) minecraft:stone"
$variants = 'castaway_paradise', 'darkness', 'lucid_lands', 'music_in_the_mire', 'the_hostile_paradise'
for ($i = 0; $i -lt $variants.Count; $i++) {
    Invoke-Build "summon ${upsrc}:magic_painting $($px + 4 + 8 * $i) $($y + 2) $($z + 0.97) 180 0 ${upsrc}:variant_$($variants[$i])"
}
Start-Sleep -Seconds 8
for ($i = 0; $i -lt $variants.Count; $i++) {
    Invoke-Teleport "/tp @s $($px + 4.5 + 8 * $i) $($y + 2) $($z - 4) 0 0"
    Start-Sleep -Seconds 3
    Save-Screenshot "lichdecor-painting-$($variants[$i])" | Out-Null
}

# a generated lich tower with U22's rooms
Invoke-Chat '/gloamingforest place gloamingforest:lich_tower 800 -60 900'
Start-Sleep -Seconds 20
Invoke-Teleport '/tp @s 800 -20 860 0 20'
Start-Sleep -Seconds 8
Save-Screenshot 'lichdecor-tower-far' | Out-Null
Invoke-Teleport '/tp @s 830 -30 900 90 10'
Start-Sleep -Seconds 6
Save-Screenshot 'lichdecor-tower-wings' | Out-Null
Invoke-Teleport '/tp @s 770 -30 900 -90 10'
Start-Sleep -Seconds 6
Save-Screenshot 'lichdecor-tower-wings-2' | Out-Null
