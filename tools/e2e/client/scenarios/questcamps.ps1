# U19 (questcamps) on the client: the Questing Ram, the quest grove, a camp and the template features
# as players see them.
#   local          the questcamps.* selftests pass on the integrated server; then
#                  questcamps.client_showcase shows a Questing Ram with five colours (its tinted wool
#                  segments lined up in colorOrder, the body stretched, the glowing lines layer) and,
#                  with a wool held while looking at it, the crosshair indicator (questing_ram_x for a
#                  missing colour, questing_ram_check for a collected one); then the quest grove, a
#                  seeded camp and the druid hut, wells, grove ruins, stone circle and graveyard side by
#                  side for the one-time review of the golden hashes (rule R19)
#   remote-vanilla the same scene on a vanilla client: the ram's segments come from the resource pack
#                  (entity property twilightforest:color_flags); no indicator (client mod only)
# Screenshots: out/questcamps/{ram,ram-indicator-x,ram-indicator-check,grove,camp,features,graveyard}-<mode>.png
$remote = $script:Mode -ne 'local'
if ($remote) { Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null }
else { Wait-LogLine 'GloamingForest world ready' 120 | Out-Null }
$hostLine = if ($remote) { '\[host\].*' } else { '' }

if (-not $remote) {
    $r = Invoke-SelfTest 'questcamps.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) questcamps selftest(s) failed" }
    if ($r.Pass -lt 12) { throw "expected at least 12 questcamps selftests to pass, got $($r.Pass)" }
}

Invoke-Chat '/gamemode creative'
$optIn = 'scoreboard objectives add gf_questcamps_display dummy'
if ($remote) { Send-HostConsole $optIn } else { Invoke-Chat "/$optIn" }
$mark = Get-LogMark
$command = Format-SelfTestCommand 'questcamps.client_showcase'
if ($remote) { Send-HostConsole $command } else { Send-SelfTestCommand $command }
$area = Wait-LogLine "${hostLine}questcamps\.client_showcase: area at (-?\d+) (-?\d+) (-?\d+)" 120 -Since $mark
if ($area -notmatch 'area at (-?\d+) (-?\d+) (-?\d+)') { throw "no showcase area in '$area'" }
$x = [int]$Matches[1]; $y = [int]$Matches[2]; $z = [int]$Matches[3]

# the ram from the side, then the indicator: a missing colour (red) and a collected one (white) held
Wait-LogLine "${hostLine}questcamps\.client_showcase: ram ready" 60 -Since $mark | Out-Null
Invoke-Teleport "/tp @s $($x + 13.5) $($y + 1) $($z + 6.5) facing $($x + 6.5) $($y + 1.5) $($z + 6.5)"
Start-Sleep -Seconds 2
Save-Screenshot "ram-$($script:Mode)" | Out-Null
Invoke-Teleport "/tp @s $($x + 9.5) $($y) $($z + 6.5) facing $($x + 6.5) $($y + 1.8) $($z + 6.5)"
Invoke-Chat '/replaceitem entity @s slot.weapon.mainhand 0 red_wool'
Start-Sleep -Seconds 1
Save-Screenshot "ram-indicator-x-$($script:Mode)" | Out-Null
Invoke-Chat '/replaceitem entity @s slot.weapon.mainhand 0 white_wool'
Start-Sleep -Seconds 1
Save-Screenshot "ram-indicator-check-$($script:Mode)" | Out-Null

Wait-LogLine "${hostLine}questcamps\.client_showcase: grove ready" 60 -Since $mark | Out-Null
Invoke-Teleport "/tp @s $($x + 30.5) $($y + 22) $($z + 40.5) facing $($x + 31.5) $($y) $($z + 17.5)"
Start-Sleep -Seconds 2
Save-Screenshot "grove-$($script:Mode)" | Out-Null

Wait-LogLine "${hostLine}questcamps\.client_showcase: camp ready" 60 -Since $mark | Out-Null
Invoke-Teleport "/tp @s $($x + 30.5) $($y + 26) $($z + 58.5) facing $($x + 30.5) $($y) $($z + 80.5)"
Start-Sleep -Seconds 2
Save-Screenshot "camp-$($script:Mode)" | Out-Null

$features = Wait-LogLine "${hostLine}questcamps\.client_showcase: features at (-?\d+) (-?\d+)" 60 -Since $mark
if ($features -notmatch 'features at (-?\d+) (-?\d+)') { throw "no feature row in '$features'" }
$fx = [int]$Matches[1]; $fz = [int]$Matches[2]
Wait-LogLine "${hostLine}questcamps\.client_showcase: features ready" 60 -Since $mark | Out-Null
Invoke-Teleport "/tp @s $($fx + 40.5) $($y + 30) $($fz - 20.5) facing $($fx + 40.5) $($y) $($fz + 8.5)"
Start-Sleep -Seconds 2
Save-Screenshot "features-$($script:Mode)" | Out-Null
Invoke-Teleport "/tp @s $($fx + 88.5) $($y + 18) $($fz - 6.5) facing $($fx + 88.5) $($y) $($fz + 10.5)"
Start-Sleep -Seconds 2
Save-Screenshot "graveyard-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}$script:SelfTestDoneRegex" 120 -Since $mark | Out-Null
$r = ConvertFrom-SelfTestLines (Get-LeviLog -Since $mark) 'questcamps.client_showcase'
if (-not (Register-SelfTestResult $r)) { throw 'questcamps.client_showcase failed' }