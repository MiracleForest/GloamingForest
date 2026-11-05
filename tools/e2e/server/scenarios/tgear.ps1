# U11 (tgear): Travellers' Gear on BDS. The selftests carry the logic: the six items (slots, durability,
# modifier slots), the 26 modifier recipes as special crafting (add, refuse a duplicate / a full piece /
# two belts), the swap-hotbar transfer, the vest / gloves merge, the add_modifier trigger of crafted gear,
# /twilightforest travellers_gear in TF's check order, haste / stealth / broken pieces, efficient eater
# (movement exhaustion halved), auto repair (none within 200 ticks of armour damage), perfect dodge
# (seeded, about 30%), never-break, the belt swap (a belt in the hotbar stays; the companion channel),
# the item display rules, the zoom channel and the red thread's multiface placement.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$log = Get-ServerLog
if (-not ($log | Select-String -Pattern 'tgear: 26 travellers modifier recipes registered' -Quiet)) { throw 'the 26 travellers modifier recipes were not registered' }

$r = Invoke-SelfTest 'tgear.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) tgear selftest(s) failed: $($r.Failed -join ', ')" }
if ($r.Pass -lt 16) { throw "expected at least 16 tgear selftests to pass, got $($r.Pass)" }

# the log lines the tests leave
foreach ($line in 'tgear: .+ added twilightforest:item_display', 'tgear: .+ swapped the hotbar', "tgear: .+'s twilightforest:travellers_goggles is broken", 'tgear: .+ dodged a projectile') {
    if (-not (Get-ServerLog | Select-String -Pattern $line -Quiet)) { throw "missing log line '$line'" }
}
