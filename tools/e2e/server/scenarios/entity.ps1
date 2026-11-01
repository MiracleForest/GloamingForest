# F8 (entity): entities, AI, multipart, projectiles and the boss kit (spike S4).
# The selftests ("entity.*") cover the unit record's e2e: the fixture boss gf_test:boss with 3
# parts (the parts follow, damage on a part goes to the parent), the BossEventPackets of its bar,
# the death chest filled from the loot buffer and the conquest of its home; plus behaviour
# dispatch, entity properties read back through Molang, the goal selector, the hostile mount, the
# projectile simulation, peaceful turning the boss back into its spawner and the spawner itself.
# The harness adds what only it can do: the entity data survives a world reload.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$r = Invoke-SelfTest 'entity.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) entity selftest(s) failed" }
if ($r.Pass -lt 12) { throw "expected at least 12 entity selftests to pass, got $($r.Pass)" }
foreach ($line in 'boss: gf_test:boss died', 'boss: death chest of gf_test:boss at', 'boss: gf_test:boss conquered gf_test:fixture',
                  'boss: gf_test:boss turned back into gf_test:fixture_block', 'boss: gf_test:fixture_block spawned gf_test:boss',
                  'entity: minecraft:horse grabbed', 'entity: minecraft:horse threw') {
    if (-not (Get-ServerLog | Select-String -SimpleMatch $line -Quiet)) { throw "missing log line: $line" }
}

# The persisted entity data (data(actor, key), the home point) travels with the actor's save.
$mark = Get-LogMark
$r = Invoke-SelfTest 'entity.persist_write'
if ($r.Pass -ne 1) { throw 'entity.persist_write did not pass' }
$written = Get-ServerLog -Since $mark | Select-String -Pattern 'entity\.persist_write: persist marker written nonce=(\d+)' | Select-Object -Last 1
if (-not $written) { throw 'entity.persist_write logged no nonce' }
$nonce = $written.Matches[0].Groups[1].Value
Write-Step "entity persist marker nonce $nonce"

Restart-Server
$mark = Get-LogMark
$r = Invoke-SelfTest 'entity.persist_read'
if ($r.Pass -ne 1) { throw 'entity.persist_read did not pass after the reload' }
if (-not (Get-ServerLog -Since $mark | Select-String -Pattern "entity\.persist_read: persist marker verified nonce=$nonce\b" -Quiet)) {
    throw "the entity data with nonce $nonce did not survive the reload"
}
Write-Step 'the entity data and home point survived the reload'

# The kit still works on the reloaded world.
$r = Invoke-SelfTest 'entity.boss_lifecycle'
if ($r.Pass -ne 1) { throw 'entity.boss_lifecycle did not pass after the reload' }
