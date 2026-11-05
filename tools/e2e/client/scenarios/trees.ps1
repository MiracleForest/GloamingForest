# Trees (U3) on the client.
# local: every tree and mushroom feature of the unit and every block (10 leaves, 10 saplings, 10
# potted saplings, the 4 magic log cores, two of them active, the mangrove root, the hardened dark
# leaves) built by trees.showcase in the local world, then photographed; a close look from above at
# the brown canopy mushroom's cap, the active cores with their particles and the leaf runes drawn by
# the client mod (src/client/trees). trees.zz_showcase_restore puts the world back afterwards.
# remote-vanilla: trees.vanilla_probe on the host (leaves of transformation over an active timewood
# clock ticking wheat); the host sends the leaf runes to the vanilla client itself, and the time core's
# particles come as resource pack effects (the content log must stay clean of Molang errors).
# Compare with TF's textures (textures/block/*leaves*, *sapling*, *_log_core*) and TF's tree shapes.
# Screenshots: out/trees*/{tree-<n>-<name>,brown-cap-top,blocks,blocks-close,cores,cores-particles,runes,vanilla-probe,selftest}.png
Save-Screenshot 'hud' | Out-Null
if ($script:Mode -eq 'local') {
    Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
    $mark = Get-LogMark
    $r = Invoke-SelfTest 'trees.showcase'
    if ($r.Fail -gt 0) { throw 'trees.showcase failed' }
    Invoke-Chat '/gamemode spectator'
    Invoke-Chat '/time set 6000'
    Invoke-Chat '/weather clear'
    $lines = @(Get-LeviLog | Select-String -Pattern 'trees showcase (\d+) (\S+) at (-?\d+) (-?\d+) (-?\d+)')
    if ($lines.Count -lt 32) { throw "the showcase placed $($lines.Count) trees" }
    $seen = @{}
    $brown = $null
    foreach ($l in $lines) {
        $m = $l.Matches[0]
        $n = [int]$m.Groups[1].Value
        if ($seen.ContainsKey($n)) { continue }
        $seen[$n] = $true
        $name = ($m.Groups[2].Value -replace '^.*[:/]', '')
        $x = [int]$m.Groups[3].Value; $y = [int]$m.Groups[4].Value; $z = [int]$m.Groups[5].Value
        if ($name -eq 'brown_canopy_mushroom') { $brown = @($x, $y, $z) }
        # from the south-east, high enough for the canopies
        Invoke-Teleport "/tp @s $($x + 22) $($y + 16) $($z + 22) 135 20"
        Start-Sleep -Seconds 3
        Save-Screenshot ('tree-{0:d2}-{1}' -f $n, $name) | Out-Null
    }
    if ($brown) {
        # the brown cap from straight above (TF: brown_mushroom_block top skin on the cap's top)
        Invoke-Teleport "/tp @s $($brown[0] + 0.5) $($brown[1] + 24) $($brown[2] + 0.5) 0 90"
        Start-Sleep -Seconds 3
        Save-Screenshot 'brown-cap-top' | Out-Null
    }
    $b = Get-LeviLog | Select-String -Pattern 'trees showcase blocks at (-?\d+) (-?\d+) (-?\d+)' | Select-Object -Last 1
    if (-not $b) { throw 'the showcase built no block row' }
    $bx = [int]$b.Matches[0].Groups[1].Value; $by = [int]$b.Matches[0].Groups[2].Value; $bz = [int]$b.Matches[0].Groups[3].Value
    Invoke-Teleport "/tp @s $($bx + 34) $($by + 6) $($bz + 14) 180 25"
    Start-Sleep -Seconds 3
    Save-Screenshot 'blocks' | Out-Null
    Invoke-Teleport "/tp @s $($bx + 9) $($by + 2) $($bz + 5) 180 15"
    Start-Sleep -Seconds 2
    Save-Screenshot 'blocks-close' | Out-Null
    # the cores (time and mining active: lit, the time core's particles on the grass around)
    Invoke-Teleport "/tp @s $($bx + 63) $($by + 2) $($bz + 5) 180 15"
    Start-Sleep -Seconds 3
    Save-Screenshot 'cores' | Out-Null
    Start-Sleep -Milliseconds 1200
    Save-Screenshot 'cores-particles' | Out-Null
    # leaves of transformation 4 blocks up shed runes (drawn by the client mod), over an active
    # timewood clock ticking wheat
    $r = Invoke-SelfTest 'trees.vanilla_probe'
    if ($r.Fail -gt 0) { throw 'trees.vanilla_probe failed' }
    $p = Get-LeviLog | Select-String -Pattern 'trees probe at (-?\d+) (-?\d+) (-?\d+)' | Select-Object -Last 1
    if (-not $p) { throw 'the probe logged no position' }
    $px = [int]$p.Matches[0].Groups[1].Value; $py = [int]$p.Matches[0].Groups[2].Value; $pz = [int]$p.Matches[0].Groups[3].Value
    Invoke-Teleport "/tp @s $($px + 0.5) $($py + 1) $($pz + 4.5) 180 -10"
    Wait-LogLine 'trees: the client draws the leaf runes' 30 | Out-Null
    Start-Sleep -Seconds 1
    Save-Screenshot 'runes' | Out-Null
    $r = Invoke-SelfTest 'trees.zz_showcase_restore'
    if ($r.Fail -gt 0) { throw 'trees.zz_showcase_restore failed' }
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
    $r = Invoke-SelfTest 'trees.vanilla_probe'
    if ($r.Fail -gt 0) { throw 'trees.vanilla_probe failed' }
    $p = Get-LeviLog | Select-String -Pattern 'trees probe at (-?\d+) (-?\d+) (-?\d+)' | Select-Object -Last 1
    if (-not $p) { throw 'the probe logged no position' }
    $px = [int]$p.Matches[0].Groups[1].Value; $py = [int]$p.Matches[0].Groups[2].Value; $pz = [int]$p.Matches[0].Groups[3].Value
    Invoke-Chat '/gamemode spectator'
    Invoke-Chat '/time set 6000'
    Invoke-Teleport "/tp @s $($px + 0.5) $($py + 1) $($pz + 4.5) 180 -10"
    if ($script:Mode -eq 'remote-vanilla') {
        Wait-LogLine '\[host\].*trees: the server sends the leaf runes to' 30 | Out-Null
    } else {
        Wait-LogLine 'trees: the client draws the leaf runes' 30 | Out-Null
    }
    Start-Sleep -Seconds 2
    Save-Screenshot 'vanilla-probe' | Out-Null
    $r = Invoke-SelfTest 'trees.zz_showcase_restore'
    if ($r.Fail -gt 0) { throw 'trees.zz_showcase_restore failed' }
}
Save-Screenshot 'selftest' | Out-Null
