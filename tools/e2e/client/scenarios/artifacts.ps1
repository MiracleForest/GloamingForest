# Scepters, charms, artifacts and death keeping (U9), client side. What it shows (compare each with upstream):
#   out/artifacts/artifacts-1-items.png       the 20 items in the hotbar / inventory (upstream's item textures; the
#                                             mystic crown shows the crown splinter icon, the flasks their
#                                             empty textures)
#   out/artifacts/artifacts-2-casket.png      three keepsake caskets (damage 0, 1, 2: keepsake_casket_0/1/2
#                                             textures, closed lid) facing south, east and north, and one
#                                             logged in obsidian (the solid model)
#   out/artifacts/artifacts-3-casket-ui.png   the casket right-clicked: the 45-slot (5 row) chest screen
#                                             (client mod) / the form (remote-vanilla)
#   out/artifacts/artifacts-4-shields.png     5 temporary fortification shields: the shield sprites above
#                                             the armour row (client mod) / the action bar count (vanilla)
#   out/artifacts/artifacts-5-bolt.png        a gloaming scepter bolt in flight (the gloaming orb sprite,
#                                             the teal magic_effect trail)
#   out/artifacts/artifacts-6-zombie.png      a loyal zombie (the green-tinted zombie) next to the player
#   out/artifacts/artifacts-7-ore-meter.png   an ore meter held after a scan: upstream's results box at the top
#                                             left (client mod) / the chat lines (vanilla)
#   out/artifacts/artifacts-8-charm.png       the charm of life saving the player: the charm circling the
#                                             player's eyes with item particles
# Local mode also runs the artifacts selftests on the integrated server.

function Invoke-Build([string]$command) {
    if ($script:Mode -eq 'local') { Invoke-Chat "/$command" } else { Send-HostConsole $command }
}
$who = if ($script:Mode -eq 'local') { '@s' } else { '@a' }

if ($script:Mode -eq 'local') {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'artifacts.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) artifacts selftest(s) failed" }
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
}
Invoke-Build 'gamerule sendcommandfeedback false'
Invoke-Build 'gamerule domobspawning false'
Invoke-Build "gamemode survival $who"

$y = -60 # the flat world's first air layer
$tx = 90; $tz = 90
Invoke-Teleport "/tp $who $($tx + 2) $($y + 1) $($tz + 5) 180 20"
Start-Sleep -Seconds 3

# 1: the items
Invoke-Build "clear $who"
foreach ($i in 'gloaming_scepter', 'lifedrain_scepter', 'zombie_scepter', 'fortification_scepter', 'exanimate_essence', 'charm_of_life_1',
               'charm_of_life_2', 'charm_of_keeping_1', 'charm_of_keeping_2', 'charm_of_keeping_3', 'keepsake_casket', 'brittle_potion_flask',
               'greater_potion_flask', 'ore_meter', 'moon_dial', 'pocket_watch', 'emperors_cloth', 'mystic_crown', 'crown_splinter', 'four_leaf_clover') {
    Invoke-Build "give $who gloamingforest:$i"
}
Open-Inventory
Start-Sleep -Seconds 1
Save-Screenshot 'artifacts-1-items' | Out-Null
Close-Screen

# 2: caskets
Invoke-Build "setblock $tx $y $tz gloamingforest:keepsake_casket [""gloamingforest:damage""=0,""gloamingforest:facing""=""south""]"
Invoke-Build "setblock $($tx + 2) $y $tz gloamingforest:keepsake_casket [""gloamingforest:damage""=1,""gloamingforest:facing""=""east""]"
Invoke-Build "setblock $($tx + 4) $y $tz gloamingforest:keepsake_casket [""gloamingforest:damage""=2,""gloamingforest:facing""=""north""]"
Invoke-Build "setblock $($tx + 6) $y $tz gloamingforest:keepsake_casket [""gloamingforest:multilogged""=""obsidian""]"
Invoke-Teleport "/tp $who $($tx + 3) $($y + 1) $($tz + 4) 180 25"
Start-Sleep -Seconds 2
Save-Screenshot 'artifacts-2-casket' | Out-Null

# 3: the casket screen
Invoke-Teleport "/tp $who $($tx + 0.5) $y $($tz + 2.5) 180 30"
Start-Sleep -Seconds 1
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Start-Sleep -Seconds 2
Save-Screenshot 'artifacts-3-casket-ui' | Out-Null
Close-Screen

# 4: shields
$mark = Get-LogMark
Invoke-Build "gloamingforest shield $who set 5 true"
Start-Sleep -Seconds 1
Save-Screenshot 'artifacts-4-shields' | Out-Null
Invoke-Build "gloamingforest shield $who set 0 true"

# 5: a bolt in flight
Invoke-Build "clear $who"
Invoke-Build "give $who gloamingforest:gloaming_scepter"
Invoke-Teleport "/tp $who $($tx + 2) $($y + 1) $($tz + 8) 90 0"
Start-Sleep -Seconds 1
Click 0.5 0.5 -Relative -Right
Start-Sleep -Milliseconds 250
Save-Screenshot 'artifacts-5-bolt' | Out-Null

# 6: a loyal zombie
Invoke-Build "summon gloamingforest:loyal_zombie $($tx + 2) $y $($tz + 5)"
Invoke-Teleport "/tp $who $($tx + 2) $($y + 1) $($tz + 9) 180 15"
Start-Sleep -Seconds 2
Save-Screenshot 'artifacts-6-zombie' | Out-Null
Invoke-Build "kill @e[type=gloamingforest:loyal_zombie]"

# 7: the ore meter
Invoke-Build "clear $who"
Invoke-Build "give $who gloamingforest:ore_meter"
Click 0.5 0.5 -Relative -Right
Start-Sleep -Seconds 6
Save-Screenshot 'artifacts-7-ore-meter' | Out-Null

# 8: the charm of life
Invoke-Build "clear $who"
Invoke-Build "give $who gloamingforest:charm_of_life_2"
Invoke-Build "damage $who 100"
Start-Sleep -Milliseconds 700
Save-Screenshot 'artifacts-8-charm' | Out-Null
Invoke-Build "clear $who"
Write-Step 'the artifacts, the casket, the shield HUD, the bolt, the zombie, the ore meter and the charm were shown'
