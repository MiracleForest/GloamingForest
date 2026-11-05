# U15 (atmosphere): the Twilight Forest's sky, fog, locked-biome weather and aurora in the real
# game (docs/spec/atmosphere.md section 5).
#
# local / remote-mod: the player goes to the Twilight Forest (/twilightforest tp) and the atmosphere.*
#   selftests run untagged (the client ones read the client mod's state there: provider, fog, the
#   aurora fading in over 60 ticks and off with an empty auroraBiomes list, locked-biome weather, the
#   Ur-Ghast rain). Then, tagged gf_atmosphere_show, the atmosphere.show_* selftests send previews (a
#   biome for every lookup, locked biomes, protected boxes) because TF's biomes come from U1, and the
#   screenshots show:
#     <mode>-sky-up-noon / -midnight (no sun or moon, TF's stars; the fixed time keeps both alike),
#     <mode>-sky-horizon, <mode>-fog-forest, <mode>-fog-spooky (the near fog, smoothed in),
#     <mode>-fog-dark-forest, <mode>-aurora (glacier), <mode>-weather-blizzard / -mosquitoes /
#     -ashes / -dark-streams / -big-rain (locked biomes), <mode>-sparkles (protected structure).
#   Local mode does it twice: Classic (graphics_mode 1, atmosphere.prelaunch.ps1) and Vibrant
#   Visuals (graphics_mode 2, after a restart); remote-mod in Classic.
# remote-vanilla: no client mod; the host pins the spooky forest's / glacier's fog for the tagged
#   vanilla player (atmosphere.show_vanilla_*: PlayerFogPacket with the resource pack fog
#   definitions) and the screenshots must differ from the cleared one:
#     vanilla-clear.png, vanilla-spooky.png, vanilla-glacier.png.
. (Join-Path $PSScriptRoot 'ui.graphics.ps1') # Set-UiGraphicsMode (F12's option helpers)

$remote  = $script:Mode -ne 'local'
$vanilla = $script:Mode -eq 'remote-vanilla'
$hostRx  = if ($remote) { '\[host\].*' } else { '' }
$tag     = 'gf_atmosphere_show'

# Mean colour (r, g, b) of the middle of a screenshot (the hotbar and the edges left out).
function Get-AtmosphereColour([string]$path) {
    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    try {
        $r = 0.0; $g = 0.0; $b = 0.0; $n = 0
        for ($y = [int]($bmp.Height * 0.1); $y -lt [int]($bmp.Height * 0.75); $y += 8) {
            for ($x = [int]($bmp.Width * 0.1); $x -lt [int]($bmp.Width * 0.9); $x += 8) {
                $c = $bmp.GetPixel($x, $y); $r += $c.R; $g += $c.G; $b += $c.B; $n++
            }
        }
        return @([Math]::Round($r / $n, 1), [Math]::Round($g / $n, 1), [Math]::Round($b / $n, 1))
    } finally { $bmp.Dispose() }
}
function Get-ColourDistance($a, $b) { [Math]::Round([Math]::Sqrt(($a[0] - $b[0]) * ($a[0] - $b[0]) + ($a[1] - $b[1]) * ($a[1] - $b[1]) + ($a[2] - $b[2]) * ($a[2] - $b[2])), 1) }

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
    if (-not $vanilla) { Wait-LogLine 'client dimension twilightforest:twilight_forest: y -32\.\.256' 90 -Since $mark | Out-Null }
    Start-Sleep -Seconds 6 # the chunks around the arrival load
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
    Set-View 90 -15
    Save-Screenshot "$mode-sky-horizon" | Out-Null

    # fog: the forest's, the spooky forest's near fog (smoothed in over seconds), the dark forest
    Set-View 0 0
    $forest = Show-Atmosphere 'show_forest' "$mode-fog-forest" 'atmosphere: fog of twilightforest:forest' 4
    $spooky = Show-Atmosphere 'show_spooky' "$mode-fog-spooky" 'atmosphere: fog of twilightforest:spooky_forest' 12
    $d = Get-ColourDistance (Get-AtmosphereColour $forest) (Get-AtmosphereColour $spooky)
    Write-Step "$mode fog forest vs spooky forest: colour distance $d"
    if ($d -lt 4) { throw "the spooky forest fog does not show ($d)" }
    Show-Atmosphere 'show_dark_forest' "$mode-fog-dark-forest" 'atmosphere: fog of twilightforest:dark_forest' 4 | Out-Null

    # the aurora over the glacier (fades in over 60 ticks)
    Set-View 0 -50
    Show-Atmosphere 'show_glacier' "$mode-aurora" 'atmosphere: aurora drawn' 5 | Out-Null

    # locked-biome weather, looking along the ground
    Set-View 0 5
    foreach ($w in @(@('show_blizzard', 'blizzard'), @('show_mosquitoes', 'mosquito'), @('show_ashes', 'ashes'), @('show_dark_streams', 'dark_stream'), @('show_big_rain', 'big_rain'))) {
        Show-Atmosphere $w[0] "$mode-weather-$($w[1] -replace '_', '-')" "atmosphere: $($w[1]) weather drawn" 3 | Out-Null
    }
    Show-Atmosphere 'show_sparkles' "$mode-sparkles" 'atmosphere: sparkles weather drawn' 3 | Out-Null
    # the Ur-Ghast's tantrum: rain splashes and sounds
    Show-Atmosphere 'show_ur_ghast' "$mode-ur-ghast-rain" 'atmosphere: Ur-Ghast rain splashes' 3 | Out-Null
    if ($env:GF_ATMOSPHERE_CALIBRATE -eq '1') {
        # which quad side the engine's materials draw: the aurora and a weather the other way round and both
        foreach ($w in 'reversed', 'both') {
            Invoke-AtmosphereTest "atmosphere.show_winding_$w" | Out-Null
            Set-View 0 -50
            Show-Atmosphere 'show_glacier' "$mode-aurora-$w" 'atmosphere: aurora drawn' 5 | Out-Null
            Set-View 0 5
            Show-Atmosphere 'show_blizzard' "$mode-weather-blizzard-$w" 'atmosphere: blizzard weather drawn' 3 | Out-Null
        }
    }
    Invoke-AtmosphereTest 'atmosphere.show_clear' | Out-Null
}

if ($vanilla) {
    Enter-Twilight
    Invoke-Chat "/tag @s add $tag"
    Set-View 0 0
    $mark = Get-LogMark
    Invoke-AtmosphereTest 'atmosphere.show_vanilla_clear' | Out-Null
    Start-Sleep -Seconds 3
    $clear = Save-Screenshot 'vanilla-clear'
    $mark = Get-LogMark
    Invoke-AtmosphereTest 'atmosphere.show_vanilla_spooky' | Out-Null
    Wait-LogLine "$hostRx`atmosphere: fog twilightforest:spooky_forest sent to" 30 -Since $mark | Out-Null
    Start-Sleep -Seconds 4
    $spooky = Save-Screenshot 'vanilla-spooky'
    $mark = Get-LogMark
    Invoke-AtmosphereTest 'atmosphere.show_vanilla_glacier' | Out-Null
    Wait-LogLine "$hostRx`atmosphere: fog twilightforest:glacier sent to" 30 -Since $mark | Out-Null
    Start-Sleep -Seconds 4
    Save-Screenshot 'vanilla-glacier' | Out-Null
    Invoke-AtmosphereTest 'atmosphere.show_vanilla_clear' | Out-Null
    $d = Get-ColourDistance (Get-AtmosphereColour $clear) (Get-AtmosphereColour $spooky)
    Write-Step "vanilla fog clear vs spooky forest: colour distance $d"
    if ($d -lt 4) { throw "the PlayerFogPacket fog does not show on the vanilla client ($d)" }
    Invoke-Chat "/tag @s remove $tag"
    return
}

Enter-Twilight
foreach ($rx in 'atmosphere: sun, moon and vanilla stars hidden', 'atmosphere: TF stars drawn') { Wait-LogLine $rx 60 | Out-Null }
Invoke-AtmosphereTest 'atmosphere.*' | Out-Null
Invoke-Chat "/tag @s add $tag"
Invoke-AtmosphereViews 'classic'
Invoke-Chat "/tag @s remove $tag"

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
    Enter-Twilight
    Invoke-Chat "/tag @s add $tag"
    Invoke-AtmosphereViews 'vibrant'
    Invoke-Chat "/tag @s remove $tag"
}
