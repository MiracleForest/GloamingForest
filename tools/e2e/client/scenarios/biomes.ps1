# Biomes (U1) on the client: what a player sees of the 20 surface biomes of the Twilight Forest.
# biomes.survey (on the world's server: the integrated one, or the BDS host in remote modes) finds a
# place inside each biome for the world's seed; the player enters the Twilight Forest (/tf tp), turns
# spectator with night vision, and at each place a test patch is built on the generated chunks
# (17x17 grass with short grass, a water pool and an oak leaf block: the biome's grass, foliage and
# water colours; the sky and fog around it) and photographed.
# - local / remote-mod (the client mod): TF's position-dependent colours (spooky forest, dark forest
#   centre, enchanted forest bands) are drawn by the mod (src/client/biomes/Tints.cpp logs the first
#   use per biome, applied to vanilla blocks by feature/ext/biomes/VanillaTint.cpp's hook); the
#   other colours come from the client biome files. biomes.grass_tint runs in local mode.
# - remote-vanilla: the client biome files reach the client with the host's resource packs; the
#   static colours only.
# The ground tint of the spooky forest (orange-red #C45123 family) must be redder than the forest's.
# Screenshots: out/biomes[-<mode>]/{twilight,biome_<name> x20,selftest}.png
$hostPrefix = if ($script:Mode -eq 'local') { '' } else { '\[host\].*' }

# The mean colour of the ground in front of the player (the bottom middle of a screenshot).
function Get-GroundTint([string]$path) {
    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    try {
        $r = 0.0; $g = 0.0; $b = 0.0; $n = 0
        for ($y = [int]($bmp.Height * 0.70); $y -lt [int]($bmp.Height * 0.90); $y += 3) {
            for ($x = [int]($bmp.Width * 0.35); $x -lt [int]($bmp.Width * 0.65); $x += 3) {
                $p = $bmp.GetPixel($x, $y); $r += $p.R; $g += $p.G; $b += $p.B; $n++
            }
        }
        return [pscustomobject]@{ R = $r / $n; G = $g / $n; B = $b / $n; RminusG = ($r - $g) / $n }
    } finally { $bmp.Dispose() }
}
function Format-Tint($t) { '#{0:X2}{1:X2}{2:X2} (R-G {3:N1})' -f [int]$t.R, [int]$t.G, [int]$t.B, $t.RminusG }

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
if ($places.Count -lt 18) { throw "biomes.survey found only $($places.Count) surface biomes" }

Invoke-Chat '/gamemode spectator'
Invoke-Chat '/effect @s night_vision 100000 0 true'
Invoke-Chat '/tf tp'
Start-Sleep -Seconds 10
Save-Screenshot 'twilight' | Out-Null

$tints = @{}
foreach ($name in $places.Keys) {
    $x = $places[$name][0]; $z = $places[$name][1]
    $y = 40
    Invoke-Teleport "/tp @s $x $($y + 8) $z"
    Start-Sleep -Seconds 6
    Invoke-Chat "/fill $($x - 8) $y $($z - 8) $($x + 8) $y $($z + 8) grass_block"
    Invoke-Chat "/fill $($x - 8) $($y + 1) $($z - 8) $($x + 8) $($y + 1) $($z + 8) air"
    Invoke-Chat "/fill $($x - 7) $($y + 1) $($z + 2) $($x - 3) $($y + 1) $($z + 6) short_grass"
    Invoke-Chat "/fill $($x + 3) $y $($z + 2) $($x + 6) $y $($z + 5) water"
    Invoke-Chat "/fill $($x - 1) $($y + 1) $($z + 6) $($x + 1) $($y + 2) $($z + 7) oak_leaves"
    # the camera south of the patch, looking north-ish down onto it (Bedrock yaw 180 faces -z)
    Invoke-Teleport "/tp @s $x $($y + 6) $($z + 14) 180 28"
    Start-Sleep -Seconds 3
    $shot = Save-Screenshot "biome_$name"
    $tints[$name] = Get-GroundTint $shot
    Write-Step "$name at $x $z`: ground $(Format-Tint $tints[$name])"
}

if ($tints.Contains('spooky_forest') -and $tints.Contains('forest')) {
    if ($tints['spooky_forest'].RminusG - $tints['forest'].RminusG -lt 10) {
        throw "the spooky forest's grass is not drawn redder than the forest's (spooky $(Format-Tint $tints['spooky_forest']), forest $(Format-Tint $tints['forest']))"
    }
}
if ($script:Mode -ne 'remote-vanilla') {
    # the client mod drew TF's position-dependent colours
    $log = Get-LeviLog
    if (-not ($log | Select-String -Pattern 'biomes: TF biome colours on \d+ grass and \d+ foliage blocks' -Quiet)) { throw 'the client mod did not register its biome tints' }
    # the vanilla grass / foliage tints go through U1's hook on the game's tessellation policies
    # (feature/ext/biomes/VanillaTint.cpp; F12's resolver hook never sees vanilla blocks)
    if (-not ($log | Select-String -Pattern 'biomes: vanilla tint hooks installed \(grass ok, foliage ok\)' -Quiet)) { throw 'the vanilla tint hooks were not installed' }
    $applied = $log | Select-String -Pattern 'biomes: vanilla tint hook applied the tint of .*' | Select-Object -First 1
    if (-not $applied) { throw 'the vanilla tint hook never applied a tint (vanilla grass and leaves drew without TF colours)' }
    Write-Step "hook: $($applied.Matches[0].Value)"
    # the chunks drawn in the TF biomes got TF's colours from the hook (not only the selftest's)
    foreach ($kind in 'grass', 'foliage') {
        $changed = $log | Select-String -Pattern "biomes: vanilla tint hook changed the $kind colour of .*" | Select-Object -First 1
        if (-not $changed) { throw "the vanilla tint hook never drew a TF $kind colour on a vanilla block" }
        Write-Step "hook: $($changed.Matches[0].Value)"
    }
    foreach ($b in 'spooky_forest', 'dark_forest_center', 'enchanted_forest') {
        if ($places.Contains($b) -and -not ($log | Select-String -Pattern "biomes: client draws TF's grass colours of twilightforest:$b" -Quiet)) {
            throw "the client mod did not draw TF's grass colours in twilightforest:$b"
        }
    }
}
Invoke-Chat '/gamemode creative'
Save-Screenshot 'selftest' | Out-Null
