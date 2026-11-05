# Trees (U3) on the client: every tree and mushroom feature of the unit and every block (10 leaves,
# 10 saplings, 10 potted saplings, the 4 magic log cores, two of them active, the mangrove root, the
# hardened dark leaves) built by trees.showcase in the local world, then photographed. Compare with
# TF's textures (textures/block/*leaves*, *sapling*, *_log_core*) and TF's tree shapes.
# Screenshots: out/trees/{tree-<n>-<name>,blocks,blocks-close,cores,selftest}.png
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
    foreach ($l in $lines) {
        $m = $l.Matches[0]
        $n = [int]$m.Groups[1].Value
        if ($seen.ContainsKey($n)) { continue }
        $seen[$n] = $true
        $name = ($m.Groups[2].Value -replace '^.*[:/]', '')
        $x = [int]$m.Groups[3].Value; $y = [int]$m.Groups[4].Value; $z = [int]$m.Groups[5].Value
        # from the south-east, high enough for the canopies
        Invoke-Teleport "/tp @s $($x + 22) $($y + 16) $($z + 22) 135 20"
        Start-Sleep -Seconds 3
        Save-Screenshot ('tree-{0:d2}-{1}' -f $n, $name) | Out-Null
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
} else {
    Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null
}
Save-Screenshot 'selftest' | Out-Null
