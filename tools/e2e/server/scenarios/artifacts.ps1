# Scepters, charms, artifacts and death keeping (U9): the scepter durabilities, the twilight bolt,
# the lifedrain beam, the loyal zombie, the fortification shields (absorption, expiry, the
# /twilightforest shield subcommand and the give_3_shields function), the charms of life and
# keeping (the casket placement and the return on respawn), the keepsake casket block (locking,
# repair), the potion flasks, the pocket watch and moon dial, the four custom recipes and the ore
# meter.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$mark = Get-LogMark
$r = Invoke-SelfTest 'artifacts.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) artifacts selftest(s) failed" }
$tests = 'artifacts.scepter_durability', 'artifacts.twilight_bolt', 'artifacts.lifedrain', 'artifacts.loyal_zombie', 'artifacts.shields',
         'artifacts.charm_of_life', 'artifacts.keeping', 'artifacts.casket', 'artifacts.flask', 'artifacts.pocket_watch', 'artifacts.recipes',
         'artifacts.ore_meter'
if ($r.Pass -lt $tests.Count) { throw "expected $($tests.Count) artifacts selftests to pass, got $($r.Pass)" }
foreach ($t in $tests) {
    if (-not (Get-ServerLog -Since $mark | Select-String -Pattern "SELFTEST PASS $([regex]::Escape($t))$" -Quiet)) { throw "$t did not pass" }
}
foreach ($p in 'artifacts: a fortification shield of .* broke', 'artifacts: give_3_shields: 3 permanent shields', 'death: .* survived lethal',
               "artifacts: .*'s charm of keeping 2 kept", 'artifacts: placed .*keepsake_casket at', 'artifacts: .* drank from a flask',
               "artifacts: .*'s ore scan finished") {
    if (-not (Get-ServerLog -Since $mark | Select-String -Pattern $p -Quiet)) { throw "no log line '$p'" }
}
Write-Step 'scepters, shields, charms, casket, flasks, ore meter and recipes work'
