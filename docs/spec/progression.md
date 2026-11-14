# Progression and advancements (U14 `progression`)

Behaviour spec of unit **U14** (wave W1), translated from the upstream 4.8 (`9e40a757`, MC 1.21.1) by F16. It
is the checklist the U14 worker implements and the A6 audit compares against. Sections 1-5 are the behaviour;
Appendix A lists every id U14 owns, including **all 45 advancements with their full JSON** (parent, icon, frame,
flags, criteria, requirements, rewards); Appendix B the strings.

## 1. Scope

- The 45 upstream advancements (`gloamingforest:<id>`, R21: U14 owns every advancement JSON and its criteria; other units
  fire their triggers by string through `progression::trigger`, recipe-unlock advancements belong to their recipe's
  unit) and their presentation: the advancement tab (root `gloamingforest:root`, earned by `in_tf` or `make_portal`; the parent chain
  of the other 44 is in Appendix A),
  advancement toasts, the "advancement required" toast and the locked-biome toast.
- **Biome progression enforcement**: registry `gloamingforest:restrictions` (9 entries `restrictions/<biome>`), the
  enforcement types (darkness, hunger, fire, frost, acid_rain), the gamerule `tfEnforcedProgression` and its client
  sync, client config `client.disableLockedBiomeToasts` (section `progression`), net channels
  `progression.enforce_status`, `progression.missing_advancement_toast`.
- Item sub-predicate `gloamingforest:color` (used by `arctic_armor_dyed`).
- U14 also owns `assets/data/advancements/**`, `assets/data/restrictions/**` and `src/feature/progression_content/**`
  (owners.tsv paths).

Not U14: the progression trigger bus, `progression::has/grant/...` and the client query API (F10 contract), landmark
protection / structure hints / conquest (F6; it reads the same advancement lists), the portal (F4) whose locking
advancement comes from F4's config, the acid rain weather and its damage type / sound (U15; the `acid_rain`
enforcement plays them by id), trigger ids owned by other units (listed by string in Appendix A criteria).

Contracts: progression, rules+fx, client.ui, landmark, data.

## 2. Upstream sources

Generated advancement JSON `data/gloamingforest/advancement/*.json` (from `data/TFAdvancementGenerator.java`),
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

- All 45 are listed in Appendix A with every field. Tree, written parent -> child throughout (`a` -> `b`, `c`: `b` and
  `c` are children of `a`; `a` -> `b` -> `c`: `c` is a child of `b`; a parenthesised group right after a node holds
  that node's children): `root` (tab root, no parent; background
  `gloamingforest:textures/block/mazestone_large_brick.png`; criteria: location in the Gloaming Forest or
  `gloamingforest:make_tf_portal`) -> `progress_naga`, `hill1` (-> `hill2` -> `hill3`), `hedge`, `gloaming_hunter`
  (-> `magic_map_focus` -> `magic_map` -> `maze_map` -> `ore_map`), `gloaming_dinner`, `kill_cicada`, `quest_ram`,
  `arborist`, `chicken_jerky`, `craft_travellers_gear` (-> `modify_travellers_gear`), `uncraft_uncrafting_table`;
  `progress_naga` -> `progress_lich`, `naga_armors`; `progress_lich` -> `progress_labyrinth` (-> `progress_hydra`
  (-> `fiery_set`, `hydra_chop`), `mazebreaker`), `progress_trophy_pedestal` (-> `progress_knights` (-> `ghast_trap`
  (-> `progress_ur_ghast`), `experiment_115` (-> `experiment_115_115`, `experiment_115_self_replenishing`))),
  `progress_yeti` (-> `progress_glacier`, `arctic_armor_dyed`, `break_glass_sword`), `progress_merge` (-> `troll`,
  `beanstalk` (-> `giants` -> `progress_troll` -> `progression_end`)), `lich_scepters`, `full_mettle_alchemist`.
  Frames (task when not listed): `goal` for the 11 `progress_*`, `beanstalk`, `giants`, `maze_map`, `quest_ram`,
  `progression_end`; `challenge` for `arborist`, `chicken_jerky`, `break_glass_sword`, `experiment_115_115`,
  `experiment_115_self_replenishing`, `fiery_set`, `full_mettle_alchemist`, `lich_scepters`, `mazebreaker`,
  `naga_armors`, `ore_map`, `gloaming_dinner`; hidden (9): `break_glass_sword`, `chicken_jerky`, `experiment_115_115`,
  `experiment_115_self_replenishing`, `full_mettle_alchemist`, `kill_cicada`, `lich_scepters`, `ore_map`,
  `uncraft_uncrafting_table`.
- Triggers referenced by criteria: vanilla `inventory_changed`, `player_killed_entity`, `consume_item`,
  `item_used_on_block`, `recipe_crafted`, `location`, `tick` (the `tick` criteria with player predicates are the
  "has the previous advancement / conquered" checks), plus upstream triggers owned by other units and fired through
  `progression::trigger` by string: `gloamingforest:make_tf_portal` (F4), `hurt_boss` (F8), `kill_bug` (U6),
  `broke_glass_sword` (U8), `drink_from_flask` (U9), `consume_hydra_chop_on_low_hunger` (U10), `add_modifier`
  (U11), `uncraft_item` (U13), `complete_quest_ram` (U19), `placed_on_trophy_pedestal` (U25), `kill_all_phantoms`
  (U25), `activate_ghast_trap` (U26); `structure_cleared` (F6) is fired on conquest and read by other units. Criteria use the `gloamingforest:color` item sub-predicate (`{color: int}`,
  -1 = any dyed colour) for `arctic_armor_dyed`.
- Earning `gloamingforest:progression_end` sends `gui.gloamingforest.progression_end.message` with a blue underlined
  `gui.gloamingforest.progression_end.discord` link (https://discord.experiment115.com/).
- Earning any advancement with a `drink_from_flask` criterion resets the flask dose tracking (U9's attachment, by
  string).
- Group advancements: entities in `#gloamingforest:multiplayer_inclusive_entities` record qualified attackers; on
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
- Enforcements (each checks the player's own tick counter again and that the tick rate runs normally). The two gates
  nest: the outer `tickCount % 20 == 0` (`ProgressionEvents`) and the enforcement's own inner check
  (`Enforcements.java`) must both pass, so the effective period is lcm(20, inner):
  - `darkness`: inner `tickCount % 60`, so every 60 ticks: Darkness for 200 ticks, amplifier = (int) multiplier, not
    ambient, particles and icon visible (`MobEffectInstance(DARKNESS, 200, m, false, true)`);
  - `hunger`: every 60 ticks Hunger 100 ticks, amplifier = current amplifier + m (or m);
  - `fire`: every 60 ticks set on fire for (int) m seconds;
  - `frost`: every 60 ticks `gloamingforest:frosted` 100 ticks, amplifier (int) m;
  - `acid_rain`: inner `tickCount % 5`, but it runs only on the outer 20-tick ticks, so the effective period is
    **20** ticks (not 5): `gloamingforest:acid_rain` damage of m; if it hurts, play
    `environment.gloamingforest.acid_rain` (U15's event, by id) at the player (1, 1).
  - For all five, the Bedrock implementation mirrors both nested gates (outer 20-tick check in the dispatcher, inner
    check in each enforcement) rather than hard-coding the effective periods.
- Gamerule `tfEnforcedProgression` (boolean, default **true**, category UPDATES): on change the server sends
  `EnforceProgressionStatusPacket(value)` to all players, which sets the client copy of the rule (used by client
  checks: locked toasts, sky/fog effects of other units through `progression::client::enforced()`).

### 3.3 Toasts (client)

- **Locked biome toast** (`LockedBiomeToastHandler`): checked on every client tick, acting only when the local
  player's `tickCount % 5 == 0`, progression is enforced, the player is not creative/spectator and
  `disableLockedBiomeToasts` (default **false**) is off. The state is two **static** fields, `timeUntilToast` (starts
  at 60) and `shownToast` (false), shared by every visit (and every world of the client process; never reset on
  world change). On such a tick:
  - inside a restricted biome whose restriction has a toast icon: `timeUntilToast--`; if `!shownToast` and
    `timeUntilToast <= 0`, show one `LockedBiomeToast` (vanilla advancement toast background, the restriction's icon at
    (6, 8), title `misc.gloamingforest.biome_locked` at (25, 7) yellow -256, description
    `misc.gloamingforest.biome_locked_2` at (25, 18) white; 10 s) and set `shownToast`. The counter keeps decrementing
    (below 0) while the player stays; a fresh countdown is 60 steps x 5 ticks = 300 ticks (~15 s);
  - anywhere else (no restriction, or one without a toast icon): only if `shownToast`, reset `timeUntilToast = 60`
    and `shownToast = false`. Without a shown toast nothing resets, so a partial countdown carries over to the next
    visit of any locked biome.
  - Ticks where the outer conditions fail (not on a 5-tick, not enforced, creative/spectator, toasts disabled) change
    nothing.
- **Missing advancement toast** (`MissingAdvancementToastPacket(title, icon)`, sent e.g. by the portal when the
  portal-locking advancement is missing, F4 by string): advancement toast background, icon at (6, 8),
  `misc.gloamingforest.advancement_required` at (25, 7) white, the advancement title at (25, 18); 10 s; fallback title
  `misc.gloamingforest.advancement_hidden` with the gloaming portal miniature icon.

## 4. Bedrock adaptation notes

- Bedrock has no data-driven advancements: F10's progression bus stores grants per player (world DB
  `gf:progression:<uuid>`), U14 loads the 45 JSONs (`assets/data/advancements/`), evaluates criteria from
  `progression::events` and the upstream triggers, and shows the advancement screen through `client::screen` (client mod:
  Upstream tab with redrawn frames, README deviation: the Java frame textures are Mojang's) and a form-based list for
  vanilla clients; toasts via `client::toast::show` (client mod) or title/actionbar for vanilla.
- Recipe-unlock advancements are generated into recipe JSON by F1 (R21); U14 does not handle them.
- Enforcement every 20 ticks with the per-player tick counters (`progression::events::onTick`); effects through
  `effects::apply` (frosted is U10's custom effect) and `combat::hurt` for acid rain.
- Gamerule via `gamerules::registerBool("tfEnforcedProgression", true)` (F10's store already knows it with upstream's
  default). Upstream's `/gamerule tfEnforcedProgression <value>` is `/gloamingforest gamerule tfEnforcedProgression
  <true|false>` on Bedrock (F10's `gamerule` subcommand: Bedrock's `/gamerule` cannot hold custom rules). Upstream's
  `gloamingforest:dev_new_world` function sets it with `/gamerule tfEnforcedProgression false`; F5's Bedrock port of
  that function cannot, so it ends with `scriptevent gloamingforest:dev_new_world teleport`, whose handler sets the rule
  through `gf::gamerules::set` (`src/feature/worldgen/Functions.cpp`). F10 already sends every rule value to
  companions over `gamerules.sync` on join and on each change (upstream's `EnforceProgressionStatusPacket`), and
  `progression::client::enforced()` reads that copy; U14 sends `progression.enforce_status` only if it needs more
  than the rule value.

## 5. Acceptance checklist (selftests `progression.*`, dx 1400-1499)

- All 45 advancements load with the parents/criteria of Appendix A; granting the chain in order unlocks the tab.
- Restriction table: each biome applies its enforcement until the listed advancement is granted (selftests with
  `progression::grant`), timing 60 ticks (20 for acid rain: the inner 5-tick check inside the outer 20-tick gate),
  multipliers.
- Gamerule false disables enforcement and toasts; packet sync.
- Locked biome toast appears once after the 60-step countdown and again (after a fresh countdown) when leaving and
  re-entering after it was shown; leaving before it was shown keeps the partial countdown (client screenshot).
- `progression_end` message; `arctic_armor_dyed` with the color predicate.

## Appendix A: Owned ids (generated inventory)

Every id this unit owns in `docs/spec/owners.tsv`, with the upstream 4.8 data the W1 worker must reproduce. Registration statements are upstream's own Java (`init/upstream*.java`), condensed (namespaces such as `MapColor.` dropped); loot, recipe, tag, advancement and data rows are summarised from upstream's generated JSON (`src/generated/resources`). Loot notation: `self` = drops the block itself when it survives the explosion; `alternatives(a → b)` = first matching child; `[if ...]` = conditions; `{...}` = functions. In summaries `mc:` is `minecraft:` and ids without a namespace are `gloamingforest:`.

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

| Id | upstream registration (`TFGameRules.java`) |
|---|---|
| `tfEnforcedProgression` | `register("tfEnforcedProgression", GameRules.Category.UPDATES, GameRules.BooleanValue.create(true, (server, value) -> PacketDistributor.sendToAllPlayers(new EnforceProgressionStatusPacket(value.get())) ) );` |

### Item sub-predicates (1)

| Id | upstream registration (`TFItemSubPredicates.java`) |
|---|---|
| `gloamingforest:color` | `TYPES.register("color", new ItemSubPredicate.Type<>(ItemColorPredicate.CODEC));` |

### Registry entries (9)

- `restrictions/gloamingforest:dark_forest`: `{"advancements":["gloamingforest:progress_lich"],"enforcement":"gloamingforest:darkness","locked_biome_toast":{"count":1,"id":"gloamingforest:lich_tower_miniature_structure"},"multiplier":0.0,"structure_key":"gloamingforest:knight_stronghold"}`
- `restrictions/gloamingforest:dark_forest_center`: `{"advancements":["gloamingforest:progress_knights"],"enforcement":"gloamingforest:darkness","locked_biome_toast":{"count":1,"id":"gloamingforest:knight_phantom_trophy"},"multiplier":0.0,"structure_key":"gloamingforest:dark_tower"}`
- `restrictions/gloamingforest:final_plateau`: `{"advancements":["gloamingforest:progress_troll"],"enforcement":"gloamingforest:acid_rain","locked_biome_toast":{"count":1,"id":"gloamingforest:lamp_of_cinders"},"multiplier":1.5,"structure_key":"gloamingforest:final_castle"}`
- `restrictions/gloamingforest:fire_swamp`: `{"advancements":["gloamingforest:progress_labyrinth"],"enforcement":"gloamingforest:fire","locked_biome_toast":{"count":1,"id":"gloamingforest:meef_stroganoff"},"multiplier":8.0,"structure_key":"gloamingforest:hydra_lair"}`
- `restrictions/gloamingforest:glacier`: `{"advancements":["gloamingforest:progress_yeti"],"enforcement":"gloamingforest:frost","locked_biome_toast":{"count":1,"id":"gloamingforest:alpha_yeti_fur"},"multiplier":1.0,"structure_key":"gloamingforest:aurora_palace"}`
- `restrictions/gloamingforest:highlands`: `{"advancements":["gloamingforest:progress_merge"],"enforcement":"gloamingforest:acid_rain","locked_biome_toast":{"count":1,"id":"gloamingforest:uberous_soil"},"multiplier":0.5,"structure_key":"gloamingforest:troll_cave"}`
- `restrictions/gloamingforest:snowy_forest`: `{"advancements":["gloamingforest:progress_lich"],"enforcement":"gloamingforest:frost","locked_biome_toast":{"count":1,"id":"gloamingforest:lich_tower_miniature_structure"},"multiplier":0.0,"structure_key":"gloamingforest:yeti_cave"}`
- `restrictions/gloamingforest:swamp`: `{"advancements":["gloamingforest:progress_lich"],"enforcement":"gloamingforest:hunger","locked_biome_toast":{"count":1,"id":"gloamingforest:lich_tower_miniature_structure"},"multiplier":1.0,"structure_key":"gloamingforest:labyrinth"}`
- `restrictions/gloamingforest:thornlands`: `{"advancements":["gloamingforest:progress_troll"],"enforcement":"gloamingforest:acid_rain","locked_biome_toast":{"count":1,"id":"gloamingforest:lamp_of_cinders"},"multiplier":1.0,"structure_key":"gloamingforest:final_castle"}`

Registries: `restrictions` (`data/<ns>/gloaming/restrictions/`)

### Config keys (1)

See [config.md](config.md) for the section mapping; defaults and translation ids below are upstream's.

| Upstream key | Default | Translation id | English (config screen) |
|---|---|---|---|
| `client.disableLockedBiomeToasts` | `false` | `locked_toasts` | Disable Locked Biome Toasts — Disables the toasts that appear when entering a locked biome. Not recommended if you're unfamiliar with progression. |

### Advancements (45)

Upstream's generated advancement JSON (`data/gloamingforest/advancement/`), every field: parent, display (icon, frame, flags, background), criteria with their trigger conditions, requirements and rewards. Title and description texts are the en_us strings of the display keys.

#### `gloamingforest:arborist`

- Title: `advancement.gloamingforest.arborist` = "Maniacal Dendrologist"; description: `advancement.gloamingforest.arborist.desc` = "Get your axes and shears ready. Search every nook and cranny and get anything and everything that comes from trees! Craft, loot, obtain every slab... sapling... leaves... more... everything. ALL OF IT!"
- Parent: `gloamingforest:root`; icon: `gloaming_oak_fence`; frame=challenge
- Criterion `beanstalk_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:beanstalk_leaves"}]}`
- Criterion `canopy_banister`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_banister"}]}`
- Criterion `canopy_boat`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_boat"}]}`
- Criterion `canopy_bookshelf`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_bookshelf"}]}`
- Criterion `canopy_button`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_button"}]}`
- Criterion `canopy_chest`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_chest"}]}`
- Criterion `canopy_chest_boat`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_chest_boat"}]}`
- Criterion `canopy_door`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_door"}]}`
- Criterion `canopy_drying_rack`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_drying_rack"}]}`
- Criterion `canopy_fence`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_fence"}]}`
- Criterion `canopy_fence_gate`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_fence_gate"}]}`
- Criterion `canopy_hanging_sign`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_hanging_sign"}]}`
- Criterion `canopy_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_leaves"}]}`
- Criterion `canopy_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_log"}]}`
- Criterion `canopy_planks`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_planks"}]}`
- Criterion `canopy_pressure_plate`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_pressure_plate"}]}`
- Criterion `canopy_sapling`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_sapling"}]}`
- Criterion `canopy_sign`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_sign"}]}`
- Criterion `canopy_slab`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_slab"}]}`
- Criterion `canopy_stairs`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_stairs"}]}`
- Criterion `canopy_trapdoor`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_trapdoor"}]}`
- Criterion `canopy_trapped_chest`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_trapped_chest"}]}`
- Criterion `canopy_wood`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:canopy_wood"}]}`
- Criterion `chiseled_canopy_bookshelf`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:chiseled_canopy_bookshelf"}]}`
- Criterion `cracked_towerwood`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:cracked_towerwood"}]}`
- Criterion `dark_banister`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:dark_banister"}]}`
- Criterion `dark_boat`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:dark_boat"}]}`
- Criterion `dark_button`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:dark_button"}]}`
- Criterion `dark_chest`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:dark_chest"}]}`
- Criterion `dark_chest_boat`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:dark_chest_boat"}]}`
- Criterion `dark_door`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:dark_door"}]}`
- Criterion `dark_drying_rack`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:dark_drying_rack"}]}`
- Criterion `dark_fence`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:dark_fence"}]}`
- Criterion `dark_fence_gate`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:dark_fence_gate"}]}`
- Criterion `dark_hanging_sign`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:dark_hanging_sign"}]}`
- Criterion `dark_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:dark_leaves"}]}`
- Criterion `dark_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:dark_log"}]}`
- Criterion `dark_planks`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:dark_planks"}]}`
- Criterion `dark_pressure_plate`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:dark_pressure_plate"}]}`
- Criterion `dark_sign`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:dark_sign"}]}`
- Criterion `dark_slab`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:dark_slab"}]}`
- Criterion `dark_stairs`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:dark_stairs"}]}`
- Criterion `dark_trapdoor`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:dark_trapdoor"}]}`
- Criterion `dark_trapped_chest`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:dark_trapped_chest"}]}`
- Criterion `dark_wood`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:dark_wood"}]}`
- Criterion `darkwood_sapling`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:darkwood_sapling"}]}`
- Criterion `encased_towerwood`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:encased_towerwood"}]}`
- Criterion `fallen_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:fallen_leaves"}]}`
- Criterion `giant_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:giant_leaves"}]}`
- Criterion `giant_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:giant_log"}]}`
- Criterion `hedge`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:hedge"}]}`
- Criterion `hollow_canopy_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:hollow_canopy_log"}]}`
- Criterion `hollow_dark_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:hollow_dark_log"}]}`
- Criterion `hollow_mangrove_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:hollow_mangrove_log"}]}`
- Criterion `hollow_mining_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:hollow_mining_log"}]}`
- Criterion `hollow_oak_sapling`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:hollow_oak_sapling"}]}`
- Criterion `hollow_sorting_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:hollow_sorting_log"}]}`
- Criterion `hollow_time_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:hollow_time_log"}]}`
- Criterion `hollow_transformation_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:hollow_transformation_log"}]}`
- Criterion `hollow_gloaming_oak_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:hollow_gloaming_oak_log"}]}`
- Criterion `huge_stalk`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:huge_stalk"}]}`
- Criterion `liveroot`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:liveroot"}]}`
- Criterion `liveroot_block`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:liveroot_block"}]}`
- Criterion `mangrove_banister`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_banister"}]}`
- Criterion `mangrove_boat`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_boat"}]}`
- Criterion `mangrove_button`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_button"}]}`
- Criterion `mangrove_chest`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_chest"}]}`
- Criterion `mangrove_chest_boat`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_chest_boat"}]}`
- Criterion `mangrove_door`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_door"}]}`
- Criterion `mangrove_drying_rack`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_drying_rack"}]}`
- Criterion `mangrove_fence`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_fence"}]}`
- Criterion `mangrove_fence_gate`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_fence_gate"}]}`
- Criterion `mangrove_hanging_sign`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_hanging_sign"}]}`
- Criterion `mangrove_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_leaves"}]}`
- Criterion `mangrove_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_log"}]}`
- Criterion `mangrove_planks`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_planks"}]}`
- Criterion `mangrove_pressure_plate`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_pressure_plate"}]}`
- Criterion `mangrove_root`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_root"}]}`
- Criterion `mangrove_sapling`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_sapling"}]}`
- Criterion `mangrove_sign`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_sign"}]}`
- Criterion `mangrove_slab`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_slab"}]}`
- Criterion `mangrove_stairs`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_stairs"}]}`
- Criterion `mangrove_trapdoor`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_trapdoor"}]}`
- Criterion `mangrove_trapped_chest`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_trapped_chest"}]}`
- Criterion `mangrove_wood`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mangrove_wood"}]}`
- Criterion `mining_banister`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mining_banister"}]}`
- Criterion `mining_boat`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mining_boat"}]}`
- Criterion `mining_button`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mining_button"}]}`
- Criterion `mining_chest`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mining_chest"}]}`
- Criterion `mining_chest_boat`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mining_chest_boat"}]}`
- Criterion `mining_door`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mining_door"}]}`
- Criterion `mining_drying_rack`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mining_drying_rack"}]}`
- Criterion `mining_fence`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mining_fence"}]}`
- Criterion `mining_fence_gate`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mining_fence_gate"}]}`
- Criterion `mining_hanging_sign`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mining_hanging_sign"}]}`
- Criterion `mining_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mining_leaves"}]}`
- Criterion `mining_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mining_log"}]}`
- Criterion `mining_planks`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mining_planks"}]}`
- Criterion `mining_pressure_plate`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mining_pressure_plate"}]}`
- Criterion `mining_sapling`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mining_sapling"}]}`
- Criterion `mining_sign`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mining_sign"}]}`
- Criterion `mining_slab`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mining_slab"}]}`
- Criterion `mining_stairs`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mining_stairs"}]}`
- Criterion `mining_trapdoor`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mining_trapdoor"}]}`
- Criterion `mining_trapped_chest`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mining_trapped_chest"}]}`
- Criterion `mining_wood`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mining_wood"}]}`
- Criterion `mossy_towerwood`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mossy_towerwood"}]}`
- Criterion `rainbow_oak_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:rainbow_oak_leaves"}]}`
- Criterion `rainbow_oak_sapling`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:rainbow_oak_sapling"}]}`
- Criterion `root`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:root"}]}`
- Criterion `root_strand`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:root_strand"}]}`
- Criterion `sorting_banister`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:sorting_banister"}]}`
- Criterion `sorting_boat`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:sorting_boat"}]}`
- Criterion `sorting_button`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:sorting_button"}]}`
- Criterion `sorting_chest`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:sorting_chest"}]}`
- Criterion `sorting_chest_boat`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:sorting_chest_boat"}]}`
- Criterion `sorting_door`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:sorting_door"}]}`
- Criterion `sorting_drying_rack`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:sorting_drying_rack"}]}`
- Criterion `sorting_fence`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:sorting_fence"}]}`
- Criterion `sorting_fence_gate`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:sorting_fence_gate"}]}`
- Criterion `sorting_hanging_sign`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:sorting_hanging_sign"}]}`
- Criterion `sorting_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:sorting_leaves"}]}`
- Criterion `sorting_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:sorting_log"}]}`
- Criterion `sorting_planks`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:sorting_planks"}]}`
- Criterion `sorting_pressure_plate`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:sorting_pressure_plate"}]}`
- Criterion `sorting_sapling`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:sorting_sapling"}]}`
- Criterion `sorting_sign`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:sorting_sign"}]}`
- Criterion `sorting_slab`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:sorting_slab"}]}`
- Criterion `sorting_stairs`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:sorting_stairs"}]}`
- Criterion `sorting_trapdoor`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:sorting_trapdoor"}]}`
- Criterion `sorting_trapped_chest`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:sorting_trapped_chest"}]}`
- Criterion `sorting_wood`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:sorting_wood"}]}`
- Criterion `stripped_canopy_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:stripped_canopy_log"}]}`
- Criterion `stripped_canopy_wood`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:stripped_canopy_wood"}]}`
- Criterion `stripped_dark_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:stripped_dark_log"}]}`
- Criterion `stripped_dark_wood`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:stripped_dark_wood"}]}`
- Criterion `stripped_mangrove_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:stripped_mangrove_log"}]}`
- Criterion `stripped_mangrove_wood`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:stripped_mangrove_wood"}]}`
- Criterion `stripped_mining_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:stripped_mining_log"}]}`
- Criterion `stripped_mining_wood`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:stripped_mining_wood"}]}`
- Criterion `stripped_sorting_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:stripped_sorting_log"}]}`
- Criterion `stripped_sorting_wood`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:stripped_sorting_wood"}]}`
- Criterion `stripped_time_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:stripped_time_log"}]}`
- Criterion `stripped_time_wood`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:stripped_time_wood"}]}`
- Criterion `stripped_transformation_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:stripped_transformation_log"}]}`
- Criterion `stripped_transformation_wood`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:stripped_transformation_wood"}]}`
- Criterion `stripped_gloaming_oak_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:stripped_gloaming_oak_log"}]}`
- Criterion `stripped_gloaming_oak_wood`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:stripped_gloaming_oak_wood"}]}`
- Criterion `thorn_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:thorn_leaves"}]}`
- Criterion `thorn_rose`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:thorn_rose"}]}`
- Criterion `time_banister`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:time_banister"}]}`
- Criterion `time_boat`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:time_boat"}]}`
- Criterion `time_button`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:time_button"}]}`
- Criterion `time_chest`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:time_chest"}]}`
- Criterion `time_chest_boat`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:time_chest_boat"}]}`
- Criterion `time_door`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:time_door"}]}`
- Criterion `time_drying_rack`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:time_drying_rack"}]}`
- Criterion `time_fence`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:time_fence"}]}`
- Criterion `time_fence_gate`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:time_fence_gate"}]}`
- Criterion `time_hanging_sign`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:time_hanging_sign"}]}`
- Criterion `time_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:time_leaves"}]}`
- Criterion `time_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:time_log"}]}`
- Criterion `time_planks`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:time_planks"}]}`
- Criterion `time_pressure_plate`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:time_pressure_plate"}]}`
- Criterion `time_sapling`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:time_sapling"}]}`
- Criterion `time_sign`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:time_sign"}]}`
- Criterion `time_slab`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:time_slab"}]}`
- Criterion `time_stairs`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:time_stairs"}]}`
- Criterion `time_trapdoor`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:time_trapdoor"}]}`
- Criterion `time_trapped_chest`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:time_trapped_chest"}]}`
- Criterion `time_wood`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:time_wood"}]}`
- Criterion `towerwood`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:towerwood"}]}`
- Criterion `transformation_banister`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:transformation_banister"}]}`
- Criterion `transformation_boat`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:transformation_boat"}]}`
- Criterion `transformation_button`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:transformation_button"}]}`
- Criterion `transformation_chest`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:transformation_chest"}]}`
- Criterion `transformation_chest_boat`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:transformation_chest_boat"}]}`
- Criterion `transformation_door`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:transformation_door"}]}`
- Criterion `transformation_drying_rack`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:transformation_drying_rack"}]}`
- Criterion `transformation_fence`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:transformation_fence"}]}`
- Criterion `transformation_fence_gate`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:transformation_fence_gate"}]}`
- Criterion `transformation_hanging_sign`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:transformation_hanging_sign"}]}`
- Criterion `transformation_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:transformation_leaves"}]}`
- Criterion `transformation_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:transformation_log"}]}`
- Criterion `transformation_planks`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:transformation_planks"}]}`
- Criterion `transformation_pressure_plate`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:transformation_pressure_plate"}]}`
- Criterion `transformation_sapling`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:transformation_sapling"}]}`
- Criterion `transformation_sign`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:transformation_sign"}]}`
- Criterion `transformation_slab`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:transformation_slab"}]}`
- Criterion `transformation_stairs`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:transformation_stairs"}]}`
- Criterion `transformation_trapdoor`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:transformation_trapdoor"}]}`
- Criterion `transformation_trapped_chest`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:transformation_trapped_chest"}]}`
- Criterion `transformation_wood`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:transformation_wood"}]}`
- Criterion `gloaming_oak_banister`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_oak_banister"}]}`
- Criterion `gloaming_oak_boat`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_oak_boat"}]}`
- Criterion `gloaming_oak_button`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_oak_button"}]}`
- Criterion `gloaming_oak_chest`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_oak_chest"}]}`
- Criterion `gloaming_oak_chest_boat`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_oak_chest_boat"}]}`
- Criterion `gloaming_oak_door`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_oak_door"}]}`
- Criterion `gloaming_oak_drying_rack`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_oak_drying_rack"}]}`
- Criterion `gloaming_oak_fence`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_oak_fence"}]}`
- Criterion `gloaming_oak_fence_gate`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_oak_fence_gate"}]}`
- Criterion `gloaming_oak_hanging_sign`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_oak_hanging_sign"}]}`
- Criterion `gloaming_oak_leaves`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_oak_leaves"}]}`
- Criterion `gloaming_oak_log`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_oak_log"}]}`
- Criterion `gloaming_oak_planks`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_oak_planks"}]}`
- Criterion `gloaming_oak_pressure_plate`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_oak_pressure_plate"}]}`
- Criterion `gloaming_oak_sapling`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_oak_sapling"}]}`
- Criterion `gloaming_oak_sign`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_oak_sign"}]}`
- Criterion `gloaming_oak_slab`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_oak_slab"}]}`
- Criterion `gloaming_oak_stairs`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_oak_stairs"}]}`
- Criterion `gloaming_oak_trapdoor`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_oak_trapdoor"}]}`
- Criterion `gloaming_oak_trapped_chest`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_oak_trapped_chest"}]}`
- Criterion `gloaming_oak_wood`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_oak_wood"}]}`
- Requirements: `[["gloaming_oak_log"],["gloaming_oak_wood"],["stripped_gloaming_oak_log"],["stripped_gloaming_oak_wood"],["hollow_gloaming_oak_log"],["gloaming_oak_leaves"],["gloaming_oak_sapling"],["gloaming_oak_planks"],["gloaming_oak_slab"],["gloaming_oak_stairs"],["gloaming_oak_button"],["gloaming_oak_fence"],["gloaming_oak_fence_gate"],["gloaming_oak_pressure_plate"],["gloaming_oak_door"],["gloaming_oak_trapdoor"],["gloaming_oak_sign"],["gloaming_oak_hanging_sign"],["gloaming_oak_chest"],["gloaming_oak_trapped_chest"],["gloaming_oak_banister"],["gloaming_oak_drying_rack"],["gloaming_oak_boat"],["gloaming_oak_chest_boat"],["canopy_log"],["canopy_wood"],["stripped_canopy_log"],["stripped_canopy_wood"],["hollow_canopy_log"],["canopy_leaves"],["canopy_sapling"],["canopy_planks"],["canopy_slab"],["canopy_stairs"],["canopy_button"],["canopy_fence"],["canopy_fence_gate"],["canopy_pressure_plate"],["canopy_door"],["canopy_trapdoor"],["canopy_sign"],["canopy_hanging_sign"],["canopy_chest"],["canopy_trapped_chest"],["canopy_banister"],["canopy_drying_rack"],["canopy_bookshelf"],["chiseled_canopy_bookshelf"],["canopy_boat"],["canopy_chest_boat"],["mangrove_log"],["mangrove_wood"],["stripped_mangrove_log"],["stripped_mangrove_wood"],["hollow_mangrove_log"],["mangrove_leaves"],["mangrove_sapling"],["mangrove_planks"],["mangrove_slab"],["mangrove_stairs"],["mangrove_button"],["mangrove_fence"],["mangrove_fence_gate"],["mangrove_pressure_plate"],["mangrove_door"],["mangrove_trapdoor"],["mangrove_sign"],["mangrove_hanging_sign"],["mangrove_chest"],["mangrove_trapped_chest"],["mangrove_banister"],["mangrove_drying_rack"],["mangrove_boat"],["mangrove_chest_boat"],["dark_log"],["dark_wood"],["stripped_dark_log"],["stripped_dark_wood"],["hollow_dark_log"],["dark_leaves"],["darkwood_sapling"],["dark_planks"],["dark_slab"],["dark_stairs"],["dark_button"],["dark_fence"],["dark_fence_gate"],["dark_pressure_plate"],["dark_door"],["dark_trapdoor"],["dark_sign"],["dark_hanging_sign"],["dark_chest"],["dark_trapped_chest"],["dark_banister"],["dark_drying_rack"],["dark_boat"],["dark_chest_boat"],["time_log"],["time_wood"],["stripped_time_log"],["stripped_time_wood"],["hollow_time_log"],["time_leaves"],["time_sapling"],["time_planks"],["time_slab"],["time_stairs"],["time_button"],["time_fence"],["time_fence_gate"],["time_pressure_plate"],["time_door"],["time_trapdoor"],["time_sign"],["time_hanging_sign"],["time_chest"],["time_trapped_chest"],["time_banister"],["time_drying_rack"],["time_boat"],["time_chest_boat"],["transformation_log"],["transformation_wood"],["stripped_transformation_log"],["stripped_transformation_wood"],["hollow_transformation_log"],["transformation_leaves"],["transformation_sapling"],["transformation_planks"],["transformation_slab"],["transformation_stairs"],["transformation_button"],["transformation_fence"],["transformation_fence_gate"],["transformation_pressure_plate"],["transformation_door"],["transformation_trapdoor"],["transformation_sign"],["transformation_hanging_sign"],["transformation_chest"],["transformation_trapped_chest"],["transformation_banister"],["transformation_drying_rack"],["transformation_boat"],["transformation_chest_boat"],["mining_log"],["mining_wood"],["stripped_mining_log"],["stripped_mining_wood"],["hollow_mining_log"],["mining_leaves"],["mining_sapling"],["mining_planks"],["mining_slab"],["mining_stairs"],["mining_button"],["mining_fence"],["mining_fence_gate"],["mining_pressure_plate"],["mining_door"],["mining_trapdoor"],["mining_sign"],["mining_hanging_sign"],["mining_chest"],["mining_trapped_chest"],["mining_banister"],["mining_drying_rack"],["mining_boat"],["mining_chest_boat"],["sorting_log"],["sorting_wood"],["stripped_sorting_log"],["stripped_sorting_wood"],["hollow_sorting_log"],["sorting_leaves"],["sorting_sapling"],["sorting_planks"],["sorting_slab"],["sorting_stairs"],["sorting_button"],["sorting_fence"],["sorting_fence_gate"],["sorting_pressure_plate"],["sorting_door"],["sorting_trapdoor"],["sorting_sign"],["sorting_hanging_sign"],["sorting_chest"],["sorting_trapped_chest"],["sorting_banister"],["sorting_drying_rack"],["sorting_boat"],["sorting_chest_boat"],["towerwood"],["cracked_towerwood"],["mossy_towerwood"],["encased_towerwood"],["root"],["root_strand"],["liveroot_block"],["liveroot"],["hollow_oak_sapling"],["rainbow_oak_sapling"],["rainbow_oak_leaves"],["giant_log"],["giant_leaves"],["huge_stalk"],["beanstalk_leaves"],["thorn_leaves"],["thorn_rose"],["hedge"],["fallen_leaves"],["mangrove_root"]]`
- Rewards: `{"experience":1000}`
- sends_telemetry_event: True

#### `gloamingforest:arctic_armor_dyed`

- Title: `advancement.gloamingforest.arctic_dyed` = "Getting in Fashion"; description: `advancement.gloamingforest.arctic_dyed.desc` = "Dye four unique pieces of Arctic armor"
- Parent: `gloamingforest:progress_yeti`; icon: `arctic_chestplate`; 
- Criterion `boots`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:arctic_boots","predicates":{"gloamingforest:color":{}}}]}`
- Criterion `chestplate`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:arctic_chestplate","predicates":{"gloamingforest:color":{}}}]}`
- Criterion `helmet`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:arctic_helmet","predicates":{"gloamingforest:color":{}}}]}`
- Criterion `leggings`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:arctic_leggings","predicates":{"gloamingforest:color":{}}}]}`
- Requirements: `[["helmet"],["chestplate"],["leggings"],["boots"]]`
- Rewards: `{"experience":25}`
- sends_telemetry_event: True

#### `gloamingforest:beanstalk`

- Title: `advancement.gloamingforest.beanstalk` = "Jack and the Beanstalk"; description: `advancement.gloamingforest.beanstalk.desc` = "Obtain some %s in the troll caves and use them on the glowing soil beneath the clouds"
- Parent: `gloamingforest:progress_merge`; icon: `huge_stalk`; frame=goal
- Criterion `beans`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:magic_beans"}]}`
- Criterion `use_beans`: `minecraft:item_used_on_block` `{"location":[{"condition":"minecraft:location_check","predicate":{"block":{"blocks":"gloamingforest:uberous_soil"}}},{"condition":"minecraft:match_tool","predicate":{"items":"gloamingforest:magic_beans"}}]}`
- Requirements: `[["beans"],["use_beans"]]`
- sends_telemetry_event: True

#### `gloamingforest:break_glass_sword`

- Title: `advancement.gloamingforest.glass_sword` = "One Hit Wonder"; description: `advancement.gloamingforest.glass_sword.desc` = "Hope you made good use of that."
- Parent: `gloamingforest:progress_yeti`; icon: `glass_sword`; frame=challenge, hidden=True
- Criterion `broken_sword`: `gloamingforest:broke_glass_sword` `{}`
- Requirements: `[["broken_sword"]]`
- Rewards: `{"experience":42}`
- sends_telemetry_event: True

#### `gloamingforest:chicken_jerky`

- Title: `advancement.gloamingforest.chicken_jerky` = "CHICKEN JERKEY!"; description: `` = ""
- Parent: `gloamingforest:root`; icon: `chicken_jerky`; frame=challenge, hidden=True
- Criterion `jerky`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"minecraft:zombie","flags":{"is_baby":true},"vehicle":{"type":"minecraft:chicken"}}}],"killing_blow":{"direct_entity":{"equipment":{"mainhand":{"items":"gloamingforest:chicken_jerky"}}}}}`
- Requirements: `[["jerky"]]`
- sends_telemetry_event: True

#### `gloamingforest:craft_travellers_gear`

- Title: `advancement.gloamingforest.craft_travellers_gear` = "Around the Forest in 80 Days"; description: `advancement.gloamingforest.craft_travellers_gear.desc` = "Craft a piece of traveller's gear"
- Parent: `gloamingforest:root`; icon: `travellers_boots`; 
- Criterion `craft_belt`: `minecraft:recipe_crafted` `{"recipe_id":"gloamingforest:equipment/travellers_belt"}`
- Criterion `craft_boots`: `minecraft:recipe_crafted` `{"recipe_id":"gloamingforest:equipment/travellers_boots"}`
- Criterion `craft_gloves`: `minecraft:recipe_crafted` `{"recipe_id":"gloamingforest:equipment/travellers_gloves"}`
- Criterion `craft_goggles`: `minecraft:recipe_crafted` `{"recipe_id":"gloamingforest:equipment/travellers_goggles"}`
- Criterion `craft_vest`: `minecraft:recipe_crafted` `{"recipe_id":"gloamingforest:equipment/travellers_chest"}`
- Criterion `craft_wings`: `minecraft:recipe_crafted` `{"recipe_id":"gloamingforest:equipment/travellers_wings"}`
- Requirements: `[["craft_goggles","craft_vest","craft_wings","craft_boots","craft_belt","craft_gloves"]]`
- sends_telemetry_event: True

#### `gloamingforest:experiment_115`

- Title: `advancement.gloamingforest.experiment_115` = "Mystery Meat?"; description: `advancement.gloamingforest.experiment_115.desc` = "It looks like cake, though..."
- Parent: `gloamingforest:progress_knights`; icon: `experiment_115`; 
- Criterion `eat_experiment_115`: `minecraft:consume_item` `{"item":{"items":"gloamingforest:experiment_115"}}`
- Requirements: `[["eat_experiment_115"]]`
- sends_telemetry_event: True

#### `gloamingforest:experiment_115_115`

- Title: `advancement.gloamingforest.experiment_115_3` = "Eating 115 Everyday, 115 Years, Forever"; description: `advancement.gloamingforest.experiment_115_3.desc` = "eating experiment one-hundred-fifteen all day, forever, one-hundred-fifteen times experiment one-hundred-fifteen, experiment one-hundred-fifteen dot com, double-u double-u double-u dot experiment one-hundred-fifteen dot com, one-hundred-fifteen years, every minute experiment one-hundred-fifteen dot com, double-u double-u double-u dot one-hundred-fifteen times experiment one-hundred-fifteen dot com"
- Parent: `gloamingforest:experiment_115`; icon: `experiment_115 {"gloamingforest:e115_variant":"think"}`; frame=challenge, hidden=True
- Criterion `eat_115_e115`: `minecraft:location` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","stats":[{"type":"minecraft:custom","stat":"gloamingforest:e115_slices_eaten","value":{"min":115}}]}}}]}`
- Requirements: `[["eat_115_e115"]]`
- sends_telemetry_event: True

#### `gloamingforest:experiment_115_self_replenishing`

- Title: `advancement.gloamingforest.experiment_115_2` = "Making a note: Huge Success!"; description: `advancement.gloamingforest.experiment_115_2.desc` = "It's so delicious and moist!"
- Parent: `gloamingforest:experiment_115`; icon: `experiment_115 {"gloamingforest:e115_variant":"full"}`; frame=challenge, hidden=True
- Criterion `place_complete_e115`: `minecraft:item_used_on_block` `{"location":[{"condition":"minecraft:location_check","predicate":{"block":{"blocks":"gloamingforest:experiment_115","state":{"regenerate":"true"}}}},{"condition":"minecraft:match_tool","predicate":{"items":"minecraft:redstone"}}]}`
- Requirements: `[["place_complete_e115"]]`
- sends_telemetry_event: True

#### `gloamingforest:fiery_set`

- Title: `advancement.gloamingforest.fiery_set` = "Gallons of Blood and Tears"; description: `advancement.gloamingforest.fiery_set.desc` = "Wield a fiery tool or weapon while having at least one piece of fiery armor in your inventory"
- Parent: `gloamingforest:progress_hydra`; icon: `fiery_sword`; frame=challenge
- Criterion `fiery_boots`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:fiery_boots"}]}`
- Criterion `fiery_chestplate`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:fiery_chestplate"}]}`
- Criterion `fiery_helmet`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:fiery_helmet"}]}`
- Criterion `fiery_leggings`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:fiery_leggings"}]}`
- Criterion `fiery_pick`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:fiery_pickaxe"}]}`
- Criterion `fiery_sword`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:fiery_sword"}]}`
- Requirements: `[["fiery_pick","fiery_sword"],["fiery_helmet","fiery_chestplate","fiery_leggings","fiery_boots"]]`
- Rewards: `{"experience":75}`
- sends_telemetry_event: True

#### `gloamingforest:full_mettle_alchemist`

- Title: `advancement.gloamingforest.full_mettle_alchemist` = "Full Mettle Alchemist"; description: `advancement.gloamingforest.full_mettle_alchemist.desc` = "Drink three doses of Harming II from a potion flask in under 6 seconds and survive"
- Parent: `gloamingforest:progress_lich`; icon: `brittle_potion_flask {"gloamingforest:flask_contents":{"breakable":false,"doses":4,"potion":{"potion":"minecraft:strong_harming"}}}`; frame=challenge, hidden=True
- Criterion `drink_4_harming`: `gloamingforest:drink_from_flask` `{"doses":3,"potion":"minecraft:strong_harming","seconds":{"max":6}}`
- Requirements: `[["drink_4_harming"]]`
- Rewards: `{"experience":100}`
- sends_telemetry_event: True

#### `gloamingforest:ghast_trap`

- Title: `advancement.gloamingforest.ghast_trap` = "Something Strange in Towerwood"; description: `advancement.gloamingforest.ghast_trap.desc` = "Slay the %ss around a %s and activate to wrench the %s from the sky!"
- Parent: `gloamingforest:progress_knights`; icon: `ghast_trap`; 
- Criterion `activate_ghast_trap`: `gloamingforest:activate_ghast_trap` `{}`
- Requirements: `[["activate_ghast_trap"]]`
- sends_telemetry_event: True

#### `gloamingforest:giants`

- Title: `advancement.gloamingforest.giants` = "I'm on Cloud Nine"; description: `advancement.gloamingforest.giants.desc` = "Kill a %s in the clouds, retrieve a %s, and return back to the troll caves"
- Parent: `gloamingforest:beanstalk`; icon: `giant_pickaxe`; frame=goal
- Criterion `giant`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:giant_miner"}}]}`
- Criterion `pickaxe`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:magic_beans"}]}`
- Requirements: `[["giant"],["pickaxe"]]`
- sends_telemetry_event: True

#### `gloamingforest:hedge`

- Title: `advancement.gloamingforest.hedge` = "Bug Stomper"; description: `advancement.gloamingforest.hedge.desc` = "Defeat a spider in a Hedge Maze"
- Parent: `gloamingforest:root`; icon: `hedge`; 
- Criterion `hedge_spider`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:hedge_spider","location":{"structures":"gloamingforest:hedge_maze"}}}]}`
- Criterion `swarm_spider`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:swarm_spider","location":{"structures":"gloamingforest:hedge_maze"}}}]}`
- Requirements: `[["hedge_spider","swarm_spider"]]`
- sends_telemetry_event: True

#### `gloamingforest:hill1`

- Title: `advancement.gloamingforest.hill1` = "The Boots Are Mine!"; description: `advancement.gloamingforest.hill1.desc` = "Defeat a %s in a Small Hollow Hill"
- Parent: `gloamingforest:root`; icon: `mc:iron_boots`; 
- Criterion `redcap`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:redcap","location":{"structures":"gloamingforest:small_hollow_hill"}}}]}`
- Requirements: `[["redcap"]]`
- sends_telemetry_event: True

#### `gloamingforest:hill2`

- Title: `advancement.gloamingforest.hill2` = "What Was That Noise?"; description: `advancement.gloamingforest.hill2.desc` = "Defeat a %s in a Medium Hollow Hill"
- Parent: `gloamingforest:hill1`; icon: `ironwood_pickaxe`; 
- Criterion `redcap`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:redcap_sapper","location":{"structures":"gloamingforest:medium_hollow_hill"}}}]}`
- Requirements: `[["redcap"]]`
- sends_telemetry_event: True

#### `gloamingforest:hill3`

- Title: `advancement.gloamingforest.hill3` = "I See Right Through You"; description: `advancement.gloamingforest.hill3.desc` = "Defeat a %s in a Large Hollow Hill"
- Parent: `gloamingforest:hill2`; icon: `mc:glowstone_dust`; 
- Criterion `redcap`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:wraith","location":{"structures":"gloamingforest:large_hollow_hill"}}}]}`
- Requirements: `[["redcap"]]`
- sends_telemetry_event: True

#### `gloamingforest:hydra_chop`

- Title: `advancement.gloamingforest.hydra_chop` = "Hydra Chop, Baby!"; description: `advancement.gloamingforest.hydra_chop.desc` = "Chow down on a %s when your hunger bar is depleted"
- Parent: `gloamingforest:progress_hydra`; icon: `hydra_chop`; 
- Criterion `hydra_chop`: `gloamingforest:consume_hydra_chop_on_low_hunger` `{}`
- Requirements: `[["hydra_chop"]]`
- sends_telemetry_event: True

#### `gloamingforest:kill_cicada`

- Title: `advancement.gloamingforest.kill_cicada` = "Shut"; description: `advancement.gloamingforest.kill_cicada.desc` = "Kill a Cicada"
- Parent: `gloamingforest:root`; icon: `cicada`; announce_to_chat=False, hidden=True
- Criterion `kill_cicada`: `gloamingforest:kill_bug` `{"bug":"gloamingforest:cicada"}`
- Requirements: `[["kill_cicada"]]`
- sends_telemetry_event: True

#### `gloamingforest:lich_scepters`

- Title: `advancement.gloamingforest.lich_scepters` = "By Our Powers Combined!"; description: `advancement.gloamingforest.lich_scepters.desc` = "Acquire all four scepters of power"
- Parent: `gloamingforest:progress_lich`; icon: `zombie_scepter`; frame=challenge, hidden=True
- Criterion `scepters`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:lifedrain_scepter"},{"items":"gloamingforest:gloaming_scepter"},{"items":"gloamingforest:zombie_scepter"},{"items":"gloamingforest:fortification_scepter"}]}`
- Requirements: `[["scepters"]]`
- Rewards: `{"experience":100}`
- sends_telemetry_event: True

#### `gloamingforest:magic_map`

- Title: `advancement.gloamingforest.magic_map` = "I Can See Forever"; description: `advancement.gloamingforest.magic_map.desc` = "Craft the %s"
- Parent: `gloamingforest:magic_map_focus`; icon: `filled_magic_map`; 
- Criterion `magic_map`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:filled_magic_map"}]}`
- Requirements: `[["magic_map"]]`
- sends_telemetry_event: True

#### `gloamingforest:magic_map_focus`

- Title: `advancement.gloamingforest.magic_map_focus` = "With Fire It Writes"; description: `advancement.gloamingforest.magic_map_focus.desc` = "Craft the %s with a %s, %s, and %s"
- Parent: `gloamingforest:gloaming_hunter`; icon: `magic_map_focus`; 
- Criterion `focus`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:magic_map_focus"}]}`
- Requirements: `[["focus"]]`
- sends_telemetry_event: True

#### `gloamingforest:maze_map`

- Title: `advancement.gloamingforest.maze_map` = "And Now, to Find the Exit"; description: `advancement.gloamingforest.maze_map.desc` = "Craft the %s after obtaining the focus from the labyrinth"
- Parent: `gloamingforest:magic_map`; icon: `filled_maze_map`; frame=goal
- Criterion `maze_map`: `minecraft:recipe_crafted` `{"recipe_id":"gloamingforest:maze_map"}`
- Requirements: `[["maze_map"]]`
- sends_telemetry_event: True

#### `gloamingforest:mazebreaker`

- Title: `advancement.gloamingforest.mazebreaker` = "Breaking the Maze"; description: `advancement.gloamingforest.mazebreaker.desc` = "Find the %s pickaxe in the secret labyrinth vault"
- Parent: `gloamingforest:progress_labyrinth`; icon: `mazebreaker_pickaxe`; frame=challenge
- Criterion `pick`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:mazebreaker_pickaxe"}]}`
- Requirements: `[["pick"]]`
- Rewards: `{"experience":50}`
- sends_telemetry_event: True

#### `gloamingforest:modify_travellers_gear`

- Title: `advancement.gloamingforest.modify_travellers_gear` = "Apparel Oft Proclaims the Man"; description: `advancement.gloamingforest.modify_travellers_gear.desc` = "Add a modifier to a piece of traveller's gear"
- Parent: `gloamingforest:craft_travellers_gear`; icon: `travellers_boots {"gloamingforest:water_walk":{}}`; 
- Criterion `add_any_modifier`: `gloamingforest:add_modifier` `{}`
- Requirements: `[["add_any_modifier"]]`
- sends_telemetry_event: True

#### `gloamingforest:naga_armors`

- Title: `advancement.gloamingforest.naga_armors` = "Naga Armorer"; description: `advancement.gloamingforest.naga_armors.desc` = "Craft a %s and %s"
- Parent: `gloamingforest:progress_naga`; icon: `naga_chestplate`; frame=challenge
- Criterion `chestplate`: `minecraft:recipe_crafted` `{"recipe_id":"gloamingforest:equipment/naga_chestplate"}`
- Criterion `leggings`: `minecraft:recipe_crafted` `{"recipe_id":"gloamingforest:equipment/naga_leggings"}`
- Requirements: `[["chestplate"],["leggings"]]`
- Rewards: `{"experience":25}`
- sends_telemetry_event: True

#### `gloamingforest:ore_map`

- Title: `advancement.gloamingforest.ore_map` = "How Can That Be Worth It?"; description: `advancement.gloamingforest.ore_map.desc` = "Craft the %s"
- Parent: `gloamingforest:maze_map`; icon: `filled_ore_map`; frame=challenge, hidden=True
- Criterion `ore_map`: `minecraft:recipe_crafted` `{"recipe_id":"gloamingforest:ore_map"}`
- Requirements: `[["ore_map"]]`
- sends_telemetry_event: True

#### `gloamingforest:progress_glacier`

- Title: `advancement.gloamingforest.progress_glacier` = "Clear Skies"; description: `advancement.gloamingforest.progress_glacier.desc` = "Defeat the %s atop the Aurora Palace"
- Parent: `gloamingforest:progress_yeti`; icon: `snow_queen_trophy`; frame=goal
- Criterion `previous_progression`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"gloamingforest:progress_yeti":true}}}}]}`
- Criterion `queen`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:snow_queen"}}]}`
- Criterion `trophy`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:snow_queen_trophy"}]}`
- Criterion `was_in_fight`: `gloamingforest:hurt_boss` `{"hurt_entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:snow_queen"}}]}`
- Requirements: `[["queen","trophy","was_in_fight"],["previous_progression"]]`
- sends_telemetry_event: True

#### `gloamingforest:progress_hydra`

- Title: `advancement.gloamingforest.kill_hydra` = "Hydra Slayer"; description: `advancement.gloamingforest.kill_hydra.desc` = "Defeat the mighty %s and empower yourself"
- Parent: `gloamingforest:progress_labyrinth`; icon: `hydra_trophy`; frame=goal
- Criterion `kill_hydra`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:hydra"}}]}`
- Criterion `stroganoff`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"gloamingforest:progress_labyrinth":true}}}}]}`
- Criterion `trophy`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:hydra_trophy"}]}`
- Criterion `was_in_fight`: `gloamingforest:hurt_boss` `{"hurt_entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:hydra"}}]}`
- Requirements: `[["kill_hydra","trophy","was_in_fight"],["stroganoff"]]`
- sends_telemetry_event: True

#### `gloamingforest:progress_knights`

- Title: `advancement.gloamingforest.progress_knights` = "Carminite Acclimation"; description: `advancement.gloamingforest.progress_knights.desc` = "Settle the restless phantoms in the knight's tomb and the carminite tower's devices will obey you"
- Parent: `gloamingforest:progress_trophy_pedestal`; icon: `knight_phantom_trophy`; frame=goal
- Criterion `all_knights`: `gloamingforest:kill_all_phantoms` `{}`
- Criterion `previous_progression`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"gloamingforest:progress_trophy_pedestal":true}}}}]}`
- Criterion `trophy`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:knight_phantom_trophy"}]}`
- Requirements: `[["all_knights","trophy"],["previous_progression"]]`
- sends_telemetry_event: True

#### `gloamingforest:progress_labyrinth`

- Title: `advancement.gloamingforest.progress_labyrinth` = "Mighty Stroganoff"; description: `advancement.gloamingforest.progress_labyrinth.desc` = "Eat the Meef Stroganoff to acclimatize your body to the dangerous heat of the Fire Swamp"
- Parent: `gloamingforest:progress_lich`; icon: `minotaur_labyrinth_miniature_structure`; frame=goal
- Criterion `kill_lich`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"gloamingforest:progress_lich":true}}}}]}`
- Criterion `meef`: `minecraft:consume_item` `{"item":{"items":"gloamingforest:meef_stroganoff"}}`
- Requirements: `[["meef"],["kill_lich"]]`
- sends_telemetry_event: True

#### `gloamingforest:progress_lich`

- Title: `advancement.gloamingforest.kill_lich` = "Bring Out Your Dead"; description: `advancement.gloamingforest.kill_lich.desc` = "Slay the %s at top of his tower and retrieve a scepter to clear poisonous mosquitoes from the Swamp, see through blinding darkness of the Dark Forest's curse, and resist the Snowy Forest's chill"
- Parent: `gloamingforest:progress_naga`; icon: `lich_tower_miniature_structure`; frame=goal
- Criterion `kill_lich`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:lich"}}]}`
- Criterion `kill_naga`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"gloamingforest:progress_naga":true}}}}]}`
- Criterion `lifedrain_scepter`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:lifedrain_scepter"}]}`
- Criterion `shield_scepter`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:fortification_scepter"}]}`
- Criterion `trophy`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:lich_trophy"}]}`
- Criterion `gloaming_scepter`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:gloaming_scepter"}]}`
- Criterion `was_in_fight`: `gloamingforest:hurt_boss` `{"hurt_entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:lich"}}]}`
- Criterion `zombie_scepter`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:zombie_scepter"}]}`
- Requirements: `[["kill_lich","trophy","lifedrain_scepter","gloaming_scepter","zombie_scepter","shield_scepter","was_in_fight"],["kill_naga"]]`
- sends_telemetry_event: True

#### `gloamingforest:progress_merge`

- Title: `advancement.gloamingforest.progress_merge` = "Ultimate Showdown"; description: `advancement.gloamingforest.progress_merge.desc` = "Slay the %s, %s, and %s to clear the acid rain and embolden yourself for the Highlands"
- Parent: `gloamingforest:progress_lich`; icon: `uberous_soil`; frame=goal
- Criterion `hydra`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"gloamingforest:progress_hydra":true}}}}]}`
- Criterion `snow_queen`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"gloamingforest:progress_glacier":true}}}}]}`
- Criterion `ur_ghast`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"gloamingforest:progress_ur_ghast":true}}}}]}`
- Requirements: `[["hydra"],["ur_ghast"],["snow_queen"]]`
- sends_telemetry_event: True

#### `gloamingforest:progress_naga`

- Title: `advancement.gloamingforest.kill_naga` = "Time To Even The Scales"; description: `advancement.gloamingforest.kill_naga.desc` = "Slay the %s in its forest courtyard and obtain a %s to overcome the barrier magic surrounding the Lich's tower"
- Parent: `gloamingforest:root`; icon: `naga_courtyard_miniature_structure`; frame=goal
- Criterion `naga`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:naga"}}]}`
- Criterion `scale`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:naga_scale"}]}`
- Criterion `trophy`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:naga_trophy"}]}`
- Criterion `was_in_fight`: `gloamingforest:hurt_boss` `{"hurt_entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:naga"}}]}`
- Requirements: `[["naga","trophy","scale","was_in_fight"]]`
- sends_telemetry_event: True

#### `gloamingforest:progress_troll`

- Title: `advancement.gloamingforest.progress_troll` = "I Wish For More Burning"; description: `advancement.gloamingforest.progress_troll.desc` = "Find the %s in the troll caves, and you can burn away the thorn barriers"
- Parent: `gloamingforest:giants`; icon: `lamp_of_cinders`; frame=goal
- Criterion `lamp`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:lamp_of_cinders"}]}`
- Criterion `previous_progression`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"gloamingforest:progress_merge":true}}}}]}`
- Requirements: `[["lamp"],["previous_progression"]]`
- sends_telemetry_event: True

#### `gloamingforest:progress_trophy_pedestal`

- Title: `advancement.gloamingforest.progress_trophy_pedestal` = "Trophied Champion"; description: `advancement.gloamingforest.progress_trophy_pedestal.desc` = "Claim your title by placing a trophy on the pedestal in the Dark Forest ruins"
- Parent: `gloamingforest:progress_lich`; icon: `trophy_pedestal`; frame=goal
- Criterion `kill_lich`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"gloamingforest:progress_lich":true}}}}]}`
- Criterion `trophy_pedestal`: `gloamingforest:placed_on_trophy_pedestal` `{}`
- Requirements: `[["trophy_pedestal"],["kill_lich"]]`
- sends_telemetry_event: True

#### `gloamingforest:progress_ur_ghast`

- Title: `advancement.gloamingforest.progress_ur_ghast` = "Tears of Fire"; description: `advancement.gloamingforest.progress_ur_ghast.desc` = "Touch the fiery red tears of the %s"
- Parent: `gloamingforest:ghast_trap`; icon: `dark_tower_miniature_structure`; frame=goal
- Criterion `ghast`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:ur_ghast"}}]}`
- Criterion `previous_progression`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"gloamingforest:progress_knights":true}}}}]}`
- Criterion `trophy`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:ur_ghast_trophy"}]}`
- Criterion `was_in_fight`: `gloamingforest:hurt_boss` `{"hurt_entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:ur_ghast"}}]}`
- Requirements: `[["ghast","trophy","was_in_fight"],["previous_progression"]]`
- sends_telemetry_event: True

#### `gloamingforest:progress_yeti`

- Title: `advancement.gloamingforest.progress_yeti` = "Alpha Fur"; description: `advancement.gloamingforest.progress_yeti.desc` = "Line your garments with the soft fur from the %s, keeping you safe from the Glacier's cold"
- Parent: `gloamingforest:progress_lich`; icon: `alpha_yeti_fur`; frame=goal
- Criterion `fur`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:alpha_yeti_fur"}]}`
- Criterion `previous_progression`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"gloamingforest:progress_lich":true}}}}]}`
- Criterion `trophy`: `minecraft:inventory_changed` `{"items":[{"items":"gloamingforest:alpha_yeti_trophy"}]}`
- Criterion `was_in_fight`: `gloamingforest:hurt_boss` `{"hurt_entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:alpha_yeti"}}]}`
- Criterion `yeti`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:alpha_yeti"}}]}`
- Requirements: `[["yeti","trophy","fur","was_in_fight"],["previous_progression"]]`
- sends_telemetry_event: True

#### `gloamingforest:progression_end`

- Title: `advancement.gloamingforest.progression_end` = "End of Progression"; description: `advancement.gloamingforest.progression_end.desc` = "Anything past this point in the Highlands is a work in progress. It will be finished in the future"
- Parent: `gloamingforest:progress_troll`; icon: `mc:structure_void`; frame=goal, announce_to_chat=False
- Criterion `plateau`: `minecraft:location` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"location":{"biomes":"gloamingforest:final_plateau"}}}]}`
- Criterion `previous_progression`: `minecraft:tick` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type_specific":{"type":"minecraft:player","advancements":{"gloamingforest:progress_troll":true}}}}]}`
- Requirements: `[["previous_progression"],["plateau"]]`
- sends_telemetry_event: True

#### `gloamingforest:quest_ram`

- Title: `advancement.gloamingforest.quest_ram` = "Consummate Baaahs"; description: `advancement.gloamingforest.quest_ram.desc` = "Give the %s what it is missing"
- Parent: `gloamingforest:root`; icon: `quest_ram_trophy`; frame=goal
- Criterion `quest_ram_complete`: `gloamingforest:complete_quest_ram` `{}`
- Requirements: `[["quest_ram_complete"]]`
- Rewards: `{"experience":100}`
- sends_telemetry_event: True

#### `gloamingforest:root`

- Title: `advancement.gloamingforest.root` = "Gloaming Forest"; description: `advancement.gloamingforest.root.desc` = "Enter the mysterious, magical woodlands: the Gloaming Forest"
- Parent: `(root)`; icon: `gloaming_portal_miniature_structure`; announce_to_chat=False; background `gloamingforest:textures/block/mazestone_large_brick.png`
- Criterion `in_tf`: `minecraft:location` `{"player":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `make_portal`: `gloamingforest:make_tf_portal` `{}`
- Requirements: `[["in_tf","make_portal"]]`
- sends_telemetry_event: True

#### `gloamingforest:troll`

- Title: `advancement.gloamingforest.troll` = "We Do a Little Trolling"; description: `advancement.gloamingforest.troll.desc` = "Find and kill a %s"
- Parent: `gloamingforest:progress_merge`; icon: `magic_beans`; 
- Criterion `troll`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:troll","location":{"structures":"gloamingforest:troll_cave"}}}]}`
- Requirements: `[["troll"]]`
- sends_telemetry_event: True

#### `gloamingforest:gloaming_dinner`

- Title: `advancement.gloamingforest.gloaming_dining` = "We Dine At Eternal Sundown"; description: `advancement.gloamingforest.gloaming_dining.desc` = "Eat every edible item type exclusive to Gloaming Forest"
- Parent: `gloamingforest:root`; icon: `mc:bowl`; frame=challenge
- Criterion `berry`: `minecraft:consume_item` `{"item":{"items":["gloamingforest:raspberry","gloamingforest:blackberry","gloamingforest:blueberry","gloamingforest:maloberry"]}}`
- Criterion `berry_medley`: `minecraft:consume_item` `{"item":{"items":"gloamingforest:berry_medley"}}`
- Criterion `experiment_115`: `minecraft:consume_item` `{"item":{"items":"gloamingforest:experiment_115"}}`
- Criterion `hydra_chop`: `minecraft:consume_item` `{"item":{"items":"gloamingforest:hydra_chop"}}`
- Criterion `jerky`: `minecraft:consume_item` `{"item":{"items":"#c:foods/jerky"}}`
- Criterion `maze_wafer`: `minecraft:consume_item` `{"item":{"items":"gloamingforest:maze_wafer"}}`
- Criterion `meef`: `minecraft:consume_item` `{"item":{"items":["gloamingforest:raw_meef","gloamingforest:cooked_meef"]}}`
- Criterion `meef_stroganoff`: `minecraft:consume_item` `{"item":{"items":"gloamingforest:meef_stroganoff"}}`
- Criterion `moss_soup`: `minecraft:consume_item` `{"item":{"items":"gloamingforest:moss_soup"}}`
- Criterion `slime_drop`: `minecraft:consume_item` `{"item":{"items":["gloamingforest:gelatinous_slime_drop","gloamingforest:gelatinous_maze_slime_drop"]}}`
- Criterion `venison`: `minecraft:consume_item` `{"item":{"items":["gloamingforest:raw_venison","gloamingforest:cooked_venison"]}}`
- Requirements: `[["venison"],["meef"],["meef_stroganoff"],["hydra_chop"],["maze_wafer"],["experiment_115"],["jerky"],["berry"],["slime_drop"],["berry_medley"],["moss_soup"]]`
- Rewards: `{"experience":75}`
- sends_telemetry_event: True

#### `gloamingforest:gloaming_hunter`

- Title: `advancement.gloamingforest.gloaming_hunter` = "The Silence of the Forest"; description: `advancement.gloamingforest.gloaming_hunter.desc` = "Hunt some of the local wildlife"
- Parent: `gloamingforest:root`; icon: `raven_feather`; 
- Criterion `adherent`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:adherent","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `alpha_yeti`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:alpha_yeti","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `armored_giant`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:armored_giant","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `bighorn_sheep`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:bighorn_sheep","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `blockchain_goblin`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:blockchain_goblin","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `boar`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:boar","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `carminite_broodling`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:carminite_broodling","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `carminite_ghastguard`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:carminite_ghastguard","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `carminite_ghastling`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:carminite_ghastling","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `carminite_golem`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:carminite_golem","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `death_tome`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:death_tome","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `deer`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:deer","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `dwarf_rabbit`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:dwarf_rabbit","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `fire_beetle`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:fire_beetle","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `giant_miner`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:giant_miner","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `harbinger_cube`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:harbinger_cube","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `hedge_spider`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:hedge_spider","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `helmet_crab`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:helmet_crab","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `hostile_wolf`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:hostile_wolf","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `hydra`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:hydra","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `king_spider`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:king_spider","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `knight_phantom`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:knight_phantom","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `kobold`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:kobold","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `lich`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:lich","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `lich_minion`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:lich_minion","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `lower_goblin_knight`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:lower_goblin_knight","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `maze_slime`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:maze_slime","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `minoshroom`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:minoshroom","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `minotaur`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:minotaur","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `mist_wolf`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:mist_wolf","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `mosquito_swarm`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:mosquito_swarm","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `naga`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:naga","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `penguin`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:penguin","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `pinch_beetle`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:pinch_beetle","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `plateau_boss`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:plateau_boss","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `quest_ram`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:quest_ram","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `raven`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:raven","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `redcap`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:redcap","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `redcap_sapper`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:redcap_sapper","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `skeleton_druid`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:skeleton_druid","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `slime_beetle`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:slime_beetle","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `snow_guardian`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:snow_guardian","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `snow_queen`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:snow_queen","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `squirrel`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:squirrel","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `stable_ice_core`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:stable_ice_core","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `swarm_spider`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:swarm_spider","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `tiny_bird`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:tiny_bird","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `towerwood_borer`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:towerwood_borer","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `troll`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:troll","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `unstable_ice_core`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:unstable_ice_core","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `upper_goblin_knight`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:upper_goblin_knight","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `ur_ghast`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:ur_ghast","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `winter_wolf`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:winter_wolf","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `wraith`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:wraith","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Criterion `yeti`: `minecraft:player_killed_entity` `{"entity":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"type":"gloamingforest:yeti","location":{"dimension":"gloamingforest:gloaming_forest"}}}]}`
- Requirements: `[["adherent","armored_giant","bighorn_sheep","blockchain_goblin","dwarf_rabbit","death_tome","deer","fire_beetle","giant_miner","lower_goblin_knight","upper_goblin_knight","harbinger_cube","hedge_spider","helmet_crab","hostile_wolf","hydra","king_spider","knight_phantom","kobold","lich","lich_minion","maze_slime","carminite_ghastling","minoshroom","minotaur","mist_wolf","mosquito_swarm","naga","penguin","pinch_beetle","plateau_boss","quest_ram","raven","redcap","redcap_sapper","skeleton_druid","slime_beetle","snow_guardian","snow_queen","squirrel","stable_ice_core","swarm_spider","tiny_bird","carminite_broodling","carminite_ghastguard","carminite_golem","towerwood_borer","troll","unstable_ice_core","ur_ghast","boar","winter_wolf","wraith","yeti","alpha_yeti"]]`
- sends_telemetry_event: True

#### `gloamingforest:uncraft_uncrafting_table`

- Title: `advancement.gloamingforest.uncraft_uncrafting_table` = "A Step too Far"; description: `advancement.gloamingforest.uncraft_uncrafting_table.desc` = "Uncraft an Uncrafting Table"
- Parent: `gloamingforest:root`; icon: `uncrafting_table`; hidden=True
- Criterion `uncraft_table`: `gloamingforest:uncraft_item` `{"item":{"items":"gloamingforest:uncrafting_table"}}`
- Requirements: `[["uncraft_table"]]`
- sends_telemetry_event: True

## Appendix B: Strings (95 en_us keys owned)

Keys already shown above (block/item/entity names, subtitles) are not repeated. The 29 Bedrock locales are generated from upstream's lang files (R9); owners use these keys with the English text as fallback.

| Key | English |
|---|---|
| `config.gloamingforest.locked_toasts` | Disable Locked Biome Toasts |
| `config.gloamingforest.locked_toasts.tooltip` | Disables the toasts that appear when entering a locked biome. Not recommended if you're unfamiliar with progression. |
| `gamerule.tfEnforcedProgression` | Gloaming Forest: Enforced Progression |
| `gamerule.tfEnforcedProgression.description` | Some Gloaming Forest biomes are locked until you defeat certain bosses in the dimension. Check your advancements for the progression order. |
| `misc.gloamingforest.biome_locked` | Biome Locked! |
| `misc.gloamingforest.biome_locked_2` | Check your advancements |

