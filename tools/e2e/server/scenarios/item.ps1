# F9 (item): the item, enchantment, effect, recipe and data runtimes on BDS. The selftests carry the
# logic (item behaviour hooks and Java's details of them, cooldowns, tooltip lore, the dispenser
# fixture, custom enchantments through the anvil and the grindstone, carrier effects next to vanilla
# instances and Java's hidden effects, special crafting and its remainders, recipes, tags with the
# vanilla and c: bridge, data maps and the composter / furnace, stats, traits, Travellers' Gear
# modifiers, the creative catalog, rarity and display contexts) and the spike S6 probes (banner
# patterns, trim materials appended and sent, catalog groups, the glint marker). The real-path tests
# (item.real_*) need a real player: the client scenario drives them; here they only clean up.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

# the data runtimes loaded at enable (the gf_test fixture data at least)
$log = Get-ServerLog
foreach ($line in 'tags: \d+ tag files loaded', 'recipes: \d+ data recipes loaded', 'data maps: \d+ loaded') {
    if (-not ($log | Select-String -Pattern $line -Quiet)) { throw "missing log line '$line'" }
}
# recipes whose neoforge:conditions fail (TF's jeed integration) are skipped without a warning
if ($log | Select-String -Pattern 'no loader for recipe type jeed:' -Quiet) { throw 'the jeed recipes were parsed although their mod_loaded condition fails' }
# the prelaunch flag (item.prelaunch.ps1): this run, and only this run, registers the S6 trim probe at load
if (-not ($log | Select-String -Pattern 'item: S6 trim probe registered at load' -Quiet)) { throw 'the S6 trim probe was not registered at load (prelaunch flag)' }

$r = Invoke-SelfTest 'item.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) item selftest(s) failed: $($r.Failed -join ', ')" }
if ($r.Pass -lt 43) { throw "expected at least 43 item selftests to pass, got $($r.Pass)" }
# TF's recipes, tags and data maps name only ids the game knows (item.java_ids_registry's counts)
Get-ServerLog | Select-String -Pattern 'SELFTEST INFO item\.(java_ids_registry|recipes_java_ids)' | ForEach-Object { Write-Step $_.Line }

# the S6 evidence lines (docs/spikes/S6.md quotes them)
$mark = Get-LogMark
$r = Invoke-SelfTest 'item.spike_*'
if ($r.Fail -gt 0) { throw 'an S6 spike selftest failed' }
Get-ServerLog -Since $mark | Select-String -Pattern 'SELFTEST INFO item\.spike_' | ForEach-Object { Write-Step $_.Line }

# the data reload of /twilightforest reload reads the data again
$mark = Get-LogMark
Send-Console 'twilightforest reload'
Wait-LogLine 'tags: \d+ tag files loaded' 30 -Since $mark | Out-Null
$r = Invoke-SelfTest 'item.recipes_fixture'
if ($r.Pass -ne 1) { throw 'the recipes are gone after /twilightforest reload' }

# the tag caches are rebuilt by the reload: the c: bridge still answers
$r = Invoke-SelfTest 'item.tags_common'
if ($r.Fail -gt 0) { throw 'the c: tag bridge failed after the reload' }
