# Progression and advancements (U14 `progression`)

Behaviour spec of unit **U14** (wave W1), translated from The Twilight Forest 4.8 (`9e40a757`, MC 1.21.1) by F16. It
is the checklist the U14 worker implements and the A6 audit compares against. Sections 1-5 are the behaviour;
Appendix A lists every id U14 owns, including **all 45 advancements with their full JSON** (parent, icon, frame,
flags, criteria, requirements, rewards); Appendix B the strings.

## 1. Scope

- The 45 TF advancements (`twilightforest:<id>`, R21: U14 owns every advancement JSON and its criteria; other units
  fire their triggers by string through `progression::trigger`, recipe-unlock advancements belong to their recipe's
  unit) and their presentation: the advancement tab (root `twilightforest:root`... see the parent chain in Appendix A),
  advancement toasts, the "advancement required" toast and the locked-biome toast.
- **Biome progression enforcement**: registry `twilightforest:restrictions` (9 entries `restrictions/<biome>`), the
  enforcement types (darkness, hunger, fire, frost, acid_rain), the gamerule `tfEnforcedProgression` and its client
  sync, client config `client.disableLockedBiomeToasts` (section `progression`), net channels
  `progression.enforce_status`, `progression.missing_advancement_toast`.
- Item sub-predicate `twilightforest:color` (used by `arctic_armor_dyed`).
- U14 also owns `assets/data/advancements/**`, `assets/data/restrictions/**` and `src/feature/progression_content/**`
  (owners.tsv paths).

Not U14: the progression trigger bus, `progression::has/grant/...` and the client query API (F10 contract), landmark
protection / structure hints / conquest (F6; it reads the same advancement lists), the portal (F4) whose locking
advancement comes from F4's config, the acid rain weather and its damage type / sound (U15; the `acid_rain`
enforcement plays them by id), trigger ids owned by other units (listed by string in Appendix A criteria).

Contracts: progression, rules+fx, client.ui, landmark, data.

## 2. TF sources

Generated advancement JSON `data/twilightforest/advancement/*.json` (from `data/TFAdvancementGenerator.java`),
`init/TFAdvancements.java` (trigger registry), `advancements/*Trigger.java`, `advancements/predicate/ItemColorPredicate.java`,
`init/TFItemSubPredicates.java`, `init/custom/Restrictions.java`, `util/Restriction.java`, `init/custom/Enforcements.java`,
`util/Enforcement.java`, `util/PlayerHelper.java`, `events/ProgressionEvents.java` (`performProtectionAndPortalChecks`:
enforcement every 20 ticks), `init/TFGameRules.java`, `network/EnforceProgressionStatusPacket.java`,
`network/MissingAdvancementToastPacket.java`, `client/LockedBiomeToast.java`, `client/event/LockedBiomeToastHandler.java`,
`client/MissingAdvancementToast.java`, `events/EntityEvents.java` (`alertPlayerCastleIsWIP`, group advancements,
`resetFlaskLogic`), `util/StructureHints.java`, `world/components/structures/util/AdvancementLockedStructure.java`,
`config/TFClientConfig.java`.

## 3. Behaviour

### 3.1 Advancements

- All 45 are listed in Appendix A with every field. Tree (child <- parent): `root` (tab root, background
  `twilightforest:textures/block/mazestone_large_brick.png`; criteria: location in the Twilight Forest or
  `twilightforest:make_tf_portal`) <- none; from `root`: `progress_naga`, `hill1` <- `hill2` <- `hill3`, `hedge`,
  `twilight_hunter` -> `magic_map_focus` -> `magic_map` -> `maze_map` -> `ore_map`, `twilight_dinner`, `kill_cicada`,
  `quest_ram`, `arborist`, `chicken_jerky`, `craft_travellers_gear` -> `modify_travellers_gear`,
  `uncraft_uncrafting_table`; `progress_naga` -> `progress_lich`, `naga_armors`; `progress_lich` ->
  `progress_labyrinth` (-> `progress_hydra` -> `fiery_set`, `hydra_chop`; -> `mazebreaker`), `progress_trophy_pedestal`
  -> `progress_knights` (-> `ghast_trap` -> `progress_ur_ghast`; -> `experiment_115` -> `experiment_115_115`,
  `experiment_115_self_replenishing`), `progress_yeti` (-> `progress_glacier`, `arctic_armor_dyed`,
  `break_glass_sword`), `progress_merge` (-> `troll`, `beanstalk` -> `giants` -> `progress_troll` ->
  `progression_end`), `lich_scepters`, `full_mettle_alchemist`. Frames: `goal` for the `progress_*` chain,
  `beanstalk`, `giants`, `maze_map`, `quest_ram`, `progression_end`; `challenge` for `arborist`, `chicken_jerky`,
  `break_glass_sword`, `experiment_115_115`, `experiment_115_self_replenishing`, `fiery_set`,
  `full_mettle_alchemist`, `lich_scepters`, `mazebreaker`, `naga_armors`, `ore_map`, `twilight_dinner`; hidden:
  `break_glass_sword`, `chicken_jerky`, `experiment_115_115`, `experiment_115_self_replenishing`,
  `full_mettle_alchemist`, `kill_cicada`, `lich_scepters`, `ore_map`.
- Triggers referenced by criteria: vanilla `inventory_changed`, `player_killed_entity`, `consume_item`,
  `item_used_on_block`, `recipe_crafted`, `location`, `tick` (the `tick` criteria with player predicates are the
  "has the previous advancement / conquered" checks), plus TF triggers owned by other units and fired through
  `progression::trigger` by string: `twilightforest:make_tf_portal` (F4), `hurt_boss` (F8), `kill_bug` (U6),
  `broke_glass_sword` (U8), `drink_from_flask` (U9), `consume_hydra_chop_on_low_hunger` (U10), `add_modifier`
  (U11), `uncraft_item` (U13), `complete_quest_ram` (U19), `placed_on_trophy_pedestal` (U25), `kill_all_phantoms`
  (U25), `activate_ghast_trap` (U26); `structure_cleared` (F6) is fired on conquest and read by other units. Criteria use the `twilightforest:color` item sub-predicate (`{color: int}`,
  -1 = any dyed colour) for `arctic_armor_dyed`.
- Earning `twilightforest:progression_end` sends `gui.twilightforest.progression_end.message` with a blue underlined
  `gui.twilightforest.progression_end.discord` link (https://discord.experiment115.com/).
- Earning any advancement with a `drink_from_flask` criterion resets the flask dose tracking (U9's attachment, by
  string).
- Group advancements: entities in `#twilightforest:multiplayer_inclusive_entities` record qualified attackers; on
  death every qualified player is granted the boss advancement (F8's multiplayer fight data; U14 provides the
  advancements).

### 3.2 Biome restrictions (`Restriction`, `Enforcement`)

Restriction JSON `{structure_key?, enforcement, multiplier, locked_biome_toast?, advancements[]}`; a restriction
applies to a player in that biome until they have **all** listed advancements.

| Biome (`restrictions/<id>`) | Hint structure | Enforcement | Multiplier | Toast icon | Required |
|---|---|---|---|---|---|
| `dark_forest` | knight stronghold | darkness | 0 | lich tower miniature | `progress_lich` |
| `dark_forest_center` | dark tower | darkness | 0 | knight phantom trophy | `progress_knights` |
| `final_plateau` | final castle | acid_rain | 1.5 | lamp of cinders | `progress_troll` |
| `fire_swamp` | hydra lair | fire | 8 | meef stroganoff | `progress_labyrinth` |
| `glacier` | aurora palace | frost | 1 | alpha yeti fur | `progress_yeti` |
| `highlands` | troll cave | acid_rain | 0.5 | uberous soil | `progress_merge` |
| `snowy_forest` | yeti cave | frost | 0 | lich tower miniature | `progress_lich` |
| `swamp` | labyrinth | hunger | 1 | lich tower miniature | `progress_lich` |
| `thornlands` | final castle | acid_rain | 1 | lamp of cinders | `progress_troll` |

- Server, every **20** ticks per player when `tfEnforcedProgression` is true and the player is not creative /
  spectator: if the biome at the player has an unmet restriction, run its enforcement and try a structure hint for
  the hint structure (F6's `StructureHints.tryHintForStructure`, by string).
- Enforcements (each checks the player's own tick counter and that the tick rate runs normally):
  - `darkness`: every 60 ticks Darkness for 200 ticks, amplifier = (int) multiplier, visible particles off/ambient
    (`MobEffectInstance(DARKNESS, 200, m, false, true)`);
  - `hunger`: every 60 ticks Hunger 100 ticks, amplifier = current amplifier + m (or m);
  - `fire`: every 60 ticks set on fire for (int) m seconds;
  - `frost`: every 60 ticks `twilightforest:frosted` 100 ticks, amplifier (int) m;
  - `acid_rain`: every **5** ticks `twilightforest:acid_rain` damage of m; if it hurts, play
    `environment.twilightforest.acid_rain` (U15's event, by id) at the player (1, 1).
- Gamerule `tfEnforcedProgression` (boolean, default **true**, category UPDATES): on change the server sends
  `EnforceProgressionStatusPacket(value)` to all players, which sets the client copy of the rule (used by client
  checks: locked toasts, sky/fog effects of other units through `progression::client::enforced()`).

### 3.3 Toasts (client)

- **Locked biome toast** (`LockedBiomeToastHandler`): every 5 client ticks, when progression is enforced, the player
  is not creative/spectator and `disableLockedBiomeToasts` (default **false**) is off: inside a restricted biome with a
  toast icon, a countdown from 60 (5-tick steps, ~15 s) shows one `LockedBiomeToast` (vanilla advancement toast
  background, the restriction's icon at (6, 8), title `misc.twilightforest.biome_locked` at (25, 7) yellow -256,
  description `misc.twilightforest.biome_locked_2` at (25, 18) white; 10 s). Leaving resets the countdown and allows a
  new toast.
- **Missing advancement toast** (`MissingAdvancementToastPacket(title, icon)`, sent e.g. by the portal when the
  portal-locking advancement is missing, F4 by string): advancement toast background, icon at (6, 8),
  `misc.twilightforest.advancement_required` at (25, 7) white, the advancement title at (25, 18); 10 s; fallback title
  `misc.twilightforest.advancement_hidden` with the twilight portal miniature icon.

## 4. Bedrock adaptation notes

- Bedrock has no data-driven advancements: F10's progression bus stores grants per player (world DB
  `gf:progression:<uuid>`), U14 loads the 45 JSONs (`assets/data/advancements/`), evaluates criteria from
  `progression::events` and the TF triggers, and shows the advancement screen through `client::screen` (client mod:
  TF tab with redrawn frames, README deviation: the Java frame textures are Mojang's) and a form-based list for
  vanilla clients; toasts via `client::toast::show` (client mod) or title/actionbar for vanilla.
- Recipe-unlock advancements are generated into recipe JSON by F1 (R21); U14 does not handle them.
- Enforcement every 20 ticks with the per-player tick counters (`progression::events::onTick`); effects through
  `effects::apply` (frosted is U10's custom effect) and `combat::hurt` for acid rain.
- Gamerule via `gamerules::registerBool("tfEnforcedProgression", true)`; sync to companions with
  `progression.enforce_status`.

## 5. Acceptance checklist (selftests `progression.*`, dx 1400-1499)

- All 45 advancements load with the parents/criteria of Appendix A; granting the chain in order unlocks the tab.
- Restriction table: each biome applies its enforcement until the listed advancement is granted (selftests with
  `progression::grant`), timing 60 ticks (5 for acid rain), multipliers.
- Gamerule false disables enforcement and toasts; packet sync.
- Locked biome toast appears once after the countdown and again after leaving and re-entering (client screenshot).
- `progression_end` message; `arctic_armor_dyed` with the color predicate.

## Appendix A: Owned ids (generated inventory)

Every id this unit owns in `docs/spec/owners.tsv`, with the TF 4.8 data the W1 worker must reproduce. Registration statements are TF's own Java (`init/TF*.java`), condensed (namespaces such as `MapColor.` dropped); loot, recipe, tag, advancement and data rows are summarised from TF's generated JSON (`src/generated/resources`). Loot notation: `self` = drops the block itself when it survives the explosion; `alternatives(a → b)` = first matching child; `[if ...]` = conditions; `{...}` = functions. In summaries `mc:` is `minecraft:` and ids without a namespace are `twilightforest:`.

| Kind | Count |
|---|---|
| `advancement` | 45 |
| `config_key` | 1 |
| `config_section` | 1 |
| `e2e` | 1 |
| `gamerule` | 1 |
| `item_sub_predicate` | 1 |
| `lang` | 95 |
| `net` | 3 |
| `path` | 22 |
| `registry` | 1 |
| `registry_entry` | 9 |
| `selftest` | 1 |
| `selftest_dx` | 1 |
| `worlddb` | 1 |

### Unit facts

| Kind | Ids |
|---|---|
| Selftest glob (`selftest`) | `progression.*` |
| Selftest dx range (`selftest_dx`) | `1400-1499` |
| World DB keys (`worlddb`) | `gf:progression:*` |
| E2E scenario (`e2e`) | `progression` |
| Net channels (`net`) | `progression.*`, `progression.enforce_status`, `progression.missing_advancement_toast` |
| Config section (`config_section`) | `progression` |
| Owned paths (`path`) | `**/*.d/progression.json`, `assets/data/*/progression.json`, `assets/data/advancements/**`, `assets/data/restrictions/**`, `assets/gen/audio/progression/**`, `assets/gen/geo/progression/**`, `assets/gen/import/progression/**`, `assets/lang/*/progression.json`, `assets/pack/{bp,rp}/**/progression/**`, `assets/pack/rp/texts.d/*/progression.lang`, `assets/structures/progression/**`, `assets/tags/**/*.d/progression.json`, `docs/units/progression.md`, `src/client/progression/**`, `src/core/progression/**`, `src/feature/ext/progression/**`, `src/feature/progression_content/**`, `tests/progression/**`, `tools/e2e/*/scenarios/progression{,.*}.ps1`, `tools/import/overrides/progression.json`, `tools/sounds/recipes/progression.json`, `tools/structures/progression/**` |

### Game rules (1)

| Id | TF registration (`TFGameRules.java`) |
|---|---|
| `tfEnforcedProgression` | `register("tfEnforcedProgression", GameRules.Category.UPDATES, GameRules.BooleanValue.create(true, (server, value) -> PacketDistributor.sendToAllPlayers(new EnforceProgressionStatusPacket(value.get())) ) );` |

### Item sub-predicates (1)

| Id | TF registration (`TFItemSubPredicates.java`) |
|---|---|
| `twilightforest:color` | `TYPES.register("color", new ItemSubPredicate.Type<>(ItemColorPredicate.CODEC));` |

### Registry entries (9)

- `restrictions/twilightforest:dark_forest`: `{"advancements":["twilightforest:progress_lich"],"enforcement":"twilightforest:darkness","locked_biome_toast":{"count":1,"id":"twilightforest:lich_tower_miniature_structure"},"multiplier":0.0,"structure_key":"twilightforest:knight_stronghold"}`
- `restrictions/twilightforest:dark_forest_center`: `{"advancements":["twilightforest:progress_knights"],"enforcement":"twilightforest:darkness","locked_biome_toast":{"count":1,"id":"twilightforest:knight_phantom_trophy"},"multiplier":0.0,"structure_key":"twilightforest:dark_tower"}`
- `restrictions/twilightforest:final_plateau`: `{"advancements":["twilightforest:progress_troll"],"enforcement":"twilightforest:acid_rain","locked_biome_toast":{"count":1,"id":"twilightforest:lamp_of_cinders"},"multiplier":1.5,"structure_key":"twilightforest:final_castle"}`
- `restrictions/twilightforest:fire_swamp`: `{"advancements":["twilightforest:progress_labyrinth"],"enforcement":"twilightforest:fire","locked_biome_toast":{"count":1,"id":"twilightforest:meef_stroganoff"},"multiplier":8.0,"structure_key":"twilightforest:hydra_lair"}`
- `restrictions/twilightforest:glacier`: `{"advancements":["twilightforest:progress_yeti"],"enforcement":"twilightforest:frost","locked_biome_toast":{"count":1,"id":"twilightforest:alpha_yeti_fur"},"multiplier":1.0,"structure_key":"twilightforest:aurora_palace"}`
- `restrictions/twilightforest:highlands`: `{"advancements":["twilightforest:progress_merge"],"enforcement":"twilightforest:acid_rain","locked_biome_toast":{"count":1,"id":"twilightforest:uberous_soil"},"multiplier":0.5,"structure_key":"twilightforest:troll_cave"}`
- `restrictions/twilightforest:snowy_forest`: `{"advancements":["twilightforest:progress_lich"],"enforcement":"twilightforest:frost","locked_biome_toast":{"count":1,"id":"twilightforest:lich_tower_miniature_structure"},"multiplier":0.0,"structure_key":"twilightforest:yeti_cave"}`
- `restrictions/twilightforest:swamp`: `{"advancements":["twilightforest:progress_lich"],"enforcement":"twilightforest:hunger","locked_biome_toast":{"count":1,"id":"twilightforest:lich_tower_miniature_structure"},"multiplier":1.0,"structure_key":"twilightforest:labyrinth"}`
- `restrictions/twilightforest:thornlands`: `{"advancements":["twilightforest:progress_troll"],"enforcement":"twilightforest:acid_rain","locked_biome_toast":{"count":1,"id":"twilightforest:lamp_of_cinders"},"multiplier":1.0,"structure_key":"twilightforest:final_castle"}`

Registries: `restrictions` (`data/<ns>/twilight/restrictions/`)

### Config keys (1)

See [config.md](config.md) for the section mapping; defaults and translation ids below are TF's.

| TF key | Default | Translation id | English (config screen) |
|---|---|---|---|
| `client.disableLockedBiomeToasts` | `false` | `locked_toasts` | Disable Locked Biome Toasts — Disables the toasts that appear when entering a locked biome. Not recommended if you're unfamiliar with progression. |

### Advancements (45)

TF's generated advancement JSON (`data/twilightforest/advancement/`), every field: parent, display (icon, frame, flags, background), criteria with their trigger conditions, requirements and rewards. Title and description texts are the en_us strings of the display keys.

#### `twilightforest:arborist`

- Title: `advancement.twilightforest.arborist` = "Maniacal Dendrologist"; description: `advancement.twilightforest.arborist.desc` = "Get your axes and shears ready. Search every nook and cranny and get anything and everything that comes from trees! Craft, loot, obtain every slab... sapling... leaves... more... everything. ALL OF IT!"
- Parent: `twilightforest:root`; icon: `twilight_oak_fence`; frame=challenge
- Criterion `beanstalk_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:beanstalk_leaves"}]}`
- Criterion `canopy_banister`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_banister"}]}`
- Criterion `canopy_boat`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_boat"}]}`
- Criterion `canopy_bookshelf`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_bookshelf"}]}`
- Criterion `canopy_button`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_button"}]}`
- Criterion `canopy_chest`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_chest"}]}`
- Criterion `canopy_chest_boat`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_chest_boat"}]}`
- Criterion `canopy_door`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_door"}]}`
- Criterion `canopy_drying_rack`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_drying_rack"}]}`
- Criterion `canopy_fence`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_fence"}]}`
- Criterion `canopy_fence_gate`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_fence_gate"}]}`
- Criterion `canopy_hanging_sign`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_hanging_sign"}]}`
- Criterion `canopy_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_leaves"}]}`
- Criterion `canopy_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_log"}]}`
- Criterion `canopy_planks`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_planks"}]}`
- Criterion `canopy_pressure_plate`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_pressure_plate"}]}`
- Criterion `canopy_sapling`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_sapling"}]}`
- Criterion `canopy_sign`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_sign"}]}`
- Criterion `canopy_slab`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_slab"}]}`
- Criterion `canopy_stairs`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_stairs"}]}`
- Criterion `canopy_trapdoor`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_trapdoor"}]}`
- Criterion `canopy_trapped_chest`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_trapped_chest"}]}`
- Criterion `canopy_wood`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:canopy_wood"}]}`
- Criterion `chiseled_canopy_bookshelf`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:chiseled_canopy_bookshelf"}]}`
- Criterion `cracked_towerwood`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:cracked_towerwood"}]}`
- Criterion `dark_banister`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:dark_banister"}]}`
- Criterion `dark_boat`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:dark_boat"}]}`
- Criterion `dark_button`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:dark_button"}]}`
- Criterion `dark_chest`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:dark_chest"}]}`
- Criterion `dark_chest_boat`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:dark_chest_boat"}]}`
- Criterion `dark_door`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:dark_door"}]}`
- Criterion `dark_drying_rack`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:dark_drying_rack"}]}`
- Criterion `dark_fence`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:dark_fence"}]}`
- Criterion `dark_fence_gate`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:dark_fence_gate"}]}`
- Criterion `dark_hanging_sign`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:dark_hanging_sign"}]}`
- Criterion `dark_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:dark_leaves"}]}`
- Criterion `dark_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:dark_log"}]}`
- Criterion `dark_planks`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:dark_planks"}]}`
- Criterion `dark_pressure_plate`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:dark_pressure_plate"}]}`
- Criterion `dark_sign`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:dark_sign"}]}`
- Criterion `dark_slab`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:dark_slab"}]}`
- Criterion `dark_stairs`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:dark_stairs"}]}`
- Criterion `dark_trapdoor`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:dark_trapdoor"}]}`
- Criterion `dark_trapped_chest`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:dark_trapped_chest"}]}`
- Criterion `dark_wood`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:dark_wood"}]}`
- Criterion `darkwood_sapling`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:darkwood_sapling"}]}`
- Criterion `encased_towerwood`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:encased_towerwood"}]}`
- Criterion `fallen_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:fallen_leaves"}]}`
- Criterion `giant_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:giant_leaves"}]}`
- Criterion `giant_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:giant_log"}]}`
- Criterion `hedge`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:hedge"}]}`
- Criterion `hollow_canopy_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:hollow_canopy_log"}]}`
- Criterion `hollow_dark_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:hollow_dark_log"}]}`
- Criterion `hollow_mangrove_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:hollow_mangrove_log"}]}`
- Criterion `hollow_mining_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:hollow_mining_log"}]}`
- Criterion `hollow_oak_sapling`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:hollow_oak_sapling"}]}`
- Criterion `hollow_sorting_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:hollow_sorting_log"}]}`
- Criterion `hollow_time_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:hollow_time_log"}]}`
- Criterion `hollow_transformation_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:hollow_transformation_log"}]}`
- Criterion `hollow_twilight_oak_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:hollow_twilight_oak_log"}]}`
- Criterion `huge_stalk`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:huge_stalk"}]}`
- Criterion `liveroot`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:liveroot"}]}`
- Criterion `liveroot_block`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:liveroot_block"}]}`
- Criterion `mangrove_banister`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_banister"}]}`
- Criterion `mangrove_boat`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_boat"}]}`
- Criterion `mangrove_button`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_button"}]}`
- Criterion `mangrove_chest`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_chest"}]}`
- Criterion `mangrove_chest_boat`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_chest_boat"}]}`
- Criterion `mangrove_door`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_door"}]}`
- Criterion `mangrove_drying_rack`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_drying_rack"}]}`
- Criterion `mangrove_fence`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_fence"}]}`
- Criterion `mangrove_fence_gate`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_fence_gate"}]}`
- Criterion `mangrove_hanging_sign`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_hanging_sign"}]}`
- Criterion `mangrove_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_leaves"}]}`
- Criterion `mangrove_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_log"}]}`
- Criterion `mangrove_planks`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_planks"}]}`
- Criterion `mangrove_pressure_plate`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_pressure_plate"}]}`
- Criterion `mangrove_root`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_root"}]}`
- Criterion `mangrove_sapling`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_sapling"}]}`
- Criterion `mangrove_sign`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_sign"}]}`
- Criterion `mangrove_slab`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_slab"}]}`
- Criterion `mangrove_stairs`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_stairs"}]}`
- Criterion `mangrove_trapdoor`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_trapdoor"}]}`
- Criterion `mangrove_trapped_chest`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_trapped_chest"}]}`
- Criterion `mangrove_wood`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mangrove_wood"}]}`
- Criterion `mining_banister`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mining_banister"}]}`
- Criterion `mining_boat`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mining_boat"}]}`
- Criterion `mining_button`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mining_button"}]}`
- Criterion `mining_chest`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mining_chest"}]}`
- Criterion `mining_chest_boat`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mining_chest_boat"}]}`
- Criterion `mining_door`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mining_door"}]}`
- Criterion `mining_drying_rack`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mining_drying_rack"}]}`
- Criterion `mining_fence`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mining_fence"}]}`
- Criterion `mining_fence_gate`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mining_fence_gate"}]}`
- Criterion `mining_hanging_sign`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mining_hanging_sign"}]}`
- Criterion `mining_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mining_leaves"}]}`
- Criterion `mining_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mining_log"}]}`
- Criterion `mining_planks`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mining_planks"}]}`
- Criterion `mining_pressure_plate`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mining_pressure_plate"}]}`
- Criterion `mining_sapling`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mining_sapling"}]}`
- Criterion `mining_sign`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mining_sign"}]}`
- Criterion `mining_slab`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mining_slab"}]}`
- Criterion `mining_stairs`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mining_stairs"}]}`
- Criterion `mining_trapdoor`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mining_trapdoor"}]}`
- Criterion `mining_trapped_chest`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mining_trapped_chest"}]}`
- Criterion `mining_wood`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mining_wood"}]}`
- Criterion `mossy_towerwood`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mossy_towerwood"}]}`
- Criterion `rainbow_oak_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:rainbow_oak_leaves"}]}`
- Criterion `rainbow_oak_sapling`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:rainbow_oak_sapling"}]}`
- Criterion `root`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:root"}]}`
- Criterion `root_strand`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:root_strand"}]}`
- Criterion `sorting_banister`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:sorting_banister"}]}`
- Criterion `sorting_boat`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:sorting_boat"}]}`
- Criterion `sorting_button`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:sorting_button"}]}`
- Criterion `sorting_chest`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:sorting_chest"}]}`
- Criterion `sorting_chest_boat`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:sorting_chest_boat"}]}`
- Criterion `sorting_door`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:sorting_door"}]}`
- Criterion `sorting_drying_rack`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:sorting_drying_rack"}]}`
- Criterion `sorting_fence`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:sorting_fence"}]}`
- Criterion `sorting_fence_gate`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:sorting_fence_gate"}]}`
- Criterion `sorting_hanging_sign`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:sorting_hanging_sign"}]}`
- Criterion `sorting_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:sorting_leaves"}]}`
- Criterion `sorting_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:sorting_log"}]}`
- Criterion `sorting_planks`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:sorting_planks"}]}`
- Criterion `sorting_pressure_plate`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:sorting_pressure_plate"}]}`
- Criterion `sorting_sapling`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:sorting_sapling"}]}`
- Criterion `sorting_sign`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:sorting_sign"}]}`
- Criterion `sorting_slab`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:sorting_slab"}]}`
- Criterion `sorting_stairs`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:sorting_stairs"}]}`
- Criterion `sorting_trapdoor`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:sorting_trapdoor"}]}`
- Criterion `sorting_trapped_chest`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:sorting_trapped_chest"}]}`
- Criterion `sorting_wood`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:sorting_wood"}]}`
- Criterion `stripped_canopy_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:stripped_canopy_log"}]}`
- Criterion `stripped_canopy_wood`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:stripped_canopy_wood"}]}`
- Criterion `stripped_dark_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:stripped_dark_log"}]}`
- Criterion `stripped_dark_wood`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:stripped_dark_wood"}]}`
- Criterion `stripped_mangrove_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:stripped_mangrove_log"}]}`
- Criterion `stripped_mangrove_wood`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:stripped_mangrove_wood"}]}`
- Criterion `stripped_mining_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:stripped_mining_log"}]}`
- Criterion `stripped_mining_wood`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:stripped_mining_wood"}]}`
- Criterion `stripped_sorting_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:stripped_sorting_log"}]}`
- Criterion `stripped_sorting_wood`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:stripped_sorting_wood"}]}`
- Criterion `stripped_time_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:stripped_time_log"}]}`
- Criterion `stripped_time_wood`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:stripped_time_wood"}]}`
- Criterion `stripped_transformation_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:stripped_transformation_log"}]}`
- Criterion `stripped_transformation_wood`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:stripped_transformation_wood"}]}`
- Criterion `stripped_twilight_oak_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:stripped_twilight_oak_log"}]}`
- Criterion `stripped_twilight_oak_wood`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:stripped_twilight_oak_wood"}]}`
- Criterion `thorn_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:thorn_leaves"}]}`
- Criterion `thorn_rose`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:thorn_rose"}]}`
- Criterion `time_banister`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:time_banister"}]}`
- Criterion `time_boat`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:time_boat"}]}`
- Criterion `time_button`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:time_button"}]}`
- Criterion `time_chest`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:time_chest"}]}`
- Criterion `time_chest_boat`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:time_chest_boat"}]}`
- Criterion `time_door`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:time_door"}]}`
- Criterion `time_drying_rack`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:time_drying_rack"}]}`
- Criterion `time_fence`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:time_fence"}]}`
- Criterion `time_fence_gate`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:time_fence_gate"}]}`
- Criterion `time_hanging_sign`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:time_hanging_sign"}]}`
- Criterion `time_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:time_leaves"}]}`
- Criterion `time_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:time_log"}]}`
- Criterion `time_planks`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:time_planks"}]}`
- Criterion `time_pressure_plate`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:time_pressure_plate"}]}`
- Criterion `time_sapling`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:time_sapling"}]}`
- Criterion `time_sign`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:time_sign"}]}`
- Criterion `time_slab`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:time_slab"}]}`
- Criterion `time_stairs`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:time_stairs"}]}`
- Criterion `time_trapdoor`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:time_trapdoor"}]}`
- Criterion `time_trapped_chest`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:time_trapped_chest"}]}`
- Criterion `time_wood`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:time_wood"}]}`
- Criterion `towerwood`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:towerwood"}]}`
- Criterion `transformation_banister`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:transformation_banister"}]}`
- Criterion `transformation_boat`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:transformation_boat"}]}`
- Criterion `transformation_button`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:transformation_button"}]}`
- Criterion `transformation_chest`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:transformation_chest"}]}`
- Criterion `transformation_chest_boat`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:transformation_chest_boat"}]}`
- Criterion `transformation_door`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:transformation_door"}]}`
- Criterion `transformation_drying_rack`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:transformation_drying_rack"}]}`
- Criterion `transformation_fence`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:transformation_fence"}]}`
- Criterion `transformation_fence_gate`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:transformation_fence_gate"}]}`
- Criterion `transformation_hanging_sign`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:transformation_hanging_sign"}]}`
- Criterion `transformation_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:transformation_leaves"}]}`
- Criterion `transformation_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:transformation_log"}]}`
- Criterion `transformation_planks`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:transformation_planks"}]}`
- Criterion `transformation_pressure_plate`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:transformation_pressure_plate"}]}`
- Criterion `transformation_sapling`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:transformation_sapling"}]}`
- Criterion `transformation_sign`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:transformation_sign"}]}`
- Criterion `transformation_slab`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:transformation_slab"}]}`
- Criterion `transformation_stairs`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:transformation_stairs"}]}`
- Criterion `transformation_trapdoor`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:transformation_trapdoor"}]}`
- Criterion `transformation_trapped_chest`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:transformation_trapped_chest"}]}`
- Criterion `transformation_wood`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:transformation_wood"}]}`
- Criterion `twilight_oak_banister`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_oak_banister"}]}`
- Criterion `twilight_oak_boat`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_oak_boat"}]}`
- Criterion `twilight_oak_button`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_oak_button"}]}`
- Criterion `twilight_oak_chest`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_oak_chest"}]}`
- Criterion `twilight_oak_chest_boat`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_oak_chest_boat"}]}`
- Criterion `twilight_oak_door`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_oak_door"}]}`
- Criterion `twilight_oak_drying_rack`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_oak_drying_rack"}]}`
- Criterion `twilight_oak_fence`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_oak_fence"}]}`
- Criterion `twilight_oak_fence_gate`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_oak_fence_gate"}]}`
- Criterion `twilight_oak_hanging_sign`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_oak_hanging_sign"}]}`
- Criterion `twilight_oak_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_oak_leaves"}]}`
- Criterion `twilight_oak_log`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_oak_log"}]}`
- Criterion `twilight_oak_planks`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_oak_planks"}]}`
- Criterion `twilight_oak_pressure_plate`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_oak_pressure_plate"}]}`
- Criterion `twilight_oak_sapling`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_oak_sapling"}]}`
- Criterion `twilight_oak_sign`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_oak_sign"}]}`
- Criterion `twilight_oak_slab`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_oak_slab"}]}`
- Criterion `twilight_oak_stairs`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_oak_stairs"}]}`
- Criterion `twilight_oak_trapdoor`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_oak_trapdoor"}]}`
- Criterion `twilight_oak_trapped_chest`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_oak_trapped_chest"}]}`
- Criterion `twilight_oak_wood`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_oak_wood"}]}`
- Requirements: `[["twilight_oak_log"],["twilight_oak_wood"],["stripped_twilight_oak_log"],["stripped_twilight_oak_wood"],["hollow_twilight_oak_log"],["twilight_oak_leaves"],["twilight_oak_sapling"],["twilight_oak_planks"],["twilight_oak_slab"],["twilight_oak_stairs"],["twilight_oak_button"],["twilight_oak_fence"],["twilight_oak_fence_gate"],["twilight_oak_pressure_plate"],["twilight_oak_door"],["twilight_oak_trapdoor"],["twilight_oak_sign"],["twilight_oak_hanging_sign"],["twilight_oak_chest"],["twilight_oak_trapped_chest"],["twilight_oak_banister"],["twilight_oak_drying_rack"],["twilight_oak_boat"],["twilight_oak_chest_boat"],["canopy_log"],["canopy_wood"],["stripped_canopy_log"],["stripped_canopy_wood"],["hollow_canopy_log"],["canopy_leaves"],["canopy_sapling"],["canopy_planks"],["canopy_slab"],["canopy_stairs"],["canopy_button"],["canopy_fence"],["canopy_fence_gate"],["canopy_pressure_plate"],["canopy_door"],["canopy_trapdoor"],["canopy_sign"],["canopy_hanging_sign"],["canopy_chest"],["canopy_trapped_chest"],["canopy_banister"],["canopy_drying_rack"],["canopy_bookshelf"],["chiseled_canopy_bookshelf"],["canopy_boat"],["canopy_chest_boat"],["mangrove_log"],["mangrove_wood"],["stripped_mangrove_log"],["stripped_mangrove_wood"],["hollow_mangrove_log"],["mangrove_leaves"],["mangrove_sapling"],["mangrove_planks"],["mangrove_slab"],["mangrove_stairs"],["mangrove_button"],["mangrove_fence"],["mangrove_fence_gate"],["mangrove_pressure_plate"],["mangrove_door"],["mangrove_trapdoor"],["mangrove_sign"],["mangrove_hanging_sign"],["mangrove_chest"],["mangrove_trapped_chest"],["mangrove_banister"],["mangrove_drying_rack"],["mangrove_boat"],["mangrove_chest_boat"],["dark_log"],["dark_wood"],["stripped_dark_log"],["stripped_dark_wood"],["hollow_dark_log"],["dark_leaves"],["darkwood_sapling"],["dark_planks"],["dark_slab"],["dark_stairs"],["dark_button"],["dark_fence"],["dark_fence_gate"],["dark_pressure_plate"],["dark_door"],["dark_trapdoor"],["dark_sign"],["dark_hanging_sign"],["dark_chest"],["dark_trapped_chest"],["dark_banister"],["dark_drying_rack"],["dark_boat"],["dark_chest_boat"],["time_log"],["time_wood"],["stripped_time_log"],["stripped_time_wood"],["hollow_time_log"],["time_leaves"],["time_sapling"],["time_planks"],["time_slab"],["time_stairs"],["time_button"],["time_fence"],["time_fence_gate"],["time_pressure_plate"],["time_door"],["time_trapdoor"],["time_sign"],["time_hanging_sign"],["time_chest"],["time_trapped_chest"],["time_banister"],["time_drying_rack"],["time_boat"],["time_chest_boat"],["transformation_log"],["transformation_wood"],["stripped_transformation_log"],["stripped_transformation_wood"],["hollow_transformation_log"],["transformation_leaves"],["transformation_sapling"],["transformation_planks"],["transformation_slab"],["transformation_stairs"],["transformation_button"],["transformation_fence"],["transformation_fence_gate"],["transformation_pressure_plate"],["transformation_door"],["transformation_trapdoor"],["transformation_sign"],["transformation_hanging_sign"],["transformation_chest"],["transformation_trapped_chest"],["transformation_banister"],["transformation_drying_rack"],["transformation_boat"],["transformation_chest_boat"],["mining_log"],["mining_wood"],["stripped_mining_log"],["stripped_mining_wood"],["hollow_mining_log"],["mining_leaves"],["mining_sapling"],["mining_planks"],["mining_slab"],["mining_stairs"],["mining_button"],["mining_fence"],["mining_fence_gate"],["mining_pressure_plate"],["mining_door"],["mining_trapdoor"],["mining_sign"],["mining_hanging_sign"],["mining_chest"],["mining_trapped_chest"],["mining_banister"],["mining_drying_rack"],["mining_boat"],["mining_chest_boat"],["sorting_log"],["sorting_wood"],["stripped_sorting_log"],["stripped_sorting_wood"],["hollow_sorting_log"],["sorting_leaves"],["sorting_sapling"],["sorting_planks"],["sorting_slab"],["sorting_stairs"],["sorting_button"],["sorting_fence"],["sorting_fence_gate"],["sorting_pressure_plate"],["sorting_door"],["sorting_trapdoor"],["sorting_sign"],["sorting_hanging_sign"],["sorting_chest"],["sorting_trapped_chest"],["sorting_banister"],["sorting_drying_rack"],["sorting_boat"],["sorting_chest_boat"],["towerwood"],["cracked_towerwood"],["mossy_towerwood"],["encased_towerwood"],["root"],["root_strand"],["liveroot_block"],["liveroot"],["hollow_oak_sapling"],["rainbow_oak_sapling"],["rainbow_oak_leaves"],["giant_log"],["giant_leaves"],["huge_stalk"],["beanstalk_leaves"],["thorn_leaves"],["thorn_rose"],["hedge"],["fallen_leaves"],["mangrove_root"]]`
- Rewards: `{"experience":1000}`
- sends_telemetry_event: True

#### `twilightforest:arctic_armor_dyed`

- Title: `advancement.twilightforest.arctic_dyed` = "Getting in Fashion"; description: `advancement.twilightforest.arctic_dyed.desc` = "Dye four unique pieces of Arctic armor"
- Parent: `twilightforest:progress_yeti`; icon: `arctic_chestplate`; 
- Criterion `boots`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:arctic_boots","predicates":{"twilightforest:color":{}}}]}`
- Criterion `chestplate`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:arctic_chestplate","predicates":{"twilightforest:color":{}}}]}`
- Criterion `helmet`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:arctic_helmet","predicates":{"twilightforest:color":{}}}]}`
- Criterion `leggings`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:arctic_leggings","predicates":{"twilightforest:color":{}}}]}`
- Requirements: `[["helmet"],["chestplate"],["leggings"],["boots"]]`
- Rewards: `{"experience":25}`
- sends_telemetry_event: True

#### `twilightforest:beanstalk`

- Title: `advancement.twilightforest.beanstalk` = "Jack and the Beanstalk"; description: `advancement.twilightforest.beanstalk.desc` = "Obtain some %s in the troll caves and use them on the glowing soil beneath the clouds"
- Parent: `twilightforest:progress_merge`; icon: `huge_stalk`; frame=goal
- Criterion `beans`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:magic_beans"}]}`
- Criterion `use_beans`: `minecraft:item_used_on_block` `{"location":[{"condition":"minecraft:location_check","predicate":{"block":{"blocks":"twilightforest:uberous_soil"}}},{"condition":"minecraft:match_tool","predicate":{"items":"twilightforest:magic_beans"}}]}`
- Requirements: `[["beans"],["use_beans"]]`
- sends_telemetry_event: True

#### `twilightforest:break_glass_sword`

- Title: `advancement.twilightforest.glass_sword` = "One Hit Wonder"; description: `advancement.twilightforest.glass_sword.desc` = "Hope you made good use of that."
- Parent: `twilightforest:progress_yeti`; icon: `glass_sword`; frame=challenge, hidden=True
- Criterion `broken_sword`: `twilightforest:broke_glass_sword` `{}`
- Requirements: `[["broken_sword"]]`
- Rewards: `{"experience":42}`
- sends_telemetry_event: True

#### `twilightforest:chicken_jerky`

- Title: `advancement.twilightforest.chicken_jerky` = "CHICKEN JERKEY!"; description: `` = ""
- Parent: `twilightforest:root`; icon: `chicken_jerky`; frame=challenge, hidden=True
- Criterion `jerky`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"minecraft:zombie","flags":{"is_baby":true},"vehicle":{"type":"minecraft:chicken"}}}],"killing_blow":{"direct_entity":{"equipment":{"mainhand":{"items":"twilightforest:chicken_jerky"}}}}}`
- Requirements: `[["jerky"]]`
- sends_telemetry_event: True

#### `twilightforest:craft_travellers_gear`

- Title: `advancement.twilightforest.craft_travellers_gear` = "Around the Forest in 80 Days"; description: `advancement.twilightforest.craft_travellers_gear.desc` = "Craft a piece of traveller's gear"
- Parent: `twilightforest:root`; icon: `travellers_boots`; 
- Criterion `craft_belt`: `minecraft:recipe_crafted` `{"recipe_id":"twilightforest:equipment/travellers_belt"}`
- Criterion `craft_boots`: `minecraft:recipe_crafted` `{"recipe_id":"twilightforest:equipment/travellers_boots"}`
- Criterion `craft_gloves`: `minecraft:recipe_crafted` `{"recipe_id":"twilightforest:equipment/travellers_gloves"}`
- Criterion `craft_goggles`: `minecraft:recipe_crafted` `{"recipe_id":"twilightforest:equipment/travellers_goggles"}`
- Criterion `craft_vest`: `minecraft:recipe_crafted` `{"recipe_id":"twilightforest:equipment/travellers_chest"}`
- Criterion `craft_wings`: `minecraft:recipe_crafted` `{"recipe_id":"twilightforest:equipment/travellers_wings"}`
- Requirements: `[["craft_goggles","craft_vest","craft_wings","craft_boots","craft_belt","craft_gloves"]]`
- sends_telemetry_event: True

#### `twilightforest:experiment_115`

- Title: `advancement.twilightforest.experiment_115` = "Mystery Meat?"; description: `advancement.twilightforest.experiment_115.desc` = "It looks like cake, though..."
- Parent: `twilightforest:progress_knights`; icon: `experiment_115`; 
- Criterion `eat_experiment_115`: `minecraft:consume_item` `{"item":{"items":"twilightforest:experiment_115"}}`
- Requirements: `[["eat_experiment_115"]]`
- sends_telemetry_event: True

#### `twilightforest:experiment_115_115`

- Title: `advancement.twilightforest.experiment_115_3` = "Eating 115 Everyday, 115 Years, Forever"; description: `advancement.twilightforest.experiment_115_3.desc` = "eating experiment one-hundred-fifteen all day, forever, one-hundred-fifteen times experiment one-hundred-fifteen, experiment one-hundred-fifteen dot com, double-u double-u double-u dot experiment one-hundred-fifteen dot com, one-hundred-fifteen years, every minute experiment one-hundred-fifteen dot com, double-u double-u double-u dot one-hundred-fifteen times experiment one-hundred-fifteen dot com"
- Parent: `twilightforest:experiment_115`; icon: `experiment_115 {"twilightforest:e115_variant":"think"}`; frame=challenge, hidden=True
- Criterion `eat_115_e115`: `minecraft:location` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","stats":[{"type":"minecraft:custom","stat":"twilightforest:e115_slices_eaten","value":{"min":115}}]}}}]}`
- Requirements: `[["eat_115_e115"]]`
- sends_telemetry_event: True

#### `twilightforest:experiment_115_self_replenishing`

- Title: `advancement.twilightforest.experiment_115_2` = "Making a note: Huge Success!"; description: `advancement.twilightforest.experiment_115_2.desc` = "It's so delicious and moist!"
- Parent: `twilightforest:experiment_115`; icon: `experiment_115 {"twilightforest:e115_variant":"full"}`; frame=challenge, hidden=True
- Criterion `place_complete_e115`: `minecraft:item_used_on_block` `{"location":[{"condition":"minecraft:location_check","predicate":{"block":{"blocks":"twilightforest:experiment_115","state":{"regenerate":"true"}}}},{"condition":"minecraft:match_tool","predicate":{"items":"minecraft:redstone"}}]}`
- Requirements: `[["place_complete_e115"]]`
- sends_telemetry_event: True

#### `twilightforest:fiery_set`

- Title: `advancement.twilightforest.fiery_set` = "Gallons of Blood and Tears"; description: `advancement.twilightforest.fiery_set.desc` = "Wield a fiery tool or weapon while having at least one piece of fiery armor in your inventory"
- Parent: `twilightforest:progress_hydra`; icon: `fiery_sword`; frame=challenge
- Criterion `fiery_boots`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:fiery_boots"}]}`
- Criterion `fiery_chestplate`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:fiery_chestplate"}]}`
- Criterion `fiery_helmet`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:fiery_helmet"}]}`
- Criterion `fiery_leggings`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:fiery_leggings"}]}`
- Criterion `fiery_pick`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:fiery_pickaxe"}]}`
- Criterion `fiery_sword`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:fiery_sword"}]}`
- Requirements: `[["fiery_pick","fiery_sword"],["fiery_helmet","fiery_chestplate","fiery_leggings","fiery_boots"]]`
- Rewards: `{"experience":75}`
- sends_telemetry_event: True

#### `twilightforest:full_mettle_alchemist`

- Title: `advancement.twilightforest.full_mettle_alchemist` = "Full Mettle Alchemist"; description: `advancement.twilightforest.full_mettle_alchemist.desc` = "Drink three doses of Harming II from a potion flask in under 6 seconds and survive"
- Parent: `twilightforest:progress_lich`; icon: `brittle_potion_flask {"twilightforest:flask_contents":{"breakable":false,"doses":4,"potion":{"potion":"minecraft:strong_harming"}}}`; frame=challenge, hidden=True
- Criterion `drink_4_harming`: `twilightforest:drink_from_flask` `{"doses":3,"potion":"minecraft:strong_harming","seconds":{"max":6}}`
- Requirements: `[["drink_4_harming"]]`
- Rewards: `{"experience":100}`
- sends_telemetry_event: True

#### `twilightforest:ghast_trap`

- Title: `advancement.twilightforest.ghast_trap` = "Something Strange in Towerwood"; description: `advancement.twilightforest.ghast_trap.desc` = "Slay the %ss around a %s and activate to wrench the %s from the sky!"
- Parent: `twilightforest:progress_knights`; icon: `ghast_trap`; 
- Criterion `activate_ghast_trap`: `twilightforest:activate_ghast_trap` `{}`
- Requirements: `[["activate_ghast_trap"]]`
- sends_telemetry_event: True

#### `twilightforest:giants`

- Title: `advancement.twilightforest.giants` = "I'm on Cloud Nine"; description: `advancement.twilightforest.giants.desc` = "Kill a %s in the clouds, retrieve a %s, and return back to the troll caves"
- Parent: `twilightforest:beanstalk`; icon: `giant_pickaxe`; frame=goal
- Criterion `giant`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:giant_miner"}}]}`
- Criterion `pickaxe`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:magic_beans"}]}`
- Requirements: `[["giant"],["pickaxe"]]`
- sends_telemetry_event: True

#### `twilightforest:hedge`

- Title: `advancement.twilightforest.hedge` = "Bug Stomper"; description: `advancement.twilightforest.hedge.desc` = "Defeat a spider in a Hedge Maze"
- Parent: `twilightforest:root`; icon: `hedge`; 
- Criterion `hedge_spider`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:hedge_spider","location":{"structures":"twilightforest:hedge_maze"}}}]}`
- Criterion `swarm_spider`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:swarm_spider","location":{"structures":"twilightforest:hedge_maze"}}}]}`
- Requirements: `[["hedge_spider","swarm_spider"]]`
- sends_telemetry_event: True

#### `twilightforest:hill1`

- Title: `advancement.twilightforest.hill1` = "The Boots Are Mine!"; description: `advancement.twilightforest.hill1.desc` = "Defeat a %s in a Small Hollow Hill"
- Parent: `twilightforest:root`; icon: `mc:iron_boots`; 
- Criterion `redcap`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:redcap","location":{"structures":"twilightforest:small_hollow_hill"}}}]}`
- Requirements: `[["redcap"]]`
- sends_telemetry_event: True

#### `twilightforest:hill2`

- Title: `advancement.twilightforest.hill2` = "What Was That Noise?"; description: `advancement.twilightforest.hill2.desc` = "Defeat a %s in a Medium Hollow Hill"
- Parent: `twilightforest:hill1`; icon: `ironwood_pickaxe`; 
- Criterion `redcap`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:redcap_sapper","location":{"structures":"twilightforest:medium_hollow_hill"}}}]}`
- Requirements: `[["redcap"]]`
- sends_telemetry_event: True

#### `twilightforest:hill3`

- Title: `advancement.twilightforest.hill3` = "I See Right Through You"; description: `advancement.twilightforest.hill3.desc` = "Defeat a %s in a Large Hollow Hill"
- Parent: `twilightforest:hill2`; icon: `mc:glowstone_dust`; 
- Criterion `redcap`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:wraith","location":{"structures":"twilightforest:large_hollow_hill"}}}]}`
- Requirements: `[["redcap"]]`
- sends_telemetry_event: True

#### `twilightforest:hydra_chop`

- Title: `advancement.twilightforest.hydra_chop` = "Hydra Chop, Baby!"; description: `advancement.twilightforest.hydra_chop.desc` = "Chow down on a %s when your hunger bar is depleted"
- Parent: `twilightforest:progress_hydra`; icon: `hydra_chop`; 
- Criterion `hydra_chop`: `twilightforest:consume_hydra_chop_on_low_hunger` `{}`
- Requirements: `[["hydra_chop"]]`
- sends_telemetry_event: True

#### `twilightforest:kill_cicada`

- Title: `advancement.twilightforest.kill_cicada` = "Shut"; description: `advancement.twilightforest.kill_cicada.desc` = "Kill a Cicada"
- Parent: `twilightforest:root`; icon: `cicada`; announce_to_chat=False, hidden=True
- Criterion `kill_cicada`: `twilightforest:kill_bug` `{"bug":"twilightforest:cicada"}`
- Requirements: `[["kill_cicada"]]`
- sends_telemetry_event: True

#### `twilightforest:lich_scepters`

- Title: `advancement.twilightforest.lich_scepters` = "By Our Powers Combined!"; description: `advancement.twilightforest.lich_scepters.desc` = "Acquire all four scepters of power"
- Parent: `twilightforest:progress_lich`; icon: `zombie_scepter`; frame=challenge, hidden=True
- Criterion `scepters`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:lifedrain_scepter"},{"items":"twilightforest:twilight_scepter"},{"items":"twilightforest:zombie_scepter"},{"items":"twilightforest:fortification_scepter"}]}`
- Requirements: `[["scepters"]]`
- Rewards: `{"experience":100}`
- sends_telemetry_event: True

#### `twilightforest:magic_map`

- Title: `advancement.twilightforest.magic_map` = "I Can See Forever"; description: `advancement.twilightforest.magic_map.desc` = "Craft the %s"
- Parent: `twilightforest:magic_map_focus`; icon: `filled_magic_map`; 
- Criterion `magic_map`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:filled_magic_map"}]}`
- Requirements: `[["magic_map"]]`
- sends_telemetry_event: True

#### `twilightforest:magic_map_focus`

- Title: `advancement.twilightforest.magic_map_focus` = "With Fire It Writes"; description: `advancement.twilightforest.magic_map_focus.desc` = "Craft the %s with a %s, %s, and %s"
- Parent: `twilightforest:twilight_hunter`; icon: `magic_map_focus`; 
- Criterion `focus`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:magic_map_focus"}]}`
- Requirements: `[["focus"]]`
- sends_telemetry_event: True

#### `twilightforest:maze_map`

- Title: `advancement.twilightforest.maze_map` = "And Now, to Find the Exit"; description: `advancement.twilightforest.maze_map.desc` = "Craft the %s after obtaining the focus from the labyrinth"
- Parent: `twilightforest:magic_map`; icon: `filled_maze_map`; frame=goal
- Criterion `maze_map`: `minecraft:recipe_crafted` `{"recipe_id":"twilightforest:maze_map"}`
- Requirements: `[["maze_map"]]`
- sends_telemetry_event: True

#### `twilightforest:mazebreaker`

- Title: `advancement.twilightforest.mazebreaker` = "Breaking the Maze"; description: `advancement.twilightforest.mazebreaker.desc` = "Find the %s pickaxe in the secret labyrinth vault"
- Parent: `twilightforest:progress_labyrinth`; icon: `mazebreaker_pickaxe`; frame=challenge
- Criterion `pick`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:mazebreaker_pickaxe"}]}`
- Requirements: `[["pick"]]`
- Rewards: `{"experience":50}`
- sends_telemetry_event: True

#### `twilightforest:modify_travellers_gear`

- Title: `advancement.twilightforest.modify_travellers_gear` = "Apparel Oft Proclaims the Man"; description: `advancement.twilightforest.modify_travellers_gear.desc` = "Add a modifier to a piece of traveller's gear"
- Parent: `twilightforest:craft_travellers_gear`; icon: `travellers_boots {"twilightforest:water_walk":{}}`; 
- Criterion `add_any_modifier`: `twilightforest:add_modifier` `{}`
- Requirements: `[["add_any_modifier"]]`
- sends_telemetry_event: True

#### `twilightforest:naga_armors`

- Title: `advancement.twilightforest.naga_armors` = "Naga Armorer"; description: `advancement.twilightforest.naga_armors.desc` = "Craft a %s and %s"
- Parent: `twilightforest:progress_naga`; icon: `naga_chestplate`; frame=challenge
- Criterion `chestplate`: `minecraft:recipe_crafted` `{"recipe_id":"twilightforest:equipment/naga_chestplate"}`
- Criterion `leggings`: `minecraft:recipe_crafted` `{"recipe_id":"twilightforest:equipment/naga_leggings"}`
- Requirements: `[["chestplate"],["leggings"]]`
- Rewards: `{"experience":25}`
- sends_telemetry_event: True

#### `twilightforest:ore_map`

- Title: `advancement.twilightforest.ore_map` = "How Can That Be Worth It?"; description: `advancement.twilightforest.ore_map.desc` = "Craft the %s"
- Parent: `twilightforest:maze_map`; icon: `filled_ore_map`; frame=challenge, hidden=True
- Criterion `ore_map`: `minecraft:recipe_crafted` `{"recipe_id":"twilightforest:ore_map"}`
- Requirements: `[["ore_map"]]`
- sends_telemetry_event: True

#### `twilightforest:progress_glacier`

- Title: `advancement.twilightforest.progress_glacier` = "Clear Skies"; description: `advancement.twilightforest.progress_glacier.desc` = "Defeat the %s atop the Aurora Palace"
- Parent: `twilightforest:progress_yeti`; icon: `snow_queen_trophy`; frame=goal
- Criterion `previous_progression`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"twilightforest:progress_yeti":true}}}}]}`
- Criterion `queen`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:snow_queen"}}]}`
- Criterion `trophy`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:snow_queen_trophy"}]}`
- Criterion `was_in_fight`: `twilightforest:hurt_boss` `{"hurt_entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:snow_queen"}}]}`
- Requirements: `[["queen","trophy","was_in_fight"],["previous_progression"]]`
- sends_telemetry_event: True

#### `twilightforest:progress_hydra`

- Title: `advancement.twilightforest.kill_hydra` = "Hydra Slayer"; description: `advancement.twilightforest.kill_hydra.desc` = "Defeat the mighty %s and empower yourself"
- Parent: `twilightforest:progress_labyrinth`; icon: `hydra_trophy`; frame=goal
- Criterion `kill_hydra`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:hydra"}}]}`
- Criterion `stroganoff`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"twilightforest:progress_labyrinth":true}}}}]}`
- Criterion `trophy`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:hydra_trophy"}]}`
- Criterion `was_in_fight`: `twilightforest:hurt_boss` `{"hurt_entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:hydra"}}]}`
- Requirements: `[["kill_hydra","trophy","was_in_fight"],["stroganoff"]]`
- sends_telemetry_event: True

#### `twilightforest:progress_knights`

- Title: `advancement.twilightforest.progress_knights` = "Carminite Acclimation"; description: `advancement.twilightforest.progress_knights.desc` = "Settle the restless phantoms in the knight's tomb and the carminite tower's devices will obey you"
- Parent: `twilightforest:progress_trophy_pedestal`; icon: `knight_phantom_trophy`; frame=goal
- Criterion `all_knights`: `twilightforest:kill_all_phantoms` `{}`
- Criterion `previous_progression`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"twilightforest:progress_trophy_pedestal":true}}}}]}`
- Criterion `trophy`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:knight_phantom_trophy"}]}`
- Requirements: `[["all_knights","trophy"],["previous_progression"]]`
- sends_telemetry_event: True

#### `twilightforest:progress_labyrinth`

- Title: `advancement.twilightforest.progress_labyrinth` = "Mighty Stroganoff"; description: `advancement.twilightforest.progress_labyrinth.desc` = "Eat the Meef Stroganoff to acclimatize your body to the dangerous heat of the Fire Swamp"
- Parent: `twilightforest:progress_lich`; icon: `minotaur_labyrinth_miniature_structure`; frame=goal
- Criterion `kill_lich`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"twilightforest:progress_lich":true}}}}]}`
- Criterion `meef`: `minecraft:consume_item` `{"item":{"items":"twilightforest:meef_stroganoff"}}`
- Requirements: `[["meef"],["kill_lich"]]`
- sends_telemetry_event: True

#### `twilightforest:progress_lich`

- Title: `advancement.twilightforest.kill_lich` = "Bring Out Your Dead"; description: `advancement.twilightforest.kill_lich.desc` = "Slay the %s at top of his tower and retrieve a scepter to clear poisonous mosquitoes from the Swamp, see through blinding darkness of the Dark Forest's curse, and resist the Snowy Forest's chill"
- Parent: `twilightforest:progress_naga`; icon: `lich_tower_miniature_structure`; frame=goal
- Criterion `kill_lich`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:lich"}}]}`
- Criterion `kill_naga`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"twilightforest:progress_naga":true}}}}]}`
- Criterion `lifedrain_scepter`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:lifedrain_scepter"}]}`
- Criterion `shield_scepter`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:fortification_scepter"}]}`
- Criterion `trophy`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:lich_trophy"}]}`
- Criterion `twilight_scepter`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:twilight_scepter"}]}`
- Criterion `was_in_fight`: `twilightforest:hurt_boss` `{"hurt_entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:lich"}}]}`
- Criterion `zombie_scepter`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:zombie_scepter"}]}`
- Requirements: `[["kill_lich","trophy","lifedrain_scepter","twilight_scepter","zombie_scepter","shield_scepter","was_in_fight"],["kill_naga"]]`
- sends_telemetry_event: True

#### `twilightforest:progress_merge`

- Title: `advancement.twilightforest.progress_merge` = "Ultimate Showdown"; description: `advancement.twilightforest.progress_merge.desc` = "Slay the %s, %s, and %s to clear the acid rain and embolden yourself for the Highlands"
- Parent: `twilightforest:progress_lich`; icon: `uberous_soil`; frame=goal
- Criterion `hydra`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"twilightforest:progress_hydra":true}}}}]}`
- Criterion `snow_queen`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"twilightforest:progress_glacier":true}}}}]}`
- Criterion `ur_ghast`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"twilightforest:progress_ur_ghast":true}}}}]}`
- Requirements: `[["hydra"],["ur_ghast"],["snow_queen"]]`
- sends_telemetry_event: True

#### `twilightforest:progress_naga`

- Title: `advancement.twilightforest.kill_naga` = "Time To Even The Scales"; description: `advancement.twilightforest.kill_naga.desc` = "Slay the %s in its forest courtyard and obtain a %s to overcome the barrier magic surrounding the Lich's tower"
- Parent: `twilightforest:root`; icon: `naga_courtyard_miniature_structure`; frame=goal
- Criterion `naga`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:naga"}}]}`
- Criterion `scale`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:naga_scale"}]}`
- Criterion `trophy`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:naga_trophy"}]}`
- Criterion `was_in_fight`: `twilightforest:hurt_boss` `{"hurt_entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:naga"}}]}`
- Requirements: `[["naga","trophy","scale","was_in_fight"]]`
- sends_telemetry_event: True

#### `twilightforest:progress_troll`

- Title: `advancement.twilightforest.progress_troll` = "I Wish For More Burning"; description: `advancement.twilightforest.progress_troll.desc` = "Find the %s in the troll caves, and you can burn away the thorn barriers"
- Parent: `twilightforest:giants`; icon: `lamp_of_cinders`; frame=goal
- Criterion `lamp`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:lamp_of_cinders"}]}`
- Criterion `previous_progression`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"twilightforest:progress_merge":true}}}}]}`
- Requirements: `[["lamp"],["previous_progression"]]`
- sends_telemetry_event: True

#### `twilightforest:progress_trophy_pedestal`

- Title: `advancement.twilightforest.progress_trophy_pedestal` = "Trophied Champion"; description: `advancement.twilightforest.progress_trophy_pedestal.desc` = "Claim your title by placing a trophy on the pedestal in the Dark Forest ruins"
- Parent: `twilightforest:progress_lich`; icon: `trophy_pedestal`; frame=goal
- Criterion `kill_lich`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"twilightforest:progress_lich":true}}}}]}`
- Criterion `trophy_pedestal`: `twilightforest:placed_on_trophy_pedestal` `{}`
- Requirements: `[["trophy_pedestal"],["kill_lich"]]`
- sends_telemetry_event: True

#### `twilightforest:progress_ur_ghast`

- Title: `advancement.twilightforest.progress_ur_ghast` = "Tears of Fire"; description: `advancement.twilightforest.progress_ur_ghast.desc` = "Touch the fiery red tears of the %s"
- Parent: `twilightforest:ghast_trap`; icon: `dark_tower_miniature_structure`; frame=goal
- Criterion `ghast`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:ur_ghast"}}]}`
- Criterion `previous_progression`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"twilightforest:progress_knights":true}}}}]}`
- Criterion `trophy`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:ur_ghast_trophy"}]}`
- Criterion `was_in_fight`: `twilightforest:hurt_boss` `{"hurt_entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:ur_ghast"}}]}`
- Requirements: `[["ghast","trophy","was_in_fight"],["previous_progression"]]`
- sends_telemetry_event: True

#### `twilightforest:progress_yeti`

- Title: `advancement.twilightforest.progress_yeti` = "Alpha Fur"; description: `advancement.twilightforest.progress_yeti.desc` = "Line your garments with the soft fur from the %s, keeping you safe from the Glacier's cold"
- Parent: `twilightforest:progress_lich`; icon: `alpha_yeti_fur`; frame=goal
- Criterion `fur`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:alpha_yeti_fur"}]}`
- Criterion `previous_progression`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"twilightforest:progress_lich":true}}}}]}`
- Criterion `trophy`: `minecraft:inventory_changed` `{"items":[{"items":"twilightforest:alpha_yeti_trophy"}]}`
- Criterion `was_in_fight`: `twilightforest:hurt_boss` `{"hurt_entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:alpha_yeti"}}]}`
- Criterion `yeti`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:alpha_yeti"}}]}`
- Requirements: `[["yeti","trophy","fur","was_in_fight"],["previous_progression"]]`
- sends_telemetry_event: True

#### `twilightforest:progression_end`

- Title: `advancement.twilightforest.progression_end` = "End of Progression"; description: `advancement.twilightforest.progression_end.desc` = "Anything past this point in the Highlands is a work in progress. It will be finished in the future"
- Parent: `twilightforest:progress_troll`; icon: `mc:structure_void`; frame=goal, announce_to_chat=False
- Criterion `plateau`: `minecraft:location` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"location":{"biomes":"twilightforest:final_plateau"}}}]}`
- Criterion `previous_progression`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"twilightforest:progress_troll":true}}}}]}`
- Requirements: `[["previous_progression"],["plateau"]]`
- sends_telemetry_event: True

#### `twilightforest:quest_ram`

- Title: `advancement.twilightforest.quest_ram` = "Consummate Baaahs"; description: `advancement.twilightforest.quest_ram.desc` = "Give the %s what it is missing"
- Parent: `twilightforest:root`; icon: `quest_ram_trophy`; frame=goal
- Criterion `quest_ram_complete`: `twilightforest:complete_quest_ram` `{}`
- Requirements: `[["quest_ram_complete"]]`
- Rewards: `{"experience":100}`
- sends_telemetry_event: True

#### `twilightforest:root`

- Title: `advancement.twilightforest.root` = "Twilight Forest"; description: `advancement.twilightforest.root.desc` = "Enter the mysterious, magical woodlands: the Twilight Forest"
- Parent: `(root)`; icon: `twilight_portal_miniature_structure`; announce_to_chat=False; background `twilightforest:textures/block/mazestone_large_brick.png`
- Criterion `in_tf`: `minecraft:location` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `make_portal`: `twilightforest:make_tf_portal` `{}`
- Requirements: `[["in_tf","make_portal"]]`
- sends_telemetry_event: True

#### `twilightforest:troll`

- Title: `advancement.twilightforest.troll` = "We Do a Little Trolling"; description: `advancement.twilightforest.troll.desc` = "Find and kill a %s"
- Parent: `twilightforest:progress_merge`; icon: `magic_beans`; 
- Criterion `troll`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:troll","location":{"structures":"twilightforest:troll_cave"}}}]}`
- Requirements: `[["troll"]]`
- sends_telemetry_event: True

#### `twilightforest:twilight_dinner`

- Title: `advancement.twilightforest.twilight_dining` = "We Dine At Eternal Sundown"; description: `advancement.twilightforest.twilight_dining.desc` = "Eat every edible item type exclusive to Twilight Forest"
- Parent: `twilightforest:root`; icon: `mc:bowl`; frame=challenge
- Criterion `berry`: `minecraft:consume_item` `{"item":{"items":["twilightforest:raspberry","twilightforest:blackberry","twilightforest:blueberry","twilightforest:maloberry"]}}`
- Criterion `berry_medley`: `minecraft:consume_item` `{"item":{"items":"twilightforest:berry_medley"}}`
- Criterion `experiment_115`: `minecraft:consume_item` `{"item":{"items":"twilightforest:experiment_115"}}`
- Criterion `hydra_chop`: `minecraft:consume_item` `{"item":{"items":"twilightforest:hydra_chop"}}`
- Criterion `jerky`: `minecraft:consume_item` `{"item":{"items":"#c:foods/jerky"}}`
- Criterion `maze_wafer`: `minecraft:consume_item` `{"item":{"items":"twilightforest:maze_wafer"}}`
- Criterion `meef`: `minecraft:consume_item` `{"item":{"items":["twilightforest:raw_meef","twilightforest:cooked_meef"]}}`
- Criterion `meef_stroganoff`: `minecraft:consume_item` `{"item":{"items":"twilightforest:meef_stroganoff"}}`
- Criterion `moss_soup`: `minecraft:consume_item` `{"item":{"items":"twilightforest:moss_soup"}}`
- Criterion `slime_drop`: `minecraft:consume_item` `{"item":{"items":["twilightforest:gelatinous_slime_drop","twilightforest:gelatinous_maze_slime_drop"]}}`
- Criterion `venison`: `minecraft:consume_item` `{"item":{"items":["twilightforest:raw_venison","twilightforest:cooked_venison"]}}`
- Requirements: `[["venison"],["meef"],["meef_stroganoff"],["hydra_chop"],["maze_wafer"],["experiment_115"],["jerky"],["berry"],["slime_drop"],["berry_medley"],["moss_soup"]]`
- Rewards: `{"experience":75}`
- sends_telemetry_event: True

#### `twilightforest:twilight_hunter`

- Title: `advancement.twilightforest.twilight_hunter` = "The Silence of the Forest"; description: `advancement.twilightforest.twilight_hunter.desc` = "Hunt some of the local wildlife"
- Parent: `twilightforest:root`; icon: `raven_feather`; 
- Criterion `adherent`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:adherent","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `alpha_yeti`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:alpha_yeti","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `armored_giant`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:armored_giant","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `bighorn_sheep`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:bighorn_sheep","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `blockchain_goblin`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:blockchain_goblin","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `boar`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:boar","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `carminite_broodling`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:carminite_broodling","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `carminite_ghastguard`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:carminite_ghastguard","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `carminite_ghastling`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:carminite_ghastling","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `carminite_golem`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:carminite_golem","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `death_tome`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:death_tome","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `deer`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:deer","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `dwarf_rabbit`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:dwarf_rabbit","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `fire_beetle`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:fire_beetle","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `giant_miner`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:giant_miner","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `harbinger_cube`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:harbinger_cube","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `hedge_spider`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:hedge_spider","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `helmet_crab`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:helmet_crab","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `hostile_wolf`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:hostile_wolf","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `hydra`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:hydra","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `king_spider`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:king_spider","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `knight_phantom`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:knight_phantom","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `kobold`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:kobold","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `lich`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:lich","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `lich_minion`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:lich_minion","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `lower_goblin_knight`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:lower_goblin_knight","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `maze_slime`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:maze_slime","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `minoshroom`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:minoshroom","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `minotaur`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:minotaur","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `mist_wolf`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:mist_wolf","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `mosquito_swarm`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:mosquito_swarm","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `naga`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:naga","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `penguin`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:penguin","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `pinch_beetle`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:pinch_beetle","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `plateau_boss`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:plateau_boss","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `quest_ram`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:quest_ram","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `raven`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:raven","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `redcap`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:redcap","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `redcap_sapper`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:redcap_sapper","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `skeleton_druid`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:skeleton_druid","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `slime_beetle`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:slime_beetle","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `snow_guardian`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:snow_guardian","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `snow_queen`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:snow_queen","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `squirrel`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:squirrel","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `stable_ice_core`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:stable_ice_core","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `swarm_spider`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:swarm_spider","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `tiny_bird`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:tiny_bird","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `towerwood_borer`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:towerwood_borer","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `troll`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:troll","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `unstable_ice_core`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:unstable_ice_core","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `upper_goblin_knight`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:upper_goblin_knight","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `ur_ghast`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:ur_ghast","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `winter_wolf`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:winter_wolf","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `wraith`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:wraith","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Criterion `yeti`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"twilightforest:yeti","location":{"dimension":"twilightforest:twilight_forest"}}}]}`
- Requirements: `[["adherent","armored_giant","bighorn_sheep","blockchain_goblin","dwarf_rabbit","death_tome","deer","fire_beetle","giant_miner","lower_goblin_knight","upper_goblin_knight","harbinger_cube","hedge_spider","helmet_crab","hostile_wolf","hydra","king_spider","knight_phantom","kobold","lich","lich_minion","maze_slime","carminite_ghastling","minoshroom","minotaur","mist_wolf","mosquito_swarm","naga","penguin","pinch_beetle","plateau_boss","quest_ram","raven","redcap","redcap_sapper","skeleton_druid","slime_beetle","snow_guardian","snow_queen","squirrel","stable_ice_core","swarm_spider","tiny_bird","carminite_broodling","carminite_ghastguard","carminite_golem","towerwood_borer","troll","unstable_ice_core","ur_ghast","boar","winter_wolf","wraith","yeti","alpha_yeti"]]`
- sends_telemetry_event: True

#### `twilightforest:uncraft_uncrafting_table`

- Title: `advancement.twilightforest.uncraft_uncrafting_table` = "A Step too Far"; description: `advancement.twilightforest.uncraft_uncrafting_table.desc` = "Uncraft an Uncrafting Table"
- Parent: `twilightforest:root`; icon: `uncrafting_table`; hidden=True
- Criterion `uncraft_table`: `twilightforest:uncraft_item` `{"item":{"items":"twilightforest:uncrafting_table"}}`
- Requirements: `[["uncraft_table"]]`
- sends_telemetry_event: True

## Appendix B: Strings (95 en_us keys owned)

Keys already shown above (block/item/entity names, subtitles) are not repeated. The 29 Bedrock locales are generated from TF's lang files (R9); owners use these keys with the English text as fallback.

| Key | English |
|---|---|
| `config.twilightforest.locked_toasts` | Disable Locked Biome Toasts |
| `config.twilightforest.locked_toasts.tooltip` | Disables the toasts that appear when entering a locked biome. Not recommended if you're unfamiliar with progression. |
| `gamerule.tfEnforcedProgression` | Twilight Forest: Enforced Progression |
| `gamerule.tfEnforcedProgression.description` | Some Twilight Forest biomes are locked until you defeat certain bosses in the dimension. Check your advancements for the progression order. |
| `misc.twilightforest.biome_locked` | Biome Locked! |
| `misc.twilightforest.biome_locked_2` | Check your advancements |

