# Worldgen (F5) on the client build: the same selftests as the server (the fixture feature and the
# in-memory pipeline hash identically on both builds), the 200-chunk timing logged by the client's
# integrated server, and what a client sees of biomes the server writes (spike S2), with controls:
# - painted chunks the client has never received, kept loaded by a ticking area: the probe biome
#   of worldgen.prelaunch.ps1 (magenta sky, grass, water and fog in its client biome file) 3000
#   blocks east, minecraft:desert (a vanilla biome with its own grass and water colours, painted the
#   same way) 3000 blocks south;
# - with F4's dimension: fresh Twilight Forest chunks the generator writes the probe into, and,
#   as the control, fresh chunks it writes minecraft:desert into.
# At each place a screenshot is taken and worldgen.custom_biome_view logs the biome the server and
# the client's own level hold under the player: they must be the same.
# Remote modes (a BDS host without the probe): the painted desert 3000 blocks south reaches the
# client through the chunk data; the client mod is not involved (remote-vanilla: no client mod), so
# the evidence is the screenshot: its ground must be tinted unlike the home ground.
# Screenshots: out/worldgen/{world,home,custom_biome,custom_biome_desert,generated_probe,generated_desert,selftest}.png
#              remote: out/worldgen/{world,home,remote_desert,selftest}.png

# The mean colour of the ground in front of the player (the bottom middle of a screenshot).
function Get-GroundTint([string]$path) {
    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    try {
        $r = 0.0; $g = 0.0; $b = 0.0; $n = 0
        for ($y = [int]($bmp.Height * 0.72); $y -lt [int]($bmp.Height * 0.95); $y += 3) {
            for ($x = [int]($bmp.Width * 0.30); $x -lt [int]($bmp.Width * 0.70); $x += 3) {
                $p = $bmp.GetPixel($x, $y); $r += $p.R; $g += $p.G; $b += $p.B; $n++
            }
        }
        return [pscustomobject]@{ R = $r / $n; G = $g / $n; B = $b / $n; RminusG = ($r - $g) / $n }
    } finally { $bmp.Dispose() }
}
function Format-Tint($t) { '#{0:X2}{1:X2}{2:X2} (R-G {3:N1})' -f [int]$t.R, [int]$t.G, [int]$t.B, $t.RminusG }

Save-Screenshot 'world' | Out-Null
if ($script:Mode -eq 'local') {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $mark = Get-LogMark
    $r = Invoke-SelfTest 'worldgen.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) worldgen selftest(s) failed" }
    if ($r.Pass -lt 26) { throw "expected 26 worldgen selftests to pass, got $($r.Pass)" }
    if (-not (Get-LeviLog -Since $mark | Select-String -Pattern 'worldgen timing: 200 chunks' -Quiet)) { throw 'no 200-chunk timing line logged' }
    if (-not (Get-LeviLog -Since $mark | Select-String -Pattern 'worldgen custom biome: \S+ written and read back' -Quiet)) { throw 'the custom probe biome was not written into a chunk' }

    # The biome the server and the client level hold under the player: the same, and the expected one.
    function Write-BiomeView([string]$where, [string]$expected) {
        $m = Get-LogMark
        $v = Invoke-SelfTest 'worldgen.custom_biome_view'
        if ($v.Fail -gt 0) { throw 'worldgen.custom_biome_view failed' }
        $seen = Get-LeviLog -Since $m | Select-String -Pattern 'the server has (\S+), the client level has (\S+) under its player' | Select-Object -Last 1
        if (-not $seen) { throw "no biome view line at $where" }
        $server = $seen.Matches[0].Groups[1].Value; $client = $seen.Matches[0].Groups[2].Value
        Write-Step "$where - server: $server, client level: $client"
        if ($server -ne $client) { throw "$where`: the client level holds $client, the server $server" }
        if ($server -ne $expected) { throw "$where`: the server holds $server, not $expected" }
    }

    # painted chunks: the probe 3000 blocks east, the desert control 3000 blocks south
    $homeTint = Get-GroundTint (Save-Screenshot 'home')
    Invoke-Chat '/tag @s add gf_worldgen_biome_show'
    Start-Sleep -Milliseconds 800
    $paintMark = Get-LogMark
    $p = Invoke-SelfTest 'worldgen.custom_biome'
    if ($p.Fail -gt 0) { throw 'worldgen.custom_biome failed while painting' }
    $painted = @(Get-LeviLog -Since $paintMark | Select-String -Pattern 'worldgen custom biome: painted (\d+) columns 3000 blocks (\S+) of')
    if ($painted.Count -lt 2) { throw 'the probe and the desert control were not both painted' }
    foreach ($line in $painted) { Write-Step "painted $($line.Matches[0].Groups[1].Value) columns 3000 blocks $($line.Matches[0].Groups[2].Value)" }
    Invoke-Teleport '/tp @s ~3000 ~ ~'
    Start-Sleep -Seconds 10
    $probeTint = Get-GroundTint (Save-Screenshot 'custom_biome')
    Write-BiomeView 'painted probe (east)' 'gf_test:worldgen_probe'
    Invoke-Teleport '/tp @s ~-3000 ~ ~3000'
    Start-Sleep -Seconds 10
    $desertTint = Get-GroundTint (Save-Screenshot 'custom_biome_desert')
    Write-BiomeView 'painted desert (south)' 'minecraft:desert'
    Write-Step "ground tint: home $(Format-Tint $homeTint), probe $(Format-Tint $probeTint), desert $(Format-Tint $desertTint)"
    Invoke-Teleport '/tp @s ~ ~ ~-3000'
    $restore = Invoke-SelfTest 'worldgen.custom_biome_restore'
    if ($restore.Fail -gt 0) { throw 'worldgen.custom_biome_restore failed' }

    # generated chunks (F4's dimension): the probe, then the desert control
    $genMark = Get-LogMark
    $g = Invoke-SelfTest 'worldgen.generator_view'
    if ($g.Fail -gt 0) { throw 'worldgen.generator_view failed' }
    if (Get-LeviLog -Since $genMark | Select-String -Pattern 'worldgen generator view: the biome source gives' -Quiet) {
        Start-Sleep -Seconds 12
        Save-Screenshot 'generated_probe' | Out-Null
        Write-BiomeView 'generated probe (Twilight Forest)' 'gf_test:worldgen_probe'
        Invoke-Chat '/tag @s add gf_worldgen_gen_desert'
        Start-Sleep -Milliseconds 800
        $g2 = Invoke-SelfTest 'worldgen.generator_view'
        if ($g2.Fail -gt 0) { throw 'worldgen.generator_view (desert) failed' }
        Start-Sleep -Seconds 12
        Save-Screenshot 'generated_desert' | Out-Null
        Write-BiomeView 'generated desert (Twilight Forest)' 'minecraft:desert'
        $gr = Invoke-SelfTest 'worldgen.generator_view_restore'
        if ($gr.Fail -gt 0) { throw 'worldgen.generator_view_restore failed' }
        Start-Sleep -Seconds 5
    } else {
        Write-Step 'no Twilight Forest dimension (F4): the generator path was not viewed'
    }
    Invoke-Chat '/tag @s remove gf_worldgen_biome_show'
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'worldgen.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) worldgen selftest(s) failed on the host" }
    Start-Sleep -Seconds 5
    if (-not (Get-GameProcess)) { throw 'the game closed while connected to the host' }

    # a painted vanilla biome reaches this client through the chunk data (no probe on the host)
    $homeTint = Get-GroundTint (Save-Screenshot 'home')
    Invoke-Chat '/tag @s add gf_worldgen_biome_show'
    Start-Sleep -Milliseconds 800
    $paintMark = Get-LogMark
    $p = Invoke-SelfTest 'worldgen.custom_biome'
    if ($p.Fail -gt 0) { throw 'worldgen.custom_biome failed on the host while painting' }
    $painted = Get-LeviLog -Since $paintMark | Select-String -Pattern 'worldgen custom biome: painted (\d+) columns 3000 blocks south of \S+ with minecraft:desert' | Select-Object -Last 1
    if (-not $painted) { throw 'the host did not paint the desert 3000 blocks south' }
    Write-Step "the host painted $($painted.Matches[0].Groups[1].Value) columns 3000 blocks south with minecraft:desert"
    Invoke-Teleport '/tp @s ~ ~ ~3000'
    Start-Sleep -Seconds 12
    $desert = Get-GroundTint (Save-Screenshot 'remote_desert')
    $viewMark = Get-LogMark
    $v = Invoke-SelfTest 'worldgen.custom_biome_view'
    if ($v.Fail -gt 0) { throw 'worldgen.custom_biome_view failed on the host' }
    $seen = Get-LeviLog -Since $viewMark | Select-String -Pattern 'the server has (\S+), the client level has' | Select-Object -Last 1
    if (-not $seen -or $seen.Matches[0].Groups[1].Value -ne 'minecraft:desert') { throw 'the host does not hold minecraft:desert under the player' }
    Write-Step "ground tint ($script:Mode client): home $(Format-Tint $homeTint), painted desert $(Format-Tint $desert)"
    # plains grass #91BD59 (R-G -44), desert grass #BFB755 (R-G +8): the client draws the desert
    if ($desert.RminusG - $homeTint.RminusG -lt 15) { throw "the client does not draw the painted desert (ground R-G $($homeTint.RminusG) at home, $($desert.RminusG) on the desert)" }
    Invoke-Teleport '/tp @s ~ ~ ~-3000'
    $restore = Invoke-SelfTest 'worldgen.custom_biome_restore'
    if ($restore.Fail -gt 0) { throw 'worldgen.custom_biome_restore failed on the host' }
    Invoke-Chat '/tag @s remove gf_worldgen_biome_show'
}
Save-Screenshot 'selftest' | Out-Null
