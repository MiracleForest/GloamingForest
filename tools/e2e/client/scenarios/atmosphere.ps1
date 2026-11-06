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
#     fog, smoothed in) and <mode>-fog-spooky-up (the stars unfogged over it), <mode>-fog-dark-forest,
#     <mode>-fog-boss (a boss bar's world fog),
#     <mode>-aurora-clear / <mode>-aurora (glacier), <mode>-void-plate, <mode>-weather-clear and
#     <mode>-weather-blizzard / -mosquitoes / -ashes / -dark-streams / -big-rain (locked biomes),
#     <mode>-sparkles (protected structure), <mode>-ur-ghast-rain.
#   Every view is checked against its reviewed signature in both graphics modes (Test-AtmosphereSignature:
#   the star count of the sky and over the spooky fog, no stars below the horizon over the void
#   plate, the aurora's change (green over red, lighter: a glow), no pink sunset band, the share of
#   pixels the weather changes against the clear view and the blizzard / sparkles lighter than it):
#   the reviewed screenshots of the PR are the goldens, these are their measurable parts. Then
#   <mode>-aurora-biomes-editor: TFClientConfig's "Edit Biomes" list editor.
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
if (-not ('GfAtmosphere.Signature2' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
namespace GfAtmosphere {
public static class Signature2 {
    public static double[] Analyze(int[] px, int w, int h) { return AnalyzeRegion(px, w, h, 0.42, 0.92, 0.1, 0.75); }
    // The mean change (r, g, b) of the pixels that differ by more than 40 between two shots of the
    // same view, their share, and the mean lightness of those pixels before and after.
    public static double[] Delta(int[] a, int[] b, int w, int h) {
        int x0 = (int)(w * 0.42), x1 = (int)(w * 0.92), y0 = (int)(h * 0.1), y1 = (int)(h * 0.75);
        long n = 0, changed = 0; double dr = 0, dg = 0, db = 0, la = 0, lb = 0;
        for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
            int ca = a[y * w + x], cb = b[y * w + x];
            int ra = (ca >> 16) & 255, ga = (ca >> 8) & 255, ba = ca & 255, rb = (cb >> 16) & 255, gb = (cb >> 8) & 255, bb = cb & 255;
            n++;
            if (Math.Abs(ra - rb) + Math.Abs(ga - gb) + Math.Abs(ba - bb) <= 40) continue;
            changed++; dr += rb - ra; dg += gb - ga; db += bb - ba; la += ra + ga + ba; lb += rb + gb + bb;
        }
        if (changed == 0) return new double[] { 0, 0, 0, 0, 0, 0 };
        return new double[] { dr / changed, dg / changed, db / changed, (double)changed / n, la / changed / 3, lb / changed / 3 };
    }
    public static double[] AnalyzeRegion(int[] px, int w, int h, double fx0, double fx1, double fy0, double fy1) {
        {
            int x0 = (int)(w * fx0), x1 = (int)(w * fx1), y0 = (int)(h * fy0), y1 = (int)(h * fy1);
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
    $a = [GfAtmosphere.Signature2]::Analyze($bits.Px, $bits.W, $bits.H)
    [pscustomobject]@{ Aurora = [Math]::Round($a[0], 4); Stars = [int]$a[1]; Pink = [Math]::Round($a[2], 4) }
}
# Stars (isolated bright points) in a part of a shot (fractions of the width / height).
function Get-AtmosphereStarsIn([string]$path, [double]$x0, [double]$x1, [double]$y0, [double]$y1) {
    $bits = Get-AtmosphereBits $path
    [int]([GfAtmosphere.Signature2]::AnalyzeRegion($bits.Px, $bits.W, $bits.H, $x0, $x1, $y0, $y1)[1])
}
# What changed from shot $a to shot $b: the mean change of the changed pixels (R, G, B), their share
# and their mean lightness before and after.
function Get-AtmosphereDelta([string]$a, [string]$b) {
    $pa = Get-AtmosphereBits $a; $pb = Get-AtmosphereBits $b
    $d = [GfAtmosphere.Signature2]::Delta($pa.Px, $pb.Px, $pa.W, $pa.H)
    [pscustomobject]@{ R = [Math]::Round($d[0], 1); G = [Math]::Round($d[1], 1); B = [Math]::Round($d[2], 1); Share = [Math]::Round($d[3], 4); Before = [Math]::Round($d[4], 1); After = [Math]::Round($d[5], 1) }
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
    Test-AtmosphereSignature "$mode stars in the zenith view (noon)" (Get-AtmosphereSignature $noon).Stars 30 1500
    Test-AtmosphereSignature "$mode stars in the zenith view (midnight)" (Get-AtmosphereSignature $midnight).Stars 30 1500
    Set-View 90 -15
    $horizon = Save-Screenshot "$mode-sky-horizon"
    Test-AtmosphereSignature "$mode pink (sunset) share of the horizon" (Get-AtmosphereSignature $horizon).Pink 0 0.01

    # fog: the forest's, the spooky forest's near fog (smoothed in over seconds), the dark forest
    Set-View 0 0
    $forest = Show-Atmosphere 'show_forest' "$mode-fog-forest" 'atmosphere: fog of twilightforest:forest' 4
    $spooky = Show-Atmosphere 'show_spooky' "$mode-fog-spooky" 'atmosphere: fog of twilightforest:spooky_forest' 12
    $d = Get-ColourDistance (Get-AtmosphereColour $forest) (Get-AtmosphereColour $spooky)
    Test-AtmosphereSignature "$mode fog forest vs spooky forest colour distance" $d 4 1000
    # TF draws its stars without fog (TFSkyRenderer: FogRenderer.setupNoFog): they stay over the
    # spooky forest's near fog
    Set-View 0 -50
    Start-Sleep -Seconds 1
    $spookyUp = Save-Screenshot "$mode-fog-spooky-up"
    Test-AtmosphereSignature "$mode stars over the spooky forest's fog" (Get-AtmosphereSignature $spookyUp).Stars 30 1500
    Set-View 0 0
    Show-Atmosphere 'show_dark_forest' "$mode-fog-dark-forest" 'atmosphere: fog of twilightforest:dark_forest' 4 | Out-Null
    $boss = Show-Atmosphere 'show_boss_fog' "$mode-fog-boss" 'atmosphere: fog of twilightforest:forest' 12
    $d = Get-ColourDistance (Get-AtmosphereColour $forest) (Get-AtmosphereColour $boss)
    Write-Step "$mode fog forest vs boss world fog: colour distance $d"

    # the aurora over the glacier (fades in over 60 ticks), against the same view without it: TF's
    # green-cyan glow
    Set-View 0 -50
    $clearUp = Show-Atmosphere 'show_forest' "$mode-aurora-clear" '' 3
    $aurora  = Show-Atmosphere 'show_glacier' "$mode-aurora" 'atmosphere: aurora drawn' 6
    $delta   = Get-AtmosphereDelta $clearUp $aurora
    Write-Step "$mode aurora change: $($delta.R), $($delta.G), $($delta.B) over $($delta.Share) of the view (lightness $($delta.Before) -> $($delta.After))"
    Test-AtmosphereSignature "$mode aurora changed share" $delta.Share 0.02 1
    Test-AtmosphereSignature "$mode aurora green gain over red" ([Math]::Round($delta.G - $delta.R, 1)) 10 255
    Test-AtmosphereSignature "$mode aurora lightness gain (a glow, not a shadow)" ([Math]::Round($delta.After - $delta.Before, 1)) 5 255
    if (-not $vv) { Test-AtmosphereSignature "$mode aurora coverage" ([Math]::Round((Get-AtmosphereSignature $aurora).Aurora - (Get-AtmosphereSignature $clearUp).Aurora, 4)) 0.05 0.6 }

    # the void plate (below the floor of the dimension; forced): opaque, the stars below the horizon
    # hidden, black below and the fog colour towards the horizon
    Set-View 0 10
    $noPlate = Show-Atmosphere 'show_forest' "$mode-void-plate-clear" '' 2
    $plate   = Show-Atmosphere 'show_void_plate' "$mode-void-plate" 'atmosphere: void plate drawn' 3
    $starsBelow = Get-AtmosphereStarsIn $plate 0.42 0.92 0.45 0.68
    Write-Step "$mode void plate: changed share $(Get-ChangedShare $noPlate $plate), stars below the horizon $(Get-AtmosphereStarsIn $noPlate 0.42 0.92 0.45 0.68) -> $starsBelow"
    Test-AtmosphereSignature "$mode stars below the horizon over the void plate" $starsBelow 0 2

    # locked-biome weather, looking along the ground: the effect's texture lights the view
    Set-View 0 5
    $clearGround = Show-Atmosphere 'show_forest' "$mode-weather-clear" '' 3
    foreach ($w in @(@('show_blizzard', 'blizzard'), @('show_mosquitoes', 'mosquito'), @('show_ashes', 'ashes'), @('show_dark_streams', 'dark_stream'), @('show_big_rain', 'big_rain'))) {
        $shot = Show-Atmosphere $w[0] "$mode-weather-$($w[1] -replace '_', '-')" "atmosphere: $($w[1]) weather drawn" 3
        Test-AtmosphereSignature "$mode $($w[1]) weather changed share" (Get-ChangedShare $clearGround $shot) 0.01 1
        if ($w[1] -eq 'blizzard') {
            $delta = Get-AtmosphereDelta $clearGround $shot
            Test-AtmosphereSignature "$mode blizzard flakes brighter than the clear view" ([Math]::Round($delta.After - $delta.Before, 1)) 5 255
        }
    }
    $shot = Show-Atmosphere 'show_sparkles' "$mode-sparkles" 'atmosphere: sparkles weather drawn' 3
    Test-AtmosphereSignature "$mode sparkles changed share" (Get-ChangedShare $clearGround $shot) 0.002 1
    $delta = Get-AtmosphereDelta $clearGround $shot
    Test-AtmosphereSignature "$mode sparkles brighter than the clear view" ([Math]::Round($delta.After - $delta.Before, 1)) 5 255
    # the Ur-Ghast's tantrum: rain splashes and sounds
    Show-Atmosphere 'show_ur_ghast' "$mode-ur-ghast-rain" 'atmosphere: Ur-Ghast rain splashes' 3 | Out-Null
    if ($calibrate) {
        # every candidate material of each part (feature/atmosphere/Selftests.cpp's lists)
        $cal = [ordered]@{
            stars   = @(@(0, -50), @('stars', 'lightning', 'particles_blend_tex', 'particles_add_tex', 'glow_sign_text_tex'))
            aurora  = @(@(0, -50), @('particles_blend_tex', 'lightning', 'particles_add_tex', 'glow_sign_text_tex', 'stars'))
            weather = @(@(0, 5), @('particles_blend', 'particles_add', 'glow_sign_text', 'sign_text', 'particles_alpha'))
            plate   = @(@(0, 10), @('particles_blend_tex', 'particles_opaque_tex', 'debug', 'stars'))
        }
        foreach ($part in $cal.Keys) {
            Set-View $cal[$part][0][0] $cal[$part][0][1]
            $base = Show-Atmosphere 'show_forest' "$mode-cal-$part-base" '' 3
            foreach ($m in $cal[$part][1]) {
                $shot  = Show-Atmosphere "show_cal_${part}_$m" "$mode-cal-$part-$m" '' 5
                $sig   = Get-AtmosphereSignature $shot
                $delta = Get-AtmosphereDelta $base $shot
                Write-Step "calibration $mode $part $m`: stars $($sig.Stars), change $($delta.R)/$($delta.G)/$($delta.B) over $($delta.Share), lightness $($delta.Before) -> $($delta.After)"
            }
        }
        if ($vv) {
            Set-View 90 -15
            foreach ($t in 'skytime_3', 'skytime_4', 'skytime_5', 'star_mesh_off') {
                $shot = Show-Atmosphere "show_cal_$t" "$mode-cal-$t" '' 4
                Write-Step "calibration $mode $t`: pink $((Get-AtmosphereSignature $shot).Pink), stars $((Get-AtmosphereSignature $shot).Stars)"
            }
        }
    }
    Invoke-AtmosphereTest 'atmosphere.show_clear' | Out-Null
}

# The aurora biome list editor (TFClientConfig's "Edit Biomes"), opened by the server for the
# tagged companion; closed with Esc.
function Show-AuroraBiomesEditor([string]$mode) {
    $mark = Get-LogMark
    Invoke-AtmosphereTest 'atmosphere.show_aurora_biomes_screen' | Out-Null
    Wait-LogLine 'atmosphere: aurora biome editor opened' 30 -Since $mark | Out-Null
    Start-Sleep -Seconds 2
    Save-Screenshot "$mode-aurora-biomes-editor" | Out-Null
    Press-Key 'ESC'
    Start-Sleep -Seconds 1
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
    # the aurora biome editor's form (a vanilla client draws no aurora: it explains the setting)
    Invoke-AtmosphereTest 'atmosphere.show_aurora_biomes_screen' | Out-Null
    Start-Sleep -Seconds 3
    Save-Screenshot 'vanilla-aurora-biomes-form' | Out-Null
    Press-Key 'ESC'
    Start-Sleep -Seconds 1
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
    Show-AuroraBiomesEditor 'classic'
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
