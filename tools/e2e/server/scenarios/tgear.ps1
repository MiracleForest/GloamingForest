# U11 (tgear): Travellers' Gear on BDS. The selftests carry the logic: the six items (slots, durability,
# modifier slots), the 26 modifier recipes as special crafting (add, refuse a duplicate / a full piece /
# two belts), the swap-hotbar transfer, the vest / gloves merge, the add_modifier trigger of crafted gear,
# /twilightforest travellers_gear in TF's check order, haste / stealth / broken pieces, efficient eater
# (movement exhaustion halved), auto repair (none within 200 ticks of armour damage), perfect dodge
# (seeded, about 30%), never-break, the belt swap (a belt in the hotbar stays; the companion channel),
# the item display rules and the map cycle (0 -> 1 -> -1 with its sounds), the zoom channel, the red thread's
# MultifaceBlock rules (looking-direction placement, use-on, loot per face, unsupported faces drop), the
# all-night goggles (phantom spawns denied, the rest counter runs on), the stealth target filter, a real
# arrow dodged through the combat contract and arrow magnetism (an arrow stuck before the vest stays).
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$log = Get-ServerLog
if (-not ($log | Select-String -Pattern 'tgear: 26 travellers modifier recipes registered' -Quiet)) { throw 'the 26 travellers modifier recipes were not registered' }

$r = Invoke-SelfTest 'tgear.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) tgear selftest(s) failed: $($r.Failed -join ', ')" }
if ($r.Pass -lt 21) { throw "expected at least 21 tgear selftests to pass, got $($r.Pass)" }

# the log lines the tests leave
foreach ($line in 'tgear: .+ added twilightforest:item_display', 'tgear: .+ swapped the hotbar', "tgear: .+'s twilightforest:travellers_goggles is broken", 'tgear: .+ dodged a projectile', 'tgear: .+ magnetized back') {
    if (-not (Get-ServerLog | Select-String -Pattern $line -Quiet)) { throw "missing log line '$line'" }
}
