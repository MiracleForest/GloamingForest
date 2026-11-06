# U20 (naga) on the client: the Naga and the courtyard as players see them.
#   local          the naga.* selftests pass on the integrated server; then naga.display shows a
#                  Naga (head nagahead.png scaled 2.01, 12 segments nagasegment.png scaled 2) as it
#                  is, charging (nagahead_charging.png) and dazed (nagahead_dazed.png, the head
#                  lowered, crit sparkles), its face turned to the camera, then dying (upright:
#                  NagaRenderer#getFlipDegrees 0, the charging face, the segments popping), and
#                  naga.display_courtyard builds the seeded courtyard of the golden hash test at its
#                  fixed site for the one-time review (rule R19).
#   remote-vanilla the same Naga on a vanilla client (the resource pack's entities, properties,
#                  animations and render controllers: every visual is vanilla-side; no companion
#                  packet is needed)
# Screenshots: out/naga/{naga,naga-charging,naga-dazed,naga-dying,courtyard,courtyard-center}-<mode>.png
$remote = $script:Mode -ne 'local'
if ($remote) { Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null }
else { Wait-LogLine 'GloamingForest world ready' 120 | Out-Null }
$hostLine = if ($remote) { '\[host\].*' } else { '' }

if (-not $remote) {
    $r = Invoke-SelfTest 'naga.*'
    if ($r.Fail -gt 0) { throw "$($r.Fail) naga selftest(s) failed" }
    if ($r.Pass -lt 16) { throw "expected at least 16 naga selftests to pass, got $($r.Pass)" }
}

Invoke-Chat '/gamemode creative'
# the display tests run only on request (they consume the objective): naga.* and full skip them
$optIn = 'scoreboard objectives add gf_naga_display dummy'
if ($remote) { Send-HostConsole $optIn } else { Invoke-Chat "/$optIn" }
$mark = Get-LogMark
$command = Format-SelfTestCommand 'naga.display'
if ($remote) { Send-HostConsole $command } else { Send-SelfTestCommand $command }
$area = Wait-LogLine "${hostLine}naga\.display: area at (-?\d+) (-?\d+) (-?\d+)" 90 -Since $mark
if ($area -notmatch 'area at (-?\d+) (-?\d+) (-?\d+)') { throw "no display area in '$area'" }
$x = [int]$Matches[1]; $y = [int]$Matches[2]; $z = [int]$Matches[3]
# south-east of the Naga (naga.display turns it to face this way), at the height of its head,
# looking at the face with the body trailing behind
$platform = "$($x + 7) $($y + 1) $($z + 7)"
$camera   = "/tp @s $($x + 7.5) $($y + 2) $($z + 7.5) facing $($x + 0.5) $($y + 2) $($z + 0.5)"
Invoke-Teleport $camera
Invoke-Chat "/setblock $platform glass"
Invoke-Teleport $camera
Wait-LogLine "${hostLine}naga\.display: naga shown" 60 -Since $mark | Out-Null
Start-Sleep -Seconds 2
Save-Screenshot "naga-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}naga\.display: naga charging" 60 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot "naga-charging-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}naga\.display: naga dazed" 60 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot "naga-dazed-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}naga\.display: naga dying" 60 -Since $mark | Out-Null
Save-Screenshot "naga-dying-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}$script:SelfTestDoneRegex" 90 -Since $mark | Out-Null
$r = ConvertFrom-SelfTestLines (Get-LeviLog -Since $mark) 'naga.display'
if (-not (Register-SelfTestResult $r)) { throw 'naga.display failed' }
Invoke-Chat "/setblock $platform air"

if (-not $remote) {
    Invoke-Chat "/$optIn"
    $mark = Get-LogMark
    Send-SelfTestCommand (Format-SelfTestCommand 'naga.display_courtyard')
    $at = Wait-LogLine 'naga\.display_courtyard: courtyard at (-?\d+) (-?\d+) (-?\d+)' 120 -Since $mark
    if ($at -notmatch 'courtyard at (-?\d+) (-?\d+) (-?\d+)') { throw "no courtyard position in '$at'" }
    $cx = [int]$Matches[1]; $cy = [int]$Matches[2]; $cz = [int]$Matches[3]
    Invoke-Chat '/gamemode spectator'
    Invoke-Teleport "/tp @s $($cx + 52) $($cy + 34) $($cz + 52) facing $cx $cy $cz"
    Start-Sleep -Seconds 4
    Save-Screenshot "courtyard-$($script:Mode)" | Out-Null
    Invoke-Teleport "/tp @s $($cx + 9) $($cy + 7) $($cz + 9) facing $cx $($cy + 1) $cz"
    Start-Sleep -Seconds 3
    Save-Screenshot "courtyard-center-$($script:Mode)" | Out-Null
    Wait-LogLine $script:SelfTestDoneRegex 120 -Since $mark | Out-Null
    $r = ConvertFrom-SelfTestLines (Get-LeviLog -Since $mark) 'naga.display_courtyard'
    if (-not (Register-SelfTestResult $r)) { throw 'naga.display_courtyard failed' }
    Invoke-Chat '/gamemode creative'
}
