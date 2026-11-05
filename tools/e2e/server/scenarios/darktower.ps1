# Dark tower (U26): the dark tower structure (layout, golden volume hash, a forced start), the mobs'
# goldens and the Ur-Ghast fireball (also sent back by a player), the carminite ghasts' 1000 damage
# from a reflected fireball, a borer from exploded infested towerwood, the tower devices (vanishing chain, reappearing cycle, locked door and tower
# key, carminite builder bridges and their expiry chain, antibuilder, carminite reactor, slider),
# the scripted Ur-Ghast fight in which a charged ghast trap downs the Ur-Ghast (tantrum off, trap
# damage, activate_ghast_trap) and the Ur-Ghast's phases; the BDS content log stays clean (run.ps1
# fails on content errors).
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$r = Invoke-SelfTest 'darktower.*' -TimeoutSeconds 900
if ($r.Fail -gt 0) { throw "$($r.Fail) darktower selftest(s) failed" }
if ($r.Pass -lt 17) { throw "expected 17 darktower selftests to pass, got $($r.Pass)" }
