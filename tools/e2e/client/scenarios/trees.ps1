# Trees (U3) on the client.
# local: every tree and mushroom feature of the unit and every block (10 leaves, 10 saplings, 10
# potted saplings, the 4 magic log cores, two of them active, the mangrove root, the hardened dark
# leaves) built by trees.showcase in the local world, then photographed; a close look from above at
# the brown canopy mushroom's cap, the active cores with their particles and the leaf runes drawn by
# the client mod (src/client/trees). trees.zz_showcase_restore puts the world back afterwards.
# remote-vanilla: trees.vanilla_probe on the host (leaves of transformation over an active timewood
# clock ticking wheat); the host sends the leaf runes to the vanilla client itself, and the time core's
# particles come as resource pack effects (the content log must stay clean of Molang errors).
# Compare with upstream's textures (textures/block/*leaves*, *sapling*, *_log_core*) and upstream's tree shapes.
# Screenshots: out/trees*/{tree-<n>-<name>,brown-cap-top,blocks,blocks-close,mushroom-blocks,cores,cores-particles,particles-<n>,sorting-<n>,transformed-cell,runes,leaf-items,leaf-items-hardened,vanilla-probe,selftest}.png
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
        # the brown cap from straight above (upstream: brown_mushroom_block top skin on the cap's top)
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
    # the game's own brown and red cap blocks (huge_mushroom_bits 14) at the end of the row: the
    # brown canopy mushroom's caps (brown-cap-top) must show the same skin
    Invoke-Teleport "/tp @s $($bx + 73) $($by + 2) $($bz + 4) 180 25"
    Start-Sleep -Seconds 2
    Save-Screenshot 'mushroom-blocks' | Out-Null
    # the sorting particles between two chests and the transformation particles around a fresh cell
    # of the stand-in biome (brown mesa grass), the chunk resent to this client
    $r = Invoke-SelfTest 'trees.particles_probe'
    if ($r.Fail -gt 0) { throw 'trees.particles_probe failed' }
    $q = Get-LeviLog | Select-String -Pattern 'trees particles probe at (-?\d+) (-?\d+) (-?\d+)' | Select-Object -Last 1
    if (-not $q) { throw 'the particles probe logged no position' }
    $qx = [int]$q.Matches[0].Groups[1].Value; $qy = [int]$q.Matches[0].Groups[2].Value; $qz = [int]$q.Matches[0].Groups[3].Value
    Invoke-Teleport "/tp @s $($qx + 0.5) $($qy + 5) $($qz + 9) 180 30"
    Wait-LogLine 'trees: the transformation core at .* \(chunk resent to [1-9]\d* players\)' 60 | Out-Null
    # the chunks around the probe load and render after the teleport; the cores run every 10 / 20 ticks
    Start-Sleep -Seconds 4
    Save-Screenshot 'particles-1' | Out-Null
    Start-Sleep -Milliseconds 300
    Save-Screenshot 'particles-2' | Out-Null
    Start-Sleep -Milliseconds 300
    Save-Screenshot 'particles-3' | Out-Null
    # the sortingwood engine moves a stick from the chest beside it to the chest 3 blocks away (out of
    # its input cube): the green sorting particles fly between them
    Wait-LogLine 'trees: the sorting core at .* sorted [1-9]\d* items' 60 | Out-Null
    Invoke-Teleport "/tp @s $($qx - 4.0) $($qy + 3) $($qz + 5) 180 30"
    Start-Sleep -Seconds 3
    Save-Screenshot 'sorting-1' | Out-Null
    Start-Sleep -Milliseconds 250
    Save-Screenshot 'sorting-2' | Out-Null
    Start-Sleep -Milliseconds 250
    Save-Screenshot 'sorting-3' | Out-Null
    Wait-LogLine 'trees: particles probe: [1-9]\d* chunk resends sent to real clients' 60 | Out-Null
    # one transformed 4x4 cell stays: the resent chunk shows the stand-in biome's brown grass there,
    # photographed from straight above
    Wait-LogLine 'trees: particles probe: the transformed cell at (-?\d+) (-?\d+) (-?\d+) stays' 90 | Out-Null
    $c = Get-LeviLog | Select-String -Pattern 'trees: particles probe: the transformed cell at (-?\d+) (-?\d+) (-?\d+) stays' | Select-Object -Last 1
    if (-not $c) { throw 'the particles probe kept no transformed cell' }
    $cx = [int]$c.Matches[0].Groups[1].Value; $cy = [int]$c.Matches[0].Groups[2].Value; $cz = [int]$c.Matches[0].Groups[3].Value
    Invoke-Teleport "/tp @s $($cx + 0.5) $($cy + 14) $($cz + 0.5) 0 90"
    Start-Sleep -Seconds 4
    Save-Screenshot 'transformed-cell' | Out-Null
    # leaves of transformation 4 blocks up shed runes (drawn by the client mod), over an active
    # timewood clock ticking wheat
    Invoke-Chat '/tag @s add gf_trees_probe'
    $r = Invoke-SelfTest 'trees.vanilla_probe'
    if ($r.Fail -gt 0) { throw 'trees.vanilla_probe failed' }
    $p = Get-LeviLog | Select-String -Pattern 'trees probe at (-?\d+) (-?\d+) (-?\d+)' | Select-Object -Last 1
    if (-not $p) { throw 'the probe logged no position' }
    $px = [int]$p.Matches[0].Groups[1].Value; $py = [int]$p.Matches[0].Groups[2].Value; $pz = [int]$p.Matches[0].Groups[3].Value
    Invoke-Teleport "/tp @s $($px + 0.5) $($py + 1) $($pz + 4.5) 180 -10"
    Wait-LogLine 'trees: the client draws the leaf runes' 30 | Out-Null
    Start-Sleep -Seconds 1
    Save-Screenshot 'runes' | Out-Null
    # the ten leaves as items (Java: the item colour without a world; the magic leaves at fade 0)
    Invoke-Chat '/gamemode creative'
    Invoke-Chat '/clear @s'
    $i = 0
    foreach ($leaf in 'gloaming_oak_leaves', 'canopy_leaves', 'mangrove_leaves', 'dark_leaves', 'rainbow_oak_leaves', 'time_leaves', 'transformation_leaves', 'mining_leaves', 'sorting_leaves') {
        Invoke-Chat "/replaceitem entity @s slot.hotbar $i gloamingforest:$leaf"
        $i++
    }
    Invoke-Teleport "/tp @s $($px + 0.5) $($py + 1) $($pz + 12) 180 -60"
    Start-Sleep -Seconds 2
    Save-Screenshot 'leaf-items' | Out-Null
    Invoke-Chat '/replaceitem entity @s slot.hotbar 0 gloamingforest:hardened_dark_leaves'
    Start-Sleep -Seconds 1
    Save-Screenshot 'leaf-items-hardened' | Out-Null
    Invoke-Chat '/clear @s'
    Invoke-Chat '/gamemode spectator'
    $r = Invoke-SelfTest 'trees.zz_showcase_restore'
    if ($r.Fail -gt 0) { throw 'trees.zz_showcase_restore failed' }
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
    Invoke-Chat '/tag @s add gf_trees_probe'
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
