# U14 (progression): TF's 45 advancements and their criteria engine, the biome restrictions and
# their enforcement, the screen data and the toasts run as selftests ("progression.*"):
#   progression.advancements          the 45 JSONs: parents, frames, hidden, triggers, rewards
#   progression.restrictions          the 9 restriction records
#   progression.recipe_unlocks        every rewarded recipe of the recipe unlocks resolves
#   progression.inventory_criteria    inventory_changed (all four scepters, the dyed-colour predicate)
#   progression.kill_criteria         kills of real entities by id (a deer in the overworld does not
#                                     count for twilight_hunter, one in the Twilight Forest does)
#   progression.trigger_criteria      TF's triggers, consume_item, recipe_crafted, item_used_on_block
#   progression.playthrough           scripted grants unlock the next biomes (dark forest, swamp,
#                                     snowy forest after the lich; the fire swamp after the labyrinth)
#   progression.enforce_*             each locked biome's enforcement and its timing (60 ticks; 20 for
#                                     acid rain), the gamerule and creative players
#   progression.toasts_and_screen     the companion toast and screen data, the vanilla form
#   progression.vanilla_locked_toast  the vanilla fallback of the locked biome toast
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
if (-not (Get-ServerLog | Select-String -Pattern 'progression: 45 advancements, \d+ criteria triggers, 9 biome restrictions' -Quiet)) {
    Wait-LogLine 'progression: 45 advancements, \d+ criteria triggers, 9 biome restrictions' 30 | Out-Null
}

$r = Invoke-SelfTest 'progression.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) progression selftest(s) failed" }
if ($r.Pass -lt 16) { throw "expected at least 16 progression selftests to pass, got $($r.Pass)" }

# the playthrough's grants and the enforcement of a locked biome are logged as players see them
$log = Get-ServerLog
foreach ($p in 'progression: \S+ earned twilightforest:progress_naga \(goal\)', 'progression: \S+ earned twilightforest:progress_lich \(goal\)',
    'progression: \S+ is in the locked biome twilightforest:dark_forest \(twilightforest:darkness\)',
    'progression: \S+ is in the locked biome twilightforest:highlands \(twilightforest:acid_rain\)',
    'progression: locked biome toast for \S+ \(vanilla\)') {
    if (-not ($log | Select-String -Pattern $p -Quiet)) { throw "missing log line: $p" }
}
Write-Step 'progression: the playthrough unlocked the biomes and the locked biomes enforced their effects'
