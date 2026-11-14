# U21 (lich): the Lich, its clones and minions, the Death Tome (and its lectern mimic), the rising
# zombie, the bolts and bombs (hit by a player, the bomb sparing dropped items), the boss kit wiring
# (spawner range, loot), the lich tower layout and its yard with the golden layout and volume
# hashes (placeholder rooms while U22's rooms are missing; with U22 present the hashes are logged),
# and the yard's controlled spawns. Every lich.* selftest runs in the test rows dx 2100..2199
# (rule R12).
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$mark = Get-LogMark
$r = Invoke-SelfTest 'lich.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) lich selftest(s) failed" }
if ($r.Pass -lt 16) { throw "expected at least 16 lich selftests to pass, got $($r.Pass)" }
$log = Get-ServerLog -Since $mark
foreach ($line in @('lich: shield broken by gloamingforest:lich_bolt', 'lich: bolt reflected by', 'lich: a rising zombie became a zombie',
        'lich: a death tome left its lectern', 'lich tower: ', 'lich tower: volume hash ')) {
    if (-not ($log | Select-String -SimpleMatch $line)) { throw "missing log line: $line" }
}
# the lectern tome wakes up only through its target selector, never because it lost its lectern
if ($log | Select-String -SimpleMatch 'is off its lectern') { throw 'a death tome lost its lectern during the selftests' }
$layout = ($log | Select-String -Pattern 'lich tower: (\d+) pieces.*layout hash (\d+)' | Select-Object -Last 1)
if ($layout) { Write-Step "lich tower: $($layout.Matches[0].Groups[1].Value) pieces, layout hash $($layout.Matches[0].Groups[2].Value)" }
$volume = ($log | Select-String -Pattern 'lich tower: volume hash ([0-9a-f]+)' | Select-Object -Last 1)
if ($volume) { Write-Step "lich tower: volume hash $($volume.Matches[0].Groups[1].Value)" }
Write-Step 'lich: selftests passed, the tower matches its goldens'
