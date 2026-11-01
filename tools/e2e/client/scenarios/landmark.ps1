# Landmark (F6), client: the protection boxes and particles of a refused landmark interaction.
# The selftest landmark.display builds the fixture landmark (a stone brick shell, protected like
# TF's lich tower) in its test area and lets a simulated survival player without progress_naga try
# its wall every 2 s for 12 s; this scenario brings the camera there and takes the screenshots.
#   local          the client mod: twilightforest:protection_box entities (60 ticks, refreshed) and
#                  the protection particles; the client also logs landmark.area_protection
#   remote-mod     the same against BDS, the companion packet travels over the network
#   remote-vanilla a vanilla client: the same boxes and particles, no companion packet
# Screenshots: out/landmark/{protection-boxes,protection-boxes-2}.png
if ($script:Mode -eq 'local') { Wait-LogLine 'GloamingForest world ready' 120 | Out-Null }
else { Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null }

Invoke-Chat '/gamemode creative'
$mark = Get-LogMark
Invoke-Chat '/twilightforest selftest "landmark.display"'
$area = Wait-LogLine 'landmark\.display: area at (-?\d+) (-?\d+) (-?\d+)' 90 -Since $mark
if ($area -notmatch 'area at (-?\d+) (-?\d+) (-?\d+)') { throw "no display area in '$area'" }
$x = [int]$Matches[1]; $y = [int]$Matches[2]; $z = [int]$Matches[3]
# stand north-east of the shell, a little above it, looking at the refused east wall
Invoke-Teleport "/tp @s $($x + 12) $($y + 6) $($z - 9) facing $($x + 3) $($y + 2) $z"
Wait-LogLine 'landmark\.display: protection shown' 60 -Since $mark | Out-Null
Start-Sleep -Milliseconds 700
Save-Screenshot 'protection-boxes' | Out-Null
Start-Sleep -Seconds 2
Save-Screenshot 'protection-boxes-2' | Out-Null

if ($script:Mode -ne 'remote-vanilla') {
    # the client mod got AreaProtectionPacket's equivalent (the player is within 64 blocks)
    Wait-LogLine '^(?!\[host\]).*client landmark: area protection \d+ box\(es\) at' 30 -Since $mark | Out-Null
} elseif (Get-LeviLog | Select-String -Pattern '^(?!\[host\]).*client landmark:' -Quiet) {
    throw 'a vanilla client handled a companion packet'
}
$done = Wait-LogLine 'SELFTEST DONE pass=(\d+) fail=(\d+)' 120 -Since $mark
if ($done -notmatch 'SELFTEST DONE pass=1 fail=0') { throw "landmark.display: $done" }
