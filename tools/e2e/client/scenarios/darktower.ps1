# Dark tower (U26) on the client: the device selftests pass in the local world (integrated
# server), then every block, item and mob of the unit is put in front of the camera for the
# screenshot review against TF's textures and models (%LOCALAPPDATA%\gf-tf):
#   blocks        the 15 device / tower blocks in their states (inactive and active vanishing family,
#                 locked and unlocked lock, builder inactive / active / timeout, built blocks,
#                 antibuilder, antibuilt block, reactor, reactor debris, fake gold / diamond,
#                 slider on its three axes, ghast trap off / on, infested towerwood)
#   items         tower key, borer essence, carminite and the six spawn eggs in the hotbar
#   mobs          the carminite golem, broodling, towerwood borer, ghastling, ghastguard and the
#                 Ur-Ghast (no AI while on show)
# Screenshots: out/darktower/{hud,blocks,blocks_active,items,mobs,urghast,selftest}.png
Save-Screenshot 'hud' | Out-Null
if ($script:Mode -eq 'local') {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'darktower.*' -TimeoutSeconds 900
    if ($r.Fail -gt 0) { throw "$($r.Fail) darktower selftest(s) failed" }
    if ($r.Pass -lt 9) { throw "expected 9 darktower selftests to pass, got $($r.Pass)" }

    Invoke-Chat '/gamemode creative'
    Invoke-Chat '/effect @s night_vision 600 1 true'
    Invoke-Chat '/gamerule doDaylightCycle false'
    Invoke-Chat '/time set noon'
    Invoke-Teleport '/tp @s 300 -60 300'
    Start-Sleep -Seconds 2
    $blocks = @(
        'twilightforest:vanishing_block', 'twilightforest:reappearing_block', 'twilightforest:unbreakable_vanishing_block',
        'twilightforest:locked_vanishing_block', 'twilightforest:carminite_builder', 'twilightforest:built_block',
        'twilightforest:antibuilder', 'twilightforest:antibuilt_block', 'twilightforest:carminite_reactor',
        'twilightforest:reactor_debris', 'twilightforest:fake_gold', 'twilightforest:fake_diamond',
        'twilightforest:slider', 'twilightforest:ghast_trap', 'twilightforest:infested_towerwood'
    )
    $x = 300
    foreach ($b in $blocks) { Invoke-Chat "/setblock $x -59 305 $b"; $x += 2 }
    # their other states one row behind
    Invoke-Chat '/setblock 300 -59 308 twilightforest:vanishing_block ["twilightforest:active"=true]'
    Invoke-Chat '/setblock 302 -59 308 twilightforest:reappearing_block ["twilightforest:vanished"=true]'
    Invoke-Chat '/setblock 304 -59 308 twilightforest:reappearing_block ["twilightforest:active"=true]'
    Invoke-Chat '/setblock 306 -59 308 twilightforest:locked_vanishing_block ["twilightforest:locked"=false]'
    Invoke-Chat '/setblock 308 -59 308 twilightforest:carminite_builder ["twilightforest:state"="builder_active"]'
    Invoke-Chat '/setblock 310 -59 308 twilightforest:built_block ["twilightforest:active"=true]'
    Invoke-Chat '/setblock 312 -59 308 twilightforest:carminite_builder ["twilightforest:state"="builder_timeout"]'
    Invoke-Chat '/setblock 316 -59 308 twilightforest:carminite_reactor ["twilightforest:active"=true]'
    Invoke-Chat '/setblock 324 -59 308 twilightforest:slider ["twilightforest:axis"="x"]'
    Invoke-Chat '/setblock 326 -59 308 twilightforest:slider ["twilightforest:axis"="z"]'
    Invoke-Chat '/setblock 328 -59 308 twilightforest:ghast_trap ["twilightforest:active"=true]'
    Invoke-Teleport '/tp @s 314 -56 299 0 25'
    Start-Sleep -Seconds 3
    Save-Screenshot 'blocks' | Out-Null
    Invoke-Teleport '/tp @s 314 -57 313 180 20'
    Start-Sleep -Seconds 2
    Save-Screenshot 'blocks_active' | Out-Null

    # the items in the hotbar (the HUD shows their icons)
    Invoke-Chat '/clear @s'
    $items = @('twilightforest:tower_key', 'twilightforest:borer_essence', 'twilightforest:carminite', 'twilightforest:ur_ghast_spawn_egg',
        'twilightforest:carminite_ghastguard_spawn_egg', 'twilightforest:carminite_ghastling_spawn_egg', 'twilightforest:carminite_golem_spawn_egg',
        'twilightforest:carminite_broodling_spawn_egg', 'twilightforest:towerwood_borer_spawn_egg')
    foreach ($i in $items) { Invoke-Chat "/give @s $i" }
    Invoke-Teleport '/tp @s 306 -58 315 0 5'
    Start-Sleep -Seconds 2
    Save-Screenshot 'items' | Out-Null

    # the mobs, without AI, in a row
    Invoke-Chat '/fill 290 -60 330 340 -40 360 air'
    $mobs = @(@('twilightforest:carminite_golem', 300), @('twilightforest:carminite_broodling', 304), @('twilightforest:towerwood_borer', 307),
        @('twilightforest:carminite_ghastling', 310), @('twilightforest:carminite_ghastguard', 318))
    foreach ($m in $mobs) { Invoke-Chat "/summon $($m[0]) $($m[1]) -59 340" }
    Invoke-Chat '/effect @e[family=twilightforest] slowness 600 255 true'
    Invoke-Teleport '/tp @s 309 -56 328 0 10'
    Start-Sleep -Seconds 3
    Save-Screenshot 'mobs' | Out-Null
    Invoke-Chat '/kill @e[family=twilightforest]'

    # the Ur-Ghast
    Invoke-Chat '/summon twilightforest:ur_ghast 310 -50 380'
    Invoke-Teleport '/tp @s 310 -50 345 0 0'
    Start-Sleep -Seconds 4
    Save-Screenshot 'urghast' | Out-Null
    Invoke-Chat '/kill @e[type=twilightforest:ur_ghast]'
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
}
Save-Screenshot 'selftest' | Out-Null
