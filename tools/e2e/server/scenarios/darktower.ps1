# Dark tower (U26): the tower devices (vanishing chain, reappearing cycle, locked door and tower
# key, carminite builder bridges and their expiry chain, antibuilder, carminite reactor, slider),
# the scripted Ur-Ghast fight in which a charged ghast trap downs the Ur-Ghast (tantrum off, trap
# damage, activate_ghast_trap) and the Ur-Ghast's phases; the BDS content log stays clean (run.ps1
# fails on content errors).
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$r = Invoke-SelfTest 'darktower.*' -TimeoutSeconds 900
if ($r.Fail -gt 0) { throw "$($r.Fail) darktower selftest(s) failed" }
if ($r.Pass -lt 9) { throw "expected 9 darktower selftests to pass, got $($r.Pass)" }
