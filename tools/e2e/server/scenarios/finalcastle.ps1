# Final castle (U30): the castle structure (layout, golden hash, templates, a forced start), the
# castle blocks (castle doors: use, redstone, chain, reappearing; force fields: placement
# connections, unbreakable; thorns: damage, immune items, burst, connections; burnt thorns, the thorn
# rose, the cinder furnace and its cinder logs), the thorns feature and the castle mobs; the BDS
# content log stays clean (run.ps1 fails on content errors).
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$r = Invoke-SelfTest 'finalcastle.*' -TimeoutSeconds 1500
if ($r.Fail -gt 0) { throw "$($r.Fail) finalcastle selftest(s) failed" }
if ($r.Pass -lt 21) { throw "expected 21 finalcastle selftests to pass, got $($r.Pass)" }
