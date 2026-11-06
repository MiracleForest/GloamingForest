# U18 (minorlandmarks) on a real client (post-merge acceptance). The selftest
# minorlandmarks.client_showcase forces a hedge maze start next to the player: the maze from outside
# (hedge walls, the entrance jack o'lanterns, the grass floor) and the hedge close up (TF's hedge
# texture, its 15/16 collision box). The hedge item and the structures' map icons are covered by
# the uncraftmaps scenario's magic map. Then the minorlandmarks.* selftests run on the integrated
# server in local mode (golden hashes of the hills, the maze, the hollow trees, the fallen trunks and
# every feature type).
# Screenshots: out/minorlandmarks*/minorlandmarks-maze-<mode>.png, minorlandmarks-hedge-<mode>.png.
Save-Screenshot 'hud' | Out-Null
$remote = $script:Mode -ne 'local'
$hostLine = if ($remote) { '\[host\].*' } else { '' }

if ($remote) { Wait-LogLine '\[host\].*GloamingForest world ready' 120 | Out-Null }
else { Wait-LogLine 'GloamingForest world ready' 120 | Out-Null }

$mark = Get-LogMark
$command = Format-SelfTestCommand 'minorlandmarks.client_showcase'
if ($remote) { Send-HostConsole $command } else { Send-SelfTestCommand $command }
Wait-LogLine "${hostLine}minorlandmarks\.client_showcase: hedge maze ready" 180 -Since $mark | Out-Null
Start-Sleep -Seconds 2
Save-Screenshot "minorlandmarks-maze-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}minorlandmarks\.client_showcase: hedge close ready" 60 -Since $mark | Out-Null
Start-Sleep -Seconds 1
Save-Screenshot "minorlandmarks-hedge-$($script:Mode)" | Out-Null
Wait-LogLine "${hostLine}$script:SelfTestDoneRegex" 60 -Since $mark | Out-Null
$r = ConvertFrom-SelfTestLines (Get-LeviLog -Since $mark) 'minorlandmarks.client_showcase'
if (-not (Register-SelfTestResult $r)) { throw 'minorlandmarks.client_showcase failed' }

if (-not $remote) {
    $mark = Get-LogMark
    Send-SelfTestCommand (Format-SelfTestCommand 'minorlandmarks.*')
    Wait-LogLine "$script:SelfTestDoneRegex" 600 -Since $mark | Out-Null
    $r = ConvertFrom-SelfTestLines (Get-LeviLog -Since $mark) 'minorlandmarks.*'
    if (-not (Register-SelfTestResult $r)) { throw 'minorlandmarks selftests failed' }
}
