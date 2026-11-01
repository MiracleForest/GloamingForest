# Provisions, enchantments, effects, music discs and soundscape (U10 `provisions`)

Behaviour spec of unit **U10** (wave W1), translated from The Twilight Forest 4.8 (`9e40a757`, MC 1.21.1) by F16. It
is the checklist the U10 worker implements and the A2 audit compares against. Sections 1-5 are the behaviour; Appendix A
lists every id U10 owns with TF's registration and data; Appendix B the strings.

## 1. Scope

- **Food**: `raw_venison`, `cooked_venison`, `raw_meef`, `cooked_meef`, `meef_stroganoff`, `hydra_chop`,
  `maze_wafer`, `berry_medley`, `moss_soup`, `shika_senbei`, 12 jerkies (`beef`, `chicken`, `cod`, `fugu`, `meef`,
  `monster`, `mutton`, `pork`, `rabbit`, `salmon`, `tropical_fish`, `venison` + `_jerky`), `stale_bread` (a weapon),
  and **Experiment 115** (`experiment_115` block + item, component `e115_variant`, stat `e115_slices_eaten`).
- **Enchantments** `twilightforest:fire_react`, `chill_aura`, `destruction`, `renewal`; enchantment effect types
  `apply_frosted`, `smash_blocks`, `recharge_scepter`; attachment `smash_blocks`; tag
  `item/#twilightforest:enchantable/block_and_chain`.
- **Effect** `twilightforest:frosted`.
- **Music**: 9 discs (`music_disc_findings`, `home`, `maker`, `motion`, `radiance`, `steps`, `superstitious`,
  `thread`, `wayfarer`) with jukebox songs (`findings`, `home`, `maker`, `motion`, `radiance`, `steps`,
  `superstitious`, `thread`, and `warfarer` — TF's key typo for wayfarer) and the dimension soundscape event
  `music.twilightforest.twilight_forest`; the tracks are composed originally (`tools/sounds/music/**`, R10).
- Trigger `consume_hydra_chop_on_low_hunger`, damage type `stale_sandwich`, 8 recipes.

Not U10: the block and chain item (U8), scepters (U9; renewal calls into them), the fiery/yeti set reactions (U7, they
reuse the frosted helper), drying recipes (U4).

Contracts: item, enchant+effects, combat, client.world, synth, progression.

## 2. TF sources

`init/TFItems.java`, `init/TFBlocks.java`, `init/TFEnchantments.java`, `init/TFEnchantmentEffects.java`,
`enchantment/{ApplyFrosted,SmashBlocks,RechargeScepter}Effect.java`, `components/entity/SmashBlocksEnchantmentAttachment.java`,
`init/TFMobEffects.java`, `potions/FrostedEffect.java`, `events/EntityEvents.java` (`reduceFrostedEffectIfOnFire`),
`init/TFJukeboxSongs.java`, `init/TFSounds.java`, `data/SoundGenerator.java` (music definition),
`init/TFConfiguredFeatures.java` (`TFMUSICTYPE`), `world/registration/biomes/BiomeHelper.java` (background music),
`client/event/ClientGameEvents.java` (`setMusicInDimension`), `block/Experiment115Block.java`,
`item/Experiment115Item.java`, `client/event/ClientRegistrationEvents.java` (E115 item predicates),
`item/HydraChopItem.java`, `advancements/ConsumeHydraChopTrigger.java`, `item/CustomDamageSwordItem.java`,
`data/ItemModelGenerator.java`.

## 3. Behaviour

### 3.1 Food

Nutrition / saturation modifier (TF FoodProperties; saturation = nutrition x modifier x 2):

| Item | Nutrition | Sat. mod | Extras |
|---|---|---|---|
| `raw_venison` / `cooked_venison` | 2 / 6 | 0.3 / 0.6 | |
| `raw_meef` / `cooked_meef` | 2 / 7 | 0.2 / 0.6 | |
| `meef_stroganoff` | 14 | 1.2 | always edible, Fire Resistance 3600 t, Strength 2400 t (prob 1.0), returns a bowl, stack 1, UNCOMMON, fire resistant |
| `hydra_chop` | 18 | 2.0 | Regeneration 100 t (1.0), fire resistant, UNCOMMON; eating it with food level <= 0 fires `twilightforest:consume_hydra_chop_on_low_hunger` |
| `maze_wafer` | 4 | 0.6 | |
| `berry_medley`, `moss_soup` | 5 | 0.6 | returns a bowl, stack 1 |
| `shika_senbei` | 4 | 1.0 | |
| `monster_jerky` | 4 | 0.275 | |
| `beef_jerky`, `pork_jerky`, `venison_jerky`, `meef_jerky` | 6 | 1.18 | |
| `chicken_jerky` | 4 | 1.075 | |
| `mutton_jerky`, `salmon_jerky` | 4 | 1.35 | |
| `rabbit_jerky` | 4 | 0.925 | |
| `cod_jerky` | 4 | 0.875 | |
| `tropical_fish_jerky`, `fugu_jerky` | 2 | 0.6 | |
| `experiment_115` (item) | 4 | 0.3 | eating awards `e115_slices_eaten` |

Jerkies and stale bread are made only on drying racks: the `twilightforest:drying` recipes `<x>_jerky` and
`stale_bread` belong to U4 (R4) and are referenced here by string; there are no furnace or campfire recipes for
them. Cooking recipes for venison and meef (furnace, smoker, campfire) are in Appendix A with their unlock
advancements.

`stale_bread`: a `CustomDamageSwordItem` (wood tier, +3 attack, -2.4 speed, attribute tooltip hidden, stack 1) whose
attacks use damage type `twilightforest:stale_sandwich` (death message from F10's strings).

### 3.2 Experiment 115 (`Experiment115Block`, `Experiment115Item`)

- Block (Appendix A properties): states `omnomnom` (bites taken 0..7, default 7 = one slice) and `regenerate` (bool).
  Shapes: 0-1 bites full box(1,0,1, 15,8,15); 2-3 three quarters; 4-5 half box(1,0,1, 8,8,15); 6-7 quarter
  box(1,0,1, 8,8,8). Survives only on a solid block below (else breaks).
- Item placement: using the item on a non-E115 block (not sneaking) places the block (one slice); if placement fails
  and it is food, eats it instead; sneaking passes.
- Using the item on the block (not sneaking): adds a slice (bites - 1, fail at 0), block place sound, consumes it,
  placed-block trigger. Redstone on a full, non-regenerating block: sets `regenerate`, consumes it, stat used.
- Sneak-use with both hands empty (sneaking with any held item skips block use; non-regenerating): takes a slice
  back (bites + 1 or removes the last), `entity.item.pickup` (0.5, 1),
  gives an item (not in creative).
- Otherwise (empty hand, any other item, redstone that does not apply, sneaking on a regenerating block) and the
  player can eat: eat 4 / 0.3, stat `e115_slices_eaten`, `entity.generic.eat` (0.5, 0.9 + rand*0.1),
  bites + 1 or removes the last slice, consume-item trigger with a stack of (8 - bites) items, stat used.
- Random tick: a regenerating block regrows one slice (bites - 1) until full.
- Comparator: (8 - bites) + 7 if regenerating; a regenerating block is a redstone source of 15 - 2*bites.
- Item models: `twilightforest:think` and `twilightforest:full` predicates from `e115_variant` = "think" / "full"
  (alternate sprites for creative/advancement icons).

### 3.3 Frosted effect (`FrostedEffect`, colour 0x56CBFD, harmful)

- Movement speed modifier `twilightforest:frosted_slowdown` = **-0.15** x (amplifier + 1) multiply-total (vanilla
  attribute scaling per level).
- Every tick: the entity counts as in powder snow (freezing overlay); amplifier > 0 and able to freeze -> frozen ticks
  += amplifier (capped at the freeze threshold).
- `EntityEvents.reduceFrostedEffectIfOnFire`: while frosted, freeze damage gets + amplifier/2; fire damage lowers the
  amplifier by 1 (removes the effect below 0).
- Applied by `ApplyFrostedEffect.doChillAuraEffect(victim, duration, amplifier, shouldHit)`: not to freeze-immune
  types, not when any armor piece is in `#minecraft:freeze_immune_wearables`, not to creative players.

### 3.4 Enchantments (data-driven JSON, Appendix A)

| Enchantment | Items | Max lvl | Weight | Cost | Effect |
|---|---|---|---|---|---|
| `fire_react` | armor (primary: chest armor) | 3 | 1 | min 5+9(l-1), max 20+9(l-1), anvil 8 | exclusive with Thorns and chill aura; post-attack (victim -> attacker), chance 0.15 x level: ignite the attacker for 2 / 5 / 8 s (`Ignite perLevel(2.0, 3.0)`) and damage the enchanted item by 2 |
| `chill_aura` | armor (primary: chest) | 3 | 1 | same | exclusive with Thorns and fire react; post-attack chance 0.15 x level: frosted 200 t with amplifier 0 / 1 / 2 on the attacker (`perLevel(0, 1)`), item damage 2 |
| `destruction` | `#twilightforest:enchantable/block_and_chain` | 3 | 1 | same | damage -1.5 per level; HIT_BLOCK: smash up to **12** blocks within radius **1** (horizontal square at the hit y) excluding `#block_and_chain_never_breaks`, counted in the `smash_blocks` attachment; block-and-chain harvest tier wood / stone / iron for levels 1 / 2 / 3 |
| `renewal` | `#twilightforest:scepters` | 1 | 1 | same, hand | TICK: `recharge_scepter` (see U9: refill an empty scepter from essence or repair ingredients) |

`SmashBlocksEffect`: only for server-player owners; each candidate must pass `ChainBlock.canBreakBlockAt`, the break
event and `canEntityDestroy`; destroyed without drops plus `playerDestroy` drops for non-creative owners; optional
smash sound. Fire react / chill aura books appear in TF chest loot (Appendix A of the structure units).

### 3.5 Music

- Discs: stack 1, RARE, `jukeboxPlayable` songs: radiance 123 s, steps 195 s, superstitious 192 s, home 215 s,
  wayfarer (`warfarer` key) 173 s, findings 196 s, maker 207 s, thread 201 s, motion 169 s; comparator output 15;
  description `jukebox_song.twilightforest.<key>`. Sound events `music_disc.twilightforest.<name>`.
- Dimension music: every TF biome's background music is `Music(music.twilightforest.twilight_forest, min delay 1200,
  max delay 12000, replaces current = true)`; the event is a pool of the 9 tracks (streamed, volume 0.5). In the
  Twilight Forest the client replaces the CREATIVE and UNDER_WATER music by the biome's music
  (`setMusicInDimension`).
- The audio is regenerated (R10): U10 composes the 9 tracks in `tools/sounds/music/<track>.json`; nobody opens TF
  `.ogg` files.

## 4. Bedrock adaptation notes

- Foods are BP items with `minecraft:food` (nutrition, saturation, `can_always_eat`, `using_converts_to`) and
  `minecraft:use_modifiers`; effects through the item's food effects or F9's `finishUsing`.
- TF enchantments do not exist for vanilla clients (README deviation): store them in item user data
  (`enchant::set`), show lore + glint, apply through `combat::onIncoming` / F9 hooks; anvil / table / grindstone
  support comes from F9's enchant contract.
- Frosted: `effects::registerType("twilightforest:frosted", Spec{carrier: hidden Slowness})`; freezing overlay via the
  vanilla freeze ticks where possible (client mod shows the TF icon).
- Discs: S6 decides custom records; fallback: jukebox hook + PlaySoundPacket with comparator output 15.
- Dimension music via `client::music::setProvider` (client mod) and per-biome `music` definitions in the biome JSON for
  vanilla clients (if custom dimension biomes allow it, S1).
- Experiment 115: block with integer states (8 x 2 = 16 permutations), redstone output via `redstone::registerEmitter`
  and comparator via `registerAnalog`.

## 5. Acceptance checklist (selftests `provisions.*`, dx 1000-1099)

- Every food's nutrition/saturation/effects/bowl; hydra chop trigger at food 0; stale bread damage type.
- E115: place / add / remove / eat / regenerate cycle, comparator and redstone outputs, stat.
- Frosted slowdown and freeze ticks; fire reduces amplifier; freeze damage bonus.
- Enchantment JSON values; destruction smashing 12 blocks; chill aura / fire react proc chance 0.15 x level;
  renewal recharge.
- Discs play their songs with the listed lengths; TF biomes use the TF music pool.

## Appendix A: Owned ids (generated inventory)

Every id this unit owns in `docs/spec/owners.tsv`, with the TF 4.8 data the W1 worker must reproduce. Registration statements are TF's own Java (`init/TF*.java`), condensed (namespaces such as `MapColor.` dropped); loot, recipe, tag, advancement and data rows are summarised from TF's generated JSON (`src/generated/resources`). Loot notation: `self` = drops the block itself when it survives the explosion; `alternatives(a → b)` = first matching child; `[if ...]` = conditions; `{...}` = functions. In summaries `mc:` is `minecraft:` and ids without a namespace are `twilightforest:`.

| Kind | Count |
|---|---|
| `advancement` | 8 |
| `attachment` | 1 |
| `block` | 1 |
| `damage_type` | 1 |
| `data_component` | 1 |
| `e2e` | 1 |
| `effect` | 1 |
| `enchantment` | 4 |
| `enchantment_effect` | 3 |
| `item` | 33 |
| `jukebox_song` | 9 |
| `lang` | 52 |
| `net` | 1 |
| `path` | 21 |
| `recipe` | 8 |
| `selftest` | 1 |
| `selftest_dx` | 1 |
| `sound_event` | 10 |
| `stat` | 1 |
| `tag` | 1 |
| `trigger` | 1 |
| `worlddb` | 1 |

### Unit facts

| Kind | Ids |
|---|---|
| Selftest glob (`selftest`) | `provisions.*` |
| Selftest dx range (`selftest_dx`) | `1000-1099` |
| World DB keys (`worlddb`) | `gf:provisions:*` |
| E2E scenario (`e2e`) | `provisions` |
| Net channels (`net`) | `provisions.*` |
| Owned paths (`path`) | `**/*.d/provisions.json`, `assets/data/*/provisions.json`, `assets/gen/audio/provisions/**`, `assets/gen/geo/provisions/**`, `assets/gen/import/provisions/**`, `assets/lang/*/provisions.json`, `assets/pack/{bp,rp}/**/provisions/**`, `assets/pack/rp/texts.d/*/provisions.lang`, `assets/structures/provisions/**`, `assets/tags/**/*.d/provisions.json`, `docs/units/provisions.md`, `src/client/provisions/**`, `src/core/provisions/**`, `src/feature/ext/provisions/**`, `src/feature/provisions/**`, `tests/provisions/**`, `tools/e2e/*/scenarios/provisions{,.*}.ps1`, `tools/import/overrides/provisions.json`, `tools/sounds/music/**`, `tools/sounds/recipes/provisions.json`, `tools/structures/provisions/**` |

### Blocks (1)

Item column: `BlockItem` = TF registers a plain block item with the block (`TFBlocks.register`), the id is also a `block_item` row; `-` = no item of the same id (the block is placed by another item, or has none).

Block states come from TF's blockstate JSON (`multipart:` lists only the property values the model parts test). Notes collect `RegistrationEvents` (fire, axe stripping, flower pots, jar lids), dispenser behaviours and the `neoforge:compostables` / `neoforge:furnace_fuels` data maps.

| Block | English name | TF registration (`TFBlocks`) | Block states | Item | Loot (`blocks/<id>`) | Notes |
|---|---|---|---|---|---|---|
| `twilightforest:experiment_115` | Experiment 115 | `BLOCKS.register("experiment_115", new Experiment115Block(Properties.of().noLootTable().pushReaction(DESTROY).randomTicks().sound(WOOL).strength(0.5F)));` | omnomnom: 0/1/2/3/4/5/6/7; regenerate: false/true | - | none (noLootTable) | compost chance 0.85; item tags: #c:foods/edible_when_placed, #mc:meat |

### Items (33)

| Item | English name | TF registration (`TFItems`) | Notes |
|---|---|---|---|
| `twilightforest:beef_jerky` | Beef Jerky | `ITEMS.register("beef_jerky", new Item(new Item.Properties().food(new FoodProperties.Builder().nutrition(6).saturationModifier(1.18F).build())));` | item tags: #c:foods, #c:foods/jerky, #mc:meat |
| `twilightforest:berry_medley` | Berry Medley | `ITEMS.register("berry_medley", new Item(new Item.Properties().food(new FoodProperties.Builder().nutrition(5).saturationModifier(0.6F).usingConvertsTo(Items.BOWL).build()).stacksTo(1)));` | item tags: #c:foods |
| `twilightforest:chicken_jerky` | Chicken Jerky | `ITEMS.register("chicken_jerky", new Item(new Item.Properties().food(new FoodProperties.Builder().nutrition(4).saturationModifier(1.075F).build())));` | item tags: #c:foods, #c:foods/jerky, #mc:meat |
| `twilightforest:cod_jerky` | Cod Jerky | `ITEMS.register("cod_jerky", new Item(new Item.Properties().food(new FoodProperties.Builder().nutrition(4).saturationModifier(0.875F).build())));` | item tags: #c:foods, #c:foods/jerky, #mc:meat |
| `twilightforest:cooked_meef` | Meef Steak | `ITEMS.register("cooked_meef", new Item(new Item.Properties().food(new FoodProperties.Builder().nutrition(7).saturationModifier(0.6F).build())));` | item tags: #c:foods/cooked_meat, #mc:meat |
| `twilightforest:cooked_venison` | Venison Steak | `ITEMS.register("cooked_venison", new Item(new Item.Properties().food(new FoodProperties.Builder().nutrition(6).saturationModifier(0.6F).build())));` | item tags: #c:foods/cooked_meat, #mc:meat |
| `twilightforest:experiment_115` | Experiment 115 | `ITEMS.register("experiment_115", new Experiment115Item(TFBlocks.EXPERIMENT_115.get(), new Item.Properties().food(new FoodProperties.Builder().nutrition(4).saturationModifier(0.3F).build())));` | compost chance 0.85; item tags: #c:foods/edible_when_placed, #mc:meat |
| `twilightforest:fugu_jerky` | Fugu Jerky | `ITEMS.register("fugu_jerky", new Item(new Item.Properties().food(new FoodProperties.Builder().nutrition(2).saturationModifier(0.6F).build())));` | item tags: #c:foods, #c:foods/jerky, #mc:meat |
| `twilightforest:hydra_chop` | Hydra Chop | `ITEMS.register("hydra_chop", new HydraChopItem(new Item.Properties().fireResistant().food(new FoodProperties.Builder().nutrition(18).saturationModifier(2.0F).effect(new MobEffectInstance(MobEffects.REGENERATION, 100, 0), 1.0F).build()).rarity(UNCOMMON)));` | item tags: #c:foods/cooked_meat, #mc:meat |
| `twilightforest:maze_wafer` | Maze Wafer | `ITEMS.register("maze_wafer", new Item(new Item.Properties().food(new FoodProperties.Builder().nutrition(4).saturationModifier(0.6F).build())));` | compost chance 0.65; item tags: #c:foods |
| `twilightforest:meef_jerky` | Meef Jerky | `ITEMS.register("meef_jerky", new Item(new Item.Properties().food(new FoodProperties.Builder().nutrition(6).saturationModifier(1.18F).build())));` | item tags: #c:foods, #c:foods/jerky, #mc:meat |
| `twilightforest:meef_stroganoff` | Meef Stroganoff | `ITEMS.register("meef_stroganoff", new Item(new Item.Properties().stacksTo(1).rarity(UNCOMMON).fireResistant().food(new FoodProperties.Builder().nutrition(14).saturationModifier(1.2F).alwaysEdible().effect(new MobEffectInstance(MobEffects.FIRE_RESISTANCE, 3600), 1.0F).effect(new MobEffectInstance(MobEffects.DAMAGE_BOOST, 2400), 1.0F).usingConvertsTo(Items.BOWL).build())));` | item tags: #c:foods/soup, #mc:meat |
| `twilightforest:monster_jerky` | Monster Jerky | `ITEMS.register("monster_jerky", new Item(new Item.Properties().food(new FoodProperties.Builder().nutrition(4).saturationModifier(0.275F).build())));` | item tags: #c:foods, #c:foods/jerky, #mc:meat |
| `twilightforest:moss_soup` | Moss Soup | `ITEMS.register("moss_soup", new Item(new Item.Properties().food(new FoodProperties.Builder().nutrition(5).saturationModifier(0.6F).usingConvertsTo(Items.BOWL).build()).stacksTo(1)));` | item tags: #c:foods/soup |
| `twilightforest:music_disc_findings` | Music Disc | `ITEMS.register("music_disc_findings", new Item(new Item.Properties().stacksTo(1).rarity(RARE).jukeboxPlayable(TFJukeboxSongs.FINDINGS)));` | item tags: #c:music_discs |
| `twilightforest:music_disc_home` | Music Disc | `ITEMS.register("music_disc_home", new Item(new Item.Properties().stacksTo(1).rarity(RARE).jukeboxPlayable(TFJukeboxSongs.HOME)));` | item tags: #c:music_discs |
| `twilightforest:music_disc_maker` | Music Disc | `ITEMS.register("music_disc_maker", new Item(new Item.Properties().stacksTo(1).rarity(RARE).jukeboxPlayable(TFJukeboxSongs.MAKER)));` | item tags: #c:music_discs |
| `twilightforest:music_disc_motion` | Music Disc | `ITEMS.register("music_disc_motion", new Item(new Item.Properties().stacksTo(1).rarity(RARE).jukeboxPlayable(TFJukeboxSongs.MOTION)));` | item tags: #c:music_discs |
| `twilightforest:music_disc_radiance` | Music Disc | `ITEMS.register("music_disc_radiance", new Item(new Item.Properties().stacksTo(1).rarity(RARE).jukeboxPlayable(TFJukeboxSongs.RADIANCE)));` | item tags: #c:music_discs |
| `twilightforest:music_disc_steps` | Music Disc | `ITEMS.register("music_disc_steps", new Item(new Item.Properties().stacksTo(1).rarity(RARE).jukeboxPlayable(TFJukeboxSongs.STEPS)));` | item tags: #c:music_discs |
| `twilightforest:music_disc_superstitious` | Music Disc | `ITEMS.register("music_disc_superstitious", new Item(new Item.Properties().stacksTo(1).rarity(RARE).jukeboxPlayable(TFJukeboxSongs.SUPERSTITIOUS)));` | item tags: #c:music_discs |
| `twilightforest:music_disc_thread` | Music Disc | `ITEMS.register("music_disc_thread", new Item(new Item.Properties().stacksTo(1).rarity(RARE).jukeboxPlayable(TFJukeboxSongs.THREAD)));` | item tags: #c:music_discs |
| `twilightforest:music_disc_wayfarer` | Music Disc | `ITEMS.register("music_disc_wayfarer", new Item(new Item.Properties().stacksTo(1).rarity(RARE).jukeboxPlayable(TFJukeboxSongs.WAYFARER)));` | item tags: #c:music_discs |
| `twilightforest:mutton_jerky` | Mutton Jerky | `ITEMS.register("mutton_jerky", new Item(new Item.Properties().food(new FoodProperties.Builder().nutrition(4).saturationModifier(1.35F).build())));` | item tags: #c:foods, #c:foods/jerky, #mc:meat |
| `twilightforest:pork_jerky` | Pork Jerky | `ITEMS.register("pork_jerky", new Item(new Item.Properties().food(new FoodProperties.Builder().nutrition(6).saturationModifier(1.18F).build())));` | item tags: #c:foods, #c:foods/jerky, #mc:meat |
| `twilightforest:rabbit_jerky` | Rabbit Jerky | `ITEMS.register("rabbit_jerky", new Item(new Item.Properties().food(new FoodProperties.Builder().nutrition(4).saturationModifier(0.925F).build())));` | item tags: #c:foods, #c:foods/jerky, #mc:meat |
| `twilightforest:raw_meef` | Raw Meef | `ITEMS.register("raw_meef", new Item(new Item.Properties().food(new FoodProperties.Builder().nutrition(2).saturationModifier(0.2F).build())));` | item tags: #alexscaves:raw_meats, #c:foods/raw_meat, #farmersdelight:cabbage_roll_ingredients, #mc:meat |
| `twilightforest:raw_venison` | Raw Venison | `ITEMS.register("raw_venison", new Item(new Item.Properties().food(new FoodProperties.Builder().nutrition(2).saturationModifier(0.3F).build())));` | item tags: #alexscaves:raw_meats, #c:foods/raw_meat, #farmersdelight:cabbage_roll_ingredients, #mc:meat |
| `twilightforest:salmon_jerky` | Salmon Jerky | `ITEMS.register("salmon_jerky", new Item(new Item.Properties().food(new FoodProperties.Builder().nutrition(4).saturationModifier(1.35F).build())));` | item tags: #c:foods, #c:foods/jerky, #mc:meat |
| `twilightforest:shika_senbei` | Shika Senbei | `ITEMS.register("shika_senbei", new Item(new Item.Properties().food(new FoodProperties.Builder().nutrition(4).saturationModifier(1.0F).build())));` | item tags: #deer_tempt_items |
| `twilightforest:stale_bread` | Stale Bread | `ITEMS.register("stale_bread", new CustomDamageSwordItem(TFDamageTypes.STALE_SANDWICH, Tiers.WOOD, new Item.Properties().stacksTo(1).attributes(SwordItem.createAttributes(Tiers.WOOD, 3, -2.4F).withTooltip(false))));` |  |
| `twilightforest:tropical_fish_jerky` | Tropical Fish Jerky | `ITEMS.register("tropical_fish_jerky", new Item(new Item.Properties().food(new FoodProperties.Builder().nutrition(2).saturationModifier(0.6F).build())));` | item tags: #c:foods, #c:foods/jerky, #mc:meat |
| `twilightforest:venison_jerky` | Venison Jerky | `ITEMS.register("venison_jerky", new Item(new Item.Properties().food(new FoodProperties.Builder().nutrition(6).saturationModifier(1.18F).build())));` | item tags: #c:foods, #c:foods/jerky, #mc:meat |

### Data components (1)

| Id | TF registration (`TFDataComponents.java`) |
|---|---|
| `twilightforest:e115_variant` | `COMPONENTS.register("e115_variant", () -> DataComponentType.<String>builder().persistent(Codec.STRING).networkSynchronized(ByteBufCodecs.STRING_UTF8).build());` |

### Data attachments (1)

| Id | TF registration (`TFDataAttachments.java`) |
|---|---|
| `twilightforest:smash_blocks` | `ATTACHMENT_TYPES.register("smash_blocks", () -> AttachmentType.builder(new SmashBlocksEnchantmentAttachment()).serialize(SmashBlocksEnchantmentAttachment.CODEC).build());` |

### Mob effects (1)

| Id | TF registration (`TFMobEffects.java`) |
|---|---|
| `twilightforest:frosted` | `MOB_EFFECTS.register("frosted", FrostedEffect::new);` |

### Enchantment effect types (3)

| Id | TF registration (`TFEnchantmentEffects.java`) |
|---|---|
| `twilightforest:apply_frosted` | `ENTITY_EFFECTS.register("apply_frosted", () -> ApplyFrostedEffect.CODEC);` |
| `twilightforest:recharge_scepter` | `ENTITY_EFFECTS.register("recharge_scepter", () -> RechargeScepterEffect.CODEC);` |
| `twilightforest:smash_blocks` | `ENTITY_EFFECTS.register("smash_blocks", () -> SmashBlocksEffect.CODEC);` |

### Stats (1)

| Id | TF registration (`TFStats.java`) |
|---|---|
| `twilightforest:e115_slices_eaten` (Experiment 115 Slices Eaten) | `makeTFStat("e115_slices_eaten");` |

### Advancement triggers (1)

| Id | TF registration (`TFAdvancements.java`) |
|---|---|
| `twilightforest:consume_hydra_chop_on_low_hunger` | `TRIGGERS.register("consume_hydra_chop_on_low_hunger", SimpleAdvancementTrigger::new);` |

### Damage types (1)

- `twilightforest:stale_sandwich`: `{"exhaustion":0.0,"message_id":"twilightforest.stale_sandwich","scaling":"when_caused_by_living_non_player"}` death.attack.twilightforest.stale_sandwich = "%1$s was turned into a stale sandwich by %2$s";

### Enchantments (4)

- `twilightforest:chill_aura`: `{"anvil_cost":8,"description":{"translate":"enchantment.twilightforest.chill_aura"},"effects":{"minecraft:post_attack":[{"affected":"attacker","effect":{"type":"minecraft:all_of","effects":[{"type":"twilightforest:apply_frosted","amplifier":{"type":"minecraft:linear","base":0.0,"per_level_above_first":1.0},"duration":200.0},{"type":"minecraft:damage_item","amount":2.0}]},"enchanted":"victim","requirements":{"chance":{"type":"minecraft:enchantment_level","amount":{"type":"minecraft:linear","base":0.15,"per_level_above_first":0.15}},"condition":"minecraft:random_chance"}}]},"exclusive_set":["minecraft:thorns","twilightforest:fire_react"],"max_cost":{"base":20,"per_level_above_first":9},"max_level":3,"min_cost":{"base":5,"per_level_above_first":9},"primary_items":"#minecraft:enchantable/chest_armor","slots":["armor"],"supported_items":"#minecraft:enchantable/armor","weight":1}`
- `twilightforest:destruction`: `{"anvil_cost":8,"description":{"translate":"enchantment.twilightforest.destruction"},"effects":{"minecraft:damage":[{"effect":{"type":"minecraft:add","value":{"type":"minecraft:linear","base":-1.5,"per_level_above_first":-1.5}}}],"minecraft:hit_block":[{"effect":{"type":"twilightforest:smash_blocks","immune_blocks":"#twilightforest:block_and_chain_never_breaks","max_smash":12.0,"radius":1.0}}]},"max_cost":{"base":20,"per_level_above_first":9},"max_level":3,"min_cost":{"base":5,"per_level_above_first":9},"slots":["hand"],"supported_items":"#twilightforest:enchantable/block_and_chain","weight":1}`
- `twilightforest:fire_react`: `{"anvil_cost":8,"description":{"translate":"enchantment.twilightforest.fire_react"},"effects":{"minecraft:post_attack":[{"affected":"attacker","effect":{"type":"minecraft:all_of","effects":[{"type":"minecraft:ignite","duration":{"type":"minecraft:linear","base":2.0,"per_level_above_first":3.0}},{"type":"minecraft:damage_item","amount":2.0}]},"enchanted":"victim","requirements":{"chance":{"type":"minecraft:enchantment_level","amount":{"type":"minecraft:linear","base":0.15,"per_level_above_first":0.15}},"condition":"minecraft:random_chance"}}]},"exclusive_set":["minecraft:thorns","twilightforest:chill_aura"],"max_cost":{"base":20,"per_level_above_first":9},"max_level":3,"min_cost":{"base":5,"per_level_above_first":9},"primary_items":"#minecraft:enchantable/chest_armor","slots":["armor"],"supported_items":"#minecraft:enchantable/armor","weight":1}`
- `twilightforest:renewal`: `{"anvil_cost":8,"description":{"translate":"enchantment.twilightforest.renewal"},"effects":{"minecraft:tick":[{"effect":{"type":"twilightforest:recharge_scepter"}}]},"max_cost":{"base":20,"per_level_above_first":9},"max_level":1,"min_cost":{"base":5,"per_level_above_first":9},"slots":["hand"],"supported_items":"#twilightforest:scepters","weight":1}`

### Jukebox songs (9)

- `twilightforest:findings`: `{"comparator_output":15,"description":{"translate":"jukebox_song.twilightforest.findings"},"length_in_seconds":196.0,"sound_event":"twilightforest:music_disc.twilightforest.findings"}`
- `twilightforest:home`: `{"comparator_output":15,"description":{"translate":"jukebox_song.twilightforest.home"},"length_in_seconds":215.0,"sound_event":"twilightforest:music_disc.twilightforest.home"}`
- `twilightforest:maker`: `{"comparator_output":15,"description":{"translate":"jukebox_song.twilightforest.maker"},"length_in_seconds":207.0,"sound_event":"twilightforest:music_disc.twilightforest.maker"}`
- `twilightforest:motion`: `{"comparator_output":15,"description":{"translate":"jukebox_song.twilightforest.motion"},"length_in_seconds":169.0,"sound_event":"twilightforest:music_disc.twilightforest.motion"}`
- `twilightforest:radiance`: `{"comparator_output":15,"description":{"translate":"jukebox_song.twilightforest.radiance"},"length_in_seconds":123.0,"sound_event":"twilightforest:music_disc.twilightforest.radiance"}`
- `twilightforest:steps`: `{"comparator_output":15,"description":{"translate":"jukebox_song.twilightforest.steps"},"length_in_seconds":195.0,"sound_event":"twilightforest:music_disc.twilightforest.steps"}`
- `twilightforest:superstitious`: `{"comparator_output":15,"description":{"translate":"jukebox_song.twilightforest.superstitious"},"length_in_seconds":192.0,"sound_event":"twilightforest:music_disc.twilightforest.superstitious"}`
- `twilightforest:thread`: `{"comparator_output":15,"description":{"translate":"jukebox_song.twilightforest.thread"},"length_in_seconds":201.0,"sound_event":"twilightforest:music_disc.twilightforest.thread"}`
- `twilightforest:warfarer`: `{"comparator_output":15,"description":{"translate":"jukebox_song.twilightforest.warfarer"},"length_in_seconds":173.0,"sound_event":"twilightforest:music_disc.twilightforest.wayfarer"}`

### Sound events (10)

`sounds.json` content (vanilla `event` references can be played with the matching Bedrock vanilla sound; TF file names are listed only as names: the audio itself is All Rights Reserved and is re-synthesised by F3/the owner).

| Event | Subtitle | sounds.json entries |
|---|---|---|
| `music_disc.twilightforest.findings` |  | twilightforest:music/findings stream=True |
| `music_disc.twilightforest.home` |  | twilightforest:music/home stream=True |
| `music_disc.twilightforest.maker` |  | twilightforest:music/maker stream=True |
| `music_disc.twilightforest.motion` |  | twilightforest:music/motion stream=True |
| `music_disc.twilightforest.radiance` |  | twilightforest:music/radiance stream=True |
| `music_disc.twilightforest.steps` |  | twilightforest:music/steps stream=True |
| `music_disc.twilightforest.superstitious` |  | twilightforest:music/superstitious stream=True |
| `music_disc.twilightforest.thread` |  | twilightforest:music/thread stream=True |
| `music_disc.twilightforest.wayfarer` |  | twilightforest:music/wayfarer stream=True |
| `music.twilightforest.twilight_forest` |  | twilightforest:music/superstitious volume=0.5 stream=True, twilightforest:music/steps volume=0.5 stream=True, twilightforest:music/radiance volume=0.5 stream=True, twilightforest:music/home volume=0.5 stream=True, twilightforest:music/wayfarer volume=0.5 stream=True, twilightforest:music/findings volume=0.5 stream=True, twilightforest:music/maker volume=0.5 stream=True, twilightforest:music/thread volume=0.5 stream=True, twilightforest:music/motion volume=0.5 stream=True |

### Tags (1)

| Tag | Members (TF generated JSON) |
|---|---|
| `item/#twilightforest:enchantable/block_and_chain` | block_and_chain |

### Recipes (8)

Recipe-unlock advancements (generated by F1 into the recipe JSON, R21) are listed with their recipe; criteria are the `has_*` triggers besides `has_the_recipe`.

| Recipe | Type | Ingredients | Result | Unlock advancement (criteria) |
|---|---|---|---|---|
| `twilightforest:berry_medley` | mc:crafting_shapeless | mc:bowl + raspberry + blueberry + blackberry + maloberry | berry_medley | `twilightforest:recipes/food/berry_medley` (has_blackberry: has blackberry; has_blueberry: has blueberry; has_maloberry: has maloberry; has_raspberry: has raspberry) |
| `twilightforest:food/campfired_meef` | mc:campfire_cooking | raw_meef (xp 0.35, 600 t) | cooked_meef | `twilightforest:recipes/food/food/campfired_meef` (has_food: has raw_meef) |
| `twilightforest:food/campfired_venison` | mc:campfire_cooking | raw_venison (xp 0.35, 600 t) | cooked_venison | `twilightforest:recipes/food/food/campfired_venison` (has_food: has raw_venison) |
| `twilightforest:food/smelted_meef` | mc:smelting | raw_meef (xp 0.35, 200 t) | cooked_meef | `twilightforest:recipes/food/food/smelted_meef` (has_food: has raw_meef) |
| `twilightforest:food/smelted_venison` | mc:smelting | raw_venison (xp 0.35, 200 t) | cooked_venison | `twilightforest:recipes/food/food/smelted_venison` (has_food: has raw_venison) |
| `twilightforest:food/smoked_meef` | mc:smoking | raw_meef (xp 0.35, 100 t) | cooked_meef | `twilightforest:recipes/food/food/smoked_meef` (has_food: has raw_meef) |
| `twilightforest:food/smoked_venison` | mc:smoking | raw_venison (xp 0.35, 100 t) | cooked_venison | `twilightforest:recipes/food/food/smoked_venison` (has_food: has raw_venison) |
| `twilightforest:moss_soup` | mc:crafting_shapeless | moss_patch + mc:bowl + mc:potion | moss_soup | `twilightforest:recipes/food/moss_soup` (has_moss: has moss_patch) |

## Appendix B: Strings (52 en_us keys owned)

Keys already shown above (block/item/entity names, subtitles) are not repeated. The 29 Bedrock locales are generated from TF's lang files (R9); owners use these keys with the English text as fallback.

| Key | English |
|---|---|
| `effect.twilightforest.frosted` | Frosted |
| `enchantment.twilightforest.chill_aura` | Chill Aura |
| `enchantment.twilightforest.chill_aura.desc` | Adds a chance for the attacker to gain the frosted effect after hitting the wearer. |
| `enchantment.twilightforest.destruction` | Destruction |
| `enchantment.twilightforest.destruction.desc` | Allows the Block and Chain to break blocks with a higher mining tier. |
| `enchantment.twilightforest.fire_react` | Fire React |
| `enchantment.twilightforest.fire_react.desc` | Adds a chance for the attacker to be set on fire after hitting the wearer. |
| `enchantment.twilightforest.renewal` | Renewal |
| `enchantment.twilightforest.renewal.desc` | Automatically recharges scepters when they run out if the holder has the recharge items in their inventory. |
| `jukebox_song.twilightforest.findings` | MrCompost - Findings |
| `jukebox_song.twilightforest.home` | MrCompost - Home |
| `jukebox_song.twilightforest.maker` | MrCompost - Maker |
| `jukebox_song.twilightforest.motion` | MrCompost - Motion |
| `jukebox_song.twilightforest.radiance` | Rotch Gwylt - Radiance |
| `jukebox_song.twilightforest.steps` | Rotch Gwylt - Steps |
| `jukebox_song.twilightforest.superstitious` | Rotch Gwylt - Superstitious |
| `jukebox_song.twilightforest.thread` | MrCompost - Thread |
| `jukebox_song.twilightforest.warfarer` | MrCompost - Wayfarer |

