# U22 (lichdecor): the lich tower decor. Runs every lichdecor.* selftest in the test rows dx
# 2200..2299 (rule R12): the lightable blocks (candelabra slots, support and signal, brazier,
# skull candles, ominous candles and fire), the sinister spawner command, the skull chest lock,
# the wrought iron fence finial, the chiseled canopy bookshelf's Death Tome spawner, the magic
# painting, the recipes and the golden volume hashes of the 130 room templates (logged as
# "golden <id> <hash>" while the table is empty).
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$mark = Get-LogMark
$r = Invoke-SelfTest 'lichdecor.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) lichdecor selftest(s) failed" }
if ($r.Pass -lt 11) { throw "expected at least 11 lichdecor selftests to pass, got $($r.Pass)" }
$log = Get-ServerLog -Since $mark
foreach ($size in '3x3', '5x5', '7x7', '9x9') {
    if (-not ($log | Select-String -SimpleMatch "${size}: ")) { throw "missing the room summary of $size" }
}
$goldens = @($log | Select-String -Pattern 'golden (gloamingforest:lich_tower/\S+) ([0-9a-f]{16})')
if ($goldens.Count) { Write-Step "lichdecor: $($goldens.Count) room hash(es) logged for the golden table" }
Write-Step 'lichdecor: selftests passed'
