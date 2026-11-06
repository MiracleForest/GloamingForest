# U7 (armory): TF's armour and tool tiers on BDS. The selftests ("armory.*", dx 700..799) carry the
# unit record's e2e: the 59 items' durability / protection / enchantability / toughness
# (armory.items), the tools' attack damage on a cow (armory.attack_damage), knightmetal's bonus against
# armoured and unarmoured zombies (armory.knightmetal), the minotaur axe's +7 while sprinting
# (armory.minotaur_axe), the fiery sword's 15 s and the fiery tools' 1 s fire (armory.fiery_tools), the
# fiery pickaxe smelting an iron ore's drop and TF's data recipes (armory.fiery_smelting), the
# mazebreaker's x16 and the 16 extra wear of another tool on a castle brick (armory.mazebreaker), the
# fiery / yeti set reactions (armory.set_reactions), armour toughness (armory.toughness), phantom
# armour kept on death (armory.phantom_kept), arctic dye washed off in a cauldron (armory.arctic_dye),
# the 24 crafted default enchantments (armory.crafted_enchantments) and the emperor's cloth
# no-template smithing (armory.no_template_smithing).
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

if (-not (Get-ServerLog | Select-String -Pattern 'armory: 28 armour pieces, 18 tools, 24 crafted enchantments installed' -Quiet)) {
    throw 'the armory module did not install'
}

$r = Invoke-SelfTest 'armory.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) armory selftest(s) failed: $($r.Failed -join ', ')" }
if ($r.Pass -lt 13) { throw "expected at least 13 armory selftests to pass, got $($r.Pass)" }
