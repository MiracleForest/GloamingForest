# Troll caves and yeti cave (U27): the caves.* selftests.
#   caves.troll_cave_layout / troll_cave_hash   the troll cave's pieces (main cave, connecting and
#                         garden caves, the vault), every piece saved and loaded, the golden layout and
#                         volume hash (in memory, fixed seed and start chunk)
#   caves.cloud_castle_hash  the cloud castle pieces U29's giant house starts by string (castle, cloud,
#                         two cloud trees), golden volume hash
#   caves.yeti_cave_hash  the yeti cave on its terraformed hill, the alpha yeti spawner at (34, 1, 34)
#   caves.features        troll cave big mushgloom, huge brown / red mushrooms, troll roots
#   caves.starts          forced troll cave and yeti cave starts, their locks and the terraformer
#   caves.trollsteinn_light  face states by light (random tick) and the comparator output
#   caves.troll_roots     roots under stone, ripening at light >= 6, the trollber harvest
#   caves.troll_rock      the troll takes a stone block and throws it (6 thrown_block damage)
#   caves.yeti_throw      the yeti grabs and throws a SimulatedPlayer (motion, cooldown 200)
#   caves.alpha_yeti      projectiles ignored until hit, falling ice, rampage, tired
#   caves.falling_ice     the falling ice hurts what it lands on
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
$r = Invoke-SelfTest 'caves.*' -TimeoutSeconds 900
if ($r.Fail -gt 0) { throw "$($r.Fail) caves selftest(s) failed: $($r.Failed -join ', ')" }
if ($r.Pass -lt 12) { throw "only $($r.Pass) caves selftests ran" }
