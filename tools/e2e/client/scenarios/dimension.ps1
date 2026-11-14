# F4 (dimension): a real player lights a Gloaming portal with a thrown diamond, travels into the
# Gloaming Forest (portal overlay with the client mod, camera fade on a vanilla client), sees the
# dimension render from y -32 to 255 under its fixed night (time 13000), and comes back the same
# way, in survival, through the return portal the teleporter built on the generated ground.
# The overlay / fade is measured: frames 0.5 s apart are compared with a reference frame of the
# same view without it, which gives its opacity over time, next to upstream's (the portal sprite's
# alpha 3/4 x timer / 60).
# The fixed time is checked twice: the sky straight up is sampled 6 times over 20 s and must stay
# far darker than the overworld's noon sky (no flicker back to day), and in remote modes the host
# watches what it sends the client for the same 20 s (selftest dimension.clock_watch: every tick
# ends with the time of day 13000).
# The build range: the client's own dimension reads y -32..256 (the client mod logs it; for a
# vanilla client the host logs the DimensionDataPacket it was sent), and a stone pillar from y -32
# to 255 shows both ends: its top against the sky, its foot from a room cleared at the bottom of
# the world.
# Screenshots: out/dimension/{start,sky-overworld,portal-reference,portal-lit,portal-overlay-0.5s..2.5s,
# gloaming-arrival,sky-1..6,gloaming-up,gloaming-bottom,return-overlay-1s,return-overlay-2s,back-home}.png
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

# Mean colour of a screenshot (every 16th pixel), as @(r, g, b): the top 70 % of it (the hotbar
# and, in the portal, the pool's own blocks around the feet are left out).
function Get-MeanColour([string]$path) {
    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    try {
        $r = 0.0; $g = 0.0; $b = 0.0; $n = 0
        for ($y = 0; $y -lt [int]($bmp.Height * 0.7); $y += 16) { for ($x = 0; $x -lt $bmp.Width; $x += 16) { $c = $bmp.GetPixel($x, $y); $r += $c.R; $g += $c.G; $b += $c.B; $n++ } }
        return @(($r / $n), ($g / $n), ($b / $n))
    } finally { $bmp.Dispose() }
}

# How far a frame went from the reference towards the fade colour (0: none, 1: the colour itself).
$fadeColour = @(84.0, 10.0, 186.0) # the portal sprite's average (the camera fade's colour)
function Get-FadeAlpha($frame, $reference) {
    $num = 0.0; $den = 0.0
    for ($i = 0; $i -lt 3; $i++) {
        $d = $fadeColour[$i] - $reference[$i]
        $num += ($frame[$i] - $reference[$i]) * $d; $den += $d * $d
    }
    return [Math]::Round($num / [Math]::Max($den, 1.0), 2)
}

# The time stamp of a log line: the game's '[yyyy-MM-dd HH:mm:ss.fff ...' or the host's
# '[host] HH:mm:ss.fff ...' (today; the same machine's clock).
function Get-LineTime([string]$line) {
    $m = [regex]::Match($line, '(\d{4}-\d\d-\d\d )?(\d\d:\d\d:\d\d\.\d{3}) ')
    if (-not $m.Success) { return $null }
    $day = if ($m.Groups[1].Success) { $m.Groups[1].Value.Trim() } else { (Get-Date).ToString('yyyy-MM-dd') }
    return [datetime]::ParseExact("$day $($m.Groups[2].Value)", 'yyyy-MM-dd HH:mm:ss.fff', [Globalization.CultureInfo]::InvariantCulture)
}

# Frames 0.5 s apart while the player stands in a portal, with the time each was taken.
function Save-OverlayFrames([string]$prefix, [string[]]$labels) {
    $frames = @()
    foreach ($t in $labels) {
        Start-Sleep -Milliseconds 500
        $before = Get-Date
        $shot = Save-Screenshot "$prefix-$t"
        $frames += [pscustomobject]@{ Label = $t; Path = $shot; Time = $before.AddTicks(((Get-Date) - $before).Ticks / 2) }
    }
    return $frames
}

# The opacity of the overlay frames, from when the server started the fade (or the client mod its
# overlay), next to upstream's: 0.75 x timer / 60 with the timer one tick in when it starts.
function Write-OverlayRamp($frames, $reference, [string]$startPattern, [int]$since) {
    $start = Get-LeviLog -Since $since | Select-String -Pattern $startPattern | Select-Object -First 1
    $t0 = if ($start) { Get-LineTime $start.Line } else { $null }
    $rows = @()
    foreach ($f in $frames) {
        $c = Get-MeanColour $f.Path
        $alpha = Get-FadeAlpha $c $reference
        $t = if ($t0) { [Math]::Round(($f.Time - $t0).TotalSeconds, 2) } else { $null }
        $upsrc = if ($null -ne $t) { [Math]::Round(0.75 * [Math]::Min(1.0, ($t * 20.0 + 1.0) / 60.0), 2) } else { $null }
        Write-Step ("{0}: mean colour {1:N0} {2:N0} {3:N0}, opacity {4} at {5} s (upstream {6})" -f $f.Label, $c[0], $c[1], $c[2], $alpha, $t, $upsrc)
        $rows += [pscustomobject]@{ Label = $f.Label; Alpha = $alpha; T = $t; Upsrc = $upsrc }
    }
    return $rows
}

Wait-LogLine "${hostPrefix}GloamingForest world ready" 120 | Out-Null
if ($script:Mode -eq 'local') {
    $r = Invoke-SelfTest 'dimension.registered'
    if ($r.Fail -gt 0) { throw 'dimension.registered failed' }
}
Save-Screenshot 'start' | Out-Null

# the overworld's noon sky straight up: the reference for the Gloaming night
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
Invoke-Chat '/effect @s resistance 600 4 true'
Invoke-Chat '/effect @s fire_resistance 600 0 true'
Start-Sleep -Seconds 1
# the reference for the overlay: the view from the spot the player will stand on in the portal,
# while the pool is still water (the eyes are above it)
Invoke-Teleport '/tp @s 101.6 -61 103.6 0 20'
Start-Sleep -Milliseconds 1500
$reference = Get-MeanColour (Save-Screenshot 'portal-reference')
Write-Step ("reference view: mean colour {0:N0} {1:N0} {2:N0}" -f $reference[0], $reference[1], $reference[2])

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
$frames = Save-OverlayFrames 'portal-overlay' @('0.5s', '1s', '1.5s', '2s', '2.5s')
# the client mod draws upstream's PortalOverlay with F12's HUD layers ('native'); the server sends the
# camera fade only to a player without it (vanilla clients)
$native = $false
if ($script:Mode -ne 'remote-vanilla') {
    $overlayLine = Wait-LogLine '^(?!\[host\]).*client portal overlay: ' 10 -Since $mark
    Write-Step $overlayLine
    $native = $overlayLine -match 'overlay: native'
}
if (-not $native) {
    Wait-LogLine "${hostPrefix}dimension: portal fade shown to" 10 -Since $mark | Out-Null
} elseif (Get-LeviLog -Since $mark | Select-String -Pattern 'dimension: portal fade shown to' -Quiet) {
    throw 'the server sent the camera fade to a client that draws the portal overlay itself'
}
$trip = Wait-LogLine "${hostPrefix}dimension: .* travelled from minecraft:overworld to gloamingforest:gloaming_forest at (\S+) (\S+) (\S+)" 60 -Since $mark
$startPattern = if ($native) { '^(?!\[host\]).*client portal overlay: native' } else { 'dimension: portal fade shown to' }
$rampRows = Write-OverlayRamp $frames $reference $startPattern $mark
# Bedrock's fade grows to opaque only: it grows at upstream's rate and is upstream's 0.75 at the trip, opaque
# after it. Before 2.5 s it must stay near upstream's ramp (round 3 had it at 0.65 after 0.5 s)
foreach ($row in $rampRows) {
    if ($null -ne $row.T -and $row.T -lt 2.6 -and $row.Alpha -gt $row.Upsrc + 0.25) {
        throw "the portal overlay is too opaque too early: $($row.Alpha) at $($row.T) s (upstream $($row.Upsrc))"
    }
}
$enter = Get-LeviLog -Since $mark | Select-String -Pattern 'enters the portal at' | Select-Object -First 1
$tEnter = if ($enter) { Get-LineTime $enter.Line } else { $null }
$tTrip  = Get-LineTime $trip
if ($tEnter -and $tTrip) { Write-Step ("the trip took {0:N2} s from the 60th tick in to the arrival" -f ($tTrip - $tEnter).TotalSeconds) }
$built = Get-LeviLog -Since $mark | Select-String -Pattern 'built a portal at (-?\d+) (-?\d+) (-?\d+) in gloamingforest:gloaming_forest' | Select-Object -Last 1
Start-Sleep -Seconds 5
Save-Screenshot 'gloaming-arrival' | Out-Null
# ambient_light 0.01: the client mod gives its own Gloaming Forest the brightness ramp the server's has
if ($script:Mode -ne 'remote-vanilla') {
    $ramp = Get-LeviLog -Since $mark | Select-String -Pattern 'brightness ramp now has ambient light' | Where-Object { $_.Line -notmatch '\[host\]' } | Select-Object -First 1
    if (-not $ramp) { throw 'the client did not give the Gloaming Forest its ambient light (no "brightness ramp now has ambient light" line)' }
    Write-Step $ramp.Line
}
# the build range the client built the dimension with
if ($script:Mode -ne 'remote-vanilla') {
    $range = Get-LeviLog -Since $mark | Select-String -Pattern 'client dimension gloamingforest:gloaming_forest: y (-?\d+)\.\.(-?\d+)' | Where-Object { $_.Line -notmatch '\[host\]' } | Select-Object -First 1
    if (-not $range) { throw 'the client mod did not log its Gloaming Forest build range' }
    Write-Step $range.Line
    if ($range.Matches[0].Groups[1].Value -ne '-32' -or $range.Matches[0].Groups[2].Value -ne '256') { throw "the client builds the Gloaming Forest at y $($range.Matches[0].Groups[1].Value)..$($range.Matches[0].Groups[2].Value), not -32..256" }
}
if ($script:Mode -ne 'local') {
    # what the host sent this client when it joined (DimensionDataPacket)
    $sent = Get-LeviLog | Select-String -Pattern '\[host\].*dimension definitions sent to a client give gloamingforest:gloaming_forest y (-?\d+)\.\.(-?\d+)' | Select-Object -Last 1
    if (-not $sent) { throw 'the host logged no dimension definitions sent to the client' }
    Write-Step $sent.Line
    if ($sent.Matches[0].Groups[1].Value -ne '-32' -or $sent.Matches[0].Groups[2].Value -ne '256') { throw "the client was sent y $($sent.Matches[0].Groups[1].Value)..$($sent.Matches[0].Groups[2].Value) for the Gloaming Forest, not -32..256" }
}

# the fixed time: 20 s under the Gloaming sky (and, in remote modes, the host's view of the clock it
# sends this client meanwhile)
$watchMark = Get-LogMark
if ($script:Mode -ne 'local') { Send-HostConsole 'gloamingforest selftest "dimension.clock_watch"' }
Invoke-Teleport '/tp @s ~ ~ ~ ~ -90'
$skies = @()
for ($i = 1; $i -le 6; $i++) {
    Start-Sleep -Seconds 3
    $skies += Get-SkyLuminance (Save-Screenshot "sky-$i")
}
Write-Step ("Gloaming sky luminance {0} (overworld noon {1})" -f ($skies -join ', '), $dayLum)
# (measured: the time-13000 sky straight up reads about 0.45 of the noon sky, a noon sky 1.0)
$m = $skies | Measure-Object -Maximum -Minimum
if ($m.Maximum -gt 0.6 * $dayLum) {
    throw "the Gloaming Forest sky turned bright (luminance $($m.Maximum) against $dayLum at noon): its time is not fixed at 13000"
}
if ($m.Maximum - $m.Minimum -gt 0.1 * $dayLum) {
    throw "the Gloaming Forest sky changed over 20 s (luminance $($m.Minimum)..$($m.Maximum)): its clock moves"
}
if ($script:Mode -ne 'local') {
    $watch = Wait-LogLine '\[host\].*SELFTEST (PASS|FAIL) dimension\.clock_watch' 90 -Since $watchMark
    Get-LeviLog -Since $watchMark | Select-String -Pattern '\[host\].*SELFTEST INFO dimension\.clock_watch' | ForEach-Object { Write-Step $_.Line }
    if ($watch -match 'SELFTEST FAIL') { throw "the host sent the client another time of day in the Gloaming Forest: $watch" }
    if (-not (Get-LeviLog -Since $watchMark | Select-String -Pattern 'time packets in 20 s' -Quiet)) {
        throw 'the host found no remote player in the Gloaming Forest to watch'
    }
}

# the build range: a stone pillar from y -32 to y 255 next to the arrival spot, glowstone at both ends
$tm = [regex]::Match($trip, 'at (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\s*$')
$ax = [int][Math]::Floor([double]::Parse($tm.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture))
$ay = [double]::Parse($tm.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture)
$az = [int][Math]::Floor([double]::Parse($tm.Groups[3].Value, [Globalization.CultureInfo]::InvariantCulture))
$px = $ax + 3; $pz = $az + 3
Invoke-Chat "/fill $px -32 $pz $px 255 $pz stone"
Invoke-Chat "/setblock $px 255 $pz glowstone"
Invoke-Chat "/setblock $px -32 $pz glowstone"
Start-Sleep -Seconds 2
Invoke-Teleport ('/tp @s {0} {1} {2} -45 -75' -f ($ax + 0.5), $ay, ($az + 0.5))
Start-Sleep -Seconds 2
Save-Screenshot 'gloaming-up' | Out-Null
# its foot: a room cleared at the bottom of the world around it (y -31..-26 on a stone floor at
# y -32; the pillar goes on from the ceiling), the player in a corner looking down at the
# glowstone at y -32 in the floor
Invoke-Chat ("/fill {0} -32 {1} {2} -32 {3} stone" -f ($px - 2), ($pz - 2), ($px + 2), ($pz + 2))
Invoke-Chat ("/fill {0} -31 {1} {2} -26 {3} air" -f ($px - 2), ($pz - 2), ($px + 2), ($pz + 2))
Invoke-Chat "/setblock $px -32 $pz glowstone"
Invoke-Teleport ('/tp @s {0} -31 {1} -45 35' -f ($px - 1.5), ($pz - 1.5))
Start-Sleep -Seconds 3
Save-Screenshot 'gloaming-bottom' | Out-Null
Invoke-Chat "/fill $px -31 $pz $px 255 $pz air"

# and back through the return portal, in survival (the trip, the overlay / fade and the delay of a
# survival player: playersGloamingPortalDefaultDelay)
if (-not $built) { throw 'no "built a portal" line: the return portal is unknown' }
$x = [int]$built.Matches[0].Groups[1].Value; $y = [int]$built.Matches[0].Groups[2].Value; $z = [int]$built.Matches[0].Groups[3].Value
Invoke-Chat '/gamemode survival'
$mark = Get-LogMark
Invoke-Teleport ('/tp @s {0} {1} {2} 0 20' -f ($x + 0.6), ($y + 0.1), ($z + 0.6))
$back = Save-OverlayFrames 'return-overlay' @('1s', '2s')
Wait-LogLine $startPattern 10 -Since $mark | Out-Null
Wait-LogLine "${hostPrefix}dimension: .* travelled from gloamingforest:gloaming_forest to minecraft:overworld" 60 -Since $mark | Out-Null
$enterBack = Get-LeviLog -Since $mark | Select-String -Pattern 'enters the portal at' | Select-Object -First 1
$fadeBack  = Get-LeviLog -Since $mark | Select-String -Pattern $startPattern | Select-Object -First 1
$tLeave    = if ($enterBack) { Get-LineTime $enterBack.Line } else { $null }
$tIn       = if ($fadeBack) { Get-LineTime $fadeBack.Line } else { $null }
if ($tLeave -and $tIn) {
    $delay = ($tLeave - $tIn).TotalSeconds
    Write-Step ("the way back left {0:N2} s after the player stepped in (survival delay: 3 s)" -f $delay)
    if ($delay -lt 2.5) { throw "the way back left after $delay s: the survival delay of 60 ticks was not kept" }
} else {
    Write-Step 'warning: no time stamps on the portal lines: the delay of the way back is not measured'
}
foreach ($f in $back) { $c = Get-MeanColour $f.Path; Write-Step ("{0}: mean colour {1:N0} {2:N0} {3:N0}" -f $f.Label, $c[0], $c[1], $c[2]) }
Start-Sleep -Seconds 4
Save-Screenshot 'back-home' | Out-Null
if (-not (Get-GameProcess)) { throw 'the game closed during the trip' }
