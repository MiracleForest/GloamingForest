# Final castle (U30): the castle blocks (castle doors: use, redstone, chain, reappearing; force
# fields: placement connections, unbreakable; thorns: damage, immune items, burst, connections;
# burnt thorns, the thorn rose, the cinder furnace and its cinder logs) and the thorns feature; the BDS
# content log stays clean (run.ps1 fails on content errors).
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$r = Invoke-SelfTest 'finalcastle.*' -TimeoutSeconds 900
if ($r.Fail -gt 0) { throw "$($r.Fail) finalcastle selftest(s) failed" }
if ($r.Pass -lt 11) { throw "expected 11 finalcastle selftests to pass, got $($r.Pass)" }
