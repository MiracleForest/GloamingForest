# Dark tower (U26) on the client: the unit's selftests pass in the local world (integrated server),
# then every block, item, mob and the dark tower itself are put in front of the camera for the
# screenshot review against TF's textures and models (%LOCALAPPDATA%\gf-tf):
#   blocks_1..blocks_7  the 15 device / tower blocks and their other states, four per shot, seen
#                       from five blocks away (vanishing family active / vanished, locked and
#                       unlocked lock, builder inactive / active / timeout, built blocks,
#                       antibuilder, antibuilt block, reactor, reactor debris, fake gold / diamond,
#                       slider on its three axes, ghast trap off / on, infested towerwood)
#   items               tower key, borer essence, carminite and the six spawn eggs in the hotbar
#   mob_<name>          each mob alone at close range: golem, broodling, borer, ghastling,
#                       ghastguard, the Ur-Ghast idle and, against a survival player, with its
#                       eyes open while it charges and fires (urghast_attack)
#   tower_outside       a dark tower placed with /tf place, seen from outside
#   tower_inside        the first main tower's ground floor
#   urghast_tantrum     in the Twilight Forest, an Ur-Ghast in a tantrum: boss tears falling under it and
#                       the tantrum rain the client mod fades in (TFWeatherRenderer.urGhastAlive)
# Screenshots: out/darktower/*.png
Save-Screenshot 'hud' | Out-Null
if ($script:Mode -eq 'local') {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'darktower.*' -TimeoutSeconds 1200
    if ($r.Fail -gt 0) { throw "$($r.Fail) darktower selftest(s) failed" }
    if ($r.Pass -lt 17) { throw "expected 17 darktower selftests to pass, got $($r.Pass)" }

    Invoke-Chat '/gamemode creative'
    Invoke-Chat '/difficulty easy'
    Invoke-Chat '/gamerule dofiretick false'
    Invoke-Chat '/gamerule domobspawning false'
    Invoke-Chat '/effect @s night_vision 99999 1 true'
    Invoke-Chat '/gamerule doDaylightCycle false'
    Invoke-Chat '/time set noon'
    Invoke-Teleport '/tp @s 600 -60 600'
    Start-Sleep -Seconds 2

    # ---- blocks: four per shot, two blocks apart, the camera five blocks south of them ----
    $groups = @(
        @('twilightforest:vanishing_block', 'twilightforest:vanishing_block ["twilightforest:active"=true]',
            'twilightforest:unbreakable_vanishing_block', 'twilightforest:unbreakable_vanishing_block ["twilightforest:active"=true]'),
        @('twilightforest:reappearing_block', 'twilightforest:reappearing_block ["twilightforest:active"=true]',
            'twilightforest:reappearing_block ["twilightforest:vanished"=true]', 'twilightforest:locked_vanishing_block'),
        @('twilightforest:locked_vanishing_block ["twilightforest:locked"=false]', 'twilightforest:carminite_builder',
            'twilightforest:carminite_builder ["twilightforest:state"="builder_active"]', 'twilightforest:carminite_builder ["twilightforest:state"="builder_timeout"]'),
        @('twilightforest:built_block', 'twilightforest:built_block ["twilightforest:active"=true]', 'twilightforest:antibuilder', 'twilightforest:antibuilt_block'),
        @('twilightforest:carminite_reactor', 'twilightforest:carminite_reactor ["twilightforest:active"=true]', 'twilightforest:reactor_debris', 'twilightforest:fake_gold'),
        @('twilightforest:fake_diamond', 'twilightforest:slider', 'twilightforest:slider ["twilightforest:axis"="x"]', 'twilightforest:slider ["twilightforest:axis"="z"]'),
        @('twilightforest:ghast_trap', 'twilightforest:ghast_trap ["twilightforest:active"=true]', 'twilightforest:infested_towerwood', 'twilightforest:towerwood')
    )
    $n = 1
    foreach ($g in $groups) {
        Invoke-Chat '/fill 596 -60 600 612 -50 612 air'
        Invoke-Chat '/fill 596 -61 600 612 -61 612 minecraft:smooth_stone'
        $x = 600
        foreach ($b in $g) { Invoke-Chat "/setblock $x -60 606 $b"; $x += 2 }
        # stand 5 blocks south of the row (the camera looks north, yaw 180), a little above it
        Invoke-Teleport '/tp @s 603.5 -60 610.5 180 12'
        Start-Sleep -Seconds 3
        Save-Screenshot "blocks_$n" | Out-Null
        $n++
    }
    Invoke-Chat '/fill 596 -60 600 612 -50 612 air'

    # ---- items in the hotbar (the HUD shows their icons) ----
    Invoke-Chat '/clear @s'
    $items = @('twilightforest:tower_key', 'twilightforest:borer_essence', 'twilightforest:carminite', 'twilightforest:ur_ghast_spawn_egg',
        'twilightforest:carminite_ghastguard_spawn_egg', 'twilightforest:carminite_ghastling_spawn_egg', 'twilightforest:carminite_golem_spawn_egg',
        'twilightforest:carminite_broodling_spawn_egg', 'twilightforest:towerwood_borer_spawn_egg')
    foreach ($i in $items) { Invoke-Chat "/give @s $i" }
    Invoke-Teleport '/tp @s 606 -60 615 0 5'
    Start-Sleep -Seconds 2
    Save-Screenshot 'items' | Out-Null
    Invoke-Chat '/clear @s'

    # ---- each mob alone, close to the camera (creative: nothing targets the player) ----
    # summoned relative to the player standing still on the ground, looking south (yaw 0)
    $mobs = @(@('carminite_golem', '~ ~ ~4'), @('carminite_broodling', '~ ~ ~3'), @('towerwood_borer', '~ ~ ~2'),
        @('carminite_ghastling', '~ ~1 ~4'), @('carminite_ghastguard', '~ ~1 ~9'))
    Invoke-Teleport '/tp @s 610 -60 640 0 8'
    Start-Sleep -Seconds 2
    foreach ($m in $mobs) {
        Invoke-Teleport '/tp @s 610 -60 640 0 8'
        Invoke-Chat "/summon twilightforest:$($m[0]) $($m[1])"
        Start-Sleep -Milliseconds 1200
        Save-Screenshot "mob_$($m[0])" | Out-Null
        Invoke-Chat "/kill @e[type=twilightforest:$($m[0])]"
    }

    # ---- the Ur-Ghast idle, then charging at a survival player (eyes open, fire texture) ----
    Invoke-Teleport '/tp @s 610 -60 640 0 -12'
    Invoke-Chat '/summon twilightforest:ur_ghast ~ ~4 ~24'
    Start-Sleep -Milliseconds 1500
    Save-Screenshot 'mob_ur_ghast' | Out-Null
    Invoke-Chat '/effect @s resistance 120 255 true'
    Invoke-Chat '/effect @s fire_resistance 120 1 true'
    Invoke-Chat '/gamemode survival'
    Start-Sleep -Seconds 2
    Save-Screenshot 'urghast_attack' | Out-Null
    Invoke-Chat '/gamemode creative'
    Invoke-Chat '/kill @e[type=twilightforest:ur_ghast]'
    Invoke-Chat '/kill @e[type=minecraft:fireball]'

    # ---- a dark tower: placed in loaded chunks, then seen from outside and from its ground floor ----
    Invoke-Teleport '/tp @s 800 -40 800'
    Start-Sleep -Seconds 4
    Invoke-Chat '/tf place twilightforest:dark_tower 800 -60 800'
    Start-Sleep -Seconds 12
    Invoke-Teleport '/tp @s 730 -60 730 -45 -18'
    Start-Sleep -Seconds 6
    Save-Screenshot 'tower_outside' | Out-Null
    Invoke-Teleport '/tp @s 818 -58 818 135 10'
    Start-Sleep -Seconds 4
    Save-Screenshot 'tower_inside' | Out-Null

    # ---- the Ur-Ghast's tantrum in the Twilight Forest: tears and the client's rain ----
    $mark = Get-Date
    Invoke-Chat '/tf tp'
    Wait-LogLine 'dimension: .* travelled from minecraft:overworld to twilightforest:twilight_forest' 60 -Since $mark | Out-Null
    Start-Sleep -Seconds 4
    Invoke-Chat '/gamemode creative'
    Invoke-Chat '/time set noon'
    Invoke-Chat '/tp @s ~ ~ ~ 0 -20'
    Invoke-Chat '/summon twilightforest:ur_ghast ~ ~12 ~26'
    Start-Sleep -Seconds 2
    Invoke-Chat '/damage @e[type=twilightforest:ur_ghast,c=1] 12 entity_attack'
    Wait-LogLine 'darktower: the Ur-Ghast throws a tantrum' 20 -Since $mark | Out-Null
    Wait-LogLine 'darktower: client Ur-Ghast tantrum rain' 20 -Since $mark | Out-Null
    Start-Sleep -Seconds 3
    Save-Screenshot 'urghast_tantrum' | Out-Null
    Invoke-Chat '/kill @e[type=twilightforest:ur_ghast]'
    Invoke-Chat '/kill @e[type=twilightforest:carminite_ghastling]'
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
}
Save-Screenshot 'selftest' | Out-Null
