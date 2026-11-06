# U8 (weapons): TF's weapons and throwables. The selftests ("weapons.*", dx 800..899) cover the unit
# record's e2e: every item's durability / stack / rarity (weapons.items), the crumble horn on a
# seeded random (weapons.crumble_horn), transformation powder conversions (pig -> boar -> pig),
# the glass sword shattering (and its infinite variant not), the ice bomb's terrain and zone, the
# lamp of cinders burning thorns and mobs, the ore magnet's vein pull (24 at most, coal ignored),
# the triple bow's three arrows, seeker homing, the block and chain's 10 damage and return, the
# cube of annihilation (soft blocks gone, obsidian stays), shield blocking / parrying and the
# feather fan, the moonworm queen repair recipe and the client showcase.
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$r = Invoke-SelfTest 'weapons.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) weapons selftest(s) failed" }
if ($r.Pass -lt 13) { throw "expected at least 13 weapons selftests to pass, got $($r.Pass)" }
foreach ($line in 'weapons: registered', 'weapons: block and chain hit', 'weapons: transformation powder turned',
                  'weapons: shot the twilightforest:triple_bow', 'weapons: threw a cube of annihilation') {
    if (-not (Get-ServerLog | Select-String -SimpleMatch $line -Quiet)) { throw "missing log line: $line" }
}
