# Terrain (U2): the client renders what TF's terrain generator makes. terrain.showcase generates
# 3 x 3 chunks of one key biome at a time with the real noise, surface rules and chunk blankets
# (seed 0) and copies them into the test row: the highlands (raised ground, podzol / coarse dirt
# by the surface noise), a dark forest under its hardened dark leaves canopy (surface + 14) and a
# glacier (gravel under 32 packed ice capped with ice). terrain.prelaunch.ps1 makes each sample stay
# 400 ticks; the scenario flies to the viewpoint the test logs and takes a screenshot.
# Screenshots: out/terrain/{start,highlands,dark_forest,glacier}.png
# The unit has no vanilla fallback of its own (terrain is server-side blocks): run -Mode local.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
Save-Screenshot 'start' | Out-Null

Invoke-Chat '/effect @s resistance 600 4 true'
Invoke-Chat '/effect @s night_vision 600 0 true'
Invoke-Chat '/gamemode creative'
$mark = Get-LogMark
Send-SelfTestCommand (Format-SelfTestCommand 'terrain.showcase')
foreach ($name in @('highlands', 'dark_forest', 'glacier')) {
    $line = Wait-LogLine "terrain showcase $name ready at (-?\d+) (-?\d+) (-?\d+) \(generated surface (-?\d+)\.\.(-?\d+)\)" 180 -Since $mark
    $m = [regex]::Match($line, "ready at (-?\d+) (-?\d+) (-?\d+) \(generated surface (-?\d+)\.\.(-?\d+)\)")
    $x = [int]$m.Groups[1].Value; $y = [int]$m.Groups[2].Value; $z = [int]$m.Groups[3].Value
    Write-Step "$name sample: generated surface y $($m.Groups[4].Value)..$($m.Groups[5].Value), viewpoint $x $y $z"
    Invoke-Teleport "/tp @s $x $y $z -90 28"
    Start-Sleep -Seconds 5
    Save-Screenshot $name | Out-Null
}
$done = Wait-LogLine 'SELFTEST DONE pass=(\d+) fail=(\d+)' 120 -Since $mark
$d = [regex]::Match($done, 'pass=(\d+) fail=(\d+)')
if ([int]$d.Groups[2].Value -gt 0 -or [int]$d.Groups[1].Value -lt 1) { throw "terrain.showcase failed: $done" }
Write-Step 'terrain.showcase passed'
