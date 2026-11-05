# U24 (hydra): the Hydra, its mortar, the fire jets and smokers, the hydra lair.
# The selftests ("hydra.*", dx 2400..2499) cover the unit record's e2e: a boss fight with a seeded
# random (hydra.head_regrow: a head past 120 damage dies and it and a dead head grow back;
# hydra.spawn_parts: the 44 part actors follow the body by TF's formulas; hydra.mortar: a mortar
# lands, its fuse runs out, a reflected one hurts the hydra), the bite, the part damage rules, the
# name tags, the death (heads die one by one, the mangrove death chest) and the boss spawner; the
# jets fire on redstone (hydra.encased_jet), a natural jet takes lava (hydra.fire_jet_lava), the
# smokers (hydra.smokers); the lair piece's golden volume hash (hydra.lair_golden); the shield
# shatter, the mortar's 80-tick fuse, the head names across a save and a load, the conquest of a
# forced lair, a brain-on seeded fight and the rest on the Java box.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$r = Invoke-SelfTest 'hydra.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) hydra selftest(s) failed" }
if ($r.Pass -lt 18) { throw "expected at least 18 hydra selftests to pass, got $($r.Pass)" }
foreach ($line in 'hydra: installed', 'hydra: spawned at', 'hydra: head 1 slain', 'hydra: head 1 is born again',
                  'hydra: mortar detonated', 'hydra: mortar reflected by', 'hydra: head 2 named Ladon', 'hydra: dying',
                  'boss: twilightforest:hydra_boss_spawner spawned twilightforest:hydra',
                  'hydra: encased fire jet at', 'hydra: fire jet at', 'flames', 'hydra: encased smoker at') {
    if (-not (Get-ServerLog | Select-String -SimpleMatch $line -Quiet)) { throw "missing log line: $line" }
}
