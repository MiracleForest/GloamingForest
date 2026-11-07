# Forest and hollow-hill hostiles (U17): the selftests on the BDS: the 13 mobs' goldens (F13), the
# mist wolf's blindness in the dark (none on Easy), the winter wolf's and the fire beetle's breath,
# the pinch beetle's grab, the redcap sapper planting and the redcaps lighting TNT, the druid's
# poisoning nature bolt and the log it turns into birch leaves, the slime blob's 4 damage, the swarm
# spider's 1-in-4 bite, the king spider's druid rider, the wraith's flight home through a wall and
# the entity loot tables, and the A5 spawn sweep over all 80 TF entity types (attributes against the
# Java goldens, loot, sounds, despawn rules, /summon) with two players for the targeting check; the
# BDS content log stays clean (run.ps1 fails on content errors).
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$r = Invoke-SelfTest 'hostiles.*' -TimeoutSeconds 900
if ($r.Fail -gt 0) { throw "$($r.Fail) hostiles selftest(s) failed" }
if ($r.Pass -lt 16) { throw "expected 16 hostiles selftests to pass, got $($r.Pass)" }
