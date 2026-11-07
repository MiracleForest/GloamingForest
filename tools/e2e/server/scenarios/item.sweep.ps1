# F9 (item): the sweep selftests alone (item.sweep_*: every twilightforest block placed and broken
# against TF's loot tables, every recipe of TF's data pack crafted, both against F13's oracle
# goldens; item.catalog_variant), with their reports: every difference is a FAIL line, every reviewed
# exception an INFO line. The `item` scenario runs them too (item.*).
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
$mark = Get-LogMark
$r = Invoke-SelfTest 'item.sweep_*'
Get-ServerLog -Since $mark | Select-String -Pattern 'SELFTEST (INFO|FAIL) item\.sweep_' | ForEach-Object { Write-Step $_.Line }
if ($r.Fail -gt 0) { throw "$($r.Fail) sweep selftest(s) failed: $($r.Failed -join ', ')" }
$r = Invoke-SelfTest 'item.catalog_variant'
if ($r.Fail -gt 0) { throw 'item.catalog_variant failed' }
