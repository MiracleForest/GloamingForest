# Armor and tool tiers (U7 `armory`)

Behaviour spec of unit **U7** (wave W1), translated from the upstream 4.8 (`9e40a757`, MC 1.21.1) by F16. It is
the checklist the U7 worker implements and the A2 audit compares against. Sections 1-5 are the behaviour; Appendix A
lists every id U7 owns with upstream's registration, recipes, tags and data; Appendix B the strings.

## 1. Scope

- **Armor sets**: naga scale (chestplate, leggings), ironwood, fiery, steeleaf, knightmetal, phantom (helmet,
  chestplate), arctic (dyeable), yeti; 8 armor materials (`travellers` is U11's).
- **Tool tiers**: ironwood (sword, shovel, pickaxe, axe, hoe), steeleaf (same five), fiery (sword, pickaxe),
  knightmetal (sword, pickaxe, axe), `gold_minotaur_axe`, `diamond_minotaur_axe`, `mazebreaker_pickaxe`.
- **Materials**: `naga_scale`, `raw_ironwood`, `ironwood_ingot`, `steeleaf_ingot`, `fiery_blood`, `fiery_tears`,
  `fiery_ingot`, `armor_shard`, `armor_shard_cluster`, `knightmetal_ingot`, `knightmetal_ring`, `arctic_fur`,
  `alpha_yeti_fur`.
- The `no_template_smithing` recipe type (R4), the global loot modifier `fiery_pick_smelting`, damage types `fiery`
  and `knightmetal`, sound `item.gloamingforest.knightmetal_armor.equip`, 6 trim materials, the `incorrect_for_*` and
  `repairs_*` tags, 61 recipes.

Not U7: `knightmetal_shield`, `ice_sword`, `glass_sword`, bows (U8), giant tools and the `giant_pick_grouping`
modifier (U29; `#repairs_giant_tools` is listed here because owners.tsv gives it to U7), travellers' gear (U11), the
`fire_react` / `chill_aura` enchantments (U10; they are separate from the set effects below), the emperor's cloth
component (U9; its smithing recipe uses U7's type), `#gloamingforest:kept_on_death` (U9).

Contracts: item, loot, enchant+effects, combat, recipes, geo, client.render.

## 2. Upstream sources

`init/GloamingItems.java`, `init/GloamingArmorMaterials.java`, `util/GloamingToolMaterials.java`, `init/GloamingTrimMaterials.java`,
`init/GloamingDamageTypes.java`, `init/GloamingLootModifiers.java`, `init/GloamingRecipes.java`, `item/ArcticArmorItem.java`,
`item/FieryArmorItem.java`, `item/PhantomArmorItem.java`, `item/YetiArmorItem.java`, `item/FieryPickItem.java`,
`item/FierySwordItem.java`, `item/KnightmetalSwordItem.java`, `item/KnightmetalPickItem.java`,
`item/KnightmetalAxeItem.java`, `item/MinotaurAxeItem.java`, `item/MazebreakerPickItem.java`,
`events/ToolEvents.java` (knightmetal, minotaur axe, fiery ignite, mazebreaker wear), `events/EntityEvents.java`
(`entityHurts`: fiery/yeti set reactions, `getGearCoverage`), `enchantment/ApplyFrostedEffect.java`
(`doChillAuraEffect`), `loot/modifiers/FieryToolSmeltingModifier.java`, `item/recipe/NoTemplateSmithingRecipe.java`,
`events/RegistrationEvents.java` (arctic armor cauldron washing), `client/model/armor/*` (GloamingArmorModel,
ArcticArmorModel, FieryArmorModel, KnightmetalArmorModel, PhantomArmorModel, YetiArmorModel),
`client/renderer/armor/*`, `client/event/ColorHandler.java` (arctic dye), `data/CraftingGenerator.java` (enchanted
recipe results).

## 3. Behaviour

### 3.1 Armor materials (`GloamingArmorMaterials`)

Defense per piece (boots / leggings / chestplate / helmet / body), enchantability, toughness, knockback resistance 0,
repair ingredient, equip sound; durability = vanilla `Type.getDurability(m)` (boots 13m, leggings 15m, chestplate 16m,
helmet 11m) with m:

| Material | Defense B/L/C/H/body | Ench. | Toughness | Repair | m | Rarity | Notes |
|---|---|---|---|---|---|---|---|
| `naga_scale` | 3/6/7/2/8 | 15 | 0.5 | naga scale | 21 | UNCOMMON | chestplate + leggings only |
| `ironwood` | 2/5/7/2/5 | 15 | 0 | ironwood ingot | 20 | - | |
| `fiery` | 4/7/9/4/13 | 10 | 1.5 | fiery ingot | 25 | UNCOMMON | fire resistant items; set reaction 3.3 |
| `steeleaf` | 3/6/8/3/11 | 9 | 0 | steeleaf ingot | 10 | - | |
| `knightmetal` | 3/6/8/3/11 | 8 | 1.0 | knightmetal ingot | 20 | - | equip sound `item.gloamingforest.knightmetal_armor.equip` |
| `phantom` | 3/6/8/3/10 | 8 | 2.5 | knightmetal ingot | 30 | UNCOMMON | helmet + chestplate; never lost on death (`#kept_on_death`, U9) |
| `yeti` | 3/6/7/4/11 | 15 | 3.0 | alpha yeti fur | 20 | UNCOMMON | set reaction 3.3; boots walk on powder snow |
| `arctic` | 2/5/7/2/7 | 8 | 2.0 | arctic fur | 10 | - | two layers `arctic` `_dyed` (dyeable) + `_overlay`; boots walk on powder snow |

Every other material uses `armor.equip_generic`. Texture layers `gloamingforest:<material>`.

### 3.2 Tool tiers (`GloamingToolMaterials`)

| Tier | Incorrect-for tag | Uses | Speed | Attack bonus | Ench. | Repair tag |
|---|---|---|---|---|---|---|
| IRONWOOD | `#gloamingforest:incorrect_for_ironwood_tool` | 512 | 6.5 | 2 | 25 | `#repairs_ironwood_tools` |
| FIERY | `#incorrect_for_fiery_tool` | 1024 | 9.0 | 4 | 10 | `#repairs_fiery_tools` |
| STEELEAF | `#incorrect_for_steeleaf_tool` | 131 | 8.0 | 3 | 9 | `#repairs_steeleaf_tools` |
| KNIGHTMETAL | `#incorrect_for_knightmetal_tool` | 512 | 8.0 | 3 | 8 | `#repairs_knightmetal_tools` |
| (GIANT, ICE, GLASS: U29/U8 tiers, repair tags `#repairs_giant_tools`, `#repairs_ice_tools` are U7 rows) | | 1024 / 32 / 1 | 4.0 / 1.0 / 1.0 | 1.0 / 3.5 / 36.0 | 5 / 5 / 30 | |

Item attributes (vanilla `createAttributes(tier, damage, speed)`): swords +3 / -2.4; shovels +1.5 / -3.0; pickaxes
+1 / -2.8; ironwood axe +6 / -3.1, steeleaf axe +6 / -3.0, knightmetal axe +6 / (8.0*0.05 - 3.4 = -3.0); ironwood hoe
-2 / -1.0, steeleaf hoe -3 / -0.5; minotaur axes (gold / diamond tier) +6 / -3.2; mazebreaker (diamond tier) +1 / -2.8.
Fiery sword / pickaxe are fire resistant and UNCOMMON; mazebreaker RARE and `setNoRepair`; diamond minotaur axe
UNCOMMON, gold COMMON.

Crafted tools and armor carry upstream's **default enchantments** in the recipe result components (`minecraft:enchantments`
on 24 recipes: all ironwood tools and armor, all steeleaf tools and armor, naga chestplate and leggings, the four yeti
pieces; e.g. `steeleaf_pickaxe` = Fortune II). The exact levels are in the Result column of the Appendix A recipe
table; keep them exactly (upstream's `showEnchantmentsOnItems` common key, F9's `items` section, decides whether they
show).

### 3.3 Special behaviour

- **Fiery sword** (`FierySwordItem`): on a successful hit the target (not fire immune) is set on fire for **15 s**;
  client: 20 `flame` particles around the target. **Fiery pickaxe** (`FieryPickItem`): same 15 s ignite on hit, client
  one flame particle burst. Also `ToolEvents.fieryToolSetFire`: any damage whose attacker holds a fiery sword or
  pickaxe in the main hand ignites the victim for 1 s. Tooltips `item.gloamingforest.fiery_sword.desc` ("Burns
  targets"), `fiery_pickaxe.desc` ("Auto-smelting").
- **Fiery pickaxe smelting** (global loot modifier `gloamingforest:fiery_pick_smelting`, condition match_tool
  `gloamingforest:fiery_pickaxe`): every dropped stack that has a SMELTING recipe is replaced by its result
  (count x result count); the summed recipe XP is awarded as orbs at the breaker's position (rounded).
- **Knightmetal** (`ToolEvents.doKnightmetalToolLogic`, server, direct attacker's main hand): sword or pickaxe vs a
  target with armor value > 0: + (int)(**2** x armor cover percentage) damage, or +2 when the cover is 0; axe vs a
  target with armor value 0: damage = original + **2**. Either way the target shows the enchanted-hit sparkles
  (animate packet 5). Tooltips "Extra damage to armored targets" (sword/pickaxe), "Extra damage to unarmored
  targets" (axe).
- **Minotaur axes** (`ToolEvents.addExtraAxeChargingDamage`): while the attacker is **sprinting**, +**7** damage and
  sparkles. Enchantability = gold's (22) for both axes. Tooltip `item.gloamingforest.minotaur_axe.desc`.
- **Mazebreaker** (`MazebreakerPickItem`): destroy speed x**16** on `#gloamingforest:mazebreaker_accelerated`
  blocks; cannot be repaired. Breaking an accelerated block with any other damageable tool costs **16** extra
  durability (`damageNonMazebreakerToolsMore`).
- **Set reactions** (`EntityEvents.entityHurts`, after damage > 0 from an entity): fiery pieces worn = f; fireLevel =
  5f; if rand(25) < fireLevel and the attacker is not fire immune, ignite the attacker for fireLevel/2 seconds. Yeti
  pieces worn = y; the attacker (if living) gets `gloamingforest:frosted` for 5y + 5 ticks, amplifier y, only when
  y > 0 and the attacker is not freeze immune / wearing freeze-immune armor / a creative player
  (`ApplyFrostedEffect.doChillAuraEffect`).
- **Arctic armor**: dyeable (`minecraft:dyed_color`, default 0xFFBDCFD9, tint index 1), a water cauldron removes the
  dye; arctic and yeti boots let the wearer walk on powder snow. Tooltips "Dyeable", "Chills attackers", "Burns
  attackers", "Is never lost on death".
- **Armor models** (client): upstream armor models (`GloamingModelLayers.*_ARMOR_INNER/OUTER`): yeti and phantom use their own
  models (legs inner, others outer), arctic a simple model with the dye layer, fiery an emissive layer, knightmetal a
  custom model.
- **No-template smithing** (`gloamingforest:no_template_smithing`): smithing table with an **empty** template slot,
  base matching `base`, addition matching `addition`; refused if the base already has any `additional_data`
  component; result = a copy of the base with the components set. Upstream uses it for
  `gloamingforest:emperors_cloth_smithing` (base `#emperors_cloth_applicable`, addition `emperors_cloth`, sets
  `gloamingforest:emperors_cloth` = {}).
- Damage types `gloamingforest:fiery` (fiery block step) and `gloamingforest:knightmetal` (knightmetal block) with their
  death messages (F10 strings) are dealt by U5's blocks via `combat::hurt`.
- Trim materials (`carminite`, `fiery`, `ironwood`, `knightmetal`, `naga_scale`, `steeleaf`): Appendix A JSON
  (asset name, ingredient, colour, description).

## 4. Bedrock adaptation notes

- Armor and tools are BP items with `minecraft:wearable` / `minecraft:digger` / `minecraft:damage` components (F1
  skeleton); defense, durability, enchantability, repair items and mining tiers come from the tables above (Bedrock
  mining speed per block tag; `incorrect_for_*` becomes the digger's block list).
- Attachables for the upstream armor models through F2 (`tools/models/attachable.ps1 -Item <id>`); arctic dye needs a tint
  (client mod; vanilla clients see the default colour).
- Knightmetal / minotaur bonuses, fiery ignite and set reactions through `combat::onIncoming` / `onDealt` and
  `item::Behaviour::attackBonus`; sparkles via `fx`.
- Fiery smelting: `loot::addGlobalModifier("gloamingforest:fiery_pick_smelting", ...)` with the smelting lookup from
  `recipes::ofType("minecraft:smelting")`.
- Mazebreaker speed via `item::Behaviour::destroySpeed`; extra wear via `interactions::onDestroy`.
- No-template smithing on Bedrock: the smithing table needs a template; use a native smithing hook in F9's recipes
  contract (`registerType("gloamingforest:no_template_smithing")`) and the client-mod screen; vanilla clients need a
  fallback (special crafting in the crafting table) noted in not-ported.md.
- Default enchantments on crafted items: Bedrock recipe outputs cannot carry enchantments natively; F9/U7 enchant the
  result after crafting (crafting hook) using `enchant::set`.
- Powder snow walking: Bedrock's leather-boots rule; add the upstream boots to the equivalent (`minecraft:wearable` +
  server-side check) or document.

## 5. Acceptance checklist (selftests `armory.*`, dx 700-799)

- Defense, toughness, durability and enchantability per piece; tool damage/speed attributes; repair materials.
- Knightmetal sword vs armored zombie +2 x cover; axe vs unarmored +2; minotaur axe sprint +7.
- Fiery sword ignites 15 s; fiery pickaxe smelts iron ore drops and awards XP.
- Mazebreaker x16 on mazestone; other tools lose 16 durability there.
- Fiery set reaction probability (rand(25) < 5f) and yeti frosted duration/amplifier by pieces worn.
- Emperor's cloth smithing via no-template smithing; arctic dye + cauldron wash; crafted items carry upstream's
  enchantments.

## Appendix A: Owned ids (generated inventory)

Every id this unit owns in `docs/spec/owners.tsv`, with the upstream 4.8 data the W1 worker must reproduce. Registration statements are upstream's own Java (`init/upstream*.java`), condensed (namespaces such as `MapColor.` dropped); loot, recipe, tag, advancement and data rows are summarised from upstream's generated JSON (`src/generated/resources`). Loot notation: `self` = drops the block itself when it survives the explosion; `alternatives(a → b)` = first matching child; `[if ...]` = conditions; `{...}` = functions. In summaries `mc:` is `minecraft:` and ids without a namespace are `gloamingforest:`.

| Kind | Count |
|---|---|
| `advancement` | 61 |
| `armor_material` | 8 |
| `damage_type` | 2 |
| `e2e` | 1 |
| `item` | 59 |
| `lang` | 76 |
| `loot_modifier` | 1 |
| `net` | 1 |
| `path` | 20 |
| `recipe` | 61 |
| `recipe_serializer` | 1 |
| `selftest` | 1 |
| `selftest_dx` | 1 |
| `sound_event` | 1 |
| `tag` | 14 |
| `trim_material` | 6 |
| `worlddb` | 1 |

### Unit facts

| Kind | Ids |
|---|---|
| Selftest glob (`selftest`) | `armory.*` |
| Selftest dx range (`selftest_dx`) | `700-799` |
| World DB keys (`worlddb`) | `gf:armory:*` |
| E2E scenario (`e2e`) | `armory` |
| Net channels (`net`) | `armory.*` |
| Owned paths (`path`) | `**/*.d/armory.json`, `assets/data/*/armory.json`, `assets/gen/audio/armory/**`, `assets/gen/geo/armory/**`, `assets/gen/import/armory/**`, `assets/lang/*/armory.json`, `assets/pack/{bp,rp}/**/armory/**`, `assets/pack/rp/texts.d/*/armory.lang`, `assets/structures/armory/**`, `assets/tags/**/*.d/armory.json`, `docs/units/armory.md`, `src/client/armory/**`, `src/core/armory/**`, `src/feature/armory/**`, `src/feature/ext/armory/**`, `tests/armory/**`, `tools/e2e/*/scenarios/armory{,.*}.ps1`, `tools/import/overrides/armory.json`, `tools/sounds/recipes/armory.json`, `tools/structures/armory/**` |

### Items (59)

| Item | English name | upstream registration (`GloamingItems`) | Notes |
|---|---|---|---|
| `gloamingforest:alpha_yeti_fur` | Alpha Yeti Fur | `ITEMS.register("alpha_yeti_fur", new Item(new Item.Properties().rarity(UNCOMMON)));` |  |
| `gloamingforest:arctic_boots` | Arctic Boots | `ITEMS.register("arctic_boots", new ArcticArmorItem(GloamingArmorMaterials.ARCTIC, ArmorItem.Type.BOOTS, new Item.Properties().durability(ArmorItem.Type.BOOTS.getDurability(10))));` | water cauldron washes the dye off (CauldronInteraction.DYED_ITEM); item tags: #mc:dyeable, #mc:enchantable/foot_armor, #mc:foot_armor, #mc:freeze_immune_wearables |
| `gloamingforest:arctic_chestplate` | Arctic Jacket | `ITEMS.register("arctic_chestplate", new ArcticArmorItem(GloamingArmorMaterials.ARCTIC, ArmorItem.Type.CHESTPLATE, new Item.Properties().durability(ArmorItem.Type.CHESTPLATE.getDurability(10))));` | water cauldron washes the dye off (CauldronInteraction.DYED_ITEM); item tags: #mc:chest_armor, #mc:dyeable, #mc:enchantable/chest_armor, #mc:freeze_immune_wearables |
| `gloamingforest:arctic_fur` | Arctic Fur | `ITEMS.register("arctic_fur", new Item(new Item.Properties()));` | item tags: #arctic_fur |
| `gloamingforest:arctic_helmet` | Arctic Hood | `ITEMS.register("arctic_helmet", new ArcticArmorItem(GloamingArmorMaterials.ARCTIC, ArmorItem.Type.HELMET, new Item.Properties().durability(ArmorItem.Type.HELMET.getDurability(10))));` | water cauldron washes the dye off (CauldronInteraction.DYED_ITEM); item tags: #mc:dyeable, #mc:enchantable/head_armor, #mc:freeze_immune_wearables, #mc:head_armor |
| `gloamingforest:arctic_leggings` | Arctic Leggings | `ITEMS.register("arctic_leggings", new ArcticArmorItem(GloamingArmorMaterials.ARCTIC, ArmorItem.Type.LEGGINGS, new Item.Properties().durability(ArmorItem.Type.LEGGINGS.getDurability(10))));` | water cauldron washes the dye off (CauldronInteraction.DYED_ITEM); item tags: #mc:dyeable, #mc:enchantable/leg_armor, #mc:freeze_immune_wearables, #mc:leg_armor |
| `gloamingforest:armor_shard` | Armor Shard | `ITEMS.register("armor_shard", new Item(new Item.Properties()));` | item tags: #alexscaves:ferromagnetic_items |
| `gloamingforest:armor_shard_cluster` | Armor Shard Cluster | `ITEMS.register("armor_shard_cluster", new Item(new Item.Properties()));` | item tags: #alexscaves:ferromagnetic_items, #c:raw_materials, #c:raw_materials/knightmetal |
| `gloamingforest:diamond_minotaur_axe` | Diamond Minotaur Axe | `ITEMS.register("diamond_minotaur_axe", new MinotaurAxeItem(Tiers.DIAMOND, new Item.Properties().rarity(UNCOMMON).attributes(AxeItem.createAttributes(Tiers.DIAMOND, 6.0F, -3.2F))));` | item tags: #mc:axes |
| `gloamingforest:fiery_blood` | Fiery Blood | `ITEMS.register("fiery_blood", new Item(new Item.Properties().rarity(UNCOMMON)));` | item tags: #fiery_vial |
| `gloamingforest:fiery_boots` | Fiery Boots | `ITEMS.register("fiery_boots", new FieryArmorItem(GloamingArmorMaterials.FIERY, ArmorItem.Type.BOOTS, new Item.Properties().durability(ArmorItem.Type.BOOTS.getDurability(25)).fireResistant().rarity(UNCOMMON)));` | item tags: #alexscaves:ferromagnetic_items, #mc:enchantable/foot_armor, #mc:foot_armor, #mc:freeze_immune_wearables |
| `gloamingforest:fiery_chestplate` | Fiery Chestplate | `ITEMS.register("fiery_chestplate", new FieryArmorItem(GloamingArmorMaterials.FIERY, ArmorItem.Type.CHESTPLATE, new Item.Properties().durability(ArmorItem.Type.CHESTPLATE.getDurability(25)).fireResistant().rarity(UNCOMMON)));` | item tags: #alexscaves:ferromagnetic_items, #mc:chest_armor, #mc:enchantable/chest_armor, #mc:freeze_immune_wearables |
| `gloamingforest:fiery_helmet` | Fiery Helmet | `ITEMS.register("fiery_helmet", new FieryArmorItem(GloamingArmorMaterials.FIERY, ArmorItem.Type.HELMET, new Item.Properties().durability(ArmorItem.Type.HELMET.getDurability(25)).fireResistant().rarity(UNCOMMON)));` | item tags: #alexscaves:ferromagnetic_items, #mc:enchantable/head_armor, #mc:freeze_immune_wearables, #mc:head_armor |
| `gloamingforest:fiery_ingot` | Fiery Ingot | `ITEMS.register("fiery_ingot", new Item(new Item.Properties().fireResistant().rarity(UNCOMMON)));` | item tags: #alexscaves:ferromagnetic_items, #c:ingots, #c:ingots/fiery, #mc:beacon_payment_items, #mc:trim_materials, #repairs_fiery_tools |
| `gloamingforest:fiery_leggings` | Fiery Leggings | `ITEMS.register("fiery_leggings", new FieryArmorItem(GloamingArmorMaterials.FIERY, ArmorItem.Type.LEGGINGS, new Item.Properties().durability(ArmorItem.Type.LEGGINGS.getDurability(25)).fireResistant().rarity(UNCOMMON)));` | item tags: #alexscaves:ferromagnetic_items, #mc:enchantable/leg_armor, #mc:freeze_immune_wearables, #mc:leg_armor |
| `gloamingforest:fiery_pickaxe` | Fiery Pickaxe | `ITEMS.register("fiery_pickaxe", new FieryPickItem(GloamingToolMaterials.FIERY, new Item.Properties().fireResistant().rarity(UNCOMMON).attributes(PickaxeItem.createAttributes(GloamingToolMaterials.FIERY, 1.0F, -2.8F))));` | item tags: #alexscaves:ferromagnetic_items, #mc:cluster_max_harvestables, #mc:pickaxes |
| `gloamingforest:fiery_sword` | Fiery Sword | `ITEMS.register("fiery_sword", new FierySwordItem(GloamingToolMaterials.FIERY, new Item.Properties().fireResistant().rarity(UNCOMMON).attributes(SwordItem.createAttributes(GloamingToolMaterials.FIERY, 3, -2.4F))));` | item tags: #alexscaves:ferromagnetic_items, #mc:swords |
| `gloamingforest:fiery_tears` | Fiery Tears | `ITEMS.register("fiery_tears", new Item(new Item.Properties().rarity(UNCOMMON)));` | item tags: #fiery_vial |
| `gloamingforest:gold_minotaur_axe` | Golden Minotaur Axe | `ITEMS.register("gold_minotaur_axe", new MinotaurAxeItem(Tiers.GOLD, new Item.Properties().rarity(COMMON).attributes(AxeItem.createAttributes(Tiers.GOLD, 6.0F, -3.2F))));` | item tags: #mc:axes, #mc:piglin_loved |
| `gloamingforest:ironwood_axe` | Ironwood Axe | `ITEMS.register("ironwood_axe", new AxeItem(GloamingToolMaterials.IRONWOOD, new Item.Properties().attributes(AxeItem.createAttributes(GloamingToolMaterials.IRONWOOD, 6.0F, -3.1F))));` | item tags: #alexscaves:ferromagnetic_items, #mc:axes |
| `gloamingforest:ironwood_boots` | Ironwood Boots | `ITEMS.register("ironwood_boots", new ArmorItem(GloamingArmorMaterials.IRONWOOD, ArmorItem.Type.BOOTS, new Item.Properties().durability(ArmorItem.Type.BOOTS.getDurability(20))));` | item tags: #alexscaves:ferromagnetic_items, #mc:enchantable/foot_armor, #mc:foot_armor |
| `gloamingforest:ironwood_chestplate` | Ironwood Chestplate | `ITEMS.register("ironwood_chestplate", new ArmorItem(GloamingArmorMaterials.IRONWOOD, ArmorItem.Type.CHESTPLATE, new Item.Properties().durability(ArmorItem.Type.CHESTPLATE.getDurability(20))));` | item tags: #alexscaves:ferromagnetic_items, #mc:chest_armor, #mc:enchantable/chest_armor |
| `gloamingforest:ironwood_helmet` | Ironwood Helmet | `ITEMS.register("ironwood_helmet", new ArmorItem(GloamingArmorMaterials.IRONWOOD, ArmorItem.Type.HELMET, new Item.Properties().durability(ArmorItem.Type.HELMET.getDurability(20))));` | item tags: #alexscaves:ferromagnetic_items, #mc:enchantable/head_armor, #mc:head_armor |
| `gloamingforest:ironwood_hoe` | Ironwood Hoe | `ITEMS.register("ironwood_hoe", new HoeItem(GloamingToolMaterials.IRONWOOD, new Item.Properties().attributes(HoeItem.createAttributes(GloamingToolMaterials.IRONWOOD, -2, -1.0F))));` | item tags: #alexscaves:ferromagnetic_items, #mc:hoes |
| `gloamingforest:ironwood_ingot` | Ironwood Ingot | `ITEMS.register("ironwood_ingot", new Item(new Item.Properties()));` | item tags: #alexscaves:ferromagnetic_items, #c:ingots, #c:ingots/ironwood, #mc:beacon_payment_items, #mc:trim_materials, #repairs_ironwood_tools |
| `gloamingforest:ironwood_leggings` | Ironwood Leggings | `ITEMS.register("ironwood_leggings", new ArmorItem(GloamingArmorMaterials.IRONWOOD, ArmorItem.Type.LEGGINGS, new Item.Properties().durability(ArmorItem.Type.LEGGINGS.getDurability(20))));` | item tags: #alexscaves:ferromagnetic_items, #mc:enchantable/leg_armor, #mc:leg_armor |
| `gloamingforest:ironwood_pickaxe` | Ironwood Pickaxe | `ITEMS.register("ironwood_pickaxe", new PickaxeItem(GloamingToolMaterials.IRONWOOD, new Item.Properties().attributes(PickaxeItem.createAttributes(GloamingToolMaterials.IRONWOOD, 1.0F, -2.8F))));` | item tags: #alexscaves:ferromagnetic_items, #mc:cluster_max_harvestables, #mc:pickaxes |
| `gloamingforest:ironwood_shovel` | Ironwood Shovel | `ITEMS.register("ironwood_shovel", new ShovelItem(GloamingToolMaterials.IRONWOOD, new Item.Properties().attributes(ShovelItem.createAttributes(GloamingToolMaterials.IRONWOOD, 1.5F, -3.0F))));` | item tags: #alexscaves:ferromagnetic_items, #mc:shovels |
| `gloamingforest:ironwood_sword` | Ironwood Sword | `ITEMS.register("ironwood_sword", new SwordItem(GloamingToolMaterials.IRONWOOD, new Item.Properties().attributes(SwordItem.createAttributes(GloamingToolMaterials.IRONWOOD, 3, -2.4F))));` | item tags: #alexscaves:ferromagnetic_items, #mc:swords |
| `gloamingforest:knightmetal_axe` | Knightmetal Axe | `ITEMS.register("knightmetal_axe", new KnightmetalAxeItem(GloamingToolMaterials.KNIGHTMETAL, new Item.Properties().attributes(AxeItem.createAttributes(GloamingToolMaterials.KNIGHTMETAL, 6F, GloamingToolMaterials.KNIGHTMETAL.getSpeed() * 0.05f - 3.4f))));` | item tags: #alexscaves:ferromagnetic_items, #mc:axes |
| `gloamingforest:knightmetal_boots` | Knightmetal Boots | `ITEMS.register("knightmetal_boots", new ArmorItem(GloamingArmorMaterials.KNIGHTMETAL, ArmorItem.Type.BOOTS, new Item.Properties().durability(ArmorItem.Type.BOOTS.getDurability(20))));` | item tags: #alexscaves:ferromagnetic_items, #mc:enchantable/foot_armor, #mc:foot_armor |
| `gloamingforest:knightmetal_chestplate` | Knightmetal Chestplate | `ITEMS.register("knightmetal_chestplate", new ArmorItem(GloamingArmorMaterials.KNIGHTMETAL, ArmorItem.Type.CHESTPLATE, new Item.Properties().durability(ArmorItem.Type.CHESTPLATE.getDurability(20))));` | item tags: #alexscaves:ferromagnetic_items, #mc:chest_armor, #mc:enchantable/chest_armor |
| `gloamingforest:knightmetal_helmet` | Knightmetal Helmet | `ITEMS.register("knightmetal_helmet", new ArmorItem(GloamingArmorMaterials.KNIGHTMETAL, ArmorItem.Type.HELMET, new Item.Properties().durability(ArmorItem.Type.HELMET.getDurability(20))));` | item tags: #alexscaves:ferromagnetic_items, #mc:enchantable/head_armor, #mc:head_armor |
| `gloamingforest:knightmetal_ingot` | Knightmetal Ingot | `ITEMS.register("knightmetal_ingot", new Item(new Item.Properties()));` | item tags: #alexscaves:ferromagnetic_items, #c:ingots, #c:ingots/knightmetal, #mc:beacon_payment_items, #mc:trim_materials, #repairs_knightmetal_tools |
| `gloamingforest:knightmetal_leggings` | Knightmetal Greaves | `ITEMS.register("knightmetal_leggings", new ArmorItem(GloamingArmorMaterials.KNIGHTMETAL, ArmorItem.Type.LEGGINGS, new Item.Properties().durability(ArmorItem.Type.LEGGINGS.getDurability(20))));` | item tags: #alexscaves:ferromagnetic_items, #mc:enchantable/leg_armor, #mc:leg_armor |
| `gloamingforest:knightmetal_pickaxe` | Knightmetal Pickaxe | `ITEMS.register("knightmetal_pickaxe", new KnightmetalPickItem(GloamingToolMaterials.KNIGHTMETAL, new Item.Properties().attributes(PickaxeItem.createAttributes(GloamingToolMaterials.KNIGHTMETAL, 1, -2.8F))));` | item tags: #alexscaves:ferromagnetic_items, #mc:cluster_max_harvestables, #mc:pickaxes |
| `gloamingforest:knightmetal_ring` | Knightmetal Loop | `ITEMS.register("knightmetal_ring", new Item(new Item.Properties()));` | item tags: #alexscaves:ferromagnetic_items |
| `gloamingforest:knightmetal_sword` | Knightmetal Sword | `ITEMS.register("knightmetal_sword", new KnightmetalSwordItem(GloamingToolMaterials.KNIGHTMETAL, new Item.Properties().attributes(SwordItem.createAttributes(GloamingToolMaterials.KNIGHTMETAL, 3, -2.4F))));` | item tags: #alexscaves:ferromagnetic_items, #mc:swords |
| `gloamingforest:mazebreaker_pickaxe` | Mazebreaker | `ITEMS.register("mazebreaker_pickaxe", new MazebreakerPickItem(Tiers.DIAMOND, new Item.Properties().setNoRepair().rarity(RARE).attributes(PickaxeItem.createAttributes(Tiers.DIAMOND, 1.0F, -2.8F))));` | item tags: #alexscaves:ferromagnetic_items, #mc:cluster_max_harvestables, #mc:pickaxes |
| `gloamingforest:naga_chestplate` | Naga Scale Tunic | `ITEMS.register("naga_chestplate", new ArmorItem(GloamingArmorMaterials.NAGA, ArmorItem.Type.CHESTPLATE, new Item.Properties().durability(ArmorItem.Type.CHESTPLATE.getDurability(21)).rarity(UNCOMMON)));` | item tags: #mc:chest_armor, #mc:enchantable/chest_armor |
| `gloamingforest:naga_leggings` | Naga Scale Leggings | `ITEMS.register("naga_leggings", new ArmorItem(GloamingArmorMaterials.NAGA, ArmorItem.Type.LEGGINGS, new Item.Properties().durability(ArmorItem.Type.LEGGINGS.getDurability(21)).rarity(UNCOMMON)));` | item tags: #mc:enchantable/leg_armor, #mc:leg_armor |
| `gloamingforest:naga_scale` | Naga Scale | `ITEMS.register("naga_scale", new Item(new Item.Properties().rarity(UNCOMMON)));` | item tags: #mc:trim_materials |
| `gloamingforest:phantom_chestplate` | Phantom Chestplate | `ITEMS.register("phantom_chestplate", new PhantomArmorItem(GloamingArmorMaterials.PHANTOM, ArmorItem.Type.CHESTPLATE, new Item.Properties().durability(ArmorItem.Type.CHESTPLATE.getDurability(30)).rarity(UNCOMMON)));` | item tags: #mc:chest_armor, #mc:enchantable/chest_armor, #kept_on_death |
| `gloamingforest:phantom_helmet` | Phantom Helmet | `ITEMS.register("phantom_helmet", new PhantomArmorItem(GloamingArmorMaterials.PHANTOM, ArmorItem.Type.HELMET, new Item.Properties().durability(ArmorItem.Type.HELMET.getDurability(30)).rarity(UNCOMMON)));` | item tags: #mc:enchantable/head_armor, #mc:head_armor, #kept_on_death |
| `gloamingforest:raw_ironwood` | Raw Ironwood | `ITEMS.register("raw_ironwood", new Item(new Item.Properties()));` | item tags: #alexscaves:ferromagnetic_items, #c:raw_materials, #c:raw_materials/ironwood |
| `gloamingforest:steeleaf_axe` | Steeleaf Axe | `ITEMS.register("steeleaf_axe", new AxeItem(GloamingToolMaterials.STEELEAF, new Item.Properties().attributes(AxeItem.createAttributes(GloamingToolMaterials.STEELEAF, 6.0F, -3.0F))));` | item tags: #alexscaves:ferromagnetic_items, #mc:axes |
| `gloamingforest:steeleaf_boots` | Steeleaf Boots | `ITEMS.register("steeleaf_boots", new ArmorItem(GloamingArmorMaterials.STEELEAF, ArmorItem.Type.BOOTS, new Item.Properties().durability(ArmorItem.Type.BOOTS.getDurability(10))));` | item tags: #alexscaves:ferromagnetic_items, #mc:enchantable/foot_armor, #mc:foot_armor |
| `gloamingforest:steeleaf_chestplate` | Steeleaf Chestplate | `ITEMS.register("steeleaf_chestplate", new ArmorItem(GloamingArmorMaterials.STEELEAF, ArmorItem.Type.CHESTPLATE, new Item.Properties().durability(ArmorItem.Type.CHESTPLATE.getDurability(10))));` | item tags: #alexscaves:ferromagnetic_items, #mc:chest_armor, #mc:enchantable/chest_armor |
| `gloamingforest:steeleaf_helmet` | Steeleaf Helmet | `ITEMS.register("steeleaf_helmet", new ArmorItem(GloamingArmorMaterials.STEELEAF, ArmorItem.Type.HELMET, new Item.Properties().durability(ArmorItem.Type.HELMET.getDurability(10))));` | item tags: #alexscaves:ferromagnetic_items, #mc:enchantable/head_armor, #mc:head_armor |
| `gloamingforest:steeleaf_hoe` | Steeleaf Hoe | `ITEMS.register("steeleaf_hoe", new HoeItem(GloamingToolMaterials.STEELEAF, new Item.Properties().attributes(HoeItem.createAttributes(GloamingToolMaterials.STEELEAF, -3.0F, -0.5F))));` | item tags: #alexscaves:ferromagnetic_items, #mc:hoes |
| `gloamingforest:steeleaf_ingot` | Steeleaf | `ITEMS.register("steeleaf_ingot", new Item(new Item.Properties()));` | item tags: #alexscaves:ferromagnetic_items, #c:ingots, #c:ingots/steeleaf, #mc:beacon_payment_items, #mc:trim_materials, #repairs_steeleaf_tools |
| `gloamingforest:steeleaf_leggings` | Steeleaf Leggings | `ITEMS.register("steeleaf_leggings", new ArmorItem(GloamingArmorMaterials.STEELEAF, ArmorItem.Type.LEGGINGS, new Item.Properties().durability(ArmorItem.Type.LEGGINGS.getDurability(10))));` | item tags: #alexscaves:ferromagnetic_items, #mc:enchantable/leg_armor, #mc:leg_armor |
| `gloamingforest:steeleaf_pickaxe` | Steeleaf Pickaxe | `ITEMS.register("steeleaf_pickaxe", new PickaxeItem(GloamingToolMaterials.STEELEAF, new Item.Properties().attributes(PickaxeItem.createAttributes(GloamingToolMaterials.STEELEAF, 1.0F, -2.8F))));` | item tags: #alexscaves:ferromagnetic_items, #mc:cluster_max_harvestables, #mc:pickaxes |
| `gloamingforest:steeleaf_shovel` | Steeleaf Shovel | `ITEMS.register("steeleaf_shovel", new ShovelItem(GloamingToolMaterials.STEELEAF, new Item.Properties().attributes(ShovelItem.createAttributes(GloamingToolMaterials.STEELEAF, 1.5F, -3.0F))));` | item tags: #alexscaves:ferromagnetic_items, #mc:shovels |
| `gloamingforest:steeleaf_sword` | Steeleaf Sword | `ITEMS.register("steeleaf_sword", new SwordItem(GloamingToolMaterials.STEELEAF, new Item.Properties().attributes(SwordItem.createAttributes(GloamingToolMaterials.STEELEAF, 3, -2.4F))));` | item tags: #alexscaves:ferromagnetic_items, #mc:swords |
| `gloamingforest:yeti_boots` | Yeti Boots | `ITEMS.register("yeti_boots", new YetiArmorItem(GloamingArmorMaterials.YETI, ArmorItem.Type.BOOTS, new Item.Properties().durability(ArmorItem.Type.BOOTS.getDurability(20)).rarity(UNCOMMON)));` | item tags: #mc:enchantable/foot_armor, #mc:foot_armor, #mc:freeze_immune_wearables |
| `gloamingforest:yeti_chestplate` | Yeti Jacket | `ITEMS.register("yeti_chestplate", new YetiArmorItem(GloamingArmorMaterials.YETI, ArmorItem.Type.CHESTPLATE, new Item.Properties().durability(ArmorItem.Type.CHESTPLATE.getDurability(20)).rarity(UNCOMMON)));` | item tags: #mc:chest_armor, #mc:enchantable/chest_armor, #mc:freeze_immune_wearables |
| `gloamingforest:yeti_helmet` | Yeti Horned Helm | `ITEMS.register("yeti_helmet", new YetiArmorItem(GloamingArmorMaterials.YETI, ArmorItem.Type.HELMET, new Item.Properties().durability(ArmorItem.Type.HELMET.getDurability(20)).rarity(UNCOMMON)));` | item tags: #mc:enchantable/head_armor, #mc:freeze_immune_wearables, #mc:head_armor |
| `gloamingforest:yeti_leggings` | Yeti Leggings | `ITEMS.register("yeti_leggings", new YetiArmorItem(GloamingArmorMaterials.YETI, ArmorItem.Type.LEGGINGS, new Item.Properties().durability(ArmorItem.Type.LEGGINGS.getDurability(20)).rarity(UNCOMMON)));` | item tags: #mc:enchantable/leg_armor, #mc:freeze_immune_wearables, #mc:leg_armor |

### Armor materials (8)

| Id | upstream registration (`GloamingArmorMaterials.java`) |
|---|---|
| `gloamingforest:arctic` | `ARMOR_MATERIALS.register("arctic", new ArmorMaterial(Util.make(new EnumMap<>(ArmorItem.Type.class), map -> { map.put(ArmorItem.Type.BOOTS, 2); map.put(ArmorItem.Type.LEGGINGS, 5); map.put(ArmorItem.Type.CHESTPLATE, 7); map.put(ArmorItem.Type.HELMET, 2); map.put(ArmorItem.Type.BODY, 7); }), 8, SoundEvents.ARMOR_EQUIP_GENERIC, () -> Ingredient.of(GloamingItems.ARCTIC_FUR.get()), List.of(new ArmorMaterial.Layer(GloamingForestMod.prefix("arctic"), "_dyed", true), new ArmorMaterial.Layer(GloamingForestMod.prefix("arctic"), "_overlay", false)), 2.0F, 0.0F)); ;; }), 8, SoundEvents.ARMOR_EQUIP_GENERIC, () -> Ingredient.of(GloamingItems.ARCTIC_FUR.get()), List.of(new ArmorMaterial.Layer(GloamingForestMod.prefix("arctic"), "_dyed", true), new ArmorMaterial.Layer(GloamingForestMod.prefix("arctic"), "_overlay", false)), 2.0F, 0.0F));` |
| `gloamingforest:fiery` | `ARMOR_MATERIALS.register("fiery", new ArmorMaterial(Util.make(new EnumMap<>(ArmorItem.Type.class), map -> { map.put(ArmorItem.Type.BOOTS, 4); map.put(ArmorItem.Type.LEGGINGS, 7); map.put(ArmorItem.Type.CHESTPLATE, 9); map.put(ArmorItem.Type.HELMET, 4); map.put(ArmorItem.Type.BODY, 13); }), 10, SoundEvents.ARMOR_EQUIP_GENERIC, () -> Ingredient.of(GloamingItems.FIERY_INGOT.get()), List.of(new ArmorMaterial.Layer(GloamingForestMod.prefix("fiery"))), 1.5F, 0.0F)); ;; }), 10, SoundEvents.ARMOR_EQUIP_GENERIC, () -> Ingredient.of(GloamingItems.FIERY_INGOT.get()), List.of(new ArmorMaterial.Layer(GloamingForestMod.prefix("fiery"))), 1.5F, 0.0F));` |
| `gloamingforest:ironwood` | `ARMOR_MATERIALS.register("ironwood", new ArmorMaterial(Util.make(new EnumMap<>(ArmorItem.Type.class), map -> { map.put(ArmorItem.Type.BOOTS, 2); map.put(ArmorItem.Type.LEGGINGS, 5); map.put(ArmorItem.Type.CHESTPLATE, 7); map.put(ArmorItem.Type.HELMET, 2); map.put(ArmorItem.Type.BODY, 5); }), 15, SoundEvents.ARMOR_EQUIP_GENERIC, () -> Ingredient.of(GloamingItems.IRONWOOD_INGOT.get()), List.of(new ArmorMaterial.Layer(GloamingForestMod.prefix("ironwood"))), 0.0F, 0.0F)); ;; }), 15, SoundEvents.ARMOR_EQUIP_GENERIC, () -> Ingredient.of(GloamingItems.IRONWOOD_INGOT.get()), List.of(new ArmorMaterial.Layer(GloamingForestMod.prefix("ironwood"))), 0.0F, 0.0F));` |
| `gloamingforest:knightmetal` | `ARMOR_MATERIALS.register("knightmetal", new ArmorMaterial(Util.make(new EnumMap<>(ArmorItem.Type.class), map -> { map.put(ArmorItem.Type.BOOTS, 3); map.put(ArmorItem.Type.LEGGINGS, 6); map.put(ArmorItem.Type.CHESTPLATE, 8); map.put(ArmorItem.Type.HELMET, 3); map.put(ArmorItem.Type.BODY, 11); }), 8, GloamingSounds.KNIGHTMETAL_EQUIP, () -> Ingredient.of(GloamingItems.KNIGHTMETAL_INGOT.get()), List.of(new ArmorMaterial.Layer(GloamingForestMod.prefix("knightmetal"))), 1.0F, 0.0F)); ;; }), 8, GloamingSounds.KNIGHTMETAL_EQUIP, () -> Ingredient.of(GloamingItems.KNIGHTMETAL_INGOT.get()), List.of(new ArmorMaterial.Layer(GloamingForestMod.prefix("knightmetal"))), 1.0F, 0.0F));` |
| `gloamingforest:naga_scale` | `ARMOR_MATERIALS.register("naga_scale", new ArmorMaterial(Util.make(new EnumMap<>(ArmorItem.Type.class), map -> { map.put(ArmorItem.Type.BOOTS, 3); map.put(ArmorItem.Type.LEGGINGS, 6); map.put(ArmorItem.Type.CHESTPLATE, 7); map.put(ArmorItem.Type.HELMET, 2); map.put(ArmorItem.Type.BODY, 8); }), 15, SoundEvents.ARMOR_EQUIP_GENERIC, () -> Ingredient.of(GloamingItems.NAGA_SCALE.get()), List.of(new ArmorMaterial.Layer(GloamingForestMod.prefix("naga_scale"))), 0.5F, 0.0F)); ;; }), 15, SoundEvents.ARMOR_EQUIP_GENERIC, () -> Ingredient.of(GloamingItems.NAGA_SCALE.get()), List.of(new ArmorMaterial.Layer(GloamingForestMod.prefix("naga_scale"))), 0.5F, 0.0F));` |
| `gloamingforest:phantom` | `ARMOR_MATERIALS.register("phantom", new ArmorMaterial(Util.make(new EnumMap<>(ArmorItem.Type.class), map -> { map.put(ArmorItem.Type.BOOTS, 3); map.put(ArmorItem.Type.LEGGINGS, 6); map.put(ArmorItem.Type.CHESTPLATE, 8); map.put(ArmorItem.Type.HELMET, 3); map.put(ArmorItem.Type.BODY, 10); }), 8, SoundEvents.ARMOR_EQUIP_GENERIC, () -> Ingredient.of(GloamingItems.KNIGHTMETAL_INGOT.get()), List.of(new ArmorMaterial.Layer(GloamingForestMod.prefix("phantom"))), 2.5F, 0.0F)); ;; }), 8, SoundEvents.ARMOR_EQUIP_GENERIC, () -> Ingredient.of(GloamingItems.KNIGHTMETAL_INGOT.get()), List.of(new ArmorMaterial.Layer(GloamingForestMod.prefix("phantom"))), 2.5F, 0.0F));` |
| `gloamingforest:steeleaf` | `ARMOR_MATERIALS.register("steeleaf", new ArmorMaterial(Util.make(new EnumMap<>(ArmorItem.Type.class), map -> { map.put(ArmorItem.Type.BOOTS, 3); map.put(ArmorItem.Type.LEGGINGS, 6); map.put(ArmorItem.Type.CHESTPLATE, 8); map.put(ArmorItem.Type.HELMET, 3); map.put(ArmorItem.Type.BODY, 11); }), 9, SoundEvents.ARMOR_EQUIP_GENERIC, () -> Ingredient.of(GloamingItems.STEELEAF_INGOT.get()), List.of(new ArmorMaterial.Layer(GloamingForestMod.prefix("steeleaf"))), 0.0F, 0.0F)); ;; }), 9, SoundEvents.ARMOR_EQUIP_GENERIC, () -> Ingredient.of(GloamingItems.STEELEAF_INGOT.get()), List.of(new ArmorMaterial.Layer(GloamingForestMod.prefix("steeleaf"))), 0.0F, 0.0F));` |
| `gloamingforest:yeti` | `ARMOR_MATERIALS.register("yeti", new ArmorMaterial(Util.make(new EnumMap<>(ArmorItem.Type.class), map -> { map.put(ArmorItem.Type.BOOTS, 3); map.put(ArmorItem.Type.LEGGINGS, 6); map.put(ArmorItem.Type.CHESTPLATE, 7); map.put(ArmorItem.Type.HELMET, 4); map.put(ArmorItem.Type.BODY, 11); }), 15, SoundEvents.ARMOR_EQUIP_GENERIC, () -> Ingredient.of(GloamingItems.ALPHA_YETI_FUR.get()), List.of(new ArmorMaterial.Layer(GloamingForestMod.prefix("yeti"))), 3.0F, 0.0F)); ;; }), 15, SoundEvents.ARMOR_EQUIP_GENERIC, () -> Ingredient.of(GloamingItems.ALPHA_YETI_FUR.get()), List.of(new ArmorMaterial.Layer(GloamingForestMod.prefix("yeti"))), 3.0F, 0.0F));` |

### Recipe serializers / types (1)

| Id | upstream registration (`GloamingRecipes.java`) |
|---|---|
| `gloamingforest:no_template_smithing` | `RECIPE_SERIALIZERS.register("no_template_smithing", NoTemplateSmithingRecipe.Serializer::new);` |

### Global loot modifier codecs (1)

| Id | upstream registration (`GloamingLootModifiers.java`) |
|---|---|
| `gloamingforest:fiery_pick_smelting` | `LOOT_MODIFIERS.register("fiery_pick_smelting", () -> FieryToolSmeltingModifier.CODEC);` |

### Damage types (2)

- `gloamingforest:fiery`: `{"effects":"burning","exhaustion":0.1,"message_id":"gloamingforest.fiery","scaling":"when_caused_by_living_non_player"}` death.attack.gloamingforest.fiery = "%1$s walked onto a Fiery block"; death.attack.gloamingforest.fiery.player = "%1$s walked onto a Fiery block while escaping %2$s";
- `gloamingforest:knightmetal`: `{"exhaustion":0.1,"message_id":"gloamingforest.knightmetal","scaling":"when_caused_by_living_non_player"}` death.attack.gloamingforest.knightmetal = "%1$s was skewered by a Knightmetal block"; death.attack.gloamingforest.knightmetal.player = "%1$s was skewered by a Knightmetal block while escaping %2$s";

### Trim materials (6)

- `gloamingforest:carminite`: `{"asset_name":"carminite","description":{"color":"#9A0000","translate":"trim_material.gloamingforest.carminite"},"ingredient":"gloamingforest:carminite","item_model_index":0.4}`
- `gloamingforest:fiery`: `{"asset_name":"fiery","description":{"color":"#FFB53C","translate":"trim_material.gloamingforest.fiery"},"ingredient":"gloamingforest:fiery_ingot","item_model_index":0.3}`
- `gloamingforest:ironwood`: `{"asset_name":"ironwood","description":{"color":"#6B6161","translate":"trim_material.gloamingforest.ironwood"},"ingredient":"gloamingforest:ironwood_ingot","item_model_index":0.2}`
- `gloamingforest:knightmetal`: `{"asset_name":"knightmetal","description":{"color":"#808C72","translate":"trim_material.gloamingforest.knightmetal"},"ingredient":"gloamingforest:knightmetal_ingot","item_model_index":0.1}`
- `gloamingforest:naga_scale`: `{"asset_name":"naga_scale","description":{"color":"#245712","translate":"trim_material.gloamingforest.naga_scale"},"ingredient":"gloamingforest:naga_scale","item_model_index":0.7}`
- `gloamingforest:steeleaf`: `{"asset_name":"steeleaf","description":{"color":"#497733","translate":"trim_material.gloamingforest.steeleaf"},"ingredient":"gloamingforest:steeleaf_ingot","item_model_index":0.7}`

Loot modifier JSON:

- `gloamingforest:fiery_pick_smelting`: `{"type":"gloamingforest:fiery_pick_smelting","conditions":[{"condition":"minecraft:match_tool","predicate":{"items":"gloamingforest:fiery_pickaxe"}}]}`

### Sound events (1)

`sounds.json` content (vanilla `event` references can be played with the matching Bedrock vanilla sound; upstream file names are listed only as names: the audio itself is All Rights Reserved and is re-synthesised by F3/the owner).

| Event | Subtitle | sounds.json entries |
|---|---|---|
| `item.gloamingforest.knightmetal_armor.equip` | subtitles.gloamingforest.item.knightmetal_armor.equip = Knightmetal Armor clanks | event item.armor.equip_netherite |

### Tags (14)

| Tag | Members (upstream generated JSON) |
|---|---|
| `banner_pattern/#gloamingforest:pattern_item/alpha_yeti` | alpha_yeti |
| `block/#gloamingforest:incorrect_for_fiery_tool` | #mc:incorrect_for_netherite_tool |
| `block/#gloamingforest:incorrect_for_ironwood_tool` | #mc:incorrect_for_iron_tool |
| `block/#gloamingforest:incorrect_for_knightmetal_tool` | #mc:incorrect_for_diamond_tool |
| `block/#gloamingforest:incorrect_for_steeleaf_tool` | #mc:incorrect_for_diamond_tool |
| `item/#gloamingforest:arctic_fur` | arctic_fur |
| `item/#gloamingforest:fiery_vial` | fiery_blood, fiery_tears |
| `item/#gloamingforest:repairs_fiery_tools` | #c:ingots/fiery |
| `item/#gloamingforest:repairs_giant_tools` | giant_cobblestone |
| `item/#gloamingforest:repairs_ice_tools` | mc:ice, mc:packed_ice, mc:blue_ice |
| `item/#gloamingforest:repairs_ironwood_tools` | #c:ingots/ironwood |
| `item/#gloamingforest:repairs_knightmetal_tools` | #c:ingots/knightmetal |
| `item/#gloamingforest:repairs_steeleaf_tools` | #c:ingots/steeleaf |
| `worldgen/biome/#gloamingforest:valid_yeti_cave_biomes` | snowy_forest |

### Recipes (61)

Recipe-unlock advancements (generated by F1 into the recipe JSON, R21) are listed with their recipe; criteria are the `has_*` triggers besides `has_the_recipe`.

| Recipe | Type | Ingredients | Result | Unlock advancement (criteria) |
|---|---|---|---|---|
| `gloamingforest:compressed_blocks/reversed/arctic_fur_block` | mc:crafting_shapeless | arctic_fur_block | arctic_fur x9 | `gloamingforest:recipes/misc/compressed_blocks/reversed/arctic_fur_block` (has_item: has arctic_fur_block) |
| `gloamingforest:compressed_blocks/reversed/fiery_block` | mc:crafting_shapeless | fiery_block | fiery_ingot x9 | `gloamingforest:recipes/misc/compressed_blocks/reversed/fiery_block` (has_item: has fiery_block) |
| `gloamingforest:compressed_blocks/reversed/ironwood_block` | mc:crafting_shapeless | ironwood_block | ironwood_ingot x9 | `gloamingforest:recipes/misc/compressed_blocks/reversed/ironwood_block` (has_item: has ironwood_block) |
| `gloamingforest:compressed_blocks/reversed/knightmetal_block` | mc:crafting_shapeless | knightmetal_block | knightmetal_ingot x9 | `gloamingforest:recipes/misc/compressed_blocks/reversed/knightmetal_block` (has_item: has knightmetal_block) |
| `gloamingforest:compressed_blocks/reversed/steeleaf_block` | mc:crafting_shapeless | steeleaf_block | steeleaf_ingot x9 | `gloamingforest:recipes/misc/compressed_blocks/reversed/steeleaf_block` (has_item: has steeleaf_block) |
| `gloamingforest:emperors_cloth_smithing` | no_template_smithing | base:#emperors_cloth_applicable + addition:emperors_cloth data {"gloamingforest:emperors_cloth":{}} | - | `gloamingforest:recipes/misc/emperors_cloth_smithing` (has_cloth: has emperors_cloth) |
| `gloamingforest:equipment/arctic_boots` | mc:crafting_shaped | '# #' / '# #' ; #=#arctic_fur | arctic_boots | `gloamingforest:recipes/combat/equipment/arctic_boots` (has_item: has #arctic_fur) |
| `gloamingforest:equipment/arctic_chestplate` | mc:crafting_shaped | '# #' / '###' / '###' ; #=#arctic_fur | arctic_chestplate | `gloamingforest:recipes/combat/equipment/arctic_chestplate` (has_item: has #arctic_fur) |
| `gloamingforest:equipment/arctic_helmet` | mc:crafting_shaped | '###' / '# #' ; #=#arctic_fur | arctic_helmet | `gloamingforest:recipes/combat/equipment/arctic_helmet` (has_item: has #arctic_fur) |
| `gloamingforest:equipment/arctic_leggings` | mc:crafting_shaped | '###' / '# #' / '# #' ; #=#arctic_fur | arctic_leggings | `gloamingforest:recipes/combat/equipment/arctic_leggings` (has_item: has #arctic_fur) |
| `gloamingforest:equipment/fiery_boots` | mc:crafting_shaped | '# #' / '# #' ; #=#c:ingots/fiery | fiery_boots | `gloamingforest:recipes/combat/equipment/fiery_boots` (has_item: has #c:ingots/fiery) |
| `gloamingforest:equipment/fiery_chestplate` | mc:crafting_shaped | '# #' / '###' / '###' ; #=#c:ingots/fiery | fiery_chestplate | `gloamingforest:recipes/combat/equipment/fiery_chestplate` (has_item: has #c:ingots/fiery) |
| `gloamingforest:equipment/fiery_helmet` | mc:crafting_shaped | '###' / '# #' ; #=#c:ingots/fiery | fiery_helmet | `gloamingforest:recipes/combat/equipment/fiery_helmet` (has_item: has #c:ingots/fiery) |
| `gloamingforest:equipment/fiery_ingot_crafting` | mc:crafting_shapeless | #fiery_vial + #c:ingots/iron | fiery_ingot | `gloamingforest:recipes/misc/equipment/fiery_ingot_crafting` (has_item: has #fiery_vial) |
| `gloamingforest:equipment/fiery_iron_boots` | mc:crafting_shapeless | mc:iron_boots + #fiery_vial + #fiery_vial + #fiery_vial + #fiery_vial | fiery_boots | `gloamingforest:recipes/combat/equipment/fiery_iron_boots` (has_item: has #fiery_vial) |
| `gloamingforest:equipment/fiery_iron_chestplate` | mc:crafting_shapeless | mc:iron_chestplate + #fiery_vial + #fiery_vial + #fiery_vial + #fiery_vial + #fiery_vial + #fiery_vial + #fiery_vial + #fiery_vial | fiery_chestplate | `gloamingforest:recipes/combat/equipment/fiery_iron_chestplate` (has_item: has #fiery_vial) |
| `gloamingforest:equipment/fiery_iron_helmet` | mc:crafting_shapeless | mc:iron_helmet + #fiery_vial + #fiery_vial + #fiery_vial + #fiery_vial + #fiery_vial | fiery_helmet | `gloamingforest:recipes/combat/equipment/fiery_iron_helmet` (has_item: has #fiery_vial) |
| `gloamingforest:equipment/fiery_iron_leggings` | mc:crafting_shapeless | mc:iron_leggings + #fiery_vial + #fiery_vial + #fiery_vial + #fiery_vial + #fiery_vial + #fiery_vial + #fiery_vial | fiery_leggings | `gloamingforest:recipes/combat/equipment/fiery_iron_leggings` (has_item: has #fiery_vial) |
| `gloamingforest:equipment/fiery_iron_pickaxe` | mc:crafting_shapeless | mc:iron_pickaxe + #fiery_vial + #fiery_vial + #fiery_vial + #c:rods/blaze + #c:rods/blaze | fiery_pickaxe | `gloamingforest:recipes/tools/equipment/fiery_iron_pickaxe` (has_item: has #fiery_vial) |
| `gloamingforest:equipment/fiery_iron_sword` | mc:crafting_shapeless | mc:iron_sword + #fiery_vial + #fiery_vial + #c:rods/blaze | fiery_sword | `gloamingforest:recipes/combat/equipment/fiery_iron_sword` (has_item: has #fiery_vial) |
| `gloamingforest:equipment/fiery_leggings` | mc:crafting_shaped | '###' / '# #' / '# #' ; #=#c:ingots/fiery | fiery_leggings | `gloamingforest:recipes/combat/equipment/fiery_leggings` (has_item: has #c:ingots/fiery) |
| `gloamingforest:equipment/fiery_pickaxe` | mc:crafting_shaped | '###' / ' X ' / ' X ' ; #=#c:ingots/fiery, X=#c:rods/blaze | fiery_pickaxe | `gloamingforest:recipes/tools/equipment/fiery_pickaxe` (has_item: has #c:ingots/fiery) |
| `gloamingforest:equipment/fiery_sword` | mc:crafting_shaped | '#' / '#' / 'X' ; #=#c:ingots/fiery, X=#c:rods/blaze | fiery_sword | `gloamingforest:recipes/combat/equipment/fiery_sword` (has_item: has #c:ingots/fiery) |
| `gloamingforest:equipment/ironwood_axe` | mc:crafting_shaped | '##' / '#X' / ' X' ; #=#c:ingots/ironwood, X=#c:rods/wooden | ironwood_axe {"minecraft:enchantments":{"levels":{"minecraft:fortune":1}}} | `gloamingforest:recipes/tools/equipment/ironwood_axe` (has_item: has #c:ingots/ironwood) |
| `gloamingforest:equipment/ironwood_boots` | mc:crafting_shaped | '# #' / '# #' ; #=#c:ingots/ironwood | ironwood_boots {"minecraft:enchantments":{"levels":{"minecraft:feather_falling":1}}} | `gloamingforest:recipes/combat/equipment/ironwood_boots` (has_item: has #c:ingots/ironwood) |
| `gloamingforest:equipment/ironwood_chestplate` | mc:crafting_shaped | '# #' / '###' / '###' ; #=#c:ingots/ironwood | ironwood_chestplate {"minecraft:enchantments":{"levels":{"minecraft:protection":1}}} | `gloamingforest:recipes/combat/equipment/ironwood_chestplate` (has_item: has #c:ingots/ironwood) |
| `gloamingforest:equipment/ironwood_helmet` | mc:crafting_shaped | '###' / '# #' ; #=#c:ingots/ironwood | ironwood_helmet {"minecraft:enchantments":{"levels":{"minecraft:aqua_affinity":1}}} | `gloamingforest:recipes/combat/equipment/ironwood_helmet` (has_item: has #c:ingots/ironwood) |
| `gloamingforest:equipment/ironwood_hoe` | mc:crafting_shaped | '##' / ' X' / ' X' ; #=#c:ingots/ironwood, X=#c:rods/wooden | ironwood_hoe {"minecraft:enchantments":{"levels":{"minecraft:efficiency":1}}} | `gloamingforest:recipes/tools/equipment/ironwood_hoe` (has_item: has #c:ingots/ironwood) |
| `gloamingforest:equipment/ironwood_leggings` | mc:crafting_shaped | '###' / '# #' / '# #' ; #=#c:ingots/ironwood | ironwood_leggings {"minecraft:enchantments":{"levels":{"minecraft:protection":1}}} | `gloamingforest:recipes/combat/equipment/ironwood_leggings` (has_item: has #c:ingots/ironwood) |
| `gloamingforest:equipment/ironwood_pickaxe` | mc:crafting_shaped | '###' / ' X ' / ' X ' ; #=#c:ingots/ironwood, X=#c:rods/wooden | ironwood_pickaxe {"minecraft:enchantments":{"levels":{"minecraft:efficiency":1}}} | `gloamingforest:recipes/tools/equipment/ironwood_pickaxe` (has_item: has #c:ingots/ironwood) |
| `gloamingforest:equipment/ironwood_shovel` | mc:crafting_shaped | '#' / 'X' / 'X' ; #=#c:ingots/ironwood, X=#c:rods/wooden | ironwood_shovel {"minecraft:enchantments":{"levels":{"minecraft:unbreaking":1}}} | `gloamingforest:recipes/tools/equipment/ironwood_shovel` (has_item: has #c:ingots/ironwood) |
| `gloamingforest:equipment/ironwood_sword` | mc:crafting_shaped | '#' / '#' / 'X' ; #=#c:ingots/ironwood, X=#c:rods/wooden | ironwood_sword {"minecraft:enchantments":{"levels":{"minecraft:knockback":1}}} | `gloamingforest:recipes/combat/equipment/ironwood_sword` (has_item: has #c:ingots/ironwood) |
| `gloamingforest:equipment/knightmetal_axe` | mc:crafting_shaped | '##' / '#X' / ' X' ; #=#c:ingots/knightmetal, X=#c:rods/wooden | knightmetal_axe | `gloamingforest:recipes/tools/equipment/knightmetal_axe` (has_item: has #c:ingots/knightmetal) |
| `gloamingforest:equipment/knightmetal_boots` | mc:crafting_shaped | '# #' / '# #' ; #=#c:ingots/knightmetal | knightmetal_boots | `gloamingforest:recipes/combat/equipment/knightmetal_boots` (has_item: has #c:ingots/knightmetal) |
| `gloamingforest:equipment/knightmetal_chestplate` | mc:crafting_shaped | '# #' / '###' / '###' ; #=#c:ingots/knightmetal | knightmetal_chestplate | `gloamingforest:recipes/combat/equipment/knightmetal_chestplate` (has_item: has #c:ingots/knightmetal) |
| `gloamingforest:equipment/knightmetal_helmet` | mc:crafting_shaped | '###' / '# #' ; #=#c:ingots/knightmetal | knightmetal_helmet | `gloamingforest:recipes/combat/equipment/knightmetal_helmet` (has_item: has #c:ingots/knightmetal) |
| `gloamingforest:equipment/knightmetal_leggings` | mc:crafting_shaped | '###' / '# #' / '# #' ; #=#c:ingots/knightmetal | knightmetal_leggings | `gloamingforest:recipes/combat/equipment/knightmetal_leggings` (has_item: has #c:ingots/knightmetal) |
| `gloamingforest:equipment/knightmetal_pickaxe` | mc:crafting_shaped | '###' / ' X ' / ' X ' ; #=#c:ingots/knightmetal, X=#c:rods/wooden | knightmetal_pickaxe | `gloamingforest:recipes/tools/equipment/knightmetal_pickaxe` (has_item: has #c:ingots/knightmetal) |
| `gloamingforest:equipment/knightmetal_ring` | mc:crafting_shaped | ' - ' / '- -' / ' - ' ; -=#c:ingots/knightmetal | knightmetal_ring | `gloamingforest:recipes/misc/equipment/knightmetal_ring` (has_item: has #c:ingots/knightmetal) |
| `gloamingforest:equipment/knightmetal_sword` | mc:crafting_shaped | '#' / '#' / 'X' ; #=#c:ingots/knightmetal, X=#c:rods/wooden | knightmetal_sword | `gloamingforest:recipes/combat/equipment/knightmetal_sword` (has_item: has #c:ingots/knightmetal) |
| `gloamingforest:equipment/naga_chestplate` | mc:crafting_shaped | '# #' / '###' / '###' ; #=naga_scale | naga_chestplate {"minecraft:enchantments":{"levels":{"minecraft:fire_protection":3}}} | `gloamingforest:recipes/combat/equipment/naga_chestplate` (has_item: has naga_scale) |
| `gloamingforest:equipment/naga_leggings` | mc:crafting_shaped | '###' / '# #' / '# #' ; #=naga_scale | naga_leggings {"minecraft:enchantments":{"levels":{"minecraft:protection":3}}} | `gloamingforest:recipes/combat/equipment/naga_leggings` (has_item: has naga_scale) |
| `gloamingforest:equipment/steeleaf_axe` | mc:crafting_shaped | '##' / '#X' / ' X' ; #=#c:ingots/steeleaf, X=#c:rods/wooden | steeleaf_axe {"minecraft:enchantments":{"levels":{"minecraft:efficiency":2}}} | `gloamingforest:recipes/tools/equipment/steeleaf_axe` (has_item: has #c:ingots/steeleaf) |
| `gloamingforest:equipment/steeleaf_boots` | mc:crafting_shaped | '# #' / '# #' ; #=#c:ingots/steeleaf | steeleaf_boots {"minecraft:enchantments":{"levels":{"minecraft:feather_falling":2}}} | `gloamingforest:recipes/combat/equipment/steeleaf_boots` (has_item: has #c:ingots/steeleaf) |
| `gloamingforest:equipment/steeleaf_chestplate` | mc:crafting_shaped | '# #' / '###' / '###' ; #=#c:ingots/steeleaf | steeleaf_chestplate {"minecraft:enchantments":{"levels":{"minecraft:blast_protection":2}}} | `gloamingforest:recipes/combat/equipment/steeleaf_chestplate` (has_item: has #c:ingots/steeleaf) |
| `gloamingforest:equipment/steeleaf_helmet` | mc:crafting_shaped | '###' / '# #' ; #=#c:ingots/steeleaf | steeleaf_helmet {"minecraft:enchantments":{"levels":{"minecraft:projectile_protection":2}}} | `gloamingforest:recipes/combat/equipment/steeleaf_helmet` (has_item: has #c:ingots/steeleaf) |
| `gloamingforest:equipment/steeleaf_hoe` | mc:crafting_shaped | '##' / ' X' / ' X' ; #=#c:ingots/steeleaf, X=#c:rods/wooden | steeleaf_hoe {"minecraft:enchantments":{"levels":{"minecraft:fortune":2}}} | `gloamingforest:recipes/tools/equipment/steeleaf_hoe` (has_item: has #c:ingots/steeleaf) |
| `gloamingforest:equipment/steeleaf_leggings` | mc:crafting_shaped | '###' / '# #' / '# #' ; #=#c:ingots/steeleaf | steeleaf_leggings {"minecraft:enchantments":{"levels":{"minecraft:fire_protection":2}}} | `gloamingforest:recipes/combat/equipment/steeleaf_leggings` (has_item: has #c:ingots/steeleaf) |
| `gloamingforest:equipment/steeleaf_pickaxe` | mc:crafting_shaped | '###' / ' X ' / ' X ' ; #=#c:ingots/steeleaf, X=#c:rods/wooden | steeleaf_pickaxe {"minecraft:enchantments":{"levels":{"minecraft:fortune":2}}} | `gloamingforest:recipes/tools/equipment/steeleaf_pickaxe` (has_item: has #c:ingots/steeleaf) |
| `gloamingforest:equipment/steeleaf_shovel` | mc:crafting_shaped | '#' / 'X' / 'X' ; #=#c:ingots/steeleaf, X=#c:rods/wooden | steeleaf_shovel {"minecraft:enchantments":{"levels":{"minecraft:efficiency":2}}} | `gloamingforest:recipes/tools/equipment/steeleaf_shovel` (has_item: has #c:ingots/steeleaf) |
| `gloamingforest:equipment/steeleaf_sword` | mc:crafting_shaped | '#' / '#' / 'X' ; #=#c:ingots/steeleaf, X=#c:rods/wooden | steeleaf_sword {"minecraft:enchantments":{"levels":{"minecraft:looting":2}}} | `gloamingforest:recipes/combat/equipment/steeleaf_sword` (has_item: has #c:ingots/steeleaf) |
| `gloamingforest:equipment/yeti_boots` | mc:crafting_shaped | '# #' / '# #' ; #=alpha_yeti_fur | yeti_boots {"minecraft:enchantments":{"levels":{"minecraft:feather_falling":4,"minecraft:protection":2}}} | `gloamingforest:recipes/combat/equipment/yeti_boots` (has_item: has alpha_yeti_fur) |
| `gloamingforest:equipment/yeti_chestplate` | mc:crafting_shaped | '# #' / '###' / '###' ; #=alpha_yeti_fur | yeti_chestplate {"minecraft:enchantments":{"levels":{"minecraft:protection":2}}} | `gloamingforest:recipes/combat/equipment/yeti_chestplate` (has_item: has alpha_yeti_fur) |
| `gloamingforest:equipment/yeti_helmet` | mc:crafting_shaped | '###' / '# #' ; #=alpha_yeti_fur | yeti_helmet {"minecraft:enchantments":{"levels":{"minecraft:protection":2}}} | `gloamingforest:recipes/combat/equipment/yeti_helmet` (has_item: has alpha_yeti_fur) |
| `gloamingforest:equipment/yeti_leggings` | mc:crafting_shaped | '###' / '# #' / '# #' ; #=alpha_yeti_fur | yeti_leggings {"minecraft:enchantments":{"levels":{"minecraft:protection":2}}} | `gloamingforest:recipes/combat/equipment/yeti_leggings` (has_item: has alpha_yeti_fur) |
| `gloamingforest:material/armor_shard_cluster` | mc:crafting_shapeless | armor_shard + armor_shard + armor_shard + armor_shard + armor_shard + armor_shard + armor_shard + armor_shard + armor_shard | armor_shard_cluster | `gloamingforest:recipes/misc/material/armor_shard_cluster` (has_item: has armor_shard) |
| `gloamingforest:material/blasted_ironwood_ingot` | mc:blasting | raw_ironwood (xp 1.0, 100 t) | ironwood_ingot | `gloamingforest:recipes/misc/material/blasted_ironwood_ingot` (has_item: has raw_ironwood) |
| `gloamingforest:material/blasted_knightmetal_ingot` | mc:blasting | armor_shard_cluster (xp 1.0, 100 t) | knightmetal_ingot | `gloamingforest:recipes/misc/material/blasted_knightmetal_ingot` (has_item: has armor_shard_cluster) |
| `gloamingforest:material/raw_ironwood` | mc:crafting_shapeless | liveroot + mc:raw_iron + #c:nuggets/gold | raw_ironwood x2 | `gloamingforest:recipes/misc/material/raw_ironwood` (has_item: has liveroot) |
| `gloamingforest:material/smelted_ironwood_ingot` | mc:smelting | raw_ironwood (xp 1.0, 200 t) | ironwood_ingot | `gloamingforest:recipes/misc/material/smelted_ironwood_ingot` (has_item: has raw_ironwood) |
| `gloamingforest:material/smelted_knightmetal_ingot` | mc:smelting | armor_shard_cluster (xp 1.0, 200 t) | knightmetal_ingot | `gloamingforest:recipes/misc/material/smelted_knightmetal_ingot` (has_item: has armor_shard_cluster) |

## Appendix B: Strings (76 en_us keys owned)

Keys already shown above (block/item/entity names, subtitles) are not repeated. The 29 Bedrock locales are generated from upstream's lang files (R9); owners use these keys with the English text as fallback.

| Key | English |
|---|---|
| `item.gloamingforest.arctic_armor.desc` | Dyeable |
| `item.gloamingforest.fiery_armor.desc` | Burns attackers |
| `item.gloamingforest.fiery_pickaxe.desc` | Auto-smelting |
| `item.gloamingforest.fiery_sword.desc` | Burns targets |
| `item.gloamingforest.knightmetal_axe.desc` | Extra damage to unarmored targets |
| `item.gloamingforest.knightmetal_pickaxe.desc` | Extra damage to armored targets |
| `item.gloamingforest.knightmetal_sword.desc` | Extra damage to armored targets |
| `item.gloamingforest.minotaur_axe.desc` | Extra damage while charging |
| `item.gloamingforest.phantom_armor.desc` | Is never lost on death |
| `item.gloamingforest.yeti_armor.desc` | Chills attackers |
| `trim_material.gloamingforest.carminite` | Carminite Material |
| `trim_material.gloamingforest.fiery` | Fiery Material |
| `trim_material.gloamingforest.ironwood` | Ironwood Material |
| `trim_material.gloamingforest.knightmetal` | Knightmetal Material |
| `trim_material.gloamingforest.naga_scale` | Naga Scale Material |
| `trim_material.gloamingforest.steeleaf` | Steeleaf Material |

