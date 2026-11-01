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
#                                           latches meeting in the middle, an open double), trapped chest
#   out/families/families-4-variants.png    horizontal, directional, binary rotated, wall and
#                                           connectable pillars, connected textures, lightable,
#                                           snowloggable (3 layers), climbable
#   out/families/families-5-close.png       the signs and chests close up
#   out/families/families-6-sign-text.png   a sign written through the editor (client mod: the native
#                                           editor screen; the text shows on its display entity)
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
            @(17, 0, 0, 'gf_test:family_chest', "[`"$f`"=`"north`",`"twilightforest:type`"=`"left`",`"twilightforest:open_count`"=1]"),
            @(18, 0, 0, 'gf_test:family_chest', "[`"$f`"=`"north`",`"twilightforest:type`"=`"right`",`"twilightforest:open_count`"=1]"),
            @(20, 0, 0, 'gf_test:family_trapped_chest', "[`"$f`"=`"north`"]"))
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
            @(17, 0, 0, 'gf_test:family_snowloggable', '["twilightforest:layers"=3]'),
            @(19, 0, 0, 'gf_test:family_climbable', ''))
    }
)

Invoke-Teleport "/tp @s 50 $($y + 1) 30 0 20"
Start-Sleep -Seconds 3
foreach ($row in $rows) {
    foreach ($b in $row.blocks) {
        $x = $row.x + $b[0]
        $states = if ($b[4]) { " $($b[4])" } else { '' }
        Invoke-Build "setblock $x $($y + $b[1]) $($z + $b[2]) $($b[3])$states"
    }
}
Start-Sleep -Seconds 12 # the chat fades
foreach ($row in $rows) {
    $cx = $row.x + 10
    Invoke-Teleport "/tp @s $cx $($y + 2) $($z - 9) 0 15"
    Start-Sleep -Seconds 3
    Save-Screenshot $row.name | Out-Null
}
# a close look at the containers (signs and chests)
Invoke-Teleport "/tp @s 69 $($y + 1) $($z - 4) 0 15"
Start-Sleep -Seconds 3
Save-Screenshot 'families-5-close' | Out-Null

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
    Close-Screen
    Wait-LogLine 'edited the sign at' 20 -Since $mark | Out-Null
    Start-Sleep -Seconds 2
    Save-Screenshot 'families-6-sign-text' | Out-Null
}
