# Woodwork (U4), short: the drying rack items (front and side) and the TF boats, without the block
# rows of the main scenario (quick checks of the rack display entity and the boat geometry).
#   out/woodwork.display/woodwork-rack-items.png, woodwork-rack-side.png, woodwork-boat-pond.png,
#   woodwork-boat-ride-third.png
function Invoke-Build([string]$command) {
    if ($script:Mode -eq 'local') { Invoke-Chat "/$command" } else { Send-HostConsole $command }
}
if ($script:Mode -eq 'local') { Wait-LogLine 'GloamingForest world ready' 120 | Out-Null } else { Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null }
Invoke-Build 'gamerule sendcommandfeedback false'
Invoke-Build 'time set 6000'
$y = -60; $z = 46
$mark = Get-LogMark
if ($script:Mode -eq 'local') { Invoke-SelfTest 'woodwork.showcase' | Out-Null } else { Send-HostConsole 'tf selftest woodwork.showcase' }
$line = Wait-LogLine 'woodwork: showcase racks at -?\d+ -?\d+ -?\d+' 60 -Since $mark
if ("$line" -notmatch 'showcase racks at (-?\d+) (-?\d+) (-?\d+)') { throw 'no showcase racks' }
$rx = [int]$Matches[1]; $ry = [int]$Matches[2]; $rz = [int]$Matches[3]
Invoke-Teleport "/tp @s $($rx + 2.5) $($ry - 0.6) $($rz - 2.5) 0 5"
Start-Sleep -Seconds 6
Save-Screenshot 'woodwork-rack-items' | Out-Null
Invoke-Teleport "/tp @s $($rx + 6.5) $($ry - 0.6) $($rz + 0.5) 90 5"
Start-Sleep -Seconds 3
Save-Screenshot 'woodwork-rack-side' | Out-Null

$bx = 900
Invoke-Teleport "/tp @s $($bx + 3) $($y + 3) $($z - 8) 0 25"
Start-Sleep -Seconds 3
Invoke-Build "fill $($bx - 1) $($y - 1) $($z - 1) $($bx + 9) $($y - 1) $($z + 6) minecraft:stone"
Invoke-Build "fill $($bx) $($y - 1) $($z) $($bx + 8) $($y - 1) $($z + 5) minecraft:water"
Start-Sleep -Seconds 2
Invoke-Build "summon twilightforest:boat $($bx + 2) $($y - 0.5) $($z + 2) 0 0 twilightforest:canopy"
Invoke-Build "summon twilightforest:chest_boat $($bx + 6) $($y - 0.5) $($z + 2) 0 0 twilightforest:sorting"
Start-Sleep -Seconds 4
Invoke-Teleport "/tp @s $($bx + 4) $($y + 2) $($z - 3) 0 30"
Start-Sleep -Seconds 3
Save-Screenshot 'woodwork-boat-pond' | Out-Null
Invoke-Chat "/ride @s start_riding @e[type=twilightforest:boat,c=1] teleport_rider"
Start-Sleep -Seconds 3
Focus-Game
[BsE2E.Native]::Key([uint16]0x74, $false); Start-Sleep -Milliseconds 60; [BsE2E.Native]::Key([uint16]0x74, $true)
Start-Sleep -Seconds 2
Save-Screenshot 'woodwork-boat-ride-third' | Out-Null
Reset-CameraPerspective
Invoke-Chat "/ride @s stop_riding"
