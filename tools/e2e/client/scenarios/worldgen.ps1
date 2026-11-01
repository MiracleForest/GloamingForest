# Worldgen (F5) on the client build: the same selftests as the server (the fixture feature and the
# in-memory pipeline hash identically on both builds), the 200-chunk timing logged by the client's
# integrated server, and the client's view of a custom biome (spike S2): the probe biome of
# worldgen.prelaunch.ps1 is painted into chunks 3000 blocks away that the client has not received,
# the player travels there, a screenshot is taken and the biome the client's own level holds under
# the player is logged (the probe's resource pack look is magenta fog and water).
# Screenshots: out/worldgen/{world,custom_biome,selftest}.png
Save-Screenshot 'world' | Out-Null
if ($script:Mode -eq 'local') {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $mark = Get-LogMark
    $r = Invoke-SelfTest 'worldgen.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) worldgen selftest(s) failed" }
    if ($r.Pass -lt 15) { throw "expected 15 worldgen selftests to pass, got $($r.Pass)" }
    if (-not (Get-LeviLog -Since $mark | Select-String -Pattern 'worldgen timing: 200 chunks' -Quiet)) { throw 'no 200-chunk timing line logged' }
    if (-not (Get-LeviLog -Since $mark | Select-String -Pattern 'worldgen custom biome: \S+ written and read back' -Quiet)) { throw 'the custom probe biome was not written into a chunk' }

    # the client's view of the custom biome
    Invoke-Chat '/tag @s add gf_worldgen_biome_show'
    Start-Sleep -Milliseconds 800
    $paintMark = Get-LogMark
    $p = Invoke-SelfTest 'worldgen.custom_biome'
    if ($p.Fail -gt 0) { throw 'worldgen.custom_biome failed while painting' }
    $painted = Get-LeviLog -Since $paintMark | Select-String -Pattern 'worldgen custom biome: painted (\d+) columns' | Select-Object -Last 1
    if (-not $painted) { throw 'the probe biome was not painted' }
    Write-Step "painted $($painted.Matches[0].Groups[1].Value) columns 3000 blocks away with the probe biome"
    # the first visit: the server sends those chunks with the custom biome
    Invoke-Teleport '/tp @s ~3000 ~ ~'
    Start-Sleep -Seconds 10
    Save-Screenshot 'custom_biome' | Out-Null
    $seenMark = Get-LogMark
    $restore = Invoke-SelfTest 'worldgen.custom_biome_restore'
    if ($restore.Fail -gt 0) { throw 'worldgen.custom_biome_restore failed' }
    $seen = Get-LeviLog -Since $seenMark | Select-String -Pattern 'the client level has (\S+) under its player' | Select-Object -Last 1
    if ($seen) { Write-Step "client level biome under the player: $($seen.Matches[0].Groups[1].Value)" }
    Invoke-Teleport '/tp @s ~-3000 ~ ~'
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'worldgen.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) worldgen selftest(s) failed on the host" }
    Start-Sleep -Seconds 5
    if (-not (Get-GameProcess)) { throw 'the game closed while connected to the host' }
}
Save-Screenshot 'selftest' | Out-Null
