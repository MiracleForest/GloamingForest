# U11 (tgear) on the client: Travellers' Gear worn by a real player and TF's keys pressed.
#
# tgear.real_arm dresses the player (goggles with the item display holding a compass, a clock and a
# moon dial, and red thread vision; a vest with gloves and haste; the belt; boots) and fills the hotbar
# with stone, torches and red thread. Then:
# 1. the item display HUD (compass position, clock with its time.png icon, moon dial with moon.png):
#    out/tgear/hud-item-display.png; the inventory with the six items: out/tgear/inventory.png;
# 2. the zoom key (Z) held: the client logs "tgear: zoom in", the FOV narrows to 0.3:
#    out/tgear/zoom.png (and out/tgear/zoom-off.png after the release);
# 3. the map cycle key (C): the client sends tgear.cycle_map_slot;
# 4. red thread on the floor and a wall behind a stone wall, seen through the wall while holding red
#    thread: out/tgear/red-thread.png;
# 5. the swap key (V): the belt takes the hotbar (tgear.real_check reads the belt's storage).
# Remote modes run arm / check on the host; a vanilla client presses nothing (no keys) and only proves
# that the gear, its lore and the red thread blocks load: out/tgear/{inventory,red-thread}.png.
$script:IsLocal = $script:Mode -eq 'local'
$hostPrefix = if ($script:IsLocal) { '' } else { '\[host\].*' }
Wait-LogLine "${hostPrefix}GloamingForest world ready" 120 | Out-Null
Wait-HudReady | Out-Null

Invoke-Teleport '/tp @s 40.5 -60 40.5 0 10'
Invoke-Chat '/time set 1000'
Invoke-Chat '/fill 38 -61 38 46 -61 46 stone'
Invoke-Chat '/fill 38 -60 38 46 -55 46 air'
$r = Invoke-SelfTest 'tgear.real_arm'
if ($r.Fail -gt 0) { throw 'tgear.real_arm failed' }
Wait-LogLine "${hostPrefix}tgear: armed " 20 | Out-Null
Start-Sleep -Seconds 2
Save-Screenshot 'hud-item-display' | Out-Null

Open-Inventory
Save-Screenshot 'inventory' | Out-Null
Close-Screen

if ($script:Mode -ne 'remote-vanilla') {
    # 2. zoom: hold Z while the screenshot is taken
    Focus-Game
    $mark = Get-LogMark
    [BsE2E.Native]::Key([uint16]0x5A, $false)
    try {
        Wait-LogLine 'tgear: zoom in' 10 -Since $mark | Out-Null
        Start-Sleep -Milliseconds 600
        # Bedrock's own binding of Z opens a screen over the view: close it while Z stays held
        if ((Get-ClientScreen) -ne 'hud_screen') { Press-Key ESC; Start-Sleep -Milliseconds 600 }
        Save-Screenshot 'zoom' | Out-Null
    } finally { [BsE2E.Native]::Key([uint16]0x5A, $true) }
    Wait-LogLine 'tgear: zoom out' 10 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 400
    if ((Get-ClientScreen) -ne 'hud_screen') { Press-Key ESC; Start-Sleep -Milliseconds 600 }
    Save-Screenshot 'zoom-off' | Out-Null

    # 3. map cycle key
    $mark = Get-LogMark
    Press-Key C
    Wait-LogLine 'tgear: map cycle key' 10 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 400
    if ((Get-ClientScreen) -ne 'hud_screen') { Press-Key ESC; Start-Sleep -Milliseconds 600 }
}

# 4. red thread on the floor and a wall, behind a stone wall in front of the player
Invoke-Chat '/fill 39 -60 44 43 -57 44 stone'
Invoke-Chat '/setblock 41 -60 46 twilightforest:red_thread ["twilightforest:down"=true]'
Invoke-Chat '/setblock 42 -60 46 twilightforest:red_thread ["twilightforest:down"=true]'
Invoke-Chat '/setblock 43 -60 46 twilightforest:red_thread ["twilightforest:down"=true,"twilightforest:south"=true]'
Invoke-Chat '/setblock 43 -59 46 twilightforest:red_thread ["twilightforest:south"=true]'
Invoke-Chat '/setblock 41 -60 42 twilightforest:red_thread ["twilightforest:down"=true]'
Invoke-Chat '/setblock 42 -60 42 twilightforest:red_thread ["twilightforest:down"=true]'
Invoke-Teleport '/tp @s 41.5 -60 39.5 0 25'
Invoke-Chat '/item replace entity @s slot.weapon.mainhand 0 twilightforest:red_thread 32'
Start-Sleep -Seconds 2
Save-Screenshot 'red-thread' | Out-Null

if ($script:Mode -ne 'remote-vanilla') {
    # 5. the swap key with stone and torches back in the hotbar
    $r = Invoke-SelfTest 'tgear.real_arm'
    if ($r.Fail -gt 0) { throw 'tgear.real_arm (again) failed' }
    Start-Sleep -Seconds 1
    $mark = Get-LogMark
    Focus-Game
    if ((Get-ClientScreen) -ne 'hud_screen') { Press-Key ESC; Start-Sleep -Milliseconds 600 }
    Press-Key V
    Wait-LogLine 'tgear: swap hotbar key' 10 -Since $mark | Out-Null
    Wait-LogLine "${hostPrefix}tgear: .+ swapped the hotbar" 10 -Since $mark | Out-Null
    Save-Screenshot 'swapped' | Out-Null
    $r = Invoke-SelfTest 'tgear.real_check'
    if ($r.Fail -gt 0) { throw 'tgear.real_check failed: the swap key did not move the hotbar into the belt' }
}

Invoke-Chat '/clear @s'
