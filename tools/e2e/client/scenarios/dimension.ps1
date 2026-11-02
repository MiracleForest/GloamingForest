# F4 (dimension): a real player lights a Twilight portal with a thrown diamond, travels into the
# Twilight Forest (portal overlay with the client mod, camera fade on a vanilla client), sees the
# dimension render from y -32 to 255 under its fixed night (time 13000), and comes back.
# The fixed time is checked twice: the sky straight up is sampled 6 times over 20 s and must stay
# far darker than the overworld's noon sky (no flicker back to day), and in remote modes the host
# watches what it sends the client for the same 20 s (selftest dimension.clock_watch: every tick
# ends with the time of day 13000).
# Screenshots: out/dimension/{start,sky-overworld,portal-lit,portal-overlay-0.5s..2s,twilight-arrival,sky-1..6,twilight-up,twilight-down,back-home}.png
$hostPrefix = if ($script:Mode -eq 'local') { '' } else { '\[host\].*' }

# Mean luminance (0..255) of the middle of a screenshot (the sky when looking straight up; the
# hotbar and the edges are left out).
function Get-SkyLuminance([string]$path) {
    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    try {
        $sum = 0.0; $n = 0
        for ($y = [int]($bmp.Height * 0.15); $y -lt [int]($bmp.Height * 0.6); $y += 6) {
            for ($x = [int]($bmp.Width * 0.3); $x -lt [int]($bmp.Width * 0.7); $x += 6) {
                $c = $bmp.GetPixel($x, $y)
                $sum += 0.2126 * $c.R + 0.7152 * $c.G + 0.0722 * $c.B; $n++
            }
        }
        return [Math]::Round($sum / [Math]::Max($n, 1), 1)
    } finally { $bmp.Dispose() }
}
Wait-LogLine "${hostPrefix}GloamingForest world ready" 120 | Out-Null
if ($script:Mode -eq 'local') {
    $r = Invoke-SelfTest 'dimension.registered'
    if ($r.Fail -gt 0) { throw 'dimension.registered failed' }
}
Save-Screenshot 'start' | Out-Null

# the overworld's noon sky straight up: the reference for the Twilight night
Invoke-Teleport '/tp @s 100.5 -60 96.5 0 -90'
Start-Sleep -Seconds 2
$dayLum = Get-SkyLuminance (Save-Screenshot 'sky-overworld')
Write-Step "overworld sky luminance $dayLum"

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
# four frames 0.5 s apart, with their mean colour (how the overlay / fade grows)
foreach ($t in 'portal-overlay-0.5s', 'portal-overlay-1s', 'portal-overlay-1.5s', 'portal-overlay-2s') {
    Start-Sleep -Milliseconds 500
    $shot = Save-Screenshot $t
    $bmp = [System.Drawing.Bitmap]::FromFile($shot)
    try {
        $r = 0.0; $g = 0.0; $b = 0.0; $n = 0
        for ($y = 0; $y -lt $bmp.Height; $y += 16) { for ($x = 0; $x -lt $bmp.Width; $x += 16) { $c = $bmp.GetPixel($x, $y); $r += $c.R; $g += $c.G; $b += $c.B; $n++ } }
        Write-Step ("{0}: mean colour {1:N0} {2:N0} {3:N0}" -f $t, ($r / $n), ($g / $n), ($b / $n))
    } finally { $bmp.Dispose() }
}
if ($script:Mode -eq 'local') {
    Wait-LogLine 'client portal overlay: ' 10 -Since $mark | Out-Null
} else {
    Wait-LogLine '\[host\].*dimension: portal fade shown to' 10 -Since $mark | Out-Null
}
$trip = Wait-LogLine "${hostPrefix}dimension: .* travelled from minecraft:overworld to twilightforest:twilight_forest at (\S+) (\S+) (\S+)" 60 -Since $mark
$built = Get-LeviLog -Since $mark | Select-String -Pattern 'built a portal at (-?\d+) (-?\d+) (-?\d+) in twilightforest:twilight_forest' | Select-Object -Last 1
Start-Sleep -Seconds 5
Save-Screenshot 'twilight-arrival' | Out-Null
# ambient_light 0.01: the client mod gives its own Twilight Forest the brightness ramp the server's has
if ($script:Mode -ne 'remote-vanilla') {
    $ramp = Get-LeviLog -Since $mark | Select-String -Pattern 'brightness ramp now has ambient light' | Where-Object { $_.Line -notmatch '\[host\]' } | Select-Object -First 1
    if (-not $ramp) { throw 'the client did not give the Twilight Forest its ambient light (no "brightness ramp now has ambient light" line)' }
    Write-Step $ramp.Line
}

# the fixed time: 20 s under the Twilight sky (and, in remote modes, the host's view of the clock it
# sends this client meanwhile)
$watchMark = Get-LogMark
if ($script:Mode -ne 'local') { Send-HostConsole 'twilightforest selftest "dimension.clock_watch"' }
Invoke-Teleport '/tp @s ~ ~ ~ ~ -90'
$skies = @()
for ($i = 1; $i -le 6; $i++) {
    Start-Sleep -Seconds 3
    $skies += Get-SkyLuminance (Save-Screenshot "sky-$i")
}
Write-Step ("Twilight sky luminance {0} (overworld noon {1})" -f ($skies -join ', '), $dayLum)
# (measured: the time-13000 sky straight up reads about 0.45 of the noon sky, a noon sky 1.0)
$m = $skies | Measure-Object -Maximum -Minimum
if ($m.Maximum -gt 0.6 * $dayLum) {
    throw "the Twilight Forest sky turned bright (luminance $($m.Maximum) against $dayLum at noon): its time is not fixed at 13000"
}
if ($m.Maximum - $m.Minimum -gt 0.1 * $dayLum) {
    throw "the Twilight Forest sky changed over 20 s (luminance $($m.Minimum)..$($m.Maximum)): its clock moves"
}
if ($script:Mode -ne 'local') {
    $watch = Wait-LogLine '\[host\].*SELFTEST (PASS|FAIL) dimension\.clock_watch' 90 -Since $watchMark
    Get-LeviLog -Since $watchMark | Select-String -Pattern '\[host\].*SELFTEST INFO dimension\.clock_watch' | ForEach-Object { Write-Step $_.Line }
    if ($watch -match 'SELFTEST FAIL') { throw "the host sent the client another time of day in the Twilight Forest: $watch" }
    if (-not (Get-LeviLog -Since $watchMark | Select-String -Pattern 'time packets in 20 s' -Quiet)) {
        throw 'the host found no remote player in the Twilight Forest to watch'
    }
}

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
