# U7 `armory`: armour and tool tiers

Upstream 4.8's armour sets, tool tiers and their materials (behaviour: [docs/spec/armory.md](../spec/armory.md)).

| Part | Where |
|---|---|
| Numbers and rules (materials, tiers, ToolEvents / EntityEvents bonuses, CombatRules absorption, crafted enchantments, trim materials; pure, gtested) | `src/core/armory/Armory.{h,cpp}`, `tests/armory/ArmoryTest.cpp` |
| Behaviour (tools, set reactions, smelting modifier, mazebreaker, snow flags, crafted enchantments, no-template smithing, tooltips, rarities) | `src/feature/armory/Module.cpp`, `src/feature/armory/Armory.h` |
| Selftests `armory.*` (dx 700..799) | `src/feature/armory/SelfTests.cpp` |
| Armour toughness (Mob::getToughnessValue) | `src/feature/ext/armory/ArmorHooks.{h,cpp}` (contract extension, R18) |
| The 59 BP items (protection, durability, enchantability, repair items, attack damage, mazebreaker speeds, rarity, fire resistance, arctic dye) | `assets/pack/bp/items/armory/*.json` |
| The arctic icons' dye layer | `assets/pack/rp/item_texture.d/armory.json` |
| e2e | `tools/e2e/server/scenarios/armory.ps1`, `tools/e2e/client/scenarios/armory.ps1` |

## Bedrock adaptations

- **Items.** The BP items hold what Bedrock reads: `minecraft:wearable` protection, durability (Java's
  `ArmorItem.Type#getDurability`), `minecraft:enchantable`, `minecraft:repairable` (25 % per material, the
  combine rule of two pieces), `minecraft:damage`, the digger speeds and mining tiers of the F1 skeleton,
  `minecraft:rarity`, `minecraft:fire_resistant`. Bedrock's attack damage is an integer added to the player's base 1:
  the half point of the shovels (4.5 / 5.5) is an `item::Behaviour::attackBonus`. Attack speed has no Bedrock
  equivalent (Bedrock has no attack cooldown). The mining speed 6.5 of ironwood is the skeleton's 7.
- **Toughness.** Bedrock's data items have no toughness: `ext/armory` adds upstream's toughness of the worn pieces to
  `Mob::getToughnessValue` (Java's float sum rounded to Bedrock's integer total).
- **Powder snow, freezing.** The arctic and yeti boots set `CanStandOnPowderSnowComponent` and every fiery, arctic and
  yeti piece (`#minecraft:freeze_immune_wearables`) `FreezingImmuneComponent` while worn (the flags leather sets).
- **Arctic dye.** `minecraft:dyeable` (default 0xBDCFD9): Bedrock dyes and washes it in cauldrons (Bedrock's leather
  rules); the icon's `dyed` layer is upstream's `<id>_0`. The worn model still uses F2's generated attachables (the undyed
  `arctic_layer_N`; fiery not full bright): a manual attachable override fails F2's `models.attachables` selftest, which
  reads each attachable at the generated path and accepts only F2's render controllers. Java's crafting-grid dyeing has
  no Bedrock equivalent for non-leather items.
- **Crafted enchantments.** The 24 recipes whose upstream result carries `minecraft:enchantments` are special crafting of
  their BP recipes (F9): the result is enchanted with `enchant::set`.
- **No-template smithing.** Bedrock's smithing table only crafts with a template, so `gloamingforest:emperors_cloth_smithing`
  has no station of its own: upstream's crafting recipe `gloamingforest:emperors_cloth_recipe` (U9: any applicable piece or
  the elytra + the cloth) gives the same result, the base with `gloamingforest:emperors_cloth`. `noTemplateSmithing`
  keeps NoTemplateSmithingRecipe's match and result (the base copied with `additional_data` as item data).
- **Fiery smelting.** `gloamingforest:fiery_pick_smelting` runs on every loot roll mined with the fiery pickaxe: upstream's
  data smelting recipes first, then the game's furnace recipes (XP from Bedrock's furnace XP of the items), XP orbs at
  the breaker.
- **Trims.** upstream's six trim materials are not registered for the smithing table: Bedrock's client draws no inventory
  icon for armour trimmed with a material outside its own list (F9's spike S6), and drawing them is client-mod work not
  done here.

## Audit round 1

- The yeti set's chill aura now goes through U10's frosted helper
  (`gf::provisions::ext::doChillAuraEffect`, ApplyFrostedEffect#doChillAuraEffect) instead of a copy of its
  immunity rules (behaviour unchanged).
- Open: upstream's six trim materials (client icon drawing), the arctic dye tint on the worn model and the fiery
  set's full-bright attachables (both wait for F2's attachable check to accept a unit's attachable).
- Report: `armory.mazebreaker` failed twice in W1 full runs with 33 durability lost (1 + 16 + 16): U23's
  `ToolEvents#damageNonMazebreakerToolsMore` (`src/feature/labyrinth/Blocks.cpp`, `ia::onDestroy`) ran
  twice for one break; not seen on main's latest full run.

## Audit round 2

Fixed:

- **Duplicate emperor's cloth recipe.** The crafting-table copy of `gloamingforest:emperors_cloth_smithing`
  (`assets/pack/bp/recipes/armory/emperors_cloth_smithing{,_elytra}.json` and its special crafting) had the same
  layout and result as upstream's crafting recipe `gloamingforest:emperors_cloth_recipe_{armor,elytra}` (U9), and the
  game logged a duplicate-recipe warning. U7 drops its copy; U9's recipe now writes the same
  `gloamingforest:emperors_cloth` key U7's `noTemplateSmithing` writes (it wrote `emperors_cloth` before: a
  piece clothed one way had no tooltip or washing the other way). `armory.no_template_smithing` checks no special
  crafting is left under the smithing id.

Open (client visuals, not verifiable without a client run this round):

- F2 now accepts a unit's manual attachable (`models.attachables` finds attachables by identifier), so the arctic
  dye on the worn model and the fiery set's full brightness (FieryArmorModel renders at light 0xF000F0) can be
  manual attachables in `assets/pack/rp/attachables/armory/`. The vanilla materials are `armor_leather` =
  `entity_alphatest_change_color` (USE_COLOR_MASK: the dye is masked by the texture alpha) and
  `entity_emissive_alpha` (ALPHA_TEST + USE_EMISSIVE, emissiveness from the alpha too), so both need textures
  whose alpha encodes upstream's `_dyed` / `_overlay` split and the glow: not done without a client run to check the
  alpha convention.
- Upstream's six trim materials: Bedrock's trim materials are the game's own list (no data-driven material), and
  icons of armour trimmed with another material are client-mod drawing; not done.
