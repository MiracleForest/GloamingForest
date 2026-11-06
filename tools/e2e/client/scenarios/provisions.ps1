# U10 (provisions) on the client (post-merge acceptance; not run per unit).
#
# provisions.real_arm fills the player's inventory with U10's items (22 foods, Experiment 115, stale
# bread, the nine music discs), gives frosted II and builds Experiment 115 in its 16 states (bites 0..7,
# plain and regenerating with sprinkles) on a stone floor 3 blocks ahead. Then:
# 1. out/provisions/e115.png: the 16 cake states, with the frosted overlay (freezing vignette) and, with
#    the client mod, the frosted icon in the HUD effect list;
# 2. out/provisions/inventory.png: the inventory with every food, stale bread and disc icon (TF's textures),
#    names coloured by rarity (uncommon hydra chop / stroganoff, rare discs);
# 3. a disc in a jukebox: out/provisions/jukebox.png (the "now playing" line names TF's song key);
# 4. local / remote-mod: /twilightforest tp: the music provider picks twilightforest:twilight_forest
#    ("music: provider chose twilightforest:twilight_forest" in the client log), out/provisions/dimension.png.
# remote-vanilla: steps 1-3 (the client only has the server's packs; no HUD icon, no provider).
$script:IsLocal = $script:Mode -eq 'local'
$hostPrefix = if ($script:IsLocal) { '' } else { '\[host\].*' }
Wait-LogLine "${hostPrefix}GloamingForest world ready" 120 | Out-Null
Wait-HudReady | Out-Null

Invoke-Teleport '/tp @s 20.5 -60 20.5 0 20'
Invoke-Chat '/time set 1000'
Invoke-Chat '/weather clear'
Invoke-Chat '/fill 8 -61 18 30 -61 30 stone'
Invoke-Chat '/fill 8 -60 18 30 -55 30 air'
Invoke-Chat '/tag @s add gf_provisions_showcase'
$mark = Get-LogMark
$r = Invoke-SelfTest 'provisions.real_arm'
if ($r.Fail -gt 0) { throw 'provisions.real_arm failed' }
Wait-LogLine "${hostPrefix}provisions: armed " 20 -Since $mark | Out-Null
Start-Sleep -Seconds 3
Save-Screenshot 'e115' | Out-Null

Open-Inventory
Save-Screenshot 'inventory' | Out-Null
Close-Screen

Invoke-Chat '/setblock 20 -60 24 jukebox'
Invoke-Chat '/replaceitem block 20 -60 24 slot.container 0 twilightforest:music_disc_radiance'
Start-Sleep -Seconds 2
Save-Screenshot 'jukebox' | Out-Null
Invoke-Chat '/setblock 20 -60 24 air'

if ($script:Mode -ne 'remote-vanilla') {
    $mark = Get-LogMark
    Invoke-Chat '/twilightforest tp'
    Start-Sleep -Seconds 8
    Wait-HudReady | Out-Null
    try { Wait-LogLine 'music: provider chose twilightforest:twilight_forest' 90 -Since $mark | Out-Null }
    catch { throw 'the Twilight Forest music was not chosen in the dimension' }
    Save-Screenshot 'dimension' | Out-Null
}

$r = Invoke-SelfTest 'provisions.real_clear'
if ($r.Fail -gt 0) { throw 'provisions.real_clear failed' }
