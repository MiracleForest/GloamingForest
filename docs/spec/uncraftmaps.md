# Uncrafting table and maps (U13 `uncraftmaps`)

Behaviour spec of unit **U13** (wave W1), translated from The Twilight Forest 4.8 (`9e40a757`, MC 1.21.1) by F16. It
is the checklist the U13 worker implements and the A2 audit compares against. Sections 1-5 are the behaviour; Appendix A
lists every id U13 owns with TF's registration and data; Appendix B the strings.

## 1. Scope

- **Uncrafting table**: block `uncrafting_table` (+ item), menu type `twilightforest:uncrafting`, screen, recipe type /
  serializer `twilightforest:uncrafting` (3 recipes: `uncrafting/tipped_arrow` 8 tipped arrows -> 8 arrows, cost 4;
  `uncrafting/written_book` written book -> book, cost 0; `relinquish_keepsakes` keepsake casket -> skull chest,
  cost 0; an input matches by item with count >= `input_count`), loot condition
  `uncrafting_table_enabled`, stat `uncrafting_table_interactions`, trigger `uncraft_item`, sound
  `block.twilightforest.uncrafting_table.activate`, tags `#banned_uncraftables`, `#banned_uncrafting_ingredients`,
  `#uncrafting_ignores_cost`, the ten `common.Uncrafting Table.*` config keys (section `uncraftmaps`), net channels
  `uncraftmaps.gui` (button presses) and `uncraftmaps.sync_config`.
- **Maps**: `magic_map` (empty) / `filled_magic_map`, `maze_map` / `filled_maze_map`, `ore_map` / `filled_ore_map`,
  `magic_map_focus`, `maze_map_focus`; cloning recipe types `magic_map_cloning_recipe`, `maze_map_cloning_recipe`;
  data maps `magic_map_color` (biome -> map colour) and `ore_map_color` (block -> map colour) (R7); tag
  `dimension_type/#allows_magic_map_charting`; net `uncraftmaps.magic_map`, `uncraftmaps.maze_map`; subcommands
  `/twilightforest map_locator ...` (owners id `maplocator`) and `/twilightforest biomepng ...` (owners id `mapbiomes`).

Not U13: landmark map decorations and the conquest flag (F6 landmark contract; U13 reads them), TF's map decoration
types (owned by the structure units), the recipes it uncrafts (everyone's).

Contracts: recipes, inventory, client.ui, client.world, landmark, data, progression.

## 2. TF sources

`block/UncraftingTableBlock.java`, `inventory/UncraftingMenu.java`, `inventory/{UncraftingContainer,UncraftingInputContainer,UncrafterPlaceRecipe,UncraftingPlaceRecipe}.java`,
`inventory/slot/{UncraftingSlot,AssemblySlot,UncraftingResultSlot}.java`, `client/UncraftingScreen.java`,
`client/UncraftingRecipeBookComponent.java`, `item/recipe/UncraftingRecipe.java`, `item/recipe/UncraftingTableCondition.java`,
`loot/conditions/UncraftingTableEnabledCondition.java`, `network/{UncraftingGui,SyncUncraftingTableConfig}Packet.java`,
`config/{TFCommonConfig,TFConfig,ConfigSetup}.java`, `item/MagicMapItem.java`, `item/EmptyMagicMapItem.java`,
`item/MazeMapItem.java`, `item/EmptyMazeMapItem.java`, `item/mapdata/{TFMagicMapData,TFMazeMapData}.java`,
`item/recipe/{MagicMapCloning,MazeMapCloning}Recipe.java`, `network/{MagicMap,MazeMap}Packet.java`,
`client/renderer/map/{ConqueredMapIconRenderer,MagicMapPlayerIconRenderer}.java`, `command/MapLocatorCommand.java`,
`command/MapBiomesCommand.java`, `init/TFDataMaps.java`, `textures/gui/guigoblintinkering.png` (imported asset).

## 3. Behaviour

### 3.1 Uncrafting table block

`strength(2.5)`, WOOD, map FIRE, `ignitedByLava`; state `powered`. Use (no item needed): if
`disableUncraftingTable` -> action-bar `block.twilightforest.uncrafting_table.disabled`, pass; else open the menu
(title `container.twilightforest.uncrafting_table`) and award `uncrafting_table_interactions`. When the redstone signal
turns on with an amethyst block below, plays `block.twilightforest.uncrafting_table.activate` (0.5, 1). Item tooltip
shows the disabled message in red when the table is disabled.

### 3.2 Menu (`UncraftingMenu`, 56 slots)

Slot indices / positions (GUI pixels, relative to the 176x166 frame):

| Index | Slot | Position |
|---|---|---|
| 0 | input (tinker input) | (13, 35) |
| 1 | result (recrafted item) | (147, 35) |
| 2-10 | uncrafting matrix (ghost, hidden: x = 300000 + 18c) | rendered in the assembly slots |
| 11-19 | assembly 3x3 | (62 + 18c, 17 + 18r) |
| 20-46 | player inventory | (8 + 18c, 84 + 18r) |
| 47-55 | hotbar | (8 + 18c, 142) |

- **Uncrafting** (input changed): candidate recipes = all 3x3-fitting crafting recipes whose result item equals the
  input and whose result count <= the input count, that are shaped (or any crafting recipe if
  `enableShapelessCrafting`), complete, with ingredients, passing the id list (`disableUncraftingRecipes`, default
  `["twilightforest:giant_log_to_oak_planks"]`, inverted by `flipRecipeList`) and the namespace list
  (`blacklistedUncraftingModIds`, inverted by `flipIdList`), plus `twilightforest:uncrafting` recipes whose input
  matches with enough count. Inputs in `#banned_uncraftables` show nothing (tooltip
  `container.twilightforest.uncrafting_table.disabled_item`, red). The recipe shown is
  `recipes[floorMod(unrecipeInCycle, n)]`; each ingredient shows its `floorMod(ingredientsInCycle, m)`-th matching item
  (excluding `#banned_uncrafting_ingredients`), count 1, laid out by the shaped pattern. Damaged inputs mark ceil(max(4,
  damageable parts) x damage fraction) damageable ingredients as **broken** (marker info tag `TwilightForestMarker`);
  ingredients with a crafting remainder or barriers are marked too. Marked ingredients cannot be taken and are not
  counted.
- Uncrafting cost (XP levels) = recipe `cost` for uncrafting recipes, else round(unmarked damageable parts
  (ingredients not in `#uncrafting_ignores_cost`) x `uncraftingXpCostMultiplier` (1.0)); 0 while the assembly grid is
  not empty; with `disableUncrafting` only uncrafting recipes work.
- Taking any uncrafting-grid ingredient (only when the assembly grid is empty, the player has the levels or is
  creative): charges the levels, copies all unmarked ingredients into the assembly grid, fires
  `twilightforest:uncraft_item` (with the input), returns a container item's contents to the player, removes the
  recipe's result count from the input.
- **Recrafting / repairing**: the assembly grid (merged over the ghost ingredients for empty cells while the input is
  present) is matched against crafting recipes (`floorMod(recipeInCycle, n)`), honouring `doLimitedCrafting`. With an
  input present the result is offered only when it is the same kind of tool/armor (pickaxe/axe/shovel/hoe/sword/bow/
  crossbow/fishing rod tags, or armor of the same slot): enchantments of the input are transferred (existing result
  enchantments kept, unsupported dropped) and the **recrafting cost** = max(1, (different item ? filled assembly
  slots : 0) + sum over enchantments of (weight modifier x level + 1) [weight 1 -> 8, 2 -> 4, 3-5 -> 2, else 1] +
  (1 + damaged parts) x result enchantment count) x `repairingXpCostMultiplier` (1.0); 0 for non-enchantable results.
  Taking the result needs the levels (creative free).
- Clicking an empty assembly slot while the assembly grid is empty and the cursor is empty targets the matching
  uncrafting slot (take the ghost ingredient). Shift-click: input -> inventory, result -> inventory (crafted-by, drop
  overflow), inventory -> input, assembly -> inventory. Closing returns the assembly grid and input. Recipe book type
  CRAFTING over the assembly grid (`UncrafterPlaceRecipe`). `stillValid` is false when the table is disabled.
- Buttons send `UncraftingGuiPacket(type)`: 0/1 next/previous uncraft recipe, 2/3 next/previous ingredient, 4/5
  next/previous recraft recipe; the server updates the cycle counters and recomputes. The server syncs the uncrafting
  config to clients on login/reload (`SyncUncraftingTableConfigPacket`: both multipliers, shapeless, ingredient
  switching, uncrafting disabled, table disabled, recipe list + flip, mod id list + flip).

### 3.3 Screen (`UncraftingScreen`, texture `textures/gui/guigoblintinkering.png`)

- Frame 176x166 drawn from (0,0). Recipe book button (vanilla sprites) at (145, 7), 20x18; the recipe book shifts
  the frame like the crafting table (narrow under width 379).
- Cycle buttons (14x9, texture u 176 v 0, hover u +14, "down" v +9): uncraft next (40, 22) / previous (40, 55);
  recraft next (121, 22) / previous (121, 55). Ingredient mini buttons (8x6, u 176 v 41, hover u +8, down v +6) at
  (27, 56) and (27, 63), hidden when `disableIngredientSwitching`. Tooltips
  `container.twilightforest.uncrafting_table.cycle_{next,back}_{uncraft,ingredient,recipe}`.
- Mouse wheel: over x 27..33, y 56..69 cycles ingredients; over x 40..54, y 22..64 the uncraft recipe; over
  x 121..135, y 22..64 the recraft recipe (up = next).
- Ghost ingredients: each uncrafting ingredient is drawn as a fake item in its assembly slot with an overlay
  0x9F8B8B8B (0x80FF8B8B when marked/broken); hovering an empty assembly slot shows the ghost item tooltip.
- Costs: uncrafting cost right-aligned ending at x 48, y 38; recrafting cost ending at x 130, y 38; colour 0x80FF20 if
  affordable (or creative) else 0xA00000.
- Labels: title at (6, 6) colour 0x404040; "Inventory" at (7, 72); if `disableUncrafting`,
  `container.twilightforest.uncrafting_table.uncrafting_disabled` in dark red at (6, 72) instead.

### 3.4 Magic map (`EmptyMagicMapItem`, `MagicMapItem`, `TFMagicMapData`)

- `magic_map` use: only in dimensions in `#twilightforest:allows_magic_map_charting` (else action bar
  `misc.twilightforest.magic_map_fail`); consumes one, stat used, `ui.cartography_table.take_result`, gives a
  `filled_magic_map` (scale 4, tracking) centred on the 2048-block magic map grid: centre = round((x - 1024)/2048) *
  2048 + 1024 (same for z). Data names `magicmap_<id>`.
- Update (holder in the same dimension, server): 16 blocks per pixel, view radius 32 pixels around the player
  (fuzzy edge beyond radius - 2, checkerboard), pixel colour = the biome's `magic_map_color` data map
  (`{color, brightness}`, default COLOR_MAGENTA) sampled every 4 blocks with stream biomes winning over neighbours
  (cached per 512x512 region); landmark centres (`LegacyLandmarkPlacements`) add the landmark's map decoration
  (`LandmarkStructure.getMapIcon`, rotation 180) and record whether it is conquered (`conquered_structures` list,
  synced through `MagicMapPacket`; conquered icons are drawn with the conquered overlay by
  `ConqueredMapIconRenderer`; the player icon by `MagicMapPlayerIconRenderer`).
- Tooltip: map id, or with advanced tooltips id, scale and level.

### 3.5 Maze and ore maps (`EmptyMazeMapItem`, `MazeMapItem`, `TFMazeMapData`)

- `maze_map` / `ore_map` use: creates `filled_maze_map` / `filled_ore_map` at scale 0 (1 block per pixel), centre
  snapped to 128-block cells (floor((x + 64)/128)*128 + 64 - 64), but inside a labyrinth chunk the map snaps to the
  labyrinth centre; records `yCenter` = the player's y. Sound and stat as above. Data names `mazemap_<id>`.
- Update while held (main or offhand, not locked): view radius **16** pixels (8 with a ceiling); every tick one column
  stripe (x & 15 == step & 15); per pixel the block at `yCenter` (if stone or air, the first stone within +-3 decides,
  brightness 2 above / 0 below), colour by map colour; ore maps add the block's `ore_map_color` data-map colour (weight
  1000) or COLOR_PINK for `#c:ores`. When the holder is more than 3 blocks above/below `yCenter`, their decoration
  becomes PLAYER_OFF_MAP. Advanced tooltip `item.twilightforest.maze_map.y_level`.
- `magic_map_focus` and `maze_map_focus` are crafting ingredients (recipes in Appendix A).
- Cloning recipes (`magic_map_cloning_recipe`, `maze_map_cloning_recipe`): a filled map + n empty maps of the same kind
  -> n + 1 copies (vanilla map cloning logic for TF maps).

### 3.6 Commands

- `/twilightforest map_locator <structure|#tag> [players] [skip_known_structures]` (permission 2): finds the nearest
  structure (search radius 100 chunks) and gives each player a vanilla filled map (scale 4) with a red X target; prints
  the vanilla locate result.
- `/twilightforest biomepng [width height [showBiomePercents]]` (permission 2, default 4096x4096): renders the biome
  layout around the source to `generated/biomemaps/<seed>/biome_map-<seed>-(x.z)-(x2.z2).png` with progress messages
  `commands.tffeature.biomepng.progress` and biome counts.

## 4. Bedrock adaptation notes

- The table needs a custom 56-slot container screen: client mod via `client::screen::registerScreen("uncrafting", ...)`
  (F12 framework, TF GUI skin from the imported `guigoblintinkering.png`); vanilla clients get a DDUI/form flow (pick
  input from the inventory, list candidate recipes, take all ingredients) through `ui::ddui` (best effort; README
  deviation). Container storage via `inventory::registerBlock` (the input and assembly grid are per-player transient).
- Recipe lookups through `recipes::producing(itemId)` and `recipes::ofType("twilightforest:uncrafting")`; config
  sync to companions over `uncraftmaps.sync_config`.
- Maps: Bedrock map items are vanilla `filled_map`s; TF map ids are custom items whose map data U13 renders with
  `client::maps::view` / `setIcon` (client mod) and `ClientboundMapItemData` equivalents (MapItemDataPacket) for
  vanilla clients with decorations mapped to the 30 vanilla types (README deviation).
- `biomepng` writes a PNG from the server: keep it (useful for debugging) under the world folder.

## 5. Acceptance checklist (selftests `uncraftmaps.*`, dx 1300-1399)

- Uncrafting an iron pickaxe gives 3 iron + 2 sticks with cost 3 (damageable parts) and marks broken parts on a damaged
  one; banned items show nothing; config lists filter recipes; uncrafting recipes (tipped arrow x8 -> 8 arrows cost
  4, written book -> book cost 0, keepsake casket -> skull chest cost 0 via `relinquish_keepsakes`).
- Recrafting cost formula with enchantments; enchantment transfer; XP requirement.
- GUI screenshot vs `guigoblintinkering.png` (buttons, costs, ghost overlays).
- Magic map centre snapping and biome colours from the data map; landmark icons with conquered state.
- Maze map snaps to labyrinth centre; ore map highlights ores; cloning recipes.
- `map_locator` gives a map with a red X.

## Appendix A: Owned ids (generated inventory)

Every id this unit owns in `docs/spec/owners.tsv`, with the TF 4.8 data the W1 worker must reproduce. Registration statements are TF's own Java (`init/TF*.java`), condensed (namespaces such as `MapColor.` dropped); loot, recipe, tag, advancement and data rows are summarised from TF's generated JSON (`src/generated/resources`). Loot notation: `self` = drops the block itself when it survives the explosion; `alternatives(a → b)` = first matching child; `[if ...]` = conditions; `{...}` = functions. In summaries `mc:` is `minecraft:` and ids without a namespace are `twilightforest:`.

| Kind | Count |
|---|---|
| `advancement` | 5 |
| `block` | 1 |
| `block_item` | 1 |
| `config_key` | 10 |
| `config_section` | 1 |
| `data_map` | 2 |
| `e2e` | 1 |
| `item` | 8 |
| `lang` | 49 |
| `loot_condition` | 1 |
| `loot_table` | 1 |
| `menu` | 1 |
| `net` | 5 |
| `path` | 20 |
| `recipe` | 10 |
| `recipe_serializer` | 3 |
| `selftest` | 1 |
| `selftest_dx` | 1 |
| `sound_event` | 1 |
| `stat` | 1 |
| `subcommand` | 2 |
| `tag` | 4 |
| `trigger` | 1 |
| `worlddb` | 1 |

### Unit facts

| Kind | Ids |
|---|---|
| Selftest glob (`selftest`) | `uncraftmaps.*` |
| Selftest dx range (`selftest_dx`) | `1300-1399` |
| World DB keys (`worlddb`) | `gf:uncraftmaps:*` |
| E2E scenario (`e2e`) | `uncraftmaps` |
| Net channels (`net`) | `uncraftmaps.*`, `uncraftmaps.gui`, `uncraftmaps.magic_map`, `uncraftmaps.maze_map`, `uncraftmaps.sync_config` |
| Config section (`config_section`) | `uncraftmaps` |
| Owned paths (`path`) | `**/*.d/uncraftmaps.json`, `assets/data/*/uncraftmaps.json`, `assets/gen/audio/uncraftmaps/**`, `assets/gen/geo/uncraftmaps/**`, `assets/gen/import/uncraftmaps/**`, `assets/lang/*/uncraftmaps.json`, `assets/pack/{bp,rp}/**/uncraftmaps/**`, `assets/pack/rp/texts.d/*/uncraftmaps.lang`, `assets/structures/uncraftmaps/**`, `assets/tags/**/*.d/uncraftmaps.json`, `docs/units/uncraftmaps.md`, `src/client/uncraftmaps/**`, `src/core/uncraftmaps/**`, `src/feature/ext/uncraftmaps/**`, `src/feature/uncraftmaps/**`, `tests/uncraftmaps/**`, `tools/e2e/*/scenarios/uncraftmaps{,.*}.ps1`, `tools/import/overrides/uncraftmaps.json`, `tools/sounds/recipes/uncraftmaps.json`, `tools/structures/uncraftmaps/**` |

### Blocks (1)

Item column: `BlockItem` = TF registers a plain block item with the block (`TFBlocks.register`), the id is also a `block_item` row; `-` = no item of the same id (the block is placed by another item, or has none).

Block states come from TF's blockstate JSON (`multipart:` lists only the property values the model parts test). Notes collect `RegistrationEvents` (fire, axe stripping, flower pots, jar lids), dispenser behaviours and the `neoforge:compostables` / `neoforge:furnace_fuels` data maps.

| Block | English name | TF registration (`TFBlocks`) | Block states | Item | Loot (`blocks/<id>`) | Notes |
|---|---|---|---|---|---|---|
| `twilightforest:uncrafting_table` | Uncrafting Table | `register("uncrafting_table", new UncraftingTableBlock(Properties.of().ignitedByLava().mapColor(FIRE).sound(WOOD).strength(2.5F)));` | powered: false/true | BlockItem | self | block tags: #c:player_workstations/crafting_tables, #mc:mineable/axe, #mineable_with_block_and_chain |

### Items (8)

| Item | English name | TF registration (`TFItems`) | Notes |
|---|---|---|---|
| `twilightforest:filled_magic_map` | Magic Map | `ITEMS.register("filled_magic_map", new MagicMapItem(new Item.Properties()));` |  |
| `twilightforest:filled_maze_map` | Maze Map | `ITEMS.register("filled_maze_map", new MazeMapItem(false, new Item.Properties()));` |  |
| `twilightforest:filled_ore_map` | Maze/Ore Map | `ITEMS.register("filled_ore_map", new MazeMapItem(true, new Item.Properties()));` |  |
| `twilightforest:magic_map` | Blank Magic Map | `ITEMS.register("magic_map", new EmptyMagicMapItem(new Item.Properties()));` |  |
| `twilightforest:magic_map_focus` | Magic Map Focus | `ITEMS.register("magic_map_focus", new Item(new Item.Properties()));` |  |
| `twilightforest:maze_map` | Blank Maze Map | `ITEMS.register("maze_map", new EmptyMazeMapItem(false, new Item.Properties()));` |  |
| `twilightforest:maze_map_focus` | Maze Map Focus | `ITEMS.register("maze_map_focus", new Item(new Item.Properties()));` |  |
| `twilightforest:ore_map` | Blank Maze/Ore Map | `ITEMS.register("ore_map", new EmptyMazeMapItem(true, new Item.Properties()));` |  |

### Recipe serializers / types (3)

| Id | TF registration (`TFRecipes.java`) |
|---|---|
| `twilightforest:magic_map_cloning_recipe` | `RECIPE_SERIALIZERS.register("magic_map_cloning_recipe", new SimpleCraftingRecipeSerializer<>(MagicMapCloningRecipe::new));` |
| `twilightforest:maze_map_cloning_recipe` | `RECIPE_SERIALIZERS.register("maze_map_cloning_recipe", new SimpleCraftingRecipeSerializer<>(MazeMapCloningRecipe::new));` |
| `twilightforest:uncrafting` | `RECIPE_SERIALIZERS.register("uncrafting", UncraftingRecipe.Serializer::new); ;; RECIPE_TYPES.register("uncrafting", () -> RecipeType.simple(TwilightForestMod.prefix("uncrafting")));` |

### Loot condition types (1)

| Id | TF registration (`TFLoot.java`) |
|---|---|
| `twilightforest:uncrafting_table_enabled` | `CONDITIONS.register("uncrafting_table_enabled", new LootItemConditionType(UncraftingTableEnabledCondition.CODEC)); ;; CONDITIONALS.register("uncrafting_table_enabled", () -> UncraftingTableCondition.CODEC);` |

### Menu types (1)

| Id | TF registration (`TFMenuTypes.java`) |
|---|---|
| `twilightforest:uncrafting` | `CONTAINERS.register("uncrafting", new MenuType<>(UncraftingMenu::fromNetwork, FeatureFlags.REGISTRY.allFlags()));` |

### Stats (1)

| Id | TF registration (`TFStats.java`) |
|---|---|
| `twilightforest:uncrafting_table_interactions` (Interactions with Uncrafting Table) | `makeTFStat("uncrafting_table_interactions");` |

### Advancement triggers (1)

| Id | TF registration (`TFAdvancements.java`) |
|---|---|
| `twilightforest:uncraft_item` | `TRIGGERS.register("uncraft_item", UncraftItemTrigger::new);` |

### Data maps (2)

- `twilightforest:magic_map_color` (registry `worldgen/biome`): `{"values":{"twilightforest:clearing":{"brightness":2,"map_color":{"color":8368696,"id":1}},"twilightforest:dark_forest":{"brightness":3,"map_color":{"color":6717235,"id":27}},"twilightforest:dark_forest_center":{"brightness":3,"map_color":{"color":14188339,"id":15}},"twilightforest:dense_forest":{"brightness":0,"map_color":{"color":31744,"id":7}},"twilightforest:dense_mushroom_forest":{"brightness":0,"map_color":{"color":15892389,"id":20}},"twilightforest:enchanted_forest":{"brightness":2,"map_color":{"color":5013401,"id":23}},"twilightforest:final_plateau":{"brightness":2,"map_color":{"color":10066329,"id":22}},"twilightforest:fire_swamp":{"brightness":1,"map_color":{"color":7340544,"id":35}},"twilightforest:firefly_forest":{"brightness":1,"map_color":{"color":55610,"id":33}},"twilightforest:forest":{"brightness":1,"map_color":{"color":31744,"id":7}},"twilightforest:glacier":{"brightness":1,"map_color":{"color":10526975,"id":5}},"twilightforest:highlands":{"brightness":0,"map_color":{"color":9923917,"id":10}},"twilightforest:lake":{"brightness":3,"map_color":{"color":4210943,"id":12}},"twilightforest:mushroom_forest":{"brightness":0,"map_color":{"color":14188339,"id":15}},"twilightforest:oak_savannah":{"brightness":0,"map_color":{"color":8368696,"id":1}},"twilightforest:snowy_forest":{"brightness":1,"map_color":{"color":16777215,"id":8}},"twilightforest:spooky_forest":{"brightness":0,"map_color":{"color":8339378,"id":24}},"twilightforest:stream":{"brightness":1,"map_color":{"color":4210943,"id":12}},"twilightforest:swamp":{"brightness":3,"map_color":{"color":6085589,"id":31}},"twilightforest:thornlands":{"brightness":3,"map_color":{"color":9402184,"id":13}}}}`
  TF registration: `TwilightForestMod.prefix("magic_map_color"), Registries.BIOME, MagicMapBiomeColor.CODEC).synced(MagicMapBiomeColor.CODEC, false).build();`
- `twilightforest:ore_map_color` (registry `block`): `{"values":{"#minecraft:coal_ores":{"map_color":{"color":1644825,"id":29}},"#minecraft:copper_ores":{"map_color":{"color":14188339,"id":15}},"#minecraft:diamond_ores":{"map_color":{"color":6085589,"id":31}},"#minecraft:emerald_ores":{"map_color":{"color":55610,"id":33}},"#minecraft:gold_ores":{"map_color":{"color":16445005,"id":30}},"#minecraft:iron_ores":{"map_color":{"color":14200723,"id":60}},"#minecraft:lapis_ores":{"map_color":{"color":4882687,"id":32}},"#minecraft:redstone_ores":{"map_color":{"color":10040115,"id":28}},"minecraft:ancient_debris":{"map_color":{"color":4993571,"id":48}}}}`
  TF registration: `TwilightForestMod.prefix("ore_map_color"), Registries.BLOCK, OreMapOreColor.CODEC).synced(OreMapOreColor.CODEC, false).build();`

### Sound events (1)

`sounds.json` content (vanilla `event` references can be played with the matching Bedrock vanilla sound; TF file names are listed only as names: the audio itself is All Rights Reserved and is re-synthesised by F3/the owner).

| Event | Subtitle | sounds.json entries |
|---|---|---|
| `block.twilightforest.uncrafting_table.activate` |  | event block.end_portal.spawn |

### Tags (4)

| Tag | Members (TF generated JSON) |
|---|---|
| `dimension_type/#twilightforest:allows_magic_map_charting` | twilight_forest_type |
| `item/#twilightforest:banned_uncraftables` | (empty) |
| `item/#twilightforest:banned_uncrafting_ingredients` | infested_towerwood, hollow_oak_sapling, time_sapling, transformation_sapling, mining_sapling, sorting_sapling, transformation_powder |
| `item/#twilightforest:uncrafting_ignores_cost` | #c:rods/wooden |

### Loot tables (1)

Block loot tables summarised in the Loot column of the block table: `twilightforest:blocks/uncrafting_table`

### Recipes (10)

Recipe-unlock advancements (generated by F1 into the recipe JSON, R21) are listed with their recipe; criteria are the `has_*` triggers besides `has_the_recipe`.

| Recipe | Type | Ingredients | Result | Unlock advancement (criteria) |
|---|---|---|---|---|
| `twilightforest:magic_map` | mc:crafting_shaped | '###' / '#•#' / '###' ; #=#c:paper, •=magic_map_focus | magic_map | `twilightforest:recipes/misc/magic_map` (has_item: has magic_map_focus) |
| `twilightforest:magic_map_cloning_recipe` | magic_map_cloning_recipe | {} | - | - |
| `twilightforest:magic_map_focus` | mc:crafting_shapeless | raven_feather + torchberries + #c:dusts/glowstone | magic_map_focus | `twilightforest:recipes/misc/magic_map_focus` (has_berries: has torchberries; has_feather: has raven_feather) |
| `twilightforest:maze_map` | mc:crafting_shaped | '###' / '#•#' / '###' ; #=#c:paper, •=maze_map_focus | maze_map | `twilightforest:recipes/misc/maze_map` (has_item: has maze_map_focus) |
| `twilightforest:maze_map_cloning_recipe` | maze_map_cloning_recipe | {} | - | - |
| `twilightforest:ore_map` | mc:crafting_shapeless | maze_map + #c:storage_blocks/diamond + #c:storage_blocks/gold + #c:storage_blocks/iron | ore_map | `twilightforest:recipes/misc/ore_map` (has_item: has maze_map) |
| `twilightforest:relinquish_keepsakes` | uncrafting | {"cost":0,"input":{"item":"twilightforest:keepsake_casket"},"key":{"C":{"item":"twilightforest:skull_chest"}},"pattern":["C"]} | - | - |
| `twilightforest:uncrafting_table` | mc:crafting_shaped | '###' / '#X#' / '###' ; #=mc:crafting_table, X=maze_map_focus [conditions [{"type":"twilightforest:uncrafting_table_enabled"}]] | uncrafting_table | `twilightforest:recipes/decorations/uncrafting_table` (has_uncrafting_table: has uncrafting_table) |
| `twilightforest:uncrafting/tipped_arrow` | uncrafting | {"cost":4,"input":{"item":"minecraft:tipped_arrow"},"input_count":8,"key":{"A":{"item":"minecraft:arrow"}},"pattern":["AAA","A A","AAA"]} | - | - |
| `twilightforest:uncrafting/written_book` | uncrafting | {"cost":0,"input":{"item":"minecraft:written_book"},"key":{"B":{"item":"minecraft:book"}},"pattern":["B"]} | - | - |

### Config keys (10)

See [config.md](config.md) for the section mapping; defaults and translation ids below are TF's.

| TF key | Default | Translation id | English (config screen) |
|---|---|---|---|
| `common.Uncrafting Table.blacklistedUncraftingModIds` | `new ArrayList<>()` | `uncrafting_mod_ids` | Disabled Mod IDs — Here, you can disable all items from certain mods from being uncrafted. Input a valid mod id to disable all uncrafting recipes from that mod. Example: "twilightforest" will disable all uncrafting recipes from this mod. |
| `common.Uncrafting Table.disableIngredientSwitching` | `false` | `ingredient_switching` | Disable Ingredient Switching — If true, the uncrafting table will no longer allow you to switch between ingredients if a recipe uses a tag for crafting. This will remove the functionality for ALL RECIPES! If you want to prevent certain ingredients from showing up in the first place, use the "twilightforest:banned_uncrafting_ingredients" tag. |
| `common.Uncrafting Table.disableUncrafting` | `false` | `disable_uncrafting` | Disable Uncrafting — Disables the uncrafting function of the uncrafting table. Recommended as a last resort if there's too many things to change about its behavior (or you're just lazy, I dont judge). Do note that special uncrafting recipes are not disabled as the mod relies on them for other things. |
| `common.Uncrafting Table.disableUncraftingRecipes` | `List.of("twilightforest:giant_log_to_oak_planks")` | `uncrafting_recipes` | Disabled Recipes — If you don't want to disable uncrafting altogether, and would rather disable certain recipes, this is for you. To add a recipe, add the mod id followed by the name of the recipe. You can check this in things like JEI. Example: "twilightforest:firefly_particle_spawner" will disable uncrafting the particle spawner into a firefly jar, firefly, and poppy. If an item has multiple crafting recipes and you wish to disable them all, add the item to the "twilightforest:banned_uncraftables" item tag. If you have a problematic ingredient, like infested towerwood for example, add the item to the "twilightforest:banned_uncrafting_ingredients" item tag. |
| `common.Uncrafting Table.disableUncraftingTable` | `false` | `disable_uncrafting_table` | Disable Uncrafting Table — Disables any usage of the uncrafting table, as well as prevents it from showing up in loot or crafted. Please note that table has more uses than just uncrafting, you can read about them here! http://benimatic.com/tfwiki/index.php?title=Uncrafting_Table It is highly recommended to keep the table enabled as the mod has special uncrafting exclusive recipes, but the option remains for people that dont want the table to be functional at all. If you are looking to just prevent normal crafting recipes from being reversed, consider using the 'disableUncrafting' option instead. |
| `common.Uncrafting Table.enableShapelessCrafting` | `false` | `shapeless_uncrafting` | Shapeless Uncrafting — If true, the uncrafting table will also be allowed to uncraft shapeless recipes. The table was originally intended to only take shaped recipes, but this option remains for people who wish to keep the functionality. |
| `common.Uncrafting Table.flipIdList` | `false` | `uncrafting_mod_id_flip` | Mod ID Blacklist — If true, this will invert the above option from a blacklist to a whitelist. |
| `common.Uncrafting Table.flipRecipeList` | `false` | `uncrafting_recipes_flip` | Recipe Blacklist — If true, this will invert the above uncrafting recipe list from a blacklist to a whitelist. |
| `common.Uncrafting Table.repairingXpCostMultiplier` | `1.0D` | `repairing_xp_cost` | Repairing Cost Multiplier — Multiplies the total XP cost of repairing an item and rounds up. Higher values means the recipe will cost more to repair, lower means less. Set to 0 to disable the cost altogether. |
| `common.Uncrafting Table.uncraftingXpCostMultiplier` | `1.0D` | `uncrafting_xp_cost` | Uncrafting Cost Multiplier — Multiplies the total XP cost of uncrafting an item and rounds up. Higher values means the recipe will cost more to uncraft, lower means less. Set to 0 to disable the cost altogether. Note that this only affects reversed crafting recipes, uncrafting recipes will still use the same cost as they normally would. |

### subcommand (2)

`mapbiomes`, `maplocator`

## Appendix B: Strings (49 en_us keys owned)

Keys already shown above (block/item/entity names, subtitles) are not repeated. The 29 Bedrock locales are generated from TF's lang files (R9); owners use these keys with the English text as fallback.

| Key | English |
|---|---|
| `block.twilightforest.uncrafting_table.disabled` | This block has been disabled. |
| `commands.tffeature.biomepng.counts_header` | Approximate biome-block counts within a %sx%s region |
| `commands.tffeature.biomepng.progress` | %s%% Done mapping |
| `commands.tffeature.biomepng.save_failed` | Could not save image! Please report this! |
| `commands.tffeature.biomepng.save_success` | Image saved! |
| `config.twilightforest.disable_uncrafting` | Disable Uncrafting |
| `config.twilightforest.disable_uncrafting_table` | Disable Uncrafting Table |
| `config.twilightforest.disable_uncrafting_table.tooltip` | Disables any usage of the uncrafting table, as well as prevents it from showing up in loot or crafted. Please note that table has more uses than just uncrafting, you can read about them here! http://benimatic.com/tfwiki/index.php?title=Uncrafting_Table It is highly recommended to keep the table enabled as the mod has special uncrafting exclusive recipes, but the option remains for people that dont want the table to be functional at all. If you are looking to just prevent normal crafting recipes from being reversed, consider using the 'disableUncrafting' option instead. |
| `config.twilightforest.disable_uncrafting.tooltip` | Disables the uncrafting function of the uncrafting table. Recommended as a last resort if there's too many things to change about its behavior (or you're just lazy, I dont judge). Do note that special uncrafting recipes are not disabled as the mod relies on them for other things. |
| `config.twilightforest.ingredient_switching` | Disable Ingredient Switching |
| `config.twilightforest.ingredient_switching.tooltip` | If true, the uncrafting table will no longer allow you to switch between ingredients if a recipe uses a tag for crafting. This will remove the functionality for ALL RECIPES! If you want to prevent certain ingredients from showing up in the first place, use the "twilightforest:banned_uncrafting_ingredients" tag. |
| `config.twilightforest.repairing_xp_cost` | Repairing Cost Multiplier |
| `config.twilightforest.repairing_xp_cost.tooltip` | Multiplies the total XP cost of repairing an item and rounds up. Higher values means the recipe will cost more to repair, lower means less. Set to 0 to disable the cost altogether. |
| `config.twilightforest.shapeless_uncrafting` | Shapeless Uncrafting |
| `config.twilightforest.shapeless_uncrafting.tooltip` | If true, the uncrafting table will also be allowed to uncraft shapeless recipes. The table was originally intended to only take shaped recipes, but this option remains for people who wish to keep the functionality. |
| `config.twilightforest.uncrafting` | Uncrafting Table |
| `config.twilightforest.uncrafting_mod_id_flip` | Mod ID Blacklist |
| `config.twilightforest.uncrafting_mod_id_flip.tooltip` | If true, this will invert the above option from a blacklist to a whitelist. |
| `config.twilightforest.uncrafting_mod_ids` | Disabled Mod IDs |
| `config.twilightforest.uncrafting_mod_ids.tooltip` | Here, you can disable all items from certain mods from being uncrafted. Input a valid mod id to disable all uncrafting recipes from that mod. Example: "twilightforest" will disable all uncrafting recipes from this mod. |
| `config.twilightforest.uncrafting_recipes` | Disabled Recipes |
| `config.twilightforest.uncrafting_recipes_flip` | Recipe Blacklist |
| `config.twilightforest.uncrafting_recipes_flip.tooltip` | If true, this will invert the above uncrafting recipe list from a blacklist to a whitelist. |
| `config.twilightforest.uncrafting_recipes.tooltip` | If you don't want to disable uncrafting altogether, and would rather disable certain recipes, this is for you. To add a recipe, add the mod id followed by the name of the recipe. You can check this in things like JEI. Example: "twilightforest:firefly_particle_spawner" will disable uncrafting the particle spawner into a firefly jar, firefly, and poppy. If an item has multiple crafting recipes and you wish to disable them all, add the item to the "twilightforest:banned_uncraftables" item tag. If you have a problematic ingredient, like infested towerwood for example, add the item to the "twilightforest:banned_uncrafting_ingredients" item tag. |
| `config.twilightforest.uncrafting_xp_cost` | Uncrafting Cost Multiplier |
| `config.twilightforest.uncrafting_xp_cost.tooltip` | Multiplies the total XP cost of uncrafting an item and rounds up. Higher values means the recipe will cost more to uncraft, lower means less. Set to 0 to disable the cost altogether. Note that this only affects reversed crafting recipes, uncrafting recipes will still use the same cost as they normally would. |
| `config.twilightforest.uncrafting.tooltip` | Settings for all things related to the Uncrafting Table. |
| `container.twilightforest.uncrafting_table` | Uncrafting Table |
| `container.twilightforest.uncrafting_table.cycle_back_ingredient` | Previous Uncrafting Ingredients |
| `container.twilightforest.uncrafting_table.cycle_back_recipe` | Previous Crafting Recipe |
| `container.twilightforest.uncrafting_table.cycle_back_uncraft` | Previous Uncrafting Recipe |
| `container.twilightforest.uncrafting_table.cycle_next_ingredient` | Next Uncrafting Ingredients |
| `container.twilightforest.uncrafting_table.cycle_next_recipe` | Next Crafting Recipe |
| `container.twilightforest.uncrafting_table.cycle_next_uncraft` | Next Uncrafting Recipe |
| `container.twilightforest.uncrafting_table.disabled_item` | Uncrafting this item is disabled. |
| `container.twilightforest.uncrafting_table.uncrafting_disabled` | Uncrafting is disabled via config |
| `gui.twilightforest.uncrafting_jei` | Uncrafting |
| `item.twilightforest.maze_map.y_level` | Y level %s |
| `misc.twilightforest.magic_map_fail` | The Magic faltered. Perhaps it doesn't work here? |

