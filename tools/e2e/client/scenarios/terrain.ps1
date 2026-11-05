# Terrain (U2): the client renders what TF's terrain generator makes.
# 1. terrain.showcase generates 3 x 3 chunks of one key biome at a time with the real noise, surface
#    rules and chunk blankets (seed 0) and copies them into the test row: the highlands (raised
#    ground, podzol / coarse dirt by the surface noise), a dark forest under its hardened dark leaves
#    canopy (surface + 14) and a glacier (gravel under 32 packed ice capped with ice).
#    terrain.prelaunch.ps1 makes each sample stay 400 ticks; the scenario flies to the viewpoint the
#    test logs (over the sample's east edge, looking west) and takes a screenshot.
# 2. terrain.live_view sends the player into Twilight Forest chunks the game generates there and
#    then (F5's pipeline with the terrain, surface, blanket and carver providers, F6's starts); the
#    biomes are U1's biome source (the forest column until U1 registers one).
# Screenshots: out/terrain/{start,highlands,dark_forest,glacier,twilight_live,twilight_live_down}.png
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
    Invoke-Teleport "/tp @s $x $y $z 90 28"
    Start-Sleep -Seconds 5
    Save-Screenshot $name | Out-Null
}
$done = Wait-LogLine 'SELFTEST DONE pass=(\d+) fail=(\d+)' 120 -Since $mark
$d = [regex]::Match($done, 'pass=(\d+) fail=(\d+)')
if ([int]$d.Groups[2].Value -gt 0 -or [int]$d.Groups[1].Value -lt 1) { throw "terrain.showcase failed: $done" }
Write-Step 'terrain.showcase passed'

# real Twilight Forest chunks
Invoke-Chat '/tag @s add gf_terrain_live'
Start-Sleep -Milliseconds 800
$liveMark = Get-LogMark
$v = Invoke-SelfTest 'terrain.live_view'
if ($v.Fail -gt 0) { throw 'terrain.live_view failed' }
$live = Get-LeviLog -Since $liveMark | Select-String -Pattern 'terrain live view at (-?\d+) (-?\d+) (-?\d+) \(terrain height (-?\d+), biome (\S*)\)' | Select-Object -Last 1
if (-not $live) { throw 'terrain.live_view did not send the player into the Twilight Forest' }
$lx = [int]$live.Matches[0].Groups[1].Value; $ly = [int]$live.Matches[0].Groups[2].Value; $lz = [int]$live.Matches[0].Groups[3].Value
Write-Step "live Twilight Forest terrain at $lx $ly $lz (terrain height $($live.Matches[0].Groups[4].Value), main biome '$($live.Matches[0].Groups[5].Value)')"
Start-Sleep -Seconds 15
Invoke-Teleport "/tp @s $lx $ly $lz 0 25"
Start-Sleep -Seconds 5
Save-Screenshot 'twilight_live' | Out-Null
Invoke-Teleport "/tp @s $lx $ly $lz 0 70"
Start-Sleep -Seconds 3
Save-Screenshot 'twilight_live_down' | Out-Null
$back = Invoke-SelfTest 'terrain.live_view_restore'
if ($back.Fail -gt 0) { throw 'terrain.live_view_restore failed' }
Start-Sleep -Seconds 5
