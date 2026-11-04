# Families (F11): every block family as the player sees it. The gf_test fixture families (one block
# per family, vanilla textures; states as docs/spec/blockstates.md: Java's names and values) are built
# in four rows and photographed from the north (the camera looks south: east is on the left):
#   out/families/families-1-shapes.png      stairs (straight, an outer corner), slabs, fences (a pair
#                                           joined only to each other, one joined only to a stone on
#                                           its east), fence gate, walls, panes
#   out/families/families-2-mechanisms.png  doors (closed, open), trapdoors, buttons, pressure plate,
#                                           logs, leaves, sapling, potted plant
#   out/families/families-3-containers.png  signs (rotations 0 / 2 / 6, wall, hanging, wall hanging
#                                           between two stones), chests (single, a double with the
#                                           latches meeting in the middle, an open double), trapped chest,
#                                           a chest with its lid half open (lid 2: 45 degrees)
#   out/families/families-4-variants.png    horizontal, directional, binary rotated, wall and
#                                           connectable pillars, connected textures, lightable,
#                                           climbable, snowloggable (3 layers)
#   out/families/families-5-close.png       the signs and chests close up
#   out/families/families-6-sign-text.png   a sign written through the editor (client mod: the native
#                                           editor screen; the text drawn flat on the board as Java's
#                                           SignRenderer, no name tag)
#   out/families/families-7-sign-text-hud.png  the same with the HUD on (a hidden HUD hides name tags:
#                                           the client mod's board text stays either way)
#   out/families/families-8-snowloggable.png   the snowloggable close up (its 3 snow layers)
#   out/families/families-9-sign-editor.png    the native sign editor (client mod: Java's
#                                           SignEditScreen, the sign drawn with the line on it)
#   out/families/families-10-tf-woodwork.png   TF's own blocks through the kit: canopy door, sign,
#                                           wall sign, hanging and wall hanging sign, chest, trapped
#                                           chest double, trapdoor, button (TF's textures)
#   out/families/families-11-tf-plants.png  a grown blackberry bush with 2 snow layers, a young
#                                           blueberry bush with 1, TF's leaves (time, mining, sorting,
#                                           transformation, rainbow oak pre-tinted with Java's colour
#                                           without a world; twilight oak by the biome; canopy and
#                                           mangrove: the client mod blends the biome foliage as
#                                           ColorHandler, a vanilla client tints evergreen / birch),
#                                           castle doors (connected textures), a time log
#   out/families/families-12-tf-close.png   TF's signs and chests close up
#   out/families/families-13-tf-bush.png    the snowy berry bush close up (client mod: the grass
#                                           below drawn snowy)
#   out/families/families-15-ct-close.png   the six-face connected texture fixture close up (client
#                                           mod: the corner piece at the L's inner corner)
#   out/families/families-14-sign-form.png  remote-vanilla: the sign editor form (its title is the
#                                           game's own sign.edit text)
#   out/families/families-25-tf-chest-hopper.png   TF's canopy double chest with a hopper on its left
#                                           half that emptied itself into the chest
#   out/families/families-26-tf-chest-hopper-screen.png   that chest right-clicked: the client mod's
#                                           54-slot chest screen (local, remote-mod) or the game's large
#                                           chest screen (remote-vanilla) with the hopper's 3 gold
#                                           ingots and 2 diamonds in the right half's first slots
#   out/families/families-27-tf-chest-hopper-live-a.png  the same screen opened again while the hopper
#                                           on the left half feeds it 64 cobblestone (one per 8 ticks)
#   out/families/families-28-tf-chest-hopper-live-b.png  6 seconds later, the screen still open: more
#                                           cobblestone in the right half's third slot (Java's ChestMenu
#                                           broadcastChanges: the client mod's screen is refreshed after a
#                                           hopper's tick, the vanilla screen follows F10's proxy)
#   out/families/families-16-tf-sign-text.png        client mod: TF's canopy signs written through the
#                                           editor and coloured with items, from the north: the wall
#                                           sign's front (4 black lines), the standing sign's back
#                                           (blue, glowing: the dark blue outline), the hanging sign
#                                           (glowing black: the cream outline), the wall hanging sign
#                                           (red dye: dark red); Java's layout and scale per kind
#   out/families/families-17-tf-sign-text-close.png  the same close up
#   out/families/families-18-tf-sign-text-far.png    the same from 20 blocks: the blue glowing text
#                                           has no outline there (isOutlineVisible: 16 blocks), the
#                                           black one keeps it
#   out/families/families-19-sign-written.png       the sign families.sign_a_write wrote (server
#                                           side): client mod on the board, vanilla client as the
#                                           name tag of the side facing it (the front; the back's
#                                           "back side" is hidden for this viewer)
#   out/families/families-20-compare-black.png ... families-23-compare-glow-blue.png   client mod:
#                                           TF's canopy signs (right) next to the game's own oak
#                                           signs (left) in the same light, written and coloured
#                                           the same way: black text, red dye, glow ink on black
#                                           (hanging signs), blue dye and glow ink (standing signs)
#   out/families/families-24-glow-far-40.png, families-24-glow-far-60.png   client mod: the glow ink
#                                           hanging signs of picture 22 through a spyglass from 40 and
#                                           60 blocks: black letters over the cream outline, no
#                                           z-fighting (Java's POLYGON_OFFSET)
# families.classic runs this scenario in the Classic look (graphics_mode 1): Vibrant Visuals lights
# sign text (the game's own too), Classic shows Java's colours as they are.
# Local mode also runs the families selftests on the integrated server; remote modes build through
# the host console (the vanilla client sees the downloaded resource pack).

function Invoke-Build([string]$command) {
    if ($script:Mode -eq 'local') { Invoke-Chat "/$command" } else { Send-HostConsole $command }
}

if ($script:Mode -eq 'local') {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $mark = Get-LogMark
    $r = Invoke-SelfTest 'families.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) families selftest(s) failed" }
    if (Get-LeviLog -Since $mark | Select-String -Pattern 'fixture families missing|no gf_test packs' -Quiet) { throw 'the fixture families were not generated (tools/families)' }
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
    $mark = Get-LogMark
    $r = Invoke-SelfTest 'families.fixtures'
    if ($r.Fail -gt 0) { throw 'families.fixtures failed on the host' }
    if (Get-LeviLog -Since $mark | Select-String -Pattern 'fixture families missing|no gf_test packs' -Quiet) {
        throw 'the host has no fixture families: run pwsh tools/families/run.ps1 after the build (it updates bin/server)'
    }
}
# no command feedback in the chat over the screenshots
Invoke-Build 'gamerule sendcommandfeedback false'

$y  = -60 # the flat world's first air layer
$z  = 46
$f  = 'twilightforest:facing'
$rows = @(
    @{ name = 'families-1-shapes'; x = 0; blocks = @(
            @(0, 0, 0, 'gf_test:family_stairs', "[`"$f`"=`"north`",`"twilightforest:half`"=`"bottom`"]"),
            @(1, 0, 0, 'gf_test:family_stairs', "[`"$f`"=`"east`",`"twilightforest:half`"=`"top`"]"),
            @(2, 0, 1, 'gf_test:family_stairs', "[`"$f`"=`"west`",`"twilightforest:half`"=`"bottom`"]"),
            @(2, 0, 2, 'gf_test:family_stairs', "[`"$f`"=`"north`",`"twilightforest:half`"=`"bottom`"]"),
            @(4, 0, 0, 'gf_test:family_slab', '["twilightforest:type"="bottom"]'),
            @(5, 0, 0, 'gf_test:family_slab', '["twilightforest:type"="top"]'),
            @(6, 0, 0, 'gf_test:family_slab', '["twilightforest:type"="double"]'),
            @(8, 0, 0, 'gf_test:family_fence', ''),
            @(9, 0, 0, 'gf_test:family_fence', ''),
            @(11, 0, 0, 'gf_test:family_fence', ''),
            @(12, 0, 0, 'minecraft:stone', ''),
            @(14, 0, 0, 'gf_test:family_fence_gate', "[`"$f`"=`"north`"]"),
            @(16, 0, 0, 'gf_test:family_wall', ''),
            @(17, 0, 0, 'gf_test:family_wall', ''),
            @(17, 0, 1, 'gf_test:family_wall', ''),
            @(19, 0, 0, 'gf_test:family_pane', ''),
            @(20, 0, 0, 'gf_test:family_pane', ''))
    },
    @{ name = 'families-2-mechanisms'; x = 30; blocks = @(
            @(0, 0, 0, 'gf_test:family_door', "[`"$f`"=`"north`",`"twilightforest:half`"=`"lower`"]"),
            @(0, 1, 0, 'gf_test:family_door', "[`"$f`"=`"north`",`"twilightforest:half`"=`"upper`"]"),
            @(2, 0, 0, 'gf_test:family_door', "[`"$f`"=`"north`",`"twilightforest:half`"=`"lower`",`"twilightforest:open`"=true,`"twilightforest:hinge`"=`"right`"]"),
            @(2, 1, 0, 'gf_test:family_door', "[`"$f`"=`"north`",`"twilightforest:half`"=`"upper`",`"twilightforest:open`"=true,`"twilightforest:hinge`"=`"right`"]"),
            @(4, 0, 0, 'gf_test:family_trapdoor', '["twilightforest:half"="bottom"]'),
            @(5, 0, 0, 'gf_test:family_trapdoor', "[`"$f`"=`"north`",`"twilightforest:open`"=true]"),
            @(7, 0, 0, 'minecraft:stone', ''),
            @(7, 1, 0, 'gf_test:family_button', '["twilightforest:face"="floor"]'),
            @(7, 0, -1, 'gf_test:family_button', "[`"twilightforest:face`"=`"wall`",`"$f`"=`"north`",`"twilightforest:powered`"=true]"),
            @(8, 0, 0, 'gf_test:family_pressure_plate', ''),
            @(10, 0, 0, 'gf_test:family_log', '["twilightforest:axis"="y"]'),
            @(11, 0, 0, 'gf_test:family_log', '["twilightforest:axis"="x"]'),
            @(12, 0, 0, 'gf_test:family_stripped_log', '["twilightforest:axis"="y"]'),
            @(13, 0, 0, 'gf_test:family_wood', ''),
            @(14, 0, 0, 'gf_test:family_leaves', '["twilightforest:persistent"=true]'),
            @(15, 0, 0, 'gf_test:family_sapling', ''),
            @(16, 0, 0, 'gf_test:family_potted', ''))
    },
    @{ name = 'families-3-containers'; x = 60; blocks = @(
            @(0, 0, 0, 'gf_test:family_sign', '["twilightforest:rotation"=0]'),
            @(1, 0, 0, 'gf_test:family_sign', '["twilightforest:rotation"=2]'),
            @(2, 0, 0, 'gf_test:family_sign', '["twilightforest:rotation"=6]'),
            @(4, 0, 1, 'minecraft:stone', ''),
            @(4, 0, 0, 'gf_test:family_wall_sign', "[`"$f`"=`"north`"]"),
            @(6, 2, 0, 'minecraft:stone', ''),
            @(6, 1, 0, 'gf_test:family_hanging_sign', '["twilightforest:rotation"=8]'),
            @(8, 1, 0, 'minecraft:stone', ''),
            @(10, 1, 0, 'minecraft:stone', ''),
            @(9, 1, 0, 'gf_test:family_wall_hanging_sign', "[`"$f`"=`"north`"]"),
            @(12, 0, 0, 'gf_test:family_chest', "[`"$f`"=`"north`"]"),
            @(14, 0, 0, 'gf_test:family_chest', "[`"$f`"=`"north`",`"twilightforest:type`"=`"left`"]"),
            @(15, 0, 0, 'gf_test:family_chest', "[`"$f`"=`"north`",`"twilightforest:type`"=`"right`"]"),
            @(17, 0, 0, 'gf_test:family_chest', "[`"$f`"=`"north`",`"twilightforest:type`"=`"left`",`"gloamingforest:lid`"=4]"),
            @(18, 0, 0, 'gf_test:family_chest', "[`"$f`"=`"north`",`"twilightforest:type`"=`"right`",`"gloamingforest:lid`"=4]"),
            @(20, 0, 0, 'gf_test:family_trapped_chest', "[`"$f`"=`"north`"]"),
            @(13, 0, -2, 'gf_test:family_chest', "[`"$f`"=`"north`",`"gloamingforest:lid`"=2]"))
    },
    @{ name = 'families-4-variants'; x = 90; blocks = @(
            @(0, 0, 0, 'gf_test:family_horizontal', "[`"$f`"=`"north`"]"),
            @(1, 0, 0, 'gf_test:family_directional', "[`"$f`"=`"north`"]"),
            @(2, 0, 0, 'gf_test:family_binary', '["twilightforest:rotated"=false]'),
            @(3, 0, 0, 'gf_test:family_binary', '["twilightforest:rotated"=true]'),
            @(5, 0, 0, 'gf_test:family_wall_pillar', '["twilightforest:axis"="y","twilightforest:up"=true]'),
            @(5, 1, 0, 'gf_test:family_wall_pillar', '["twilightforest:axis"="y","twilightforest:down"=true]'),
            @(7, 0, 0, 'gf_test:family_connectable_pillar', '["twilightforest:axis"="y","twilightforest:up"=true]'),
            @(7, 1, 0, 'gf_test:family_connectable_pillar', '["twilightforest:axis"="y","twilightforest:down"=true]'),
            @(9, 0, 0, 'gf_test:family_ct', ''),
            @(10, 0, 0, 'gf_test:family_ct', ''),
            @(9, 1, 0, 'gf_test:family_ct', ''),
            @(12, 0, 0, 'gf_test:family_ct_flat', ''),
            @(13, 0, 0, 'gf_test:family_ct_flat', ''),
            @(12, 1, 0, 'gf_test:family_ct_flat', ''),
            @(15, 0, 0, 'gf_test:family_lightable', '["twilightforest:lighting"="normal"]'),
            @(17, 0, 0, 'gf_test:family_climbable', ''),
            @(20, 0, 0, 'gf_test:family_snowloggable', '["twilightforest:layers"=3]'))
    },
    # TF's own blocks through the kit (F1's skeleton and textures): canopy door, sign, wall sign,
    # hanging sign, wall hanging sign, chest, trapped chest double
    @{ name = 'families-10-tf-woodwork'; x = 120; blocks = @(
            @(0, 0, 0, 'twilightforest:canopy_door', "[`"$f`"=`"north`",`"twilightforest:half`"=`"lower`"]"),
            @(0, 1, 0, 'twilightforest:canopy_door', "[`"$f`"=`"north`",`"twilightforest:half`"=`"upper`"]"),
            @(2, 0, 0, 'twilightforest:canopy_sign', '["twilightforest:rotation"=0]'),
            @(4, 0, 1, 'minecraft:stone', ''),
            @(4, 0, 0, 'twilightforest:canopy_wall_sign', "[`"$f`"=`"north`"]"),
            @(6, 2, 0, 'minecraft:stone', ''),
            @(6, 1, 0, 'twilightforest:canopy_hanging_sign', '["twilightforest:rotation"=8]'),
            @(8, 1, 0, 'minecraft:stone', ''),
            @(10, 1, 0, 'minecraft:stone', ''),
            @(9, 1, 0, 'twilightforest:canopy_wall_hanging_sign', "[`"$f`"=`"north`"]"),
            @(12, 0, 0, 'twilightforest:canopy_chest', "[`"$f`"=`"north`"]"),
            @(14, 0, 0, 'twilightforest:canopy_trapped_chest', "[`"$f`"=`"north`",`"twilightforest:type`"=`"left`"]"),
            @(15, 0, 0, 'twilightforest:canopy_trapped_chest', "[`"$f`"=`"north`",`"twilightforest:type`"=`"right`"]"),
            @(17, 0, 0, 'twilightforest:canopy_trapdoor', "[`"$f`"=`"north`",`"twilightforest:half`"=`"bottom`"]"),
            @(19, 0, 0, 'twilightforest:canopy_planks', ''),
            @(19, 1, 0, 'twilightforest:canopy_button', "[`"twilightforest:face`"=`"floor`",`"$f`"=`"north`"]"))
    },
    # a snowy grown berry bush (TF's shape and snow), TF's leaves (the time, mining and rainbow oak
    # leaves pre-tinted with Java's colour without a world, twilight oak by the biome), castle
    # doors (six-face connected textures)
    @{ name = 'families-11-tf-plants'; x = 150; blocks = @(
            @(0, 0, 0, 'twilightforest:blackberry_bush', '["twilightforest:age"=3,"twilightforest:layers"=2]'),
            @(2, 0, 0, 'twilightforest:blueberry_bush', '["twilightforest:age"=0,"twilightforest:layers"=1]'),
            @(4, 0, 0, 'twilightforest:time_leaves', '["twilightforest:persistent"=true]'),
            @(5, 0, 0, 'twilightforest:mining_leaves', '["twilightforest:persistent"=true]'),
            @(6, 0, 0, 'twilightforest:sorting_leaves', '["twilightforest:persistent"=true]'),
            @(7, 0, 0, 'twilightforest:transformation_leaves', '["twilightforest:persistent"=true]'),
            @(8, 0, 0, 'twilightforest:rainbow_oak_leaves', '["twilightforest:persistent"=true]'),
            @(9, 0, 0, 'twilightforest:twilight_oak_leaves', '["twilightforest:persistent"=true]'),
            @(10, 0, 0, 'twilightforest:canopy_leaves', '["twilightforest:persistent"=true]'),
            @(11, 0, 0, 'twilightforest:mangrove_leaves', '["twilightforest:persistent"=true]'),
            @(12, 0, 0, 'twilightforest:blue_castle_door', ''),
            @(12, 1, 0, 'twilightforest:blue_castle_door', ''),
            @(13, 0, 0, 'twilightforest:blue_castle_door', ''),
            @(15, 0, 0, 'twilightforest:time_log', '["twilightforest:axis"="y"]'),
            @(15, 1, 0, 'twilightforest:time_leaves', '["twilightforest:persistent"=true]'),
            # grass with its sides in view: under a snowy bush (the client mod draws it snowy) and bare
            @(18, 0, 0, 'minecraft:grass_block', ''),
            @(18, 1, 0, 'twilightforest:blackberry_bush', '["twilightforest:age"=3,"twilightforest:layers"=2]'),
            @(20, 0, 0, 'minecraft:grass_block', ''),
            @(20, 1, 0, 'gf_test:family_snowloggable', '["twilightforest:layers"=3]'),
            @(22, 0, 0, 'minecraft:grass_block', ''))
    }
)

Invoke-Teleport "/tp @s 50 $($y + 1) 30 0 20"
Start-Sleep -Seconds 3
foreach ($row in $rows) {
    # a host places blocks only in loaded chunks: go to each row first
    Invoke-Teleport "/tp @s $($row.x + 10) $($y + 2) $($z - 9) 0 15"
    Start-Sleep -Seconds 4
    foreach ($b in $row.blocks) {
        $x = $row.x + $b[0]
        $states = if ($b[4]) { " $($b[4])" } else { '' }
        Invoke-Build "setblock $x $($y + $b[1]) $($z + $b[2]) $($b[3])$states"
    }
}
Start-Sleep -Seconds 12 # the chat fades
# the lids stand open only while players look inside: a chest whose chunk loads again (the camera
# went far away) closes its lid (Chests.cpp), so the open ones are set again before their pictures
function Set-OpenLids {
    $row = $rows[2]
    foreach ($b in $row.blocks) {
        if ($b[4] -match 'gloamingforest:lid') { Invoke-Build "setblock $($row.x + $b[0]) $($y + $b[1]) $($z + $b[2]) $($b[3]) $($b[4])" }
    }
}
foreach ($row in $rows) {
    $cx = $row.x + 10
    Invoke-Teleport "/tp @s $cx $($y + 2) $($z - 9) 0 15"
    Start-Sleep -Seconds 3
    if ($row.name -eq 'families-3-containers') {
        Set-OpenLids
        Start-Sleep -Seconds 2
    }
    Save-Screenshot $row.name | Out-Null
}
# a close look at the containers (signs and chests)
Invoke-Teleport "/tp @s 69 $($y + 1) $($z - 4) 0 15"
Start-Sleep -Seconds 3
Set-OpenLids
Start-Sleep -Seconds 2
Save-Screenshot 'families-5-close' | Out-Null
# a close look at the snowloggable (3 snow layers under the sapling; it stands 5 blocks from the lit
# lamp: block light 10, so TF's SnowLoggable.meltSnow, which melts above 11, leaves the snow)
Invoke-Teleport "/tp @s 110.5 $($y + 1) $($z - 2.5) 0 40"
Start-Sleep -Seconds 3
Save-Screenshot 'families-8-snowloggable' | Out-Null
# TF's chests and signs close up (their TF textures), and the snowy berry bush on the grass (the
# client mod draws the grass below snowy, TFBushBlock.updateSnowBeneath)
Invoke-Teleport "/tp @s 133 $($y + 1) $($z - 4) 0 15"
Start-Sleep -Seconds 3
Save-Screenshot 'families-12-tf-close' | Out-Null
Invoke-Teleport "/tp @s 170.5 $($y + 2) $($z - 3.5) 0 25"
Start-Sleep -Seconds 3
Save-Screenshot 'families-13-tf-bush' | Out-Null
# the six-face connected texture fixture close up: its packs draw two connected sides as the
# cornerless piece; the client mod draws the corner piece where the diagonal block is missing (the
# inner corner of the L)
Invoke-Teleport "/tp @s 99.5 $($y + 1) $($z - 2.5) 0 10"
Start-Sleep -Seconds 3
Save-Screenshot 'families-15-ct-close' | Out-Null

# TF's canopy double chest fed by a hopper on its LEFT half (Hoppers.cpp: the game's hopper sees the
# chest's block actor container, the double chest as Java's CompoundContainer, the right half first),
# then opened by a right click: the client mod's 54-slot chest screen (local, remote-mod) or the
# game's large chest screen through F10's proxy (remote-vanilla), the hopper's 3 gold ingots and 2
# diamonds in the first slots (the right half's), the left half's rows empty
$hx = 190
Invoke-Teleport "/tp @s $($hx + 1) $($y + 2) $($z - 6) 0 15"
Start-Sleep -Seconds 4
Invoke-Build "setblock $hx $y $z twilightforest:canopy_chest [`"$f`"=`"north`",`"twilightforest:type`"=`"left`"]"
Invoke-Build "setblock $($hx + 1) $y $z twilightforest:canopy_chest [`"$f`"=`"north`",`"twilightforest:type`"=`"right`"]"
Invoke-Build "setblock $hx $($y + 1) $z hopper [`"facing_direction`"=0]"
Invoke-Build "replaceitem block $hx $($y + 1) $z slot.container 0 gold_ingot 3"
Invoke-Build "replaceitem block $hx $($y + 1) $z slot.container 1 diamond 2"
Start-Sleep -Seconds 12 # 5 transfers of 8 ticks; the chat fades
Invoke-Teleport "/tp @s $($hx + 1) $($y + 1) $($z - 4) 0 20"
Start-Sleep -Seconds 3
Save-Screenshot 'families-25-tf-chest-hopper' | Out-Null
# stand south of the chest, looking north at it
Invoke-Teleport "/tp @s $($hx + 0.5) $y $($z + 2.5) 180 30"
Start-Sleep -Seconds 2
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
if (Test-ClientTrace) {
    Wait-LogLine 'client screen: (inventory\.chest|large_chest_screen)' 20 -Since $mark | Out-Null
} else {
    Wait-LogLine 'inventory: \S+ opened the container at .* \(vanilla screen, large chest\)' 20 -Since $mark | Out-Null
}
Start-Sleep -Seconds 2
Save-Screenshot 'families-26-tf-chest-hopper-screen' | Out-Null
Close-Screen
Start-Sleep -Seconds 1
# the hopper on the left half gets 64 cobblestone and the screen opens again at once: it moves one
# item every 8 ticks (25 s for all), so two shots 6 s apart show the stack in the right half's third
# slot growing while the screen stays open
Invoke-Build "replaceitem block $hx $($y + 1) $z slot.container 0 cobblestone 64"
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
if (Test-ClientTrace) {
    Wait-LogLine 'client screen: (inventory\.chest|large_chest_screen)' 20 -Since $mark | Out-Null
} else {
    Wait-LogLine 'inventory: \S+ opened the container at .* \(vanilla screen, large chest\)' 20 -Since $mark | Out-Null
}
Start-Sleep -Seconds 1
Save-Screenshot 'families-27-tf-chest-hopper-live-a' | Out-Null
Start-Sleep -Seconds 6
Save-Screenshot 'families-28-tf-chest-hopper-live-b' | Out-Null
Close-Screen
Start-Sleep -Seconds 1
Invoke-Build "replaceitem block $hx $($y + 1) $z slot.container 0 air"

# a sign written in game: the client mod's native editor (an empty-hand click on the sign), the
# text on the sign's display entity; vanilla clients get the editor as a form
if ($script:Mode -ne 'remote-vanilla') {
    # the sign at rotation 0 shows its front to the south: stand south of it, looking north
    Invoke-Teleport "/tp @s 60.5 $($y) $($z + 2.5) 180 25"
    Start-Sleep -Seconds 2
    $mark = Get-LogMark
    Click 0.5 0.5 -Relative -Right
    Wait-LogLine 'client screen: families.sign' 20 -Since $mark | Out-Null
    Type-Text 'Twilight'
    Start-Sleep -Seconds 1
    Save-Screenshot 'families-9-sign-editor' | Out-Null
    Close-Screen
    # the typed line reached the server (Esc keeps it, as Java's SignEditScreen)
    Wait-LogLine 'edited the sign at 60 .*\(1 of 4 lines written; lines \[0\]\)' 20 -Since $mark | Out-Null
    Start-Sleep -Seconds 2
    Save-Screenshot 'families-6-sign-text' | Out-Null
    # the text is a name tag, and a hidden HUD (gfx_hidehud, some client instances) hides name tags
    # too: one more with the HUD on (F1 when it is hidden, then F1 again: the option stays as it was)
    $hudHidden = $false
    foreach ($o in Get-ChildItem -Recurse -Path (Get-GameDataDir) -Filter 'options.txt' -ErrorAction SilentlyContinue) {
        if ((Get-Content $o.FullName) -match '^gfx_hidehud:1') { $hudHidden = $true }
    }
    if ($hudHidden) { Press-Key F1; Start-Sleep -Milliseconds 800 }
    Save-Screenshot 'families-7-sign-text-hud' | Out-Null
    if ($hudHidden) { Press-Key F1; Start-Sleep -Milliseconds 800 }

    # Java's sign text on TF's signs, written through the editor and coloured with items (client
    # mod: drawn flat on the board as SignRenderer / HangingSignRenderer draw it; the display
    # entities' name tags hidden): the canopy wall sign's front (black), the standing sign's back
    # (blue dye, then glow ink: glowing blue with its outline), the hanging sign's front (glow ink:
    # black with the cream outline), the wall hanging sign's front (red dye: dark red)
    $tf = $rows[4]
    function Write-Sign([double]$x, [double]$yy, [double]$zz, [double]$pitch, [string[]]$lines) {
        Invoke-Teleport "/tp @s $x $yy $zz 0 $pitch"
        Start-Sleep -Seconds 1
        $m = Get-LogMark
        Click 0.5 0.5 -Relative -Right
        Wait-LogLine 'client screen: families.sign' 20 -Since $m | Out-Null
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($i -gt 0) { Press-Key ENTER }   # SignEditScreen: Enter goes to the next line
            Type-Text $lines[$i]
        }
        Start-Sleep -Milliseconds 500
        Close-Screen
        $edited = Wait-LogLine 'edited the sign at' 20 -Since $m
        # the lines written are the first ones, one after another (an Enter handled twice, or a key
        # held into auto-repeat by a slow connection, skips a line: the log names the slots used)
        $want = (0..($lines.Count - 1)) -join ','
        if ($edited -notmatch "lines \[$([regex]::Escape($want))\]") { Write-Warning "the sign editor wrote other slots than [$want]: $edited" }
    }
    function Use-OnSign([double]$x, [double]$yy, [double]$zz, [double]$pitch, [string]$item) {
        Invoke-Build "replaceitem entity @a slot.weapon.mainhand 0 $item"
        Invoke-Teleport "/tp @s $x $yy $zz 0 $pitch"
        Start-Sleep -Seconds 1
        $m = Get-LogMark
        Click 0.5 0.5 -Relative -Right
        Wait-LogLine "used minecraft:$item on the sign" 20 -Since $m | Out-Null
    }
    # stand north of each sign (a teleport to a whole z stands in the block's middle: 1.5 blocks
    # from the sign's block) looking south at its board (the floor signs' boards about half a
    # block up: 25 degrees down; the hanging ones' at eye height: 6 degrees)
    $sx = $tf.x
    Invoke-Build "replaceitem entity @a slot.weapon.mainhand 0 air"
    Write-Sign ($sx + 4.5) $y ($z - 2) 25 @('Welcome to', 'the', 'Twilight', 'Forest')
    Write-Sign ($sx + 2.5) $y ($z - 2) 25 @('The back of', 'a canopy sign')
    Use-OnSign ($sx + 2.5) $y ($z - 2) 25 'blue_dye'
    Use-OnSign ($sx + 2.5) $y ($z - 2) 25 'glow_ink_sac'
    Invoke-Build "replaceitem entity @a slot.weapon.mainhand 0 air"
    Write-Sign ($sx + 6.5) $y ($z - 2) 6 @('Hanging', 'sign')
    Use-OnSign ($sx + 6.5) $y ($z - 2) 6 'glow_ink_sac'
    Invoke-Build "replaceitem entity @a slot.weapon.mainhand 0 air"
    Write-Sign ($sx + 9.5) $y ($z - 2) 6 @('Wall', 'hanging')
    Use-OnSign ($sx + 9.5) $y ($z - 2) 6 'red_dye'
    Invoke-Build "replaceitem entity @a slot.weapon.mainhand 0 air"
    Start-Sleep -Seconds 1
    Invoke-Teleport "/tp @s $($sx + 6) $($y + 1) $($z - 6) 0 12"
    Start-Sleep -Seconds 2
    Save-Screenshot 'families-16-tf-sign-text' | Out-Null
    Invoke-Teleport "/tp @s $($sx + 5) $($y) $($z - 3) 0 8"
    Start-Sleep -Seconds 2
    Save-Screenshot 'families-17-tf-sign-text-close' | Out-Null
    # from 20 blocks away the coloured glowing text loses its outline (isOutlineVisible: 16 blocks);
    # the black one keeps it
    Invoke-Teleport "/tp @s $($sx + 5) $($y + 2) $($z - 20) 0 5"
    Start-Sleep -Seconds 2
    Save-Screenshot 'families-18-tf-sign-text-far' | Out-Null

    # Vanilla references in the same light: each TF canopy sign (west) next to the game's own oak
    # sign (east of it: on the left in the pictures), written and coloured the same way (the game's
    # own editor and dye handling for the oak ones): black text on wall signs, red dye on wall
    # signs, glow ink on black hanging signs, blue dye and glow ink on standing signs
    $vx = 200
    $compare = @(
        @(0, 0, 1, 'minecraft:stone', ''),
        @(0, 0, 0, 'twilightforest:canopy_wall_sign', "[`"$f`"=`"north`"]"),
        @(1, 0, 1, 'minecraft:stone', ''),
        @(1, 0, 0, 'minecraft:wall_sign', '["facing_direction"=2]'),
        @(4, 0, 1, 'minecraft:stone', ''),
        @(4, 0, 0, 'twilightforest:canopy_wall_sign', "[`"$f`"=`"north`"]"),
        @(5, 0, 1, 'minecraft:stone', ''),
        @(5, 0, 0, 'minecraft:wall_sign', '["facing_direction"=2]'),
        @(8, 2, 0, 'minecraft:stone', ''),
        @(8, 1, 0, 'twilightforest:canopy_hanging_sign', '["twilightforest:rotation"=8]'),
        @(9, 2, 0, 'minecraft:stone', ''),
        @(9, 1, 0, 'minecraft:oak_hanging_sign', '["hanging"=true,"attached_bit"=false,"facing_direction"=2]'),
        @(12, 0, 0, 'twilightforest:canopy_sign', '["twilightforest:rotation"=8]'),
        @(13, 0, 0, 'minecraft:standing_sign', '["ground_sign_direction"=8]')
    )
    Invoke-Teleport "/tp @s $($vx + 6) $($y + 2) $($z - 9) 0 15"
    Start-Sleep -Seconds 4
    foreach ($b in $compare) {
        $states = if ($b[4]) { " $($b[4])" } else { '' }
        Invoke-Build "setblock $($vx + $b[0]) $($y + $b[1]) $($z + $b[2]) $($b[3])$states"
    }
    Start-Sleep -Seconds 2
    # the game's own editor and items on an oak sign (no GloamingForest log lines to wait for)
    function Write-VanillaSign([double]$x, [double]$yy, [double]$zz, [double]$pitch, [string]$line) {
        Invoke-Teleport "/tp @s $x $yy $zz 0 $pitch"
        Start-Sleep -Seconds 1
        $m = Get-LogMark
        Click 0.5 0.5 -Relative -Right
        Wait-LogLine 'client screen: sign_screen' 20 -Since $m | Out-Null
        Start-Sleep -Seconds 1
        Type-Text $line
        Start-Sleep -Milliseconds 500
        Close-Screen
        Wait-LogLine 'client screen: hud_screen' 20 -Since $m | Out-Null
        Start-Sleep -Seconds 1
    }
    function Use-OnVanillaSign([double]$x, [double]$yy, [double]$zz, [double]$pitch, [string]$item) {
        Invoke-Build "replaceitem entity @a slot.weapon.mainhand 0 $item"
        Invoke-Teleport "/tp @s $x $yy $zz 0 $pitch"
        Start-Sleep -Seconds 1
        Click 0.5 0.5 -Relative -Right
        Start-Sleep -Seconds 1
    }
    Invoke-Build "replaceitem entity @a slot.weapon.mainhand 0 air"
    Write-Sign ($vx + 0.5) $y ($z - 2) 25 @('Black text')
    Write-VanillaSign ($vx + 1.5) $y ($z - 2) 25 'Black text'
    Write-Sign ($vx + 4.5) $y ($z - 2) 25 @('Red dye')
    Write-VanillaSign ($vx + 5.5) $y ($z - 2) 25 'Red dye'
    Write-Sign ($vx + 8.5) $y ($z - 2) 6 @('Glow ink')
    Write-VanillaSign ($vx + 9.5) $y ($z - 2) 6 'Glow ink'
    Write-Sign ($vx + 12.5) $y ($z - 2) 25 @('Blue glow')
    Write-VanillaSign ($vx + 13.5) $y ($z - 2) 25 'Blue glow'
    Use-OnSign ($vx + 4.5) $y ($z - 2) 25 'red_dye'
    Use-OnVanillaSign ($vx + 5.5) $y ($z - 2) 25 'red_dye'
    Use-OnSign ($vx + 8.5) $y ($z - 2) 6 'glow_ink_sac'
    Use-OnVanillaSign ($vx + 9.5) $y ($z - 2) 6 'glow_ink_sac'
    Use-OnSign ($vx + 12.5) $y ($z - 2) 25 'blue_dye'
    Use-OnSign ($vx + 12.5) $y ($z - 2) 25 'glow_ink_sac'
    Use-OnVanillaSign ($vx + 13.5) $y ($z - 2) 25 'blue_dye'
    Use-OnVanillaSign ($vx + 13.5) $y ($z - 2) 25 'glow_ink_sac'
    Invoke-Build "replaceitem entity @a slot.weapon.mainhand 0 air"
    Start-Sleep -Seconds 1
    # one pair a picture, 2.5 blocks from the boards: TF's on the right, the oak one on the left
    Invoke-Teleport "/tp @s $($vx + 1) $y $($z - 2.5) 0 20"
    Start-Sleep -Seconds 2
    Save-Screenshot 'families-20-compare-black' | Out-Null
    Invoke-Teleport "/tp @s $($vx + 5) $y $($z - 2.5) 0 20"
    Start-Sleep -Seconds 2
    Save-Screenshot 'families-21-compare-red' | Out-Null
    Invoke-Teleport "/tp @s $($vx + 9) $y $($z - 2.5) 0 4"
    Start-Sleep -Seconds 2
    Save-Screenshot 'families-22-compare-glow-black' | Out-Null
    Invoke-Teleport "/tp @s $($vx + 13) $y $($z - 2.5) 0 20"
    Start-Sleep -Seconds 2
    Save-Screenshot 'families-23-compare-glow-blue' | Out-Null

    # Glowing black text far away, through a spyglass (the zoom makes the letters big enough to see;
    # black glowing text keeps its outline at any distance): Java draws the line over its 8 outline
    # copies with a depth offset, the client mod nearer to the camera along the view rays by a lead
    # that grows with the distance, so the cream outline never shows through the black letters
    # (z-fighting) at 40 or 60 blocks (the text is drawn within 64); the oak hanging sign beside it
    Invoke-Build "replaceitem entity @a slot.weapon.mainhand 0 spyglass"
    foreach ($far in 40, 60) {
        Invoke-Teleport "/tp @s $($vx + 9) $y $($z - $far) 0 0"
        Start-Sleep -Seconds 3
        Move-Mouse 0.5 0.5 -Relative
        [BsE2E.Native]::MouseButton(1, $false) # hold use: look through the spyglass
        try {
            Start-Sleep -Milliseconds 1500
            Save-Screenshot "families-24-glow-far-$far" | Out-Null
        } finally { [BsE2E.Native]::MouseButton(1, $true) }
        Start-Sleep -Milliseconds 500
    }
    Invoke-Build "replaceitem entity @a slot.weapon.mainhand 0 air"
}

# The text of a sign written by the server (families.sign_a_write): the client mod draws it on the
# board (front: four lines), a vanilla client shows the name tag of the side facing it (the front:
# the viewer stands south of it; the back's name tag is hidden for this viewer)
$mark = Get-LogMark
$r = Invoke-SelfTest 'families.sign_a_write'
if ($r.Pass -ne 1) { throw 'families.sign_a_write did not pass' }
$written = Wait-LogLine 'sign written at (-?\d+) (-?\d+) (-?\d+)' 20 -Since $mark
if ("$written" -match 'sign written at (-?\d+) (-?\d+) (-?\d+)') {
    $wx = [int]$Matches[1]; $wy = [int]$Matches[2]; $wz = [int]$Matches[3]
    # the sign stands at rotation 0: its front faces south
    Invoke-Teleport "/tp @s $($wx + 0.5) $($wy) $($wz + 2.5) 180 20"
    Start-Sleep -Seconds 4
    Save-Screenshot 'families-19-sign-written' | Out-Null
} else { throw "no 'sign written at' line from families.sign_a_write" }

# The client mod drew the sign text on the boards, hid the display entities' name tags and blended
# the canopy / mangrove leaves with the biome foliage (client lines, not the host's). The server
# names each player it sent sign text to: the client mod user (remote-mod), never the vanilla
# client (remote-vanilla: the host has written signs by now, so a send would have happened)
$clientLines = @(
    'families: sign text drawn on the board',
    'families: the sign text display entities show no name tag',
    'families: canopy and mangrove leaves blend the biome foliage colour'
)
$sentLine = 'families: sign text sent to the companion'
$sidesLine = 'families: the vanilla client .* is shown the sign sides facing it'
if ($script:Mode -eq 'remote-vanilla') {
    Start-Sleep -Seconds 2
    $sent = Get-LeviLog | Where-Object { $_ -match "^\[host\].*$([regex]::Escape($sentLine))" }
    if ($sent) { throw "the host sent sign text to the vanilla client: $($sent | Select-Object -First 1)" }
    # the vanilla client is told which side's name tag shows (the one facing it)
    Wait-LogLine "^\[host\].*$sidesLine" 20 | Out-Null
} else {
    foreach ($l in $clientLines) { Wait-LogLine "^(?!\[host\]).*$([regex]::Escape($l))" 20 | Out-Null }
    if ($script:Mode -eq 'remote-mod') { Wait-LogLine "^\[host\].*$([regex]::Escape($sentLine))" 20 | Out-Null }
    else { Wait-LogLine ([regex]::Escape($sentLine)) 20 | Out-Null }
}

# a vanilla client edits a sign through a form; its title is the game's own sign.edit text
if ($script:Mode -eq 'remote-vanilla') {
    Invoke-Teleport "/tp @s 60.5 $($y) $($z + 2.5) 180 25"
    Start-Sleep -Seconds 2
    Click 0.5 0.5 -Relative -Right
    Start-Sleep -Seconds 4
    Save-Screenshot 'families-14-sign-form' | Out-Null
    Close-Screen
}
