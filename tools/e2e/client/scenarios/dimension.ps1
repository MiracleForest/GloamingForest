# F4 (dimension): a real player lights a Twilight portal with a thrown diamond, travels into the
# Twilight Forest (portal overlay with the client mod, camera fade on a vanilla client), sees the
# dimension render from y -32 to 255 under its fixed night (time 13000), and comes back.
# Screenshots: out/dimension/{start,portal-lit,portal-overlay-1s,portal-overlay-2s,twilight-arrival,twilight-up,twilight-down,back-home}.png
$hostPrefix = if ($script:Mode -eq 'local') { '' } else { '\[host\].*' }
Wait-LogLine "${hostPrefix}GloamingForest world ready" 120 | Out-Null
if ($script:Mode -eq 'local') {
    $r = Invoke-SelfTest 'dimension.registered'
    if ($r.Fail -gt 0) { throw 'dimension.registered failed' }
}
Save-Screenshot 'start' | Out-Null

# the pool: 2x2 water at x 101..102, z 103..104 in the grass layer (y -61), a grass rim with short
# grass on its sides (not the corners)
Invoke-Teleport '/tp @s 100.5 -60 100.5 0 30'
Invoke-Chat '/fill 100 -61 102 103 -61 105 grass_block'
Invoke-Chat '/fill 101 -60 102 102 -60 102 short_grass'
Invoke-Chat '/fill 101 -60 105 102 -60 105 short_grass'
Invoke-Chat '/fill 100 -60 103 100 -60 104 short_grass'
Invoke-Chat '/fill 103 -60 103 103 -60 104 short_grass'
Invoke-Chat '/fill 101 -61 103 102 -61 104 water'
Invoke-Chat '/clear @s'
Invoke-Chat '/give @s diamond 1'
# the lighting strikes everything within 3 blocks (destructivePortalLightning)
Invoke-Chat '/effect @s resistance 300 4 true'
Invoke-Chat '/effect @s fire_resistance 300 0 true'
Start-Sleep -Seconds 1

# throw the diamond into the pool from its north rim, looking south and down
Invoke-Teleport '/tp @s 101.5 -60 102.4 0 60'
$mark = Get-LogMark
Focus-Game
Press-Key Q
try {
    Wait-LogLine "${hostPrefix}dimension: portal lit at" 20 -Since $mark | Out-Null
} catch {
    # a short throw lands on the rim: once more from closer
    Invoke-Chat '/give @s diamond 1'
    Invoke-Teleport '/tp @s 101.5 -60 102.8 0 75'
    Focus-Game
    Press-Key Q
    Wait-LogLine "${hostPrefix}dimension: portal lit at" 20 -Since $mark | Out-Null
}
Invoke-Teleport '/tp @s 101.5 -60 99.5 0 25'
Start-Sleep -Seconds 2
Save-Screenshot 'portal-lit' | Out-Null

# stand in the portal: the overlay / fade grows over 3 s, then the trip
$mark = Get-LogMark
Invoke-Teleport '/tp @s 101.6 -61 103.6 0 20'
Start-Sleep -Milliseconds 1000
Save-Screenshot 'portal-overlay-1s' | Out-Null
Start-Sleep -Milliseconds 1300
Save-Screenshot 'portal-overlay-2s' | Out-Null
if ($script:Mode -eq 'local') {
    Wait-LogLine 'client portal overlay: ' 10 -Since $mark | Out-Null
} else {
    Wait-LogLine '\[host\].*dimension: portal fade shown to' 10 -Since $mark | Out-Null
}
$trip = Wait-LogLine "${hostPrefix}dimension: .* travelled from minecraft:overworld to twilightforest:twilight_forest at (\S+) (\S+) (\S+)" 60 -Since $mark
$built = Get-LeviLog -Since $mark | Select-String -Pattern 'built a portal at (-?\d+) (-?\d+) (-?\d+) in twilightforest:twilight_forest' | Select-Object -Last 1
Start-Sleep -Seconds 5
Save-Screenshot 'twilight-arrival' | Out-Null

# the build range: a stone pillar from y -32 to y 255 next to the player
Invoke-Chat '/fill ~3 -32 ~3 ~3 255 ~3 stone'
Invoke-Chat '/setblock ~3 255 ~3 glowstone'
Invoke-Chat '/setblock ~3 -32 ~3 glowstone'
Start-Sleep -Seconds 2
Invoke-Teleport '/tp @s ~ ~ ~ -45 -75'
Start-Sleep -Seconds 2
Save-Screenshot 'twilight-up' | Out-Null
Invoke-Teleport '/tp @s ~ ~ ~ -45 75'
Start-Sleep -Seconds 2
Save-Screenshot 'twilight-down' | Out-Null
Invoke-Chat '/fill ~3 -32 ~3 ~3 255 ~3 air'

# and back through the return portal
if ($built) {
    $x = [int]$built.Matches[0].Groups[1].Value; $y = [int]$built.Matches[0].Groups[2].Value; $z = [int]$built.Matches[0].Groups[3].Value
    # the Twilight Forest is still void (F5): the portal there has no floor, so the way back is
    # taken in creative mode (playersTfPortalCreativeDelay: 1 tick) before the player falls through
    Invoke-Chat '/gamemode creative'
    $mark = Get-LogMark
    Invoke-Teleport ('/tp @s {0} {1} {2} 0 20' -f ($x + 0.6), ($y + 0.4), ($z + 0.6))
    Wait-LogLine "${hostPrefix}dimension: .* travelled from twilightforest:twilight_forest to minecraft:overworld" 60 -Since $mark | Out-Null
    Start-Sleep -Seconds 4
    Save-Screenshot 'back-home' | Out-Null
    Invoke-Chat '/gamemode survival'
} else {
    Write-Step 'warning: no return portal line; the way back was not tested'
}
if (-not (Get-GameProcess)) { throw 'the game closed during the trip' }
