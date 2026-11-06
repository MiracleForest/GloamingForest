# Knight stronghold (U25): the stronghold's layout (piece limits, protection, spawn indices, save /
# load) and golden volume hash, the stronghold shield's face rule, the trophy pedestal (a trophy on
# top activates it and breaks the shields within 5; refused while progression is enforced and nobody
# nearby has progress_lich), the mobs' goldens and the goblin knight pair, the goblin knight's armour
# breaking by attack angle and the rider's shield, the block-and-chain goblin's throw range, the
# helmet crab's blue flag and the Knight Phantom fight (six knights from the spawner, numbers and
# weapons, formations, the last death's dark chest with the trophy); the BDS content log stays clean
# (run.ps1 fails on content errors).
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$r = Invoke-SelfTest 'stronghold.*' -TimeoutSeconds 900
if ($r.Fail -gt 0) { throw "$($r.Fail) stronghold selftest(s) failed" }
if ($r.Pass -lt 9) { throw "expected 9 stronghold selftests to pass, got $($r.Pass)" }
