# Worldgen (F5) on the client build: the same selftests as the server (the fixture feature and the
# in-memory pipeline hash identically on both builds), the 200-chunk timing logged by the client's
# integrated server, and what a client sees of biomes the server writes (spike S2), with controls:
# - painted chunks the client has never received, kept loaded by a ticking area: the probe biome
#   of worldgen.prelaunch.ps1 (magenta fog and water) 3000 blocks east, minecraft:desert (a vanilla
#   biome with its own grass and water colours, painted the same way) 3000 blocks south;
# - with F4's dimension: fresh Twilight Forest chunks the generator writes the probe into, and,
#   as the control, fresh chunks it writes minecraft:desert into.
# At each place a screenshot is taken and worldgen.custom_biome_view logs the biome the server and
# the client's own level hold under the player.
# Screenshots: out/worldgen/{world,custom_biome,custom_biome_desert,generated_probe,generated_desert,selftest}.png
Save-Screenshot 'world' | Out-Null
if ($script:Mode -eq 'local') {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $mark = Get-LogMark
    $r = Invoke-SelfTest 'worldgen.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) worldgen selftest(s) failed" }
    if ($r.Pass -lt 24) { throw "expected 24 worldgen selftests to pass, got $($r.Pass)" }
    if (-not (Get-LeviLog -Since $mark | Select-String -Pattern 'worldgen timing: 200 chunks' -Quiet)) { throw 'no 200-chunk timing line logged' }
    if (-not (Get-LeviLog -Since $mark | Select-String -Pattern 'worldgen custom biome: \S+ written and read back' -Quiet)) { throw 'the custom probe biome was not written into a chunk' }

    function Write-BiomeView([string]$where) {
        $m = Get-LogMark
        $v = Invoke-SelfTest 'worldgen.custom_biome_view'
        if ($v.Fail -gt 0) { throw 'worldgen.custom_biome_view failed' }
        $seen = Get-LeviLog -Since $m | Select-String -Pattern 'the server has (\S+), the client level has (\S+) under its player' | Select-Object -Last 1
        if (-not $seen) { throw "no biome view line at $where" }
        Write-Step "$where - server: $($seen.Matches[0].Groups[1].Value), client level: $($seen.Matches[0].Groups[2].Value)"
    }

    # painted chunks: the probe 3000 blocks east, the desert control 3000 blocks south
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
    Save-Screenshot 'custom_biome' | Out-Null
    Write-BiomeView 'painted probe (east)'
    Invoke-Teleport '/tp @s ~-3000 ~ ~3000'
    Start-Sleep -Seconds 10
    Save-Screenshot 'custom_biome_desert' | Out-Null
    Write-BiomeView 'painted desert (south)'
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
        Write-BiomeView 'generated probe (Twilight Forest)'
        Invoke-Chat '/tag @s add gf_worldgen_gen_desert'
        Start-Sleep -Milliseconds 800
        $g2 = Invoke-SelfTest 'worldgen.generator_view'
        if ($g2.Fail -gt 0) { throw 'worldgen.generator_view (desert) failed' }
        Start-Sleep -Seconds 12
        Save-Screenshot 'generated_desert' | Out-Null
        Write-BiomeView 'generated desert (Twilight Forest)'
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
}
Save-Screenshot 'selftest' | Out-Null
