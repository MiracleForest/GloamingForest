# U23 (labyrinth): the labyrinth, the Minoshroom and the minotaurs, the maze slime, the maze slime
# block and the mazebreaker rule. The selftests ("labyrinth.*", dx 2300..2399) cover the unit
# record's e2e: the golden volume hash of a seeded labyrinth (labyrinth.golden_hash, with
# labyrinth.maze_layout: the pieces, the two levels, the spawn lists and the save / load of every
# piece; labyrinth.structure_start: a forced start in the world), the boss fight (labyrinth.
# minoshroom_stats, labyrinth.ground_slam, labyrinth.spawner_height_rule), the minotaurs' axes,
# the maze slime's health and split, the mazestone tool-damage rule (labyrinth.tool_penalty) and
# the maze slime block (labyrinth.slime_block).
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$r = Invoke-SelfTest 'labyrinth.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) labyrinth selftest(s) failed" }
if ($r.Pass -lt 13) { throw "expected at least 13 labyrinth selftests to pass, got $($r.Pass)" }
foreach ($line in 'labyrinth: installed', 'boss: twilightforest:minoshroom_boss_spawner spawned twilightforest:minoshroom') {
    if (-not (Get-ServerLog | Select-String -SimpleMatch $line -Quiet)) { throw "missing log line: $line" }
}
