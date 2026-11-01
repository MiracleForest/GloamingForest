# Families (F11): every block family as the player sees it. The gf_test fixture families (one block
# per family, vanilla textures) are built in four rows and photographed:
#   out/families/families-1-shapes.png      stairs, slabs, fences, fence gate, walls, panes
#   out/families/families-2-mechanisms.png  door, trapdoors, button, pressure plate, logs, leaves, sapling, potted plant
#   out/families/families-3-containers.png  signs (standing, wall, hanging, wall hanging), chests, trapped chest
#   out/families/families-4-variants.png    horizontal, directional, binary rotated, wall and connectable pillars,
#                                           connected texture, lightable, snowloggable, climbable
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
    if (Get-LeviLog -Since $mark | Select-String -Pattern 'fixture families missing' -Quiet) { throw 'the fixture families were not generated (tools/families)' }
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
    $mark = Get-LogMark
    $r = Invoke-SelfTest 'families.fixtures'
    if ($r.Fail -gt 0) { throw 'families.fixtures failed on the host' }
    if (Get-LeviLog -Since $mark | Select-String -Pattern 'fixture families missing' -Quiet) {
        throw 'the host has no fixture families: run pwsh tools/families/run.ps1 after the build (it updates bin/server)'
    }
}

$y  = -60 # the flat world's first air layer
$z  = 46
$rows = @(
    @{ name = 'families-1-shapes'; x = 0; blocks = @(
            @(0, 0, 0, 'gf_test:family_stairs', '["minecraft:vertical_half"="bottom","minecraft:cardinal_direction"="north"]'),
            @(1, 0, 0, 'gf_test:family_stairs', '["minecraft:vertical_half"="top","minecraft:cardinal_direction"="east"]'),
            @(3, 0, 0, 'gf_test:family_slab', '["minecraft:vertical_half"="bottom"]'),
            @(4, 0, 0, 'gf_test:family_slab', '["minecraft:vertical_half"="top"]'),
            @(5, 0, 0, 'gf_test:family_slab', '["twilightforest:double"=1]'),
            @(7, 0, 0, 'gf_test:family_fence', ''),
            @(8, 0, 0, 'gf_test:family_fence', ''),
            @(9, 0, 0, 'gf_test:family_fence_gate', '["minecraft:cardinal_direction"="north"]'),
            @(10, 0, 0, 'gf_test:family_fence', ''),
            @(12, 0, 0, 'gf_test:family_wall', ''),
            @(13, 0, 0, 'gf_test:family_wall', ''),
            @(13, 0, 1, 'gf_test:family_wall', ''),
            @(15, 0, 0, 'gf_test:family_pane', ''),
            @(16, 0, 0, 'gf_test:family_pane', ''))
    },
    @{ name = 'families-2-mechanisms'; x = 30; blocks = @(
            @(0, 0, 0, 'gf_test:family_door', '["minecraft:cardinal_direction"="north","twilightforest:upper"=0]'),
            @(0, 1, 0, 'gf_test:family_door', '["minecraft:cardinal_direction"="north","twilightforest:upper"=1]'),
            @(2, 0, 0, 'gf_test:family_trapdoor', '["minecraft:vertical_half"="bottom"]'),
            @(3, 0, 0, 'gf_test:family_trapdoor', '["minecraft:cardinal_direction"="north","twilightforest:open"=1]'),
            @(5, 0, 0, 'minecraft:stone', ''),
            @(5, 1, 0, 'gf_test:family_button', '["minecraft:block_face"="up"]'),
            @(5, 0, -1, 'gf_test:family_button', '["minecraft:block_face"="north","twilightforest:powered"=1]'),
            @(6, 0, 0, 'gf_test:family_pressure_plate', ''),
            @(8, 0, 0, 'gf_test:family_log', '["minecraft:block_face"="up"]'),
            @(9, 0, 0, 'gf_test:family_log', '["minecraft:block_face"="east"]'),
            @(10, 0, 0, 'gf_test:family_stripped_log', '["minecraft:block_face"="up"]'),
            @(11, 0, 0, 'gf_test:family_wood', ''),
            @(12, 0, 0, 'gf_test:family_leaves', '["twilightforest:persistent"=1]'),
            @(13, 0, 0, 'gf_test:family_sapling', ''),
            @(14, 0, 0, 'gf_test:family_potted', ''))
    },
    @{ name = 'families-3-containers'; x = 60; blocks = @(
            @(0, 0, 0, 'gf_test:family_sign', '["twilightforest:rotation"=8]'),
            @(1, 0, 0, 'gf_test:family_sign', '["twilightforest:rotation"=10]'),
            @(3, 0, 1, 'minecraft:stone', ''),
            @(3, 0, 0, 'gf_test:family_wall_sign', '["minecraft:cardinal_direction"="north"]'),
            @(5, 2, 0, 'minecraft:stone', ''),
            @(5, 1, 0, 'gf_test:family_hanging_sign', '["twilightforest:rotation"=8]'),
            @(7, 2, 1, 'minecraft:stone', ''),
            @(7, 2, 0, 'gf_test:family_wall_hanging_sign', '["minecraft:cardinal_direction"="north"]'),
            @(9, 0, 0, 'gf_test:family_chest', '["minecraft:cardinal_direction"="north"]'),
            @(11, 0, 0, 'gf_test:family_chest', '["minecraft:cardinal_direction"="north","twilightforest:type"="left"]'),
            @(12, 0, 0, 'gf_test:family_chest', '["minecraft:cardinal_direction"="north","twilightforest:type"="right"]'),
            @(14, 0, 0, 'gf_test:family_trapped_chest', '["minecraft:cardinal_direction"="north"]'))
    },
    @{ name = 'families-4-variants'; x = 90; blocks = @(
            @(0, 0, 0, 'gf_test:family_horizontal', '["minecraft:cardinal_direction"="north"]'),
            @(1, 0, 0, 'gf_test:family_directional', '["minecraft:facing_direction"="north"]'),
            @(2, 0, 0, 'gf_test:family_binary', '["minecraft:cardinal_direction"="north"]'),
            @(3, 0, 0, 'gf_test:family_binary', '["minecraft:cardinal_direction"="east"]'),
            @(5, 0, 0, 'gf_test:family_wall_pillar', '["minecraft:block_face"="up"]'),
            @(5, 1, 0, 'gf_test:family_wall_pillar', '["minecraft:block_face"="up"]'),
            @(7, 0, 0, 'gf_test:family_connectable_pillar', '["minecraft:block_face"="up"]'),
            @(7, 1, 0, 'gf_test:family_connectable_pillar', '["minecraft:block_face"="up"]'),
            @(9, 0, 0, 'gf_test:family_ct', ''),
            @(10, 0, 0, 'gf_test:family_ct', ''),
            @(12, 0, 0, 'gf_test:family_lightable', '["twilightforest:lighting"="normal"]'),
            @(14, 0, 0, 'gf_test:family_snowloggable', '["twilightforest:layers"=3]'),
            @(16, 0, 0, 'gf_test:family_climbable', ''))
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
Start-Sleep -Seconds 3
foreach ($row in $rows) {
    $cx = $row.x + 8
    Invoke-Teleport "/tp @s $cx $($y + 2) $($z - 8) 0 15"
    Start-Sleep -Seconds 3
    Save-Screenshot $row.name | Out-Null
}
# a close look at the containers (signs and chests)
Invoke-Teleport "/tp @s 67 $($y + 1) $($z - 4) 0 15"
Start-Sleep -Seconds 3
Save-Screenshot 'families-5-close' | Out-Null
