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
#                                           editor screen; the text shows on its display entity)
#   out/families/families-7-sign-text-hud.png  the same with the HUD on (the text's name tag shows)
#   out/families/families-8-snowloggable.png   the snowloggable close up (its 3 snow layers)
#   out/families/families-9-sign-editor.png    the native sign editor (client mod: Java's
#                                           SignEditScreen, the sign drawn with the line on it)
#   out/families/families-10-tf-woodwork.png   TF's own blocks through the kit: canopy door, sign,
#                                           wall sign, hanging and wall hanging sign, chest, trapped
#                                           chest double, trapdoor, button (TF's textures)
#   out/families/families-11-tf-plants.png  a grown blackberry bush with 2 snow layers, a young
#                                           blueberry bush with 1, TF's leaves (time, mining, sorting,
#                                           transformation, rainbow oak pre-tinted with Java's colour
#                                           without a world; twilight oak and canopy by the biome),
#                                           castle doors (connected textures), a time log
#   out/families/families-12-tf-close.png   TF's signs and chests close up
#   out/families/families-13-tf-bush.png    the snowy berry bush close up (client mod: the grass
#                                           below drawn snowy)
#   out/families/families-15-ct-close.png   the six-face connected texture fixture close up (client
#                                           mod: the corner piece at the L's inner corner)
#   out/families/families-14-sign-form.png  remote-vanilla: the sign editor form (its title is the
#                                           game's own sign.edit text)
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
    Wait-LogLine 'edited the sign at 60 .*\(1 of 4 lines written\)' 20 -Since $mark | Out-Null
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
