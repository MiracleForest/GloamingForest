# Import (F1): the client loads the full content skeleton (resource pack: textures, block geometry,
# texts, client entities), the import.* selftests pass (local), a row of upstream blocks of every shape
# family renders in the world, and the creative inventory shows upstream's items with their icons and names
# (a search for "gloaming" and upstream's group at the end of the construction category).
# Remote modes: the BDS host sends the same packs to the client (remote-vanilla: no client mod).
# Screenshots: out/import/{blocks-row,creative-*}.png: compare them with upstream's textures in %LOCALAPPDATA%\gf-upstream\view.
$remote = $script:Mode -ne 'local'
function Invoke-ImportCommand([string]$Command) {
    # remote: the host console runs it (the joined player is not an operator); local: chat
    if ($remote) { Send-HostConsole ($Command.TrimStart('/') -replace '@s', '@a'); Start-Sleep -Milliseconds 400 }
    else { Invoke-Chat $Command }
}

if (-not $remote) {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'import.*' -TimeoutSeconds 900
    if ($r.Fail -gt 0) { throw "$($r.Fail) import selftest(s) failed" }
    $expected = @('import.blocks', 'import.items', 'import.entities', 'import.lang_and_data', 'import.java_states',
        'import.rotations', 'import.light', 'import.behaviour', 'import.sound_types', 'import.bool_defaults', 'import.place_blocks',
        'import.harvest_tags', 'import.harvest_mining', 'import.harvest_drops')
    $missing = @($expected | Where-Object { $r.Passed -notcontains $_ })
    if ($missing.Count -gt 0) { throw "import selftest(s) did not pass: $($missing -join ', ')" }
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
}

# A showcase row in front of the player (the flat test world's surface is y -61).
$blocks = @(
    'gloamingforest:gloaming_oak_log', 'gloamingforest:canopy_planks', 'gloamingforest:canopy_stairs', 'gloamingforest:canopy_slab',
    'gloamingforest:canopy_fence', 'gloamingforest:gloaming_oak_leaves', 'gloamingforest:gloaming_oak_sapling', 'gloamingforest:nagastone',
    'gloamingforest:mazestone_brick', 'gloamingforest:castle_brick', 'gloamingforest:canopy_door', 'gloamingforest:canopy_trapdoor',
    'gloamingforest:firefly_jar', 'gloamingforest:huge_mushgloom', 'gloamingforest:brown_thorns', 'gloamingforest:uncrafting_table'
)
Invoke-ImportCommand '/gamemode creative @s'
Invoke-ImportCommand '/time set noon'
if ($remote) { Invoke-ImportCommand '/tp @s 300 -60 300 facing 300 -60 306'; Start-Sleep -Seconds 3 }
else { Invoke-Teleport '/tp @s 300 -60 300 facing 300 -60 306' }
$x = 292
foreach ($b in $blocks) {
    Invoke-ImportCommand "/setblock $x -60 306 $b"
    $x++
}
Start-Sleep -Seconds 3
Save-Screenshot 'blocks-row' | Out-Null

# Creative inventory (opens on "All items"): search "gloaming" (upstream's names), then the construction
# category, whose last group is upstream's "blocks" tab (bp/item_catalog.d/import.json).
Open-Inventory
Save-Screenshot 'creative-open' | Out-Null
Click 0.5 0.311 -Relative          # the search field
Type-Text 'gloaming'
Start-Sleep -Seconds 2
Save-Screenshot 'creative-search-gloaming' | Out-Null
Click 0.217 0.1875 -Relative       # the construction tab
Start-Sleep -Seconds 1
Move-Mouse 0.5 0.6 -Relative
Scroll -400
Start-Sleep -Seconds 2
Save-Screenshot 'creative-construction-end' | Out-Null
Click 0.6016 0.514 -Relative       # the last group of the category: upstream's blocks tab
Start-Sleep -Seconds 2
Save-Screenshot 'creative-construction-tf' | Out-Null
Close-Screen
if ($remote -and -not (Get-GameProcess)) { throw 'the game closed while connected to the host' }
