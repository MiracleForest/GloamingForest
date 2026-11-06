# U10 (provisions) on BDS. The selftests carry the logic: the 23 foods (nutrition, saturation, the effects
# of the hydra chop and meef stroganoff, the bowls, consume_hydra_chop_on_low_hunger at food level 0,
# e115_slices_eaten), stale bread's twilightforest:stale_sandwich hits, Experiment 115 (placement on a
# solid block, adding / taking / eating slices, redstone regeneration, comparator and signal, support),
# frosted (the Slowness carrier, the frozen counter, fire lowering it, the freeze damage bonus, the
# immunity rules), the four enchantments (fire react / chill aura reactions at 0.15 x level, destruction's
# damage and smash_blocks, renewal recharging an empty scepter) and the nine music discs (record
# component, jukebox song by name).
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$log = Get-ServerLog
if (-not ($log | Select-String -Pattern 'provisions: 23 foods, 9 music discs, frosted, 4 enchantments and experiment 115 registered' -Quiet)) {
    throw 'the provisions module did not register'
}

$r = Invoke-SelfTest 'provisions.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) provisions selftest(s) failed: $($r.Failed -join ', ')" }
if ($r.Pass -lt 6) { throw "expected at least 6 provisions selftests to pass, got $($r.Pass)" }

foreach ($line in 'provisions: .+ ate a hydra chop on an empty stomach', 'provisions: .+ placed experiment 115', 'provisions: .+ ate a slice of experiment 115', 'provisions: .+ made regenerating experiment 115') {
    if (-not (Get-ServerLog | Select-String -Pattern $line -Quiet)) { throw "missing log line '$line'" }
}
