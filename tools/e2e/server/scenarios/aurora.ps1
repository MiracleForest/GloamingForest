# Aurora palace, Snow Queen and mushroom tower (U28): the aurora.* selftests.
#   aurora.palace_layout / palace_hash / mushroom_tower_hash  the golden layouts and volume hashes of
#                         both structures (in memory, fixed seed and start chunk), the Snow Queen's
#                         spawner at (7, 34, 7) of the boss tower, a boss wing saved and loaded
#   aurora.palace_start   forced starts of both landmarks (far from the test rows) and the palace's lock
#   aurora.mob_attributes the attributes of the five mobs (F13's goldens), the snow guardian's gear,
#                         the queen's seven shields, summon phase and six summons
#   aurora.ice_crystal_lifetime, aurora.unstable_ice_core, aurora.stable_ice_core_snowballs,
#   aurora.transform_blocks, aurora.map_colors, aurora.snow_queen_fight, aurora.snow_queen_drop,
#   aurora.snow_queen_beam, aurora.snow_queen_shields (piercing arrow, the 0.4 lift), aurora.snow_queen_death
#   (death chest, progress_glacier), aurora.snow_queen_spawner, aurora.aurora_block_mining (10 ticks held,
#   a tap does not break it), aurora.aurora_block_variant, aurora.ice_mobs_melt, aurora.snow_guardian_gear,
#   aurora.boss_wing_in_world (R19: the queen's tower placed in the world, volume hash),
#   aurora.client_miner (the client scenario's setup: progress_glacier for real players; none here)
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
$r = Invoke-SelfTest 'aurora.*' -TimeoutSeconds 900
if ($r.Fail -gt 0) { throw "$($r.Fail) aurora selftest(s) failed: $($r.Failed -join ', ')" }
if ($r.Pass -lt 22) { throw "only $($r.Pass) aurora selftests ran" }
