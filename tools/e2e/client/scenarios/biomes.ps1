# Biomes (U1) on the client: what a player sees of the 20 surface biomes of the Twilight Forest.
# biomes.survey (on the world's server: the integrated one, or the BDS host in remote modes) finds a
# place inside each biome for the world's seed; the player enters the Twilight Forest (/tf tp), turns
# spectator with night vision, and at each place a test patch is built on the generated chunks
# (17x17 grass with short grass, a 5x5 pool three blocks deep, oak leaves, TF's twilight oak leaves
# and a fiddlehead: the biome's grass, foliage and water colours; the sky and fog around it) and
# photographed; in five biomes the pool is also photographed from straight above.
# - local / remote-mod (the client mod): TF's colours of every column (position-dependent colours,
#   modifiers, colour maps) are drawn by the mod (src/client/biomes/Tints.cpp, applied by
#   feature/ext/biomes/VanillaTint.cpp's hooks on the game's grass / foliage policies, its seasons
#   hook for leaves and its water colour hook); biomes.grass_tint runs in local mode.
# - remote-vanilla: the client biome files reach the client with the host's resource packs; the
#   static colours only.
# Checks: the spooky forest's grass is redder than the forest's; the pools of the fire swamp and the
# spooky forest are redder, the swamp's and the dense forest's greener than the forest's (TF's water
# colours #6C2C2C, #BC8857, #95B55F, #005522 against #3F76E4).
# Screenshots: out/biomes[-<mode>]/{twilight,biome_<name> x20,water_<name> x5,selftest}.png
$hostPrefix = if ($script:Mode -eq 'local') { '' } else { '\[host\].*' }

# The mean colour of a part of a screenshot (fractions of the width / height).
function Get-MeanColour([string]$path, [double]$x0, [double]$x1, [double]$y0, [double]$y1) {
    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    try {
        $r = 0.0; $g = 0.0; $b = 0.0; $n = 0
        for ($y = [int]($bmp.Height * $y0); $y -lt [int]($bmp.Height * $y1); $y += 3) {
            for ($x = [int]($bmp.Width * $x0); $x -lt [int]($bmp.Width * $x1); $x += 3) {
                $p = $bmp.GetPixel($x, $y); $r += $p.R; $g += $p.G; $b += $p.B; $n++
            }
        }
        return [pscustomobject]@{ R = $r / $n; G = $g / $n; B = $b / $n; RminusG = ($r - $g) / $n; RminusB = ($r - $b) / $n; GminusB = ($g - $b) / $n }
    } finally { $bmp.Dispose() }
}
# The ground in front of the player (the bottom middle of a screenshot).
function Get-GroundTint([string]$path) { Get-MeanColour $path 0.35 0.65 0.70 0.90 }
function Format-Tint($t) { '#{0:X2}{1:X2}{2:X2} (R-G {3:N1}, R-B {4:N1}, G-B {5:N1})' -f [int]$t.R, [int]$t.G, [int]$t.B, $t.RminusG, $t.RminusB, $t.GminusB }

Wait-LogLine "${hostPrefix}GloamingForest world ready" 120 | Out-Null
Wait-LogLine "${hostPrefix}biomes: Twilight Forest biome source installed" 60 | Out-Null

$mark = Get-LogMark
$r = Invoke-SelfTest 'biomes.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) biomes selftest(s) failed" }
$places = [ordered]@{}
foreach ($line in (Get-LeviLog -Since $mark | Select-String -Pattern 'survey twilightforest:(\S+) at (-?\d+) (-?\d+)')) {
    $name = $line.Matches[0].Groups[1].Value
    if (-not $places.Contains($name)) { $places[$name] = @([int]$line.Matches[0].Groups[2].Value, [int]$line.Matches[0].Groups[3].Value) }
}
Write-Step "surveyed $($places.Count) biomes: $($places.Keys -join ', ')"
$surface = 'clearing', 'dark_forest', 'dark_forest_center', 'dense_forest', 'dense_mushroom_forest', 'enchanted_forest', 'final_plateau',
    'fire_swamp', 'firefly_forest', 'forest', 'glacier', 'highlands', 'lake', 'mushroom_forest', 'oak_savannah', 'snowy_forest',
    'spooky_forest', 'stream', 'swamp', 'thornlands'
$missing = @($surface | Where-Object { -not $places.Contains($_) })
if ($missing.Count -gt 0) { throw "biomes.survey did not find $($missing -join ', ') (all 20 surface biomes are photographed)" }

Invoke-Chat '/gamemode spectator'
Invoke-Chat '/effect @s night_vision 100000 0 true'
Invoke-Chat '/tf tp'
Start-Sleep -Seconds 10
# the dimension above ground: over the forest, looking across it
$fx = $places['forest'][0]; $fz = $places['forest'][1]
Invoke-Teleport "/tp @s $fx 70 $fz 180 15"
Start-Sleep -Seconds 6
Save-Screenshot 'twilight' | Out-Null

$waterShots = 'forest', 'fire_swamp', 'swamp', 'spooky_forest', 'dense_forest'
$tints = @{}
$water = @{}
foreach ($name in $places.Keys) {
    $x = $places[$name][0]; $z = $places[$name][1]
    $y = 40
    Invoke-Teleport "/tp @s $x $($y + 8) $z"
    Start-Sleep -Seconds 6
    Invoke-Chat "/fill $($x - 8) $y $($z - 8) $($x + 8) $y $($z + 8) grass_block"
    Invoke-Chat "/fill $($x - 8) $($y + 1) $($z - 8) $($x + 8) $($y + 4) $($z + 8) air"
    Invoke-Chat "/fill $($x - 7) $($y + 1) $($z + 2) $($x - 3) $($y + 1) $($z + 6) short_grass"
    # the pool: a 7x7 basin of grass, 5x5 of water three blocks deep
    Invoke-Chat "/fill $($x + 1) $($y - 3) $($z + 1) $($x + 7) $y $($z + 7) grass_block"
    Invoke-Chat "/fill $($x + 2) $($y - 2) $($z + 2) $($x + 6) $y $($z + 6) water"
    Invoke-Chat "/fill $($x - 1) $($y + 1) $($z + 6) $($x + 1) $($y + 2) $($z + 7) oak_leaves"
    Invoke-Chat "/fill $($x - 3) $($y + 1) $($z + 6) $($x - 2) $($y + 2) $($z + 7) twilightforest:twilight_oak_leaves"
    Invoke-Chat "/setblock $($x - 5) $($y + 1) $z twilightforest:fiddlehead"
    # the camera south of the patch, looking north-ish down onto it (Bedrock yaw 180 faces -z)
    Invoke-Teleport "/tp @s $x $($y + 6) $($z + 14) 180 28"
    Start-Sleep -Seconds 3
    $shot = Save-Screenshot "biome_$name"
    $tints[$name] = Get-GroundTint $shot
    Write-Step "$name at $x $z`: ground $(Format-Tint $tints[$name])"
    if ($waterShots -contains $name) {
        # straight down onto the middle of the pool: only water in the middle of the picture
        Invoke-Teleport "/tp @s $($x + 4.5) $($y + 4) $($z + 4.5) 180 90"
        Start-Sleep -Seconds 3
        $wshot = Save-Screenshot "water_$name"
        $water[$name] = Get-MeanColour $wshot 0.40 0.60 0.40 0.60
        Write-Step "$name water: $(Format-Tint $water[$name])"
    }
}

if ($tints['spooky_forest'].RminusG - $tints['forest'].RminusG -lt 10) {
    throw "the spooky forest's grass is not drawn redder than the forest's (spooky $(Format-Tint $tints['spooky_forest']), forest $(Format-Tint $tints['forest']))"
}
# TF's water colours: redder (R-B) or greener (G-B) than the forest's #3F76E4
$forestWater = $water['forest']
foreach ($check in @(
        @{ Biome = 'fire_swamp'; Axis = 'RminusB'; What = 'redder' },
        @{ Biome = 'spooky_forest'; Axis = 'RminusB'; What = 'redder' },
        @{ Biome = 'swamp'; Axis = 'GminusB'; What = 'greener' },
        @{ Biome = 'dense_forest'; Axis = 'GminusB'; What = 'greener' })) {
    $w = $water[$check.Biome]
    if ($w.($check.Axis) - $forestWater.($check.Axis) -lt 10) {
        throw "the $($check.Biome) pool is not $($check.What) than the forest's ($(Format-Tint $w) against $(Format-Tint $forestWater))"
    }
}
Write-Step 'TF water colours: fire swamp and spooky forest redder, swamp and dense forest greener than the forest'

if ($script:Mode -ne 'remote-vanilla') {
    # the client mod drew TF's colours
    $log = Get-LeviLog
    if (-not ($log | Select-String -Pattern 'biomes: TF biome colours on \d+ grass and \d+ foliage blocks' -Quiet)) { throw 'the client mod did not register its biome tints' }
    # the grass / foliage tints, leaves without seasons and the water colours go through U1's hooks
    # (feature/ext/biomes/VanillaTint.cpp; F12's resolver hook never sees vanilla blocks)
    if (-not ($log | Select-String -Pattern 'biomes: vanilla tint hooks installed \(grass ok, foliage ok, seasons ok, water ok\)' -Quiet)) { throw 'the vanilla tint hooks were not installed' }
    $applied = $log | Select-String -Pattern 'biomes: vanilla tint hook applied the tint of .*' | Select-Object -First 1
    if (-not $applied) { throw 'the vanilla tint hook never applied a tint (vanilla grass and leaves drew without TF colours)' }
    Write-Step "hook: $($applied.Matches[0].Value)"
    # the chunks drawn in the TF biomes got TF's colours from the hook (not only the selftest's)
    foreach ($kind in 'grass', 'foliage') {
        $changed = $log | Select-String -Pattern "biomes: vanilla tint hook changed the $kind colour of .*" | Select-Object -First 1
        if (-not $changed) { throw "the vanilla tint hook never drew a TF $kind colour on a vanilla block" }
        Write-Step "hook: $($changed.Matches[0].Value)"
    }
    foreach ($pattern in "biomes: TF's biome foliage colour drawn on twilightforest:.*", 'biomes: water colour hook applied .*', "biomes: leaves at .* drawn without Bedrock's seasons map .*") {
        $hit = $log | Select-String -Pattern $pattern | Select-Object -First 1
        if (-not $hit) { throw "missing log line '$pattern'" }
        Write-Step "hook: $($hit.Matches[0].Value)"
    }
    foreach ($b in 'spooky_forest', 'dark_forest_center', 'enchanted_forest') {
        if (-not ($log | Select-String -Pattern "biomes: client draws TF's grass colours of twilightforest:$b" -Quiet)) {
            throw "the client mod did not draw TF's grass colours in twilightforest:$b"
        }
    }
}
Invoke-Chat '/gamemode creative'
Save-Screenshot 'selftest' | Out-Null
