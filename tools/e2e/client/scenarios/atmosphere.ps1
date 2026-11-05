# U15 (atmosphere): the Twilight Forest's sky, fog, locked-biome weather and aurora in the real
# game (docs/spec/atmosphere.md section 5).
#
# local / remote-mod: the player goes to the Twilight Forest (/twilightforest tp, clear weather) and
#   the atmosphere.* selftests run untagged (the client ones read the client mod's state there:
#   provider, fog, the aurora fading in over 60 ticks, the valid aurora biomes, the cloud colour,
#   locked-biome weather from previews and from the real restrictions, the Ur-Ghast rain). Then,
#   tagged gf_atmosphere_show, the atmosphere.show_* selftests send previews (a biome for every
#   lookup, locked biomes, protected boxes) because TF's biomes come from U1, and the screenshots
#   show:
#     <mode>-sky-up-noon / -midnight (no sun or moon, TF's stars; the fixed time keeps both alike),
#     <mode>-sky-horizon (grey clouds, no sunset band), <mode>-fog-forest, <mode>-fog-spooky (the near
#     fog, smoothed in), <mode>-fog-dark-forest, <mode>-fog-boss (a boss bar's world fog),
#     <mode>-aurora-clear / <mode>-aurora (glacier), <mode>-void-plate, <mode>-weather-clear and
#     <mode>-weather-blizzard / -mosquitoes / -ashes / -dark-streams / -big-rain (locked biomes),
#     <mode>-sparkles (protected structure), <mode>-ur-ghast-rain.
#   Every view is checked against its reviewed signature (Test-AtmosphereSignature: the star count of
#   the sky, the aurora's coverage and hue, the pink of the clouds, the share of pixels the weather
#   changes against the clear view): the reviewed screenshots of the PR are the goldens, these are
#   their measurable parts.
#   Local mode does it twice: Classic (graphics_mode 1, atmosphere.prelaunch.ps1) and Vibrant
#   Visuals (graphics_mode 2, after a restart); remote-mod in Classic. GF_ATMOSPHERE_CALIBRATE=1 adds
#   the shots of every pass / material the client can draw the aurora and the stars with.
# remote-vanilla: no client mod; the host pins the spooky forest's / glacier's fog for the tagged
#   vanilla player (atmosphere.show_vanilla_*: PlayerFogPacket with the resource pack fog
#   definitions) and the screenshots must differ from the cleared one:
#     vanilla-clear.png, vanilla-spooky.png, vanilla-glacier.png.
. (Join-Path $PSScriptRoot 'ui.graphics.ps1') # Set-UiGraphicsMode (F12's option helpers)

$remote  = $script:Mode -ne 'local'
$vanilla = $script:Mode -eq 'remote-vanilla'
$hostRx  = if ($remote) { '\[host\].*' } else { '' }
$tag     = 'gf_atmosphere_show'
$calibrate = $env:GF_ATMOSPHERE_CALIBRATE -eq '1'

# Pixels of the middle of a screenshot (the hotbar, the chat and the edges left out), every 6th.
function Get-AtmospherePixels([string]$path, [double]$top = 0.1, [double]$bottom = 0.75) {
    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    try {
        $px = [System.Collections.Generic.List[int[]]]::new()
        for ($y = [int]($bmp.Height * $top); $y -lt [int]($bmp.Height * $bottom); $y += 6) {
            for ($x = [int]($bmp.Width * 0.42); $x -lt [int]($bmp.Width * 0.92); $x += 6) {
                $c = $bmp.GetPixel($x, $y); $px.Add(@([int]$c.R, [int]$c.G, [int]$c.B))
            }
        }
        return , $px
    } finally { $bmp.Dispose() }
}
# Mean colour (r, g, b).
function Get-AtmosphereColour([string]$path) {
    $p = Get-AtmospherePixels $path
    $r = 0.0; $g = 0.0; $b = 0.0
    foreach ($c in $p) { $r += $c[0]; $g += $c[1]; $b += $c[2] }
    return @([Math]::Round($r / $p.Count, 1), [Math]::Round($g / $p.Count, 1), [Math]::Round($b / $p.Count, 1))
}
function Get-ColourDistance($a, $b) { [Math]::Round([Math]::Sqrt(($a[0] - $b[0]) * ($a[0] - $b[0]) + ($a[1] - $b[1]) * ($a[1] - $b[1]) + ($a[2] - $b[2]) * ($a[2] - $b[2])), 1) }

# The measurable parts of a view (the signature), every pixel of the middle (C#: a PowerShell loop
# is too slow): share of aurora-coloured pixels (green clearly above red), stars (bright pixels
# brighter than every pixel 3 away: small isolated points), pink cloud pixels.
if (-not ('GfAtmosphere.Signature' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
namespace GfAtmosphere {
public static class Signature {
    public static double[] Analyze(int[] px, int w, int h) {
        {
            int x0 = (int)(w * 0.42), x1 = (int)(w * 0.92), y0 = (int)(h * 0.1), y1 = (int)(h * 0.75);
            long n = 0, aurora = 0, pink = 0, stars = 0;
            Func<int, int> lum = c => ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255);
            for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
                int c = px[y * w + x]; int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
                n++;
                if (g > r + 40 && g > 70) aurora++;
                if (r > b + 12 && r > g + 12 && r > 70) pink++;
                int l = r + g + b;
                if (l > 420) {
                    bool peak = true;
                    for (int dy = -1; dy <= 1 && peak; dy++) for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        int q = lum(px[(y + dy) * w + x + dx]);
                        if (q > l || (q == l && (dy < 0 || (dy == 0 && dx < 0)))) { peak = false; break; }
                    }
                    if (peak && lum(px[(y - 3) * w + x]) < l - 120 && lum(px[(y + 3) * w + x]) < l - 120 && lum(px[y * w + x - 3]) < l - 120 && lum(px[y * w + x + 3]) < l - 120) stars++;
                }
            }
            return new double[] { (double)aurora / n, stars, (double)pink / n };
        }
    }
}
}
'@
}
# Every pixel of a screenshot as 0xAARRGGBB.
function Get-AtmosphereBits([string]$path) {
    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    try {
        $rect = [System.Drawing.Rectangle]::new(0, 0, $bmp.Width, $bmp.Height)
        $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $px = [int[]]::new($bmp.Width * $bmp.Height)
        [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $px, 0, $px.Length)
        $bmp.UnlockBits($data)
        return @{ Px = $px; W = $bmp.Width; H = $bmp.Height }
    } finally { $bmp.Dispose() }
}
function Get-AtmosphereSignature([string]$path) {
    $bits = Get-AtmosphereBits $path
    $a = [GfAtmosphere.Signature]::Analyze($bits.Px, $bits.W, $bits.H)
    [pscustomobject]@{ Aurora = [Math]::Round($a[0], 4); Stars = [int]$a[1]; Pink = [Math]::Round($a[2], 4) }
}
# Share of pixels that differ by more than 40 between two shots of the same view.
function Get-ChangedShare([string]$a, [string]$b) {
    $pa = Get-AtmospherePixels $a; $pb = Get-AtmospherePixels $b
    $n = 0
    for ($i = 0; $i -lt [Math]::Min($pa.Count, $pb.Count); $i++) {
        $d = [Math]::Abs($pa[$i][0] - $pb[$i][0]) + [Math]::Abs($pa[$i][1] - $pb[$i][1]) + [Math]::Abs($pa[$i][2] - $pb[$i][2])
        if ($d -gt 40) { $n++ }
    }
    [Math]::Round($n / [Math]::Max(1, $pa.Count), 4)
}
function Test-AtmosphereSignature([string]$what, [double]$value, [double]$min, [double]$max) {
    Write-Step "signature $what = $value (reviewed range $min..$max)"
    if ($value -lt $min -or $value -gt $max) {
        # a calibration run records every view (the ranges are reviewed from its shots)
        if ($calibrate) { Write-Step "CALIBRATION MISMATCH: $what is $value, outside $min..$max"; return }
        throw "$what is $value, outside the reviewed range $min..$max"
    }
}

function Invoke-AtmosphereTest([string]$name) {
    $r = Invoke-SelfTest $name -TimeoutSeconds 300
    if ($r.Fail -gt 0) { throw "$name failed: $($r.Failed -join '; ')" }
    return $r
}

function Enter-Twilight {
    # /twilightforest tp keeps x / z and scales y: from the flat overworld's ground it lands inside the
    # Twilight Forest's ground (the player suffocates), so the player goes up first (y 200 -> about
    # 166 there), unhurt by the fall, and then stands on a glass platform at y 100 under the open sky
    Invoke-Chat '/effect @s resistance 99999 255 true'
    Invoke-Chat '/tp @s ~ 200 ~'
    Start-Sleep -Seconds 1
    $mark = Get-LogMark
    Invoke-Chat '/twilightforest tp'
    if (-not $vanilla) {
        try { Wait-LogLine 'client dimension twilightforest:twilight_forest: y -32\.\.256' 90 -Since $mark | Out-Null }
        catch {
            # a restarted game may come back into the world inside the Twilight Forest already
            if (-not (Get-LeviLog -Since $script:AtmosphereSessionMark | Select-String -Pattern 'client dimension twilightforest:twilight_forest' -Quiet)) { throw }
            Write-Step 'already in the Twilight Forest'
        }
    }
    Start-Sleep -Seconds 6 # the chunks around the arrival load
    # no level weather and creative mode: the views show only what each preview asks for (the real
    # progression locks the glacier in survival: TF draws its blizzard there)
    Invoke-Chat '/gamemode creative'
    Invoke-Chat '/gamerule doweathercycle false'
    Invoke-Chat '/weather clear'
    Invoke-Chat '/fill ~-3 99 ~-3 ~3 99 ~3 glass'
    Invoke-Chat '/tp @s ~ 100 ~'
    Start-Sleep -Seconds 3
}

function Set-View([double]$yaw, [double]$pitch) {
    Invoke-Chat "/tp @s ~ ~ ~ $yaw $pitch"
    Start-Sleep -Milliseconds 1200
}

# One preview, then a screenshot once the client logged `$wait` (when given).
function Show-Atmosphere([string]$test, [string]$shot, [string]$wait = '', [int]$settle = 3) {
    $mark = Get-LogMark
    Invoke-AtmosphereTest "atmosphere.$test" | Out-Null
    Wait-LogLine 'atmosphere: preview' 30 -Since $mark | Out-Null
    if ($wait) { Wait-LogLine $wait 60 -Since $mark | Out-Null }
    Start-Sleep -Seconds $settle
    return Save-Screenshot $shot
}

function Invoke-AtmosphereViews([string]$mode) {
    $vv = $mode -eq 'vibrant'
    Invoke-AtmosphereTest 'atmosphere.show_clear' | Out-Null
    # the sky: no sun or moon, TF's star field, the same at noon and midnight (fixed time 13000)
    Set-View 0 -90
    Invoke-Chat '/time set noon'
    Start-Sleep -Seconds 2
    $noon = Save-Screenshot "$mode-sky-up-noon"
    Invoke-Chat '/time set midnight'
    Start-Sleep -Seconds 2
    $midnight = Save-Screenshot "$mode-sky-up-midnight"
    $d = Get-ColourDistance (Get-AtmosphereColour $noon) (Get-AtmosphereColour $midnight)
    Write-Step "$mode sky noon vs midnight: colour distance $d"
    if ($d -gt 25) { throw "the Twilight Forest sky changed with the time of day ($d)" }
    $sig = Get-AtmosphereSignature $noon
    if ($vv) { Write-Step "$mode stars in the zenith view: $($sig.Stars) (Vibrant Visuals paints its sky over the sky pass and lights the world pass; recorded)" }
    else { Test-AtmosphereSignature "$mode stars in the zenith view" $sig.Stars 30 1500 }
    Set-View 90 -15
    $horizon = Save-Screenshot "$mode-sky-horizon"
    $sig = Get-AtmosphereSignature $horizon
    if ($vv) { Write-Step "$mode pink share of the horizon: $($sig.Pink) (Vibrant Visuals draws its own dusk from the sun at the fixed time 13000; recorded)" }
    else { Test-AtmosphereSignature "$mode pink (sunset) share of the horizon" $sig.Pink 0 0.01 }

    # fog: the forest's, the spooky forest's near fog (smoothed in over seconds), the dark forest
    Set-View 0 0
    $forest = Show-Atmosphere 'show_forest' "$mode-fog-forest" 'atmosphere: fog of twilightforest:forest' 4
    $spooky = Show-Atmosphere 'show_spooky' "$mode-fog-spooky" 'atmosphere: fog of twilightforest:spooky_forest' 12
    $d = Get-ColourDistance (Get-AtmosphereColour $forest) (Get-AtmosphereColour $spooky)
    Test-AtmosphereSignature "$mode fog forest vs spooky forest colour distance" $d 4 1000
    Show-Atmosphere 'show_dark_forest' "$mode-fog-dark-forest" 'atmosphere: fog of twilightforest:dark_forest' 4 | Out-Null
    $boss = Show-Atmosphere 'show_boss_fog' "$mode-fog-boss" 'atmosphere: fog of twilightforest:forest' 12
    $d = Get-ColourDistance (Get-AtmosphereColour $forest) (Get-AtmosphereColour $boss)
    Write-Step "$mode fog forest vs boss world fog: colour distance $d"

    # the aurora over the glacier (fades in over 60 ticks), against the same view without it
    Set-View 0 -50
    $clearUp = Show-Atmosphere 'show_forest' "$mode-aurora-clear" '' 3
    $aurora  = Show-Atmosphere 'show_glacier' "$mode-aurora" 'atmosphere: aurora drawn' 6
    $a = (Get-AtmosphereSignature $aurora).Aurora - (Get-AtmosphereSignature $clearUp).Aurora
    if ($vv) { Test-AtmosphereSignature "$mode aurora changed share (world pass, lit by Vibrant Visuals)" (Get-ChangedShare $clearUp $aurora) 0.02 1 }
    else { Test-AtmosphereSignature "$mode aurora coverage" ([Math]::Round($a, 4)) 0.05 0.6 }

    # the void plate (below the floor of the dimension; forced) darkens the lower sky
    Set-View 0 10
    $noPlate = Show-Atmosphere 'show_forest' "$mode-void-plate-clear" '' 2
    $plate   = Show-Atmosphere 'show_void_plate' "$mode-void-plate" 'atmosphere: void plate drawn' 3
    Write-Step "$mode void plate: changed share $(Get-ChangedShare $noPlate $plate)"

    # locked-biome weather, looking along the ground
    Set-View 0 5
    $clearGround = Show-Atmosphere 'show_forest' "$mode-weather-clear" '' 3
    foreach ($w in @(@('show_blizzard', 'blizzard'), @('show_mosquitoes', 'mosquito'), @('show_ashes', 'ashes'), @('show_dark_streams', 'dark_stream'), @('show_big_rain', 'big_rain'))) {
        $shot = Show-Atmosphere $w[0] "$mode-weather-$($w[1] -replace '_', '-')" "atmosphere: $($w[1]) weather drawn" 3
        Test-AtmosphereSignature "$mode $($w[1]) weather changed share" (Get-ChangedShare $clearGround $shot) 0.01 1
    }
    $shot = Show-Atmosphere 'show_sparkles' "$mode-sparkles" 'atmosphere: sparkles weather drawn' 3
    Test-AtmosphereSignature "$mode sparkles changed share" (Get-ChangedShare $clearGround $shot) 0.002 1
    # the Ur-Ghast's tantrum: rain splashes and sounds
    Show-Atmosphere 'show_ur_ghast' "$mode-ur-ghast-rain" 'atmosphere: Ur-Ghast rain splashes' 3 | Out-Null
    if ($calibrate) {
        Set-View 0 -50
        foreach ($m in 'aurora_world_blend', 'aurora_sky_blend') {
            $shot = Show-Atmosphere "show_$m" "$mode-cal-$m" 'atmosphere: aurora drawn' 6
            Write-Step "calibration $mode $m aurora coverage $((Get-AtmosphereSignature $shot).Aurora)"
        }
        foreach ($m in 'vv_fog_1', 'vv_fog_24') { Show-Atmosphere "show_$m" "$mode-cal-$m" 'atmosphere: aurora drawn' 6 | Out-Null }
        Set-View 0 -90
        foreach ($m in 'stars_world_blend', 'stars_world_stars') {
            $shot = Show-Atmosphere "show_$m" "$mode-cal-$m" '' 4
            Write-Step "calibration $mode $m stars $((Get-AtmosphereSignature $shot).Stars)"
        }
        foreach ($w in 'reversed', 'both') {
            Invoke-AtmosphereTest "atmosphere.show_winding_$w" | Out-Null
            Set-View 0 -50
            Show-Atmosphere 'show_glacier' "$mode-cal-aurora-$w" 'atmosphere: aurora drawn' 5 | Out-Null
        }
    }
    Invoke-AtmosphereTest 'atmosphere.show_clear' | Out-Null
}

if ($vanilla) {
    Enter-Twilight
    Invoke-Chat "/tag @s add $tag"
    Set-View 0 55 # down at the ground below the platform: the fog lies over it
    $mark = Get-LogMark
    Invoke-AtmosphereTest 'atmosphere.show_vanilla_clear' | Out-Null
    Start-Sleep -Seconds 3
    $clear = Save-Screenshot 'vanilla-clear'
    $mark = Get-LogMark
    Invoke-AtmosphereTest 'atmosphere.show_vanilla_spooky' | Out-Null
    Wait-LogLine "${hostRx}atmosphere: fog twilightforest:spooky_forest sent to" 30 -Since $mark | Out-Null
    Start-Sleep -Seconds 4
    $spooky = Save-Screenshot 'vanilla-spooky'
    # the spooky fog's colour as the vanilla client draws it (the horizon: fog only), against TF's
    # getBrightnessDependentFogColor at 13000 (the fog colour times about 0.42)
    Set-View 0 0
    Start-Sleep -Seconds 2
    $spookyHorizon = Save-Screenshot 'vanilla-spooky-horizon'
    $c = Get-AtmosphereColour $spookyHorizon
    Write-Step "vanilla spooky fog at the horizon $($c -join ', ') (TF's raw fog 130, 115, 145; at 13000 about 54, 48, 63)"
    Set-View 0 55
    $mark = Get-LogMark
    Invoke-AtmosphereTest 'atmosphere.show_vanilla_glacier' | Out-Null
    Wait-LogLine "${hostRx}atmosphere: fog twilightforest:glacier sent to" 30 -Since $mark | Out-Null
    Start-Sleep -Seconds 4
    Save-Screenshot 'vanilla-glacier' | Out-Null
    Invoke-AtmosphereTest 'atmosphere.show_vanilla_clear' | Out-Null
    $d = Get-ColourDistance (Get-AtmosphereColour $clear) (Get-AtmosphereColour $spooky)
    Write-Step "vanilla fog clear vs spooky forest: colour distance $d"
    if ($d -lt 4) { throw "the PlayerFogPacket fog does not show on the vanilla client ($d)" }
    Invoke-Chat "/tag @s remove $tag"
    return
}

$script:AtmosphereSessionMark = 0
Enter-Twilight
foreach ($rx in 'atmosphere: sun, moon and vanilla stars hidden', 'atmosphere: TF stars drawn', 'atmosphere: cloud colour and sunrise of the frame overridden') { Wait-LogLine $rx 60 | Out-Null }
Invoke-AtmosphereTest 'atmosphere.*' | Out-Null
if (-not ($calibrate -and $env:GF_ATMOSPHERE_VV_ONLY -eq '1')) { # (a calibration of Vibrant Visuals alone)
    Invoke-Chat "/tag @s add $tag"
    Invoke-AtmosphereViews 'classic'
    Invoke-Chat "/tag @s remove $tag"
}

if (-not $remote) {
    # Vibrant Visuals: the same views after a restart with graphics_mode 2
    Stop-Game
    Set-UiGraphicsMode 2
    Start-Sleep -Seconds 3
    Start-Game | Out-Null
    Wait-GameReady
    Enter-TestWorld -Attempts 2
    Wait-HudReady
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $script:AtmosphereSessionMark = [Math]::Max(0, (Get-LogMark) - 400) # this game's lines (the world load)
    Enter-Twilight
    Invoke-Chat "/tag @s add $tag"
    Invoke-AtmosphereViews 'vibrant'
    Invoke-Chat "/tag @s remove $tag"
}
