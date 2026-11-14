# Dark tower (U26) on the client: the unit's selftests pass in the local world (integrated server),
# then every block, item, mob and the dark tower itself are put in front of the camera for the
# screenshot review against upstream's textures and models (%LOCALAPPDATA%\gf-upstream\view):
#   blocks_1..blocks_7  the 15 device / tower blocks and their other states, four per shot, seen
#                       from five blocks away (vanishing family active / vanished, locked and
#                       unlocked lock, builder inactive / active / timeout, built blocks,
#                       antibuilder, antibuilt block, reactor, reactor debris, fake gold / diamond,
#                       slider on its three axes, ghast trap off / on, infested towerwood)
#   items               tower key, borer essence, carminite and the six spawn eggs in the hotbar
#   mob_<name>          each mob alone at close range: golem, broodling, borer, ghastling,
#                       ghastguard, the Ur-Ghast idle and, against a survival player, with its
#                       eyes open while it charges and fires (urghast_attack)
#   tower_outside       a dark tower placed with /gloamingforest place, seen from outside
#   tower_inside        the first main tower's ground floor
#   closeup_debris      active reactor debris (an active reactor places it) and closeup_trap: an
#                       active ghast trap, each from two blocks away
#   urghast_tantrum     in the Gloaming Forest, an Ur-Ghast in a tantrum: boss tears falling under it and
#                       the tantrum rain the client mod fades in (TFWeatherRenderer.urGhastAlive)
# Modes: local runs the selftests on the integrated server first; remote-mod and remote-vanilla join
# the BDS host and take the same shots. A companion (local, remote-mod) draws the tears and the rain
# itself from "darktower.urghast"; a vanilla client gets the tears as server-side particles and must
# never apply the companion rain.
# Screenshots: out/darktower/*.png
Save-Screenshot 'hud' | Out-Null
$remote = $script:Mode -ne 'local'
if ($remote) {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
    if ($script:Mode -eq 'remote-mod') { Wait-LogLine '\[host\].*companion joined' 60 | Out-Null }
} else {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'darktower.*' -TimeoutSeconds 1200
    if ($r.Fail -gt 0) { throw "$($r.Fail) darktower selftest(s) failed" }
    if ($r.Pass -lt 20) { throw "expected 20 darktower selftests to pass, got $($r.Pass)" }
}
if ($true) {
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
        @('gloamingforest:vanishing_block', 'gloamingforest:vanishing_block ["gloamingforest:active"=true]',
            'gloamingforest:unbreakable_vanishing_block', 'gloamingforest:unbreakable_vanishing_block ["gloamingforest:active"=true]'),
        @('gloamingforest:reappearing_block', 'gloamingforest:reappearing_block ["gloamingforest:active"=true]',
            'gloamingforest:reappearing_block ["gloamingforest:vanished"=true]', 'gloamingforest:locked_vanishing_block'),
        @('gloamingforest:locked_vanishing_block ["gloamingforest:locked"=false]', 'gloamingforest:carminite_builder',
            'gloamingforest:carminite_builder ["gloamingforest:state"="builder_active"]', 'gloamingforest:carminite_builder ["gloamingforest:state"="builder_timeout"]'),
        @('gloamingforest:built_block', 'gloamingforest:built_block ["gloamingforest:active"=true]', 'gloamingforest:antibuilder', 'gloamingforest:antibuilt_block'),
        @('gloamingforest:carminite_reactor', 'gloamingforest:carminite_reactor ["gloamingforest:active"=true]', 'gloamingforest:reactor_debris', 'gloamingforest:fake_gold'),
        @('gloamingforest:fake_diamond', 'gloamingforest:slider', 'gloamingforest:slider ["gloamingforest:axis"="x"]', 'gloamingforest:slider ["gloamingforest:axis"="z"]'),
        @('gloamingforest:ghast_trap', 'gloamingforest:ghast_trap ["gloamingforest:active"=true]', 'gloamingforest:infested_towerwood', 'gloamingforest:towerwood')
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

    # ---- close-ups: active reactor debris (placed by an active reactor) and an active ghast trap ----
    Invoke-Chat '/setblock 604 -60 606 gloamingforest:reactor_debris'
    Invoke-Chat '/setblock 606 -60 606 gloamingforest:reactor_debris'
    Invoke-Teleport '/tp @s 605 -60 608.5 180 25'
    Start-Sleep -Seconds 3
    Save-Screenshot 'closeup_debris' | Out-Null
    Invoke-Chat '/fill 596 -60 600 612 -50 612 air'
    Invoke-Chat '/setblock 604 -60 606 gloamingforest:ghast_trap ["gloamingforest:active"=true]'
    Invoke-Chat '/setblock 606 -60 606 gloamingforest:ghast_trap'
    Invoke-Teleport '/tp @s 605 -60 608.5 180 25'
    Start-Sleep -Seconds 3
    Save-Screenshot 'closeup_trap' | Out-Null
    Invoke-Chat '/fill 596 -60 600 612 -50 612 air'

    # ---- items in the hotbar (the HUD shows their icons) ----
    Invoke-Chat '/clear @s'
    $items = @('gloamingforest:tower_key', 'gloamingforest:borer_essence', 'gloamingforest:carminite', 'gloamingforest:ur_ghast_spawn_egg',
        'gloamingforest:carminite_ghastguard_spawn_egg', 'gloamingforest:carminite_ghastling_spawn_egg', 'gloamingforest:carminite_golem_spawn_egg',
        'gloamingforest:carminite_broodling_spawn_egg', 'gloamingforest:towerwood_borer_spawn_egg')
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
        Invoke-Chat "/summon gloamingforest:$($m[0]) $($m[1])"
        Start-Sleep -Milliseconds 1200
        Save-Screenshot "mob_$($m[0])" | Out-Null
        Invoke-Chat "/kill @e[type=gloamingforest:$($m[0])]"
    }

    # ---- the Ur-Ghast idle, then charging at a survival player (eyes open, fire texture) ----
    Invoke-Teleport '/tp @s 610 -60 640 0 -12'
    Invoke-Chat '/summon gloamingforest:ur_ghast ~ ~4 ~24'
    Start-Sleep -Milliseconds 1500
    Save-Screenshot 'mob_ur_ghast' | Out-Null
    Invoke-Chat '/effect @s resistance 120 255 true'
    Invoke-Chat '/effect @s fire_resistance 120 1 true'
    Invoke-Chat '/gamemode survival'
    Start-Sleep -Seconds 2
    Save-Screenshot 'urghast_attack' | Out-Null
    Invoke-Chat '/gamemode creative'
    Invoke-Chat '/kill @e[type=gloamingforest:ur_ghast]'
    Invoke-Chat '/kill @e[type=minecraft:fireball]'

    # ---- a dark tower: placed in loaded chunks, then seen from outside and from its ground floor ----
    Invoke-Teleport '/tp @s 800 -40 800'
    Start-Sleep -Seconds 4
    Invoke-Chat '/gloamingforest place gloamingforest:dark_tower 800 -60 800'
    Start-Sleep -Seconds 12
    Invoke-Teleport '/tp @s 730 -60 730 -45 -18'
    Start-Sleep -Seconds 6
    Save-Screenshot 'tower_outside' | Out-Null
    Invoke-Teleport '/tp @s 818 -58 818 135 10'
    Start-Sleep -Seconds 4
    Save-Screenshot 'tower_inside' | Out-Null

    # ---- the Ur-Ghast's tantrum in the Gloaming Forest: tears and the client's rain ----
    $mark = Get-LogMark
    Invoke-Chat '/gloamingforest tp'
    Wait-LogLine 'dimension: .* travelled from minecraft:overworld to gloamingforest:gloaming_forest' 60 -Since $mark | Out-Null
    Start-Sleep -Seconds 4
    Invoke-Chat '/gamemode creative'
    Invoke-Chat '/time set noon'
    Invoke-Chat '/tp @s ~ ~ ~ 0 -20'
    Invoke-Chat '/summon gloamingforest:ur_ghast ~ ~12 ~26'
    Start-Sleep -Seconds 2
    # 18 damage before the first tantrum (the constructor's setInTantrum(false) resets it to 18)
    Invoke-Chat '/damage @e[type=gloamingforest:ur_ghast,c=1] 20 entity_attack'
    Wait-LogLine 'darktower: the Ur-Ghast throws a tantrum' 20 -Since $mark | Out-Null
    if ($script:Mode -eq 'remote-vanilla') {
        Start-Sleep -Seconds 4
        if (Get-LeviLog -Since $mark | Select-String -Pattern '^(?!\[host\]).*darktower: client Ur-Ghast tantrum rain' -Quiet) { throw 'the vanilla client applied the companion rain' }
    } else {
        $clientLine = if ($remote) { '^(?!\[host\]).*' } else { '' }
        Wait-LogLine "${clientLine}darktower: client Ur-Ghast tantrum rain" 20 -Since $mark | Out-Null
    }
    Start-Sleep -Seconds 3
    Save-Screenshot 'urghast_tantrum' | Out-Null
    Invoke-Chat '/kill @e[type=gloamingforest:ur_ghast]'
    Invoke-Chat '/kill @e[type=gloamingforest:carminite_ghastling]'
}
Save-Screenshot 'selftest' | Out-Null
