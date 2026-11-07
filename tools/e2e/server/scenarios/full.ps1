# Every selftest of every unit in one world (integration check, rule R12). The coordinator runs
# it before each wave and at every merge.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
$r = Invoke-SelfTest -TimeoutSeconds 14400
if ($r.Fail -gt 0) { throw "$($r.Fail) selftest(s) failed" }