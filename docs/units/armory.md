# U7 `armory`: armour and tool tiers

TF 4.8's armour sets, tool tiers and their materials (behaviour: [docs/spec/armory.md](../spec/armory.md)).

| Part | Where |
|---|---|
| Numbers and rules (materials, tiers, ToolEvents / EntityEvents bonuses, CombatRules absorption, crafted enchantments, trim materials; pure, gtested) | `src/core/armory/Armory.{h,cpp}`, `tests/armory/ArmoryTest.cpp` |
| Behaviour (tools, set reactions, smelting modifier, mazebreaker, snow flags, crafted enchantments, no-template smithing, tooltips, rarities) | `src/feature/armory/Module.cpp`, `src/feature/armory/Armory.h` |
| Selftests `armory.*` (dx 700..799) | `src/feature/armory/SelfTests.cpp` |
| Armour toughness (Mob::getToughnessValue) | `src/feature/ext/armory/ArmorHooks.{h,cpp}` (contract extension, R18) |
| The 59 BP items (protection, durability, enchantability, repair items, attack damage, mazebreaker speeds, rarity, fire resistance, arctic dye) | `assets/pack/bp/items/armory/*.json` |
| The arctic icons' dye layer | `assets/pack/rp/item_texture.d/armory.json` |
| Emperor's cloth no-template smithing in the crafting table | `assets/pack/bp/recipes/armory/` |
| e2e | `tools/e2e/server/scenarios/armory.ps1`, `tools/e2e/client/scenarios/armory.ps1` |

## Bedrock adaptations

- **Items.** The BP items hold what Bedrock reads: `minecraft:wearable` protection, durability (Java's
  `ArmorItem.Type#getDurability`), `minecraft:enchantable`, `minecraft:repairable` (25 % per material, the
  combine rule of two pieces), `minecraft:damage`, the digger speeds and mining tiers of the F1 skeleton,
  `minecraft:rarity`, `minecraft:fire_resistant`. Bedrock's attack damage is an integer added to the player's base 1:
  the half point of the shovels (4.5 / 5.5) is an `item::Behaviour::attackBonus`. Attack speed has no Bedrock
  equivalent (Bedrock has no attack cooldown). The mining speed 6.5 of ironwood is the skeleton's 7.
- **Toughness.** Bedrock's data items have no toughness: `ext/armory` adds TF's toughness of the worn pieces to
  `Mob::getToughnessValue` (Java's float sum rounded to Bedrock's integer total).
- **Powder snow, freezing.** The arctic and yeti boots set `CanStandOnPowderSnowComponent` and every fiery, arctic and
  yeti piece (`#minecraft:freeze_immune_wearables`) `FreezingImmuneComponent` while worn (the flags leather sets).
- **Arctic dye.** `minecraft:dyeable` (default 0xBDCFD9): Bedrock dyes and washes it in cauldrons (Bedrock's leather
  rules); the icon's `dyed` layer is TF's `<id>_0`. The worn model still uses F2's generated attachables (the undyed
  `arctic_layer_N`; fiery not full bright): a manual attachable override fails F2's `models.attachables` selftest, which
  reads each attachable at the generated path and accepts only F2's render controllers. Java's crafting-grid dyeing has
  no Bedrock equivalent for non-leather items.
- **Crafted enchantments.** The 24 recipes whose TF result carries `minecraft:enchantments` are special crafting of
  their BP recipes (F9): the result is enchanted with `enchant::set`.
- **No-template smithing.** Bedrock's smithing table only crafts with a template, so `twilightforest:emperors_cloth_smithing`
  is crafted in the crafting table (any armour + the cloth, the elytra + the cloth) with NoTemplateSmithingRecipe's
  match and result (the base copied with `additional_data` as item data).
- **Fiery smelting.** `twilightforest:fiery_pick_smelting` runs on every loot roll mined with the fiery pickaxe: TF's
  data smelting recipes first, then the game's furnace recipes (XP from Bedrock's furnace XP of the items), XP orbs at
  the breaker.
- **Trims.** TF's six trim materials are not registered for the smithing table: Bedrock's client draws no inventory
  icon for armour trimmed with a material outside its own list (F9's spike S6), and drawing them is client-mod work not
  done here.
