# U18 (minorlandmarks): the minor landmarks and decorative features on BDS. The selftests
# minorlandmarks.* (dx 1800..1899) prove: the hedge's contact damage (a player and a zombie on it
# are hurt, a spider is spared) and its punch damage; the three hollow hills, the hedge maze, two
# hollow trees and the three fallen trunk sizes placed in memory against golden volume hashes (rule
# R19), with their piece tags read back through the piece factories; a forced hedge maze start in
# the world (5 room spawners, the chests, the hedge walls, its map icon); a hollow tree grown through
# landmark::generateAt (the robust gloaming oak sapling's path); every feature type (block spike,
# dark forest placer, fallen hollow log, small fallen log, foundation, hollow stump, lampposts,
# monolith, mycelium blob, small lake, webs, wood roots) against golden hashes; a monolith in the
# world with its two ravens; the generators, piece factories and descriptors of the seven structures.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
Wait-LogLine 'minorlandmarks: installed' 30 | Out-Null

$mark = Get-LogMark
$r = Invoke-SelfTest 'minorlandmarks.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) minorlandmarks selftest(s) failed" }
if ($r.Pass -lt 11) { throw "expected at least 11 minorlandmarks selftests to pass, got $($r.Pass)" }
foreach ($line in 'minorlandmarks: hollow hills and hedge maze registered', 'minorlandmarks: hollow trees registered',
                  'minorlandmarks: fallen trunks registered', 'minorlandmarks: 12 feature types registered', 'minorlandmarks: hedge maze placed at') {
    if (-not (Get-ServerLog | Select-String -SimpleMatch $line -Quiet)) { throw "missing log line: $line" }
}
foreach ($g in Get-ServerLog -Since $mark | Select-String -Pattern 'golden (\S+) = (\S+)') {
    Write-Step "golden $($g.Matches[0].Groups[1].Value) = $($g.Matches[0].Groups[2].Value)"
}
