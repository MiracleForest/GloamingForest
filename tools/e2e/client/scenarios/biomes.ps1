# Biomes (U1) on the client: what a player sees of the 20 surface biomes of the Twilight Forest.
# biomes.survey (on the world's server: the integrated one, or the BDS host in remote modes) finds a
# place inside each biome for the world's seed; the player enters the Twilight Forest (/tf tp), turns
# spectator with night vision, and at each place a test patch is built on the generated chunks
# (17x17 grass with short grass, a 5x5 pool three blocks deep in a white concrete basin, oak leaves,
# TF's twilight oak leaves and a fiddlehead: the biome's grass, foliage and water colours; the sky
# and fog around it) and photographed; in five biomes the pool is also photographed from straight
# above and from inside (the water fog).
# - local / remote-mod (the client mod): TF's colours of every column (position-dependent colours,
#   modifiers, colour maps, water) are drawn by the mod (src/client/biomes/Tints.cpp, applied by
#   feature/ext/biomes/VanillaTint.cpp's hooks on the game's grass / foliage / water policies, its
#   seasons hook for leaves, and the grass tint method it gives the fiddlehead's plant faces);
#   biomes.grass_tint runs in local mode.
# - remote-vanilla: the client biome files reach the client with the host's resource packs; the
#   static colours only (no fiddlehead tint: its block file declares no tint method).
# Water controls (biomes.water_controls): a pool in the overworld's own biome, a pool in TF's fire
# swamp painted into the overworld, and a pool in the vanilla swampland painted into the Twilight
# Forest; each tells whether a biome's water colour depends on the dimension or on the biome files.
# The game draws water differently by graphics mode: Vibrant Visuals (the harness default) shades it
# physically (minecraft:water_identifier settings), the Classic look (scenario biomes.classic,
# graphics_mode 1) tints it with the biome's water colour as Java does.
# Screenshots: out/<scenario>[-<mode>]/{home_water,control_*,twilight,biome_<name> x20,water_<name> x5,
# underwater_<name> x5,fiddlehead_*,selftest}.png
$hostPrefix = if ($script:Mode -eq 'local') { '' } else { '\[host\].*' }
$withMod = $script:Mode -ne 'remote-vanilla'
$classic = $script:Unit -like 'biomes.classic*'

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
        return [pscustomobject]@{ R = $r / $n; G = $g / $n; B = $b / $n; RminusG = ($r - $g) / $n; RminusB = ($r - $b) / $n; GminusB = ($g - $b) / $n
            Green = ($g - ($r + $b) / 2) / $n }
    } finally { $bmp.Dispose() }
}
# The ground in front of the player (the bottom middle of a screenshot).
function Get-GroundTint([string]$path) { Get-MeanColour $path 0.35 0.65 0.70 0.90 }
function Format-Tint($t) { '#{0:X2}{1:X2}{2:X2} (R-G {3:N1}, R-B {4:N1}, G-B {5:N1})' -f [int]$t.R, [int]$t.G, [int]$t.B, $t.RminusG, $t.RminusB, $t.GminusB }

# A 7x7 basin of white concrete (a neutral bottom: the water's own colour shows) holding 5x5 of
# water three blocks deep, its surface at y; x / z is the basin's corner minus one.
function New-Pool([int]$x, [int]$y, [int]$z) {
    Invoke-Chat "/fill $($x + 1) $($y - 3) $($z + 1) $($x + 7) $y $($z + 7) white_concrete"
    Invoke-Chat "/fill $($x + 2) $($y - 2) $($z + 2) $($x + 6) $y $($z + 6) water"
}
# Straight down onto the middle of the pool: only water in the middle of the picture.
function Measure-Pool([int]$x, [int]$y, [int]$z, [string]$shot) {
    Invoke-Teleport "/tp @s $($x + 4.5) $($y + 4) $($z + 4.5) 180 90"
    Start-Sleep -Seconds 3
    $path = Save-Screenshot $shot
    return Get-MeanColour $path 0.40 0.60 0.40 0.60
}

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

# --- water controls in the overworld: its own biome, and TF's fire swamp painted 3000 blocks east
$water = @{}
Invoke-Teleport '/tp @s 0 158 0'
Start-Sleep -Seconds 4
New-Pool 0 150 0
$water['overworld'] = Measure-Pool 0 150 0 'home_water'
Write-Step "overworld (its own biome) water: $(Format-Tint $water['overworld'])"
Invoke-Teleport '/tp @s 0 158 0'
Invoke-Chat '/tag @s add gf_biomes_water_controls'
Start-Sleep -Seconds 2
$c = Invoke-SelfTest 'biomes.water_controls'
if ($c.Fail -gt 0) { throw 'biomes.water_controls failed while painting the overworld' }
Wait-LogLine "${hostPrefix}biomes water control: painted \d+ columns with twilightforest:fire_swamp" 30 | Out-Null
Invoke-Teleport '/tp @s 3004 158 4'
Start-Sleep -Seconds 6
New-Pool 3000 150 0
$water['control_fire_swamp_overworld'] = Measure-Pool 3000 150 0 'control_fire_swamp_overworld'
Write-Step "TF fire swamp painted into the overworld, water: $(Format-Tint $water['control_fire_swamp_overworld'])"
Invoke-Teleport '/tp @s 0 158 0'
$c = Invoke-SelfTest 'biomes.water_controls_restore'
if ($c.Fail -gt 0) { throw 'biomes.water_controls_restore failed in the overworld' }

Invoke-Chat '/tf tp'
Start-Sleep -Seconds 10
# the dimension above ground: over the forest, looking across it
$fx = $places['forest'][0]; $fz = $places['forest'][1]
Invoke-Teleport "/tp @s $fx 70 $fz 180 15"
Start-Sleep -Seconds 6
Save-Screenshot 'twilight' | Out-Null

$waterShots = 'forest', 'fire_swamp', 'swamp', 'spooky_forest', 'dense_forest'
$tints = @{}
$underwater = @{}
$fiddle = @{}
foreach ($name in $places.Keys) {
    $x = $places[$name][0]; $z = $places[$name][1]
    $y = 40
    Invoke-Teleport "/tp @s $x $($y + 8) $z"
    Start-Sleep -Seconds 6
    Invoke-Chat "/fill $($x - 8) $y $($z - 8) $($x + 8) $y $($z + 8) grass_block"
    Invoke-Chat "/fill $($x - 8) $($y + 1) $($z - 8) $($x + 8) $($y + 4) $($z + 8) air"
    Invoke-Chat "/fill $($x - 7) $($y + 1) $($z + 2) $($x - 3) $($y + 1) $($z + 6) short_grass"
    New-Pool $x $y $z
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
        $water[$name] = Measure-Pool $x $y $z "water_$name"
        Write-Step "$name water: $(Format-Tint $water[$name])"
        # inside the pool: the water fog (TF's water fog colour)
        Invoke-Teleport "/tp @s $($x + 4.5) $($y - 1.5) $($z + 4.5) 180 0"
        Start-Sleep -Seconds 2
        $underwater[$name] = Get-MeanColour (Save-Screenshot "underwater_$name") 0.30 0.70 0.30 0.70
        Write-Step "$name under water: $(Format-Tint $underwater[$name])"
    }
    if ($name -eq 'forest' -or $name -eq 'swamp') {
        # the fiddlehead (and its pot) close up against white concrete, then the backdrop alone: the
        # difference is the plant's own colour (TF: the biome's grass colour on a grey sprite)
        $fxp = $x - 5; $fyp = $y + 1
        Invoke-Chat "/fill $($fxp - 1) $($fyp - 1) $($z - 1) $($fxp + 1) $($fyp + 2) $($z - 1) white_concrete"
        Invoke-Chat "/fill $($fxp - 1) $($fyp - 1) $z $($fxp + 1) $($fyp - 1) $($z + 2) white_concrete"
        $shots = [ordered]@{ fiddlehead = 'twilightforest:fiddlehead'; potted_fiddlehead = 'twilightforest:potted_fiddlehead'; backdrop = 'air' }
        foreach ($k in $shots.Keys) {
            Invoke-Chat "/setblock $fxp $fyp $z $($shots[$k])"
            Invoke-Teleport "/tp @s $($fxp + 0.5) $($fyp - 1.2) $($z + 1.9) 180 0"
            Start-Sleep -Seconds 3
            $fiddle["$name/$k"] = Get-MeanColour (Save-Screenshot "fiddlehead_${name}_$k") 0.40 0.60 0.25 0.75
            Write-Step "$name $k close up: $(Format-Tint $fiddle["$name/$k"]) green $('{0:N1}' -f $fiddle["$name/$k"].Green)"
        }
    }
}

# --- water control in the Twilight Forest: the vanilla swampland painted 3000 blocks east of the forest
Invoke-Teleport "/tp @s $fx 48 $fz"
Start-Sleep -Seconds 2
$c = Invoke-SelfTest 'biomes.water_controls'
if ($c.Fail -gt 0) { throw 'biomes.water_controls failed while painting the Twilight Forest' }
Wait-LogLine "${hostPrefix}biomes water control: painted \d+ columns with minecraft:swampland" 30 | Out-Null
Invoke-Teleport "/tp @s $($fx + 3004) 48 $($fz + 4)"
Start-Sleep -Seconds 6
New-Pool ($fx + 3000) 40 $fz
$water['control_swampland_twilight'] = Measure-Pool ($fx + 3000) 40 $fz 'control_swampland_twilight'
Write-Step "vanilla swampland painted into the Twilight Forest, water: $(Format-Tint $water['control_swampland_twilight'])"
Invoke-Teleport "/tp @s $($fx + 3004.5) 38.5 $($fz + 4.5) 180 0"
Start-Sleep -Seconds 2
$underwater['control_swampland_twilight'] = Get-MeanColour (Save-Screenshot 'control_swampland_twilight_under') 0.30 0.70 0.30 0.70
Write-Step "vanilla swampland in the Twilight Forest under water: $(Format-Tint $underwater['control_swampland_twilight'])"
Invoke-Teleport "/tp @s $fx 48 $fz"
$c = Invoke-SelfTest 'biomes.water_controls_restore'
if ($c.Fail -gt 0) { throw 'biomes.water_controls_restore failed in the Twilight Forest' }
Invoke-Chat '/tag @s remove gf_biomes_water_controls'

if ($tints['spooky_forest'].RminusG - $tints['forest'].RminusG -lt 10) {
    throw "the spooky forest's grass is not drawn redder than the forest's (spooky $(Format-Tint $tints['spooky_forest']), forest $(Format-Tint $tints['forest']))"
}

# TF's water colours: redder (R-B) or greener (G-B) than the forest's #3F76E4
$forestWater = $water['forest']
$waterFailures = @()
foreach ($check in @(
        @{ Biome = 'fire_swamp'; Axis = 'RminusB'; What = 'redder' },
        @{ Biome = 'spooky_forest'; Axis = 'RminusB'; What = 'redder' },
        @{ Biome = 'swamp'; Axis = 'GminusB'; What = 'greener' },
        @{ Biome = 'dense_forest'; Axis = 'GminusB'; What = 'greener' })) {
    $w = $water[$check.Biome]
    if ($w.($check.Axis) - $forestWater.($check.Axis) -lt 10) {
        $waterFailures += "the $($check.Biome) pool is not $($check.What) than the forest's ($(Format-Tint $w) against $(Format-Tint $forestWater))"
    } else { Write-Step "the $($check.Biome) pool is $($check.What) than the forest's" }
}
# the controls: TF's fire swamp water in the overworld, the swampland's in the Twilight Forest
if ($water['control_fire_swamp_overworld'].RminusB - $water['overworld'].RminusB -lt 10) {
    $waterFailures += "control: TF's fire swamp painted into the overworld does not draw redder water than the overworld's own biome ($(Format-Tint $water['control_fire_swamp_overworld']) against $(Format-Tint $water['overworld']))"
} else { Write-Step "control: TF's fire swamp draws its red water in the overworld" }
if ($water['control_swampland_twilight'].GminusB - $forestWater.GminusB -lt 10) {
    $waterFailures += "control: the vanilla swampland painted into the Twilight Forest does not draw greener water than TF's forest ($(Format-Tint $water['control_swampland_twilight']) against $(Format-Tint $forestWater))"
} else { Write-Step "control: the vanilla swampland draws its green water in the Twilight Forest" }
# the water fog: the fire swamp's #6C2C2C against the forest's #050533
if ($underwater['fire_swamp'].RminusB - $underwater['forest'].RminusB -lt 10) {
    $waterFailures += "the fire swamp's water fog is not redder than the forest's ($(Format-Tint $underwater['fire_swamp']) against $(Format-Tint $underwater['forest']))"
} else { Write-Step "the fire swamp's water fog is redder than the forest's" }
foreach ($f in $waterFailures) { Write-Step "water: $f" }

$fiddleFailures = @()
foreach ($name in 'forest', 'swamp') {
    foreach ($k in 'fiddlehead', 'potted_fiddlehead') {
        $d = $fiddle["$name/$k"].Green - $fiddle["$name/backdrop"].Green
        Write-Step ("{0} {1}: {2:N1} greener than the backdrop" -f $name, $k, $d)
        if ($d -lt 3) { $fiddleFailures += "the $k in the $name is drawn grey (green $('{0:N1}' -f $d) over the backdrop)" }
    }
}
foreach ($f in $fiddleFailures) { Write-Step "fiddlehead: $f" }

if ($withMod) {
    # the client mod drew TF's colours
    $log = Get-LeviLog
    if (-not ($log | Select-String -Pattern 'biomes: TF biome colours on \d+ grass and \d+ foliage blocks' -Quiet)) { throw 'the client mod did not register its biome tints' }
    # the grass / foliage tints, leaves without seasons, the water colours and the plants' grass
    # tint method go through U1's hooks (feature/ext/biomes/VanillaTint.cpp; F12's resolver hook
    # never sees vanilla blocks)
    if (-not ($log | Select-String -Pattern 'biomes: vanilla tint hooks installed \(grass ok, foliage ok, seasons ok, water ok, plants ok\)' -Quiet)) { throw 'the vanilla tint hooks were not installed' }
    $applied = $log | Select-String -Pattern 'biomes: vanilla tint hook applied the tint of .*' | Select-Object -First 1
    if (-not $applied) { throw 'the vanilla tint hook never applied a tint (vanilla grass and leaves drew without TF colours)' }
    Write-Step "hook: $($applied.Matches[0].Value)"
    # the chunks drawn in the TF biomes got TF's colours from the hook (not only the selftest's)
    foreach ($kind in 'grass', 'foliage') {
        $changed = $log | Select-String -Pattern "biomes: vanilla tint hook changed the $kind colour of .*" | Select-Object -First 1
        if (-not $changed) { throw "the vanilla tint hook never drew a TF $kind colour on a vanilla block" }
        Write-Step "hook: $($changed.Matches[0].Value)"
    }
    # (the snowy forest's leaves without the seasons map are checked by biomes.grass_tint)
    foreach ($pattern in "biomes: TF's biome foliage colour drawn on twilightforest:.*", "biomes: water tessellation hook drew TF's water of .*", "biomes: TF's grass tint given to the material instance .*") {
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
