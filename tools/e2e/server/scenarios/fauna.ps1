# Passive and ambient fauna (U16): the fauna.* selftests.
#   fauna.attributes          the ten mobs spawned with F13's goldens (health, speed, attack, follow
#                             range, box, Java's eye height), their spawn set-up, the tiny bird's and
#                             dwarf rabbit's variants (index and saved string), new flying birds landed
#   fauna.bighorn_shear       shears through the interaction (1..3 wool of the colour, sheared, the
#                             shears worn), the loot table by colour / sheared, regrowth, dyeing
#   fauna.bighorn_lamb_colour a lamb of red and yellow parents is orange (Sheep#getOffspringColor)
#   fauna.loot_tables         the bighorn colour / sheared tables (the inlined vanilla sheep table),
#                             boar, raven, squirrel and dwarf rabbit (the inlined vanilla rabbit table)
#   fauna.variants_persist    the variant registries, the saved "variant" string restored, an
#                             unknown id ignored, a dwarf rabbit kit's variant
#   fauna.kobold_bread        the bread pick-up (one held, the rest dropped, the target dropped, the
#                             eating timer by difficulty), eating it, a hint book never eaten
#   fauna.mosquito_hunger     the swarm's bite gives Hunger
#   fauna.penguin_spawn_rule  penguins spawn on ice, not on stone
#   fauna.deer_senbei         shika senbei heals a deer 4
#   fauna.flying_bird         a spooked raven takes off and lands again
#   fauna.parrot_imitations   the neoforge:parrot_imitations entries
#   fauna.prey                vanilla wolves and cats get their prey target goals (addPrey)
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
$r = Invoke-SelfTest 'fauna.*' -TimeoutSeconds 600
if ($r.Fail -gt 0) { throw "$($r.Fail) fauna selftest(s) failed: $($r.Failed -join ', ')" }
if ($r.Pass -lt 12) { throw "only $($r.Pass) fauna selftests ran" }
