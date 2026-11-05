# U21 (lich): the Lich, its clones and minions, the Death Tome, the rising zombie, the bolts and
# bombs, the boss kit wiring (spawner range, loot) and the lich tower layout (with the placeholder
# rooms while U22's rooms are missing, or U22's when present). Every lich.* selftest runs in the
# Twilight Forest test rows dx 2100..2199 (rule R12).
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$mark = Get-LogMark
$r = Invoke-SelfTest 'lich.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) lich selftest(s) failed" }
if ($r.Pass -lt 11) { throw "expected at least 11 lich selftests to pass, got $($r.Pass)" }
$log = Get-ServerLog -Since $mark
foreach ($line in @('lich: shield broken by twilightforest:lich_bolt', 'lich: bolt reflected by', 'lich: a rising zombie became a zombie', 'lich tower: ')) {
    if (-not ($log | Select-String -SimpleMatch $line)) { throw "missing log line: $line" }
}
$layout = ($log | Select-String -Pattern 'lich tower: (\d+) pieces.*layout hash (\d+)' | Select-Object -Last 1)
if ($layout) { Write-Step "lich tower: $($layout.Matches[0].Groups[1].Value) pieces, layout hash $($layout.Matches[0].Groups[2].Value)" }

# the layout is deterministic: a second run of the same test gives the same hash
$mark = Get-LogMark
$r2 = Invoke-SelfTest 'lich.tower_layout'
if ($r2.Fail -gt 0) { throw 'lich.tower_layout failed on the second run' }
$again = (Get-ServerLog -Since $mark | Select-String -Pattern 'layout hash (\d+)' | Select-Object -Last 1)
if ($layout -and $again -and $again.Matches[0].Groups[1].Value -ne $layout.Matches[0].Groups[2].Value) {
    throw "the lich tower layout changed between runs ($($layout.Matches[0].Groups[2].Value) vs $($again.Matches[0].Groups[1].Value))"
}
Write-Step 'lich: selftests passed, the tower layout is stable'
