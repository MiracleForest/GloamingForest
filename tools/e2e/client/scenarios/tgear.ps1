# U11 (tgear) on the client: Travellers' Gear worn by a real player and TF's keys pressed.
#
# tgear.real_arm dresses the player (goggles with the item display holding a map, a compass, a clock and
# a moon dial, and red thread vision; a vest with gloves and haste; the wings with swap_hotbar; boots;
# a broken second vest in the hotbar) and fills the hotbar with stone, torches and red thread. Then:
# 1. the item display HUD (map with its background and decorations, compass position at the feet, clock
#    with its time.png icon, moon dial with moon.png): out/tgear/hud-item-display.png; the inventory with
#    the worn gear (gloves on the vest's arms, the belt on the wings) and the item icons (modifier
#    overlays, the broken vest): out/tgear/inventory.png;
# 2. the zoom key (Z) held: the client logs "tgear: zoom in", the server narrows the camera FOV to 0.3 (a
#    camera FOV instruction): out/tgear/zoom.png must differ from out/tgear/zoom-off.png by 5 % or more;
# 3. the map cycle key (C): the client sends tgear.cycle_map_slot;
# 4. red thread on the floor, round a corner and on a wall, and more behind a stone wall: seen through
#    the wall while holding red thread (the glow): out/tgear/red-thread.png; close up, holding stone (the
#    block model and its connections): out/tgear/red-thread-close.png;
# 5. the swap key (V): the belt takes the hotbar (tgear.real_check reads the belt's storage).
# Remote modes run arm / check on the host; a vanilla client presses nothing (no keys) and only proves
# that the gear, its lore and the red thread blocks load: out/tgear/{inventory,red-thread}.png.
$script:IsLocal = $script:Mode -eq 'local'
$hostPrefix = if ($script:IsLocal) { '' } else { '\[host\].*' }
Wait-LogLine "${hostPrefix}GloamingForest world ready" 120 | Out-Null
Wait-HudReady | Out-Null

function Get-ImageDiff([string]$a, [string]$b) {
    # the share of sampled pixels that differ (0 = the same picture)
    Add-Type -AssemblyName System.Drawing
    $x = [System.Drawing.Bitmap]::new($a); $y = [System.Drawing.Bitmap]::new($b)
    try {
        $n = 0; $d = 0
        for ($i = 0; $i -lt $x.Width; $i += 8) {
            for ($j = 0; $j -lt $x.Height; $j += 8) {
                $n++
                $p = $x.GetPixel($i, $j); $q = $y.GetPixel($i, $j)
                if ([Math]::Abs($p.R - $q.R) + [Math]::Abs($p.G - $q.G) + [Math]::Abs($p.B - $q.B) -gt 30) { $d++ }
            }
        }
        return $d / [Math]::Max(1, $n)
    } finally { $x.Dispose(); $y.Dispose() }
}

Invoke-Teleport '/tp @s 40.5 -60 40.5 0 10'
Invoke-Chat '/time set 1000'
Invoke-Chat '/fill 38 -61 38 46 -61 46 stone'
Invoke-Chat '/fill 38 -60 38 46 -55 46 air'
# a real map for the item display: an empty map used in the hand (the game fills it around the player);
# tgear.real_arm stores it in the goggles
Invoke-Chat '/replaceitem entity @s slot.weapon.mainhand 0 empty_map'
Start-Sleep -Milliseconds 800
Focus-Game
[BsE2E.Native]::MouseButton(1, $false); Start-Sleep -Milliseconds 120; [BsE2E.Native]::MouseButton(1, $true)
Start-Sleep -Seconds 2
$mark = Get-LogMark
$r = Invoke-SelfTest 'tgear.real_arm'
if ($r.Fail -gt 0) { throw 'tgear.real_arm failed' }
Wait-LogLine "${hostPrefix}tgear: armed " 20 | Out-Null
if ($script:Mode -ne 'remote-vanilla') {
    try { Wait-LogLine "${hostPrefix}tgear: storing .+'s map " 5 -Since $mark | Out-Null } catch { throw 'the empty map was not turned into a real map to store' }
}
# let the chat lines fade, then the HUD with the map widget (the map fills in as the wearer carries it)
Start-Sleep -Seconds 10
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
        Wait-LogLine "${hostPrefix}tgear: .+ zoom in \(fov" 10 -Since $mark | Out-Null
        Start-Sleep -Milliseconds 600
        $zoomShot = Save-Screenshot 'zoom'
    } finally { [BsE2E.Native]::Key([uint16]0x5A, $true) }
    Wait-LogLine 'tgear: zoom out' 10 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 600
    if ((Get-ClientScreen) -ne 'hud_screen') { Press-Key ESC; Start-Sleep -Milliseconds 600 }
    $offShot = Save-Screenshot 'zoom-off'
    $diff = Get-ImageDiff $zoomShot $offShot
    Write-Host "zoom vs zoom-off: $([Math]::Round($diff * 100, 1)) % of the pixels differ"
    if ($diff -lt 0.05) { throw "the zoom did not narrow the FOV: zoom.png and zoom-off.png differ by $([Math]::Round($diff * 100, 1)) % (< 5 %)" }

    # 3. map cycle key
    $mark = Get-LogMark
    Press-Key C
    Wait-LogLine 'tgear: map cycle key' 10 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 400
    if ((Get-ClientScreen) -ne 'hud_screen') { Press-Key ESC; Start-Sleep -Milliseconds 600 }
}

# 4. red thread: a line on the floor in front of the player (41..43, z 42), round the corner up the
#    pillar at 44 -60 42, and threads on the floor and a wall behind a stone wall (z 46)
Invoke-Chat '/fill 39 -60 44 43 -57 44 stone'
Invoke-Chat '/setblock 44 -60 42 stone'
Invoke-Chat '/setblock 41 -60 46 twilightforest:red_thread ["twilightforest:down"=true]'
Invoke-Chat '/setblock 42 -60 46 twilightforest:red_thread ["twilightforest:down"=true]'
Invoke-Chat '/setblock 43 -60 46 twilightforest:red_thread ["twilightforest:down"=true,"twilightforest:south"=true]'
Invoke-Chat '/setblock 43 -59 46 twilightforest:red_thread ["twilightforest:south"=true]'
Invoke-Chat '/setblock 40 -60 42 twilightforest:red_thread ["twilightforest:down"=true]'
Invoke-Chat '/setblock 41 -60 42 twilightforest:red_thread ["twilightforest:down"=true]'
Invoke-Chat '/setblock 42 -60 42 twilightforest:red_thread ["twilightforest:down"=true]'
Invoke-Chat '/setblock 43 -60 42 twilightforest:red_thread ["twilightforest:down"=true,"twilightforest:east"=true]'
Invoke-Chat '/setblock 43 -59 42 twilightforest:red_thread ["twilightforest:east"=true]'
Invoke-Teleport '/tp @s 41.5 -60 39.5 0 25'
Invoke-Chat '/replaceitem entity @s slot.weapon.mainhand 0 twilightforest:red_thread 32'
Start-Sleep -Seconds 2
Save-Screenshot 'red-thread' | Out-Null
# close up, no thread in hand (the block's own model, the connections at the block's light)
Invoke-Chat '/replaceitem entity @s slot.weapon.mainhand 0 stone 16'
Invoke-Teleport '/tp @s 41.5 -60 40.2 0 50'
Start-Sleep -Seconds 2
Save-Screenshot 'red-thread-close' | Out-Null

if ($script:Mode -ne 'remote-vanilla') {
    try { Wait-LogLine 'tgear: red thread drawn' 5 | Out-Null } catch { throw 'the client never drew a red thread (RedThreadRenderer)' }
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
