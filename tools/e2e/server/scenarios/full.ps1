# Every selftest of every unit in one world (integration check, rule R12). The coordinator runs
# it before each wave and at every merge. It usually takes ~25 min; the scenario raises run.ps1's
# default budget (-TimeoutSec 900) itself to a 4 h ceiling (+ 5 min for the world-ready wait), so a
# plain `run.ps1 -Scenario full` is not cut short (a hung run holds its BDS slot that long).
$fullBudget = 14400
Request-ScenarioTime ($fullBudget + 300)
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
$r = Invoke-SelfTest -TimeoutSeconds $fullBudget
if ($r.Fail -gt 0) { throw "$($r.Fail) selftest(s) failed" }
