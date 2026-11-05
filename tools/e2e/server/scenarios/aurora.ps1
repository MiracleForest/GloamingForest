# Aurora palace, Snow Queen and mushroom tower (U28): the aurora.* selftests.
#   aurora.palace_layout / palace_hash / mushroom_tower_hash  the golden layouts and volume hashes of
#                         both structures (in memory, fixed seed and start chunk), the Snow Queen's
#                         spawner at (7, 34, 7) of the boss tower, a boss wing saved and loaded
#   aurora.palace_start   forced starts of both landmarks (far from the test rows) and the palace's lock
#   aurora.mob_attributes the attributes of the five mobs (F13's goldens), the snow guardian's gear,
#                         the queen's seven shields, summon phase and six summons
#   aurora.ice_crystal_lifetime, aurora.unstable_ice_core, aurora.stable_ice_core_snowballs,
#   aurora.snow_queen_fight, aurora.snow_queen_spawner
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
$r = Invoke-SelfTest 'aurora.*' -TimeoutSeconds 900
if ($r.Fail -gt 0) { throw "$($r.Fail) aurora selftest(s) failed: $($r.Failed -join ', ')" }
if ($r.Pass -lt 11) { throw "only $($r.Pass) aurora selftests ran" }
