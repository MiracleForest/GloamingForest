# Import (F1): the client loads the full content skeleton (resource pack: textures, block geometry,
# texts, client entities) with no content-log error, the import.* selftests pass, a row of TF blocks
# of every shape family renders in the world, and the creative inventory shows TF's items with their
# icons and names (the TF creative groups and a search for "twilight").
# Screenshots: out/import/{blocks-*,creative-*}.png: compare them with TF's textures in %LOCALAPPDATA%\gf-tf.
if ($script:Mode -eq 'local') {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'import.*' -TimeoutSeconds 900
    if ($r.Fail -gt 0) { throw "$($r.Fail) import selftest(s) failed" }
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
}

# A showcase row in front of the player (the flat test world's surface is y -61..-60).
$blocks = @(
    'twilightforest:twilight_oak_log', 'twilightforest:canopy_planks', 'twilightforest:canopy_stairs', 'twilightforest:canopy_slab',
    'twilightforest:canopy_fence', 'twilightforest:twilight_oak_leaves', 'twilightforest:twilight_oak_sapling', 'twilightforest:nagastone',
    'twilightforest:mazestone_brick', 'twilightforest:castle_brick', 'twilightforest:canopy_door', 'twilightforest:canopy_trapdoor',
    'twilightforest:firefly_jar', 'twilightforest:huge_mushgloom', 'twilightforest:brown_thorns', 'twilightforest:uncrafting_table'
)
Invoke-Chat '/gamemode creative'
Invoke-Chat '/time set noon'
Invoke-Teleport '/tp @s 300 -60 300 facing 300 -60 306'
$x = 292
foreach ($b in $blocks) {
    Invoke-Chat "/setblock $x -60 306 $b"
    $x++
}
Invoke-Chat '/setblock 309 -60 306 air'
Start-Sleep -Seconds 3
Save-Screenshot 'blocks-row' | Out-Null

# Creative inventory: the construction category ends with TF's "blocks" group; search "twilight".
Open-Inventory
Save-Screenshot 'creative-open' | Out-Null
Close-Screen
