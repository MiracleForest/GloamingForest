# Uncrafting table and maps (U13 `uncraftmaps`)

Behaviour spec of unit **U13** (wave W1), translated from the upstream 4.8 (`9e40a757`, MC 1.21.1) by F16. It
is the checklist the U13 worker implements and the A2 audit compares against. Sections 1-5 are the behaviour; Appendix A
lists every id U13 owns with upstream's registration and data; Appendix B the strings.

## 1. Scope

- **Uncrafting table**: block `uncrafting_table` (+ item), menu type `gloamingforest:uncrafting`, screen, recipe type /
  serializer `gloamingforest:uncrafting` (3 recipes: `uncrafting/tipped_arrow` 8 tipped arrows -> 8 arrows, cost 4;
  `uncrafting/written_book` written book -> book, cost 0; `relinquish_keepsakes` keepsake casket -> skull chest,
  cost 0; an input matches by item with count >= `input_count`), loot condition
  `uncrafting_table_enabled`, stat `uncrafting_table_interactions`, trigger `uncraft_item`, sound
  `block.gloamingforest.uncrafting_table.activate`, tags `#banned_uncraftables`, `#banned_uncrafting_ingredients`,
  `#uncrafting_ignores_cost`, the ten `common.Uncrafting Table.*` config keys (section `uncraftmaps`), net channels
  `uncraftmaps.gui` (button presses) and `uncraftmaps.sync_config`.
- **Maps**: `magic_map` (empty) / `filled_magic_map`, `maze_map` / `filled_maze_map`, `ore_map` / `filled_ore_map`,
  `magic_map_focus`, `maze_map_focus`; cloning recipe types `magic_map_cloning_recipe`, `maze_map_cloning_recipe`;
  data maps `magic_map_color` (biome -> map colour) and `ore_map_color` (block -> map colour) (R7); tag
  `dimension_type/#allows_magic_map_charting`; net `uncraftmaps.magic_map`, `uncraftmaps.maze_map`; subcommands
  `/gloamingforest map_locator ...` and `/gloamingforest biomepng ...` (owners.tsv `subcommand` rows `map_locator`,
  `biomepng`; 3.6).

Not U13: landmark map decorations and the conquest flag (F6 landmark contract; U13 reads them), upstream's map decoration
types (owned by the structure units), the recipes it uncrafts (everyone's).

Contracts: recipes, inventory, client.ui, client.world, landmark, data, progression.

## 2. Upstream sources

`block/UncraftingTableBlock.java`, `inventory/UncraftingMenu.java`, `inventory/{UncraftingContainer,UncraftingInputContainer,UncrafterPlaceRecipe,UncraftingPlaceRecipe}.java`,
`inventory/slot/{UncraftingSlot,AssemblySlot,UncraftingResultSlot}.java`, `client/UncraftingScreen.java`,
`client/UncraftingRecipeBookComponent.java`, `item/recipe/UncraftingRecipe.java`, `item/recipe/UncraftingTableCondition.java`,
`loot/conditions/UncraftingTableEnabledCondition.java`, `network/{UncraftingGui,SyncUncraftingTableConfig}Packet.java`,
`config/{GloamingCommonConfig,GloamingConfig,ConfigSetup}.java`, `item/MagicMapItem.java`, `item/EmptyMagicMapItem.java`,
`item/MazeMapItem.java`, `item/EmptyMazeMapItem.java`, `item/mapdata/{GloamingMagicMapData,GloamingMazeMapData}.java`,
`item/recipe/{MagicMapCloning,MazeMapCloning}Recipe.java`, `network/{MagicMap,MazeMap}Packet.java`,
`client/renderer/map/{ConqueredMapIconRenderer,MagicMapPlayerIconRenderer}.java`, `command/MapLocatorCommand.java`,
`command/MapBiomesCommand.java`, `init/GloamingDataMaps.java`, `textures/gui/guigoblintinkering.png` (imported asset).

## 3. Behaviour

### 3.1 Uncrafting table block

`strength(2.5)`, WOOD, map FIRE, `ignitedByLava`; state `powered`. Use (no item needed): if
`disableUncraftingTable` -> action-bar `block.gloamingforest.uncrafting_table.disabled`, pass; else open the menu
(title `container.gloamingforest.uncrafting_table`) and award `uncrafting_table_interactions`. When the redstone signal
turns on with an amethyst block below, plays `block.gloamingforest.uncrafting_table.activate` (0.5, 1). Item tooltip
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
  `["gloamingforest:giant_log_to_oak_planks"]`, inverted by `flipRecipeList`) and the namespace list
  (`blacklistedUncraftingModIds`, inverted by `flipIdList`), plus `gloamingforest:uncrafting` recipes whose input
  matches with enough count. Inputs in `#banned_uncraftables` show nothing (tooltip
  `container.gloamingforest.uncrafting_table.disabled_item`, red). The recipe shown is
  `recipes[floorMod(unrecipeInCycle, n)]`; each ingredient shows its `floorMod(ingredientsInCycle, m)`-th matching item
  (excluding `#banned_uncrafting_ingredients`), count 1, laid out by the shaped pattern. Damaged inputs mark ceil(max(4,
  damageable parts) x damage fraction) damageable ingredients as **broken** (marker info tag `GloamingForestMarker`);
  ingredients with a crafting remainder or barriers are marked too. Marked ingredients cannot be taken and are not
  counted.
- Uncrafting cost (XP levels) = recipe `cost` for uncrafting recipes, else round(unmarked damageable parts
  (ingredients not in `#uncrafting_ignores_cost`) x `uncraftingXpCostMultiplier` (1.0)); 0 while the assembly grid is
  not empty; with `disableUncrafting` only uncrafting recipes work.
- Taking any uncrafting-grid ingredient (only when the assembly grid is empty, the player has the levels or is
  creative): charges the levels, copies all unmarked ingredients into the assembly grid, fires
  `gloamingforest:uncraft_item` (with the input), returns a container item's contents to the player, removes the
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
  `container.gloamingforest.uncrafting_table.cycle_{next,back}_{uncraft,ingredient,recipe}`.
- Mouse wheel: over x 27..33, y 56..69 cycles ingredients; over x 40..54, y 22..64 the uncraft recipe; over
  x 121..135, y 22..64 the recraft recipe (up = next).
- Ghost ingredients: each uncrafting ingredient is drawn as a fake item in its assembly slot with an overlay
  0x9F8B8B8B (0x80FF8B8B when marked/broken); hovering an empty assembly slot shows the ghost item tooltip.
- Costs: uncrafting cost right-aligned ending at x 48, y 38; recrafting cost ending at x 130, y 38; colour 0x80FF20 if
  affordable (or creative) else 0xA00000.
- Labels: title at (6, 6) colour 0x404040; "Inventory" at (7, 72); if `disableUncrafting`,
  `container.gloamingforest.uncrafting_table.uncrafting_disabled` in dark red at (6, 72) instead.

### 3.4 Magic map (`EmptyMagicMapItem`, `MagicMapItem`, `GloamingMagicMapData`)

- `magic_map` use: only in dimensions in `#gloamingforest:allows_magic_map_charting` (else action bar
  `misc.gloamingforest.magic_map_fail`); consumes one, stat used, `ui.cartography_table.take_result`, gives a
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

### 3.5 Maze and ore maps (`EmptyMazeMapItem`, `MazeMapItem`, `GloamingMazeMapData`)

- `maze_map` / `ore_map` use: creates `filled_maze_map` / `filled_ore_map` at scale 0 (1 block per pixel), centre
  snapped to 128-block cells (floor((x + 64)/128)*128 + 64 - 64), but inside a labyrinth chunk the map snaps to the
  labyrinth centre; records `yCenter` = the player's y. Sound and stat as above. Data names `mazemap_<id>`.
- Update while held (main or offhand, not locked): view radius **16** pixels (8 with a ceiling); every tick one column
  stripe (x & 15 == step & 15); per pixel the block at `yCenter` (if stone or air, the first stone within +-3 decides,
  brightness 2 above / 0 below), colour by map colour; ore maps add the block's `ore_map_color` data-map colour (weight
  1000) or COLOR_PINK for `#c:ores`. When the holder is more than 3 blocks above/below `yCenter`, their decoration
  becomes PLAYER_OFF_MAP. Advanced tooltip `item.gloamingforest.maze_map.y_level`.
- `magic_map_focus` and `maze_map_focus` are crafting ingredients (recipes in Appendix A).
- Cloning recipes (`magic_map_cloning_recipe`, `maze_map_cloning_recipe`): a filled map + n empty maps of the same kind
  -> n + 1 copies (vanilla map cloning logic for upstream maps).

### 3.6 Commands

Upstream's literals below `/gloamingforest`, both permission 2:

- `/gloamingforest map_locator <structure> [<player> [<skip_known_structures>]]` (`MapLocatorCommand`): `<structure>`
  is a structure id or `#tag` of the structure registry (`ResourceOrTagKeyArgument`, any namespace), `<player>` one or
  more players (`EntityArgument.players()`, default: the executing player, so the console must name one),
  `<skip_known_structures>` a boolean (default false). An id or tag the registry does not have -> vanilla
  `commands.locate.structure.invalid`. The search is `ChunkGenerator.findNearestMapStructure(level, targets,
  BlockPos.containing(source position), 100, skip_known_structures)` in the source's dimension as upstream patches it: the
  upsrc-asm transformer `ResolveNearestNonRandomSpreadMapStructureTransformer` inserts
  `MapHooks.resolveNearestNonRandomSpreadMapStructure` before the method's **last** `return` (of its three; the one
  reached when no random-spread ring returned). The targets are grouped by structure placement (no placement at all,
  e.g. an empty tag: nothing found). Two searches, by placement:
  - **Random spread** (Minecraft's structures and upstream's `avoid_landmark_grid` sets `camp`, `fallen_trunk`,
    `hollow_tree` with `swamp_hollow_tree`) and **concentric rings** (Minecraft's strongholds): vanilla's search, as
    for `/locate structure`. Concentric rings: the nearest of the ring positions with a start. Random spread: rings
    k = 0..100 around the source chunk, ring k being the perimeter of the (2k + 1) x (2k + 1) square of the
    placement's `spacing`-chunk cells (so the radius is 100 rings, not 100 chunks); per placement the first start
    found on the ring's perimeter counts (`getStructureGeneratingAt`, in vanilla's perimeter order); the first ring on
    which any placement has one returns at once the nearest of everything found so far (concentric-ring results
    included), and **upstream's landmark search below never runs**. With the flag, as for vanilla explorer maps, a start
    already referenced (by an explorer map or an earlier flagged search: `StructureStart.references` at its maximum,
    1) is skipped and the start found gets a reference (`StructureManager.addReference`); an existing start without
    a reference is found and referenced; without the flag references are neither read nor written. That reference
    count belongs to the start, so it is kept with F6's start records.
  - **upstream landmark grid** (`LandmarkGridPlacement`: upstream's 16 other structure sets, `aurora_palace` to `yeti_cave`),
    reached only when no random-spread ring returned: `WorldUtil.findNearestMapLandmark`, whose result, when it finds
    one, replaces vanilla's (even a nearer concentric-ring one):
    - candidates: the centres of `LegacyLandmarkPlacements.landmarkCenterScanner(pos, 100)`, an `XZQuadrantIterator`
      over **landmark grid cells of 16 chunks** (256 blocks) around chunk ((x >> 4) & ~15, (z >> 4) & ~15), r = 100
      cells (upstream passes vanilla's radius on unchanged), each cell mapped by `getNearestCenterXZ` to its landmark centre.
      The iterator is not a full square: it yields the centre cell, then for dX = 0..r-1 and dZ = 1..r the offsets
      (dZ, -dX), (-dX, -dZ), (-dZ, dX), (dX, dZ) in that order, i.e. 1 + 4r^2 cells, a pinwheel that leaves out the
      4r edge cells (r, 1..r), (1..r, -r), (-r, -1..-r) and (-1..-r, r);
    - a centre counts for a target when the target's placement has `isStructureChunk` in the centre's chunk (the grid
      pick, `structure_grid_lock`) and the target's `biomes` contain `level.getBiome(centre)` (at y 0); upstream does not
      check that a start was, or could be, generated there;
    - with the flag, `checkStructurePresence(centre chunk, target, placement, true) == START_PRESENT` skips the centre
      (`break`: the remaining targets of that placement at that centre too). With the flag that result means a valid
      start with **0** references (`StructureCheck.checkStructureInfo`: references -1 or, with the flag, != 0 ->
      `START_NOT_PRESENT`); nothing ever references a landmark start, so every landmark whose start already exists is
      skipped, and centres not generated yet are kept. **No reference is added**;
    - the nearest by `distToLowCornerSqr(x, 0, z)` (squared horizontal distance from the source to the centre), the
      first in scan order on a tie; the position found is the landmark centre at **y 0**.

  Nothing found -> `commands.locate.structure.not_found`. U13 does not re-implement the search: F6's
  `gf::landmark::ext::locate(dimension, keyOrTag, x, z, 100, skipKnownStructures)` (`src/feature/ext/landmark/
  LandmarkExt.h`, implemented in `src/feature/landmark/Commands.cpp`) runs the random-spread rings first and the
  landmark override after, and returns the structure key and the position found (y 0 for a landmark). Two of its
  flag rules differ from upstream on main at W0d (routed to F6, which owns the fix; U13's selftests check upstream's rules): for
  random-spread starts it skips every start already evaluated and never adds a reference (`structureAt`, upstream: skip only
  referenced starts, reference the one found), and at a landmark centre it also skips `placed` records, which
  `generateAt` (`/gloamingforest place`, saplings) makes without a start (upstream: no start there, so the centre is kept).
  Otherwise upstream
  creates a vanilla filled map of the found x/z (`MapItem.create`, scale 4 with vanilla's centre snapping to the
  2048-block map grid, tracking position, unlimited tracking) with a `red_x` target decoration (`"+"`), adds a copy
  to each target player's inventory (`Inventory.add`: lost when it is full) and prints vanilla
  `commands.locate.structure.success` through `LocateCommand.showLocateResult`: the structure (its id; for a tag
  `#<tag> (<id found>)`), the coordinates and the distance. The coordinates are Java's `chat.coordinates` ("%s, %s,
  %s") of x, `~`, z in square brackets, green, with a click event that suggests `/tp @s <x> ~ <z>` and the hover text
  `chat.coordinates.tooltip` ("Click to teleport"). Upstream passes the found position as the "source" position, so the
  printed distance (and the command result) is always 0. The `commands.locate.structure.*` and `chat.coordinates` keys
  are Java vanilla's, absent from Bedrock's lang: use `tr(key, ...)` with Java's English texts ("The nearest %s is at
  %s (%s blocks away)", "There is no structure with type \"%s\"", "Could not find a structure of type \"%s\" nearby",
  "%s, %s, %s"). Bedrock chat has no click or hover events, so the coordinates are the plain text `[x, ~, z]` in green
  (`§a[x, ~, z]§r`), with no click-to-suggest and no tooltip.
- `/gloamingforest biomepng [<width> [<height> [<showBiomePercents>]]]` (`MapBiomesCommand`): integers >= 0
  (`IntegerArgumentType.integer(0)`) and a boolean; no arguments = 4096 x 4096, `<width>` alone = width x width,
  `showBiomePercents` default true. On a dedicated server (`FMLEnvironment.dist.isDedicatedServer()`) upstream returns -1 at
  once: no image, no file, no message (`NativeImage` is client code). On an integrated server (single player, the
  host of a LAN world):
  - the image is created first, `new NativeImage(width, height, false)`: a width or height of 0 (`biomepng 0`,
    `biomepng 64 0`) throws `IllegalArgumentException("Invalid texture size: <width>x<height>")` before any output.
    Every exception here ends the command with vanilla's generic failure `command.failed` ("An unexpected error
    occurred trying to execute that command"; Java puts the exception message in its hover text), from
    `Commands.performCommand`, which catches `Exception`; messages already sent stay;
  - the image is `width` x `height` pixels; pixel (x, z) is `getNoiseBiome(x - width/2, 0, z - height/2)` of the
    source's level, i.e. **quart** (4-block) coordinates around the world origin at block y 0..3, not around the
    source. Upstream loops x over `0..height-1` and z over `0..width-1` and writes `setPixelRGBA(x, z)`, so only square
    sizes stay inside the image: `setPixelRGBA` throws `IllegalArgumentException("(x, z) outside of image bounds
    (width, height)")` at the first pixel outside, and the command fails with `command.failed` (width > height: in
    the first row, before any progress message; height > width: at row x = width, after the progress messages of
    the rows before). Both checks are in Minecraft 1.21.1's client `NativeImage` (`com.mojang.blaze3d.platform.
    NativeImage`, obfuscated `faj` in the official 1.21.1 `client.jar`: the constructor throws unless width > 0 and
    height > 0; `setPixelRGBA` throws when `isOutsideBounds(x, y)`, x outside 0..width-1 or y outside
    0..height-1);
  - colours: a fixed table for 20 upstream biomes (stream and lake 0,0,255; clearing 132,245,130; oak savannah 239,245,130;
    forest 0,255,0; dense forest 0,170,0; firefly forest 88,252,102; enchanted forest 0,255,255; spooky forest
    119,0,255; mushroom forest 204,0,139; dense mushroom forest 184,48,184; swamp 0,204,187; fire swamp 140,0,0;
    dark forest 25,61,13; dark forest center 157,79,0; snowy forest 255,255,255; glacier 130,191,245; highlands
    100,65,0; thornlands 128,100,90; final plateau 128,128,128); any other biome (`underground`,
    `highlands_underground`, other dimensions' biomes) takes its `magic_map_color` data-map colour (U13's data map)
    converted to ABGR by `getMapColor` and converted again when written, so its red and blue end up swapped; without
    a data-map entry the colour is opaque black (`0xFF000000` is non-zero, so the grass-colour fallback never runs);
    each colour is cached for the rest of the session;
  - every `height / 8` rows `commands.gloamingfeature.biomepng.progress` with x / height x 100 formatted `#.00` (so the
    first is `.00`; a height from 1 to 7 makes the step 0: after the first row `x % 0` throws `ArithmeticException`
    and the command fails with `command.failed`); then, with
    `showBiomePercents`, `commands.gloamingfeature.biomepng.counts_header` (width, height) and one line per biome
    `<biome id>: <count>` + `§7 (<percent formatted #.00>%)` (upstream's counter starts at 0 for the first pixel, so each
    count is one short);
  - the PNG goes to `<world>/generated/biomemaps/<seed>/biome_map-<seed>-(<startX>.<startZ>)-(<startX + width>.<startZ
    + height>).png` with startX = floor(source x) - width/2, startZ = floor(source z) - height/2 (the name uses the
    source position, unlike the sampling); an existing file is kept, not overwritten; an I/O error ->
    `commands.gloamingfeature.biomepng.save_failed` (failure, result 0), else `commands.gloamingfeature.biomepng.save_success`.

## 4. Bedrock adaptation notes

- The table needs a custom 56-slot container screen: client mod via `client::screen::registerScreen("uncrafting", ...)`
  (F12 framework, upstream GUI skin from the imported `guigoblintinkering.png`); vanilla clients get a DDUI/form flow (pick
  input from the inventory, list candidate recipes, take all ingredients) through `ui::ddui` (best effort; README
  deviation). Container storage via `inventory::registerBlock` (the input and assembly grid are per-player transient).
- Recipe lookups through `recipes::producing(itemId)` and `recipes::ofType("gloamingforest:uncrafting")`; config
  sync to companions over `uncraftmaps.sync_config`.
- Maps: Bedrock map items are vanilla `filled_map`s; upstream map ids are custom items whose map data U13 renders with
  `client::maps::view` / `setIcon` (client mod) and `ClientboundMapItemData` equivalents (MapItemDataPacket) for
  vanilla clients with decorations mapped to the 30 vanilla types (README deviation).
- `biomepng` follows upstream per build, with no deviation: the server build (BDS, upstream's dedicated server) returns at once
  with no message, no image and no file; the client build in a local world (upstream's integrated server) samples, reports
  and writes the PNG under that world's folder (`<world>/generated/biomemaps/...` above). Its failures (a 0 size, a
  non-square size, a height from 1 to 7) end it with `tr("command.failed", "An unexpected error occurred trying to
  execute that command")` as an error, after the messages already sent, with no file; Bedrock has no hover text for
  the exception message. Never write outside the image.
- `map_locator` finds upstream's structures through F6's `gf::landmark::ext::locate` (3.6). Minecraft's own structures
  (`minecraft:` ids and tags) are generated by Bedrock, not by Java's structure sets: F6's locate returns nothing for
  them. For those U13 maps the Java id to Bedrock's structure feature name and asks Bedrock's own search,
  `WorldGenerator::findNearestStructureFeature(feature, origin, pos, mustBeInNewChunks = skip_known_structures,
  nullopt)` (a callable virtual in the 26.51.7 headers; its radius and its new-chunk rule are Bedrock's, as Bedrock's
  world generation is); an id with no Bedrock counterpart -> `commands.locate.structure.not_found`. The map is a
  vanilla `filled_map`; its `red_x` target (a Java vanilla decoration) uses the closest of Bedrock's 30 map decoration
  types (an X; the README deviation on map decorations).
- Register both commands under F0's `/gloamingforest` with `GF_REGISTER_SUBCOMMAND`, spelled as upstream's literals (R14).

## 5. Acceptance checklist (selftests `uncraftmaps.*`, dx 1300-1399)

- Uncrafting an iron pickaxe gives 3 iron + 2 sticks with cost 3 (damageable parts) and marks broken parts on a damaged
  one; banned items show nothing; config lists filter recipes; uncrafting recipes (tipped arrow x8 -> 8 arrows cost
  4, written book -> book cost 0, keepsake casket -> skull chest cost 0 via `relinquish_keepsakes`).
- Recrafting cost formula with enchantments; enchantment transfer; XP requirement.
- GUI screenshot vs `guigoblintinkering.png` (buttons, costs, ghost overlays).
- Magic map centre snapping and biome colours from the data map; landmark icons with conquered state.
- Maze map snaps to labyrinth centre; ore map highlights ores; cloning recipes.
- `map_locator` gives a map with a red X to each named player; a landmark is found at its grid centre (y 0) within
  100 grid cells, with the flag a landmark whose start exists is skipped and no reference is added, and for targets
  of both kinds a random-spread hit returns before the landmark search; `biomepng` writes upstream's file name with the
  progress and count messages in a local world, does nothing on BDS, and fails with `command.failed` for a 0 or
  non-square size.

## Appendix A: Owned ids (generated inventory)

Every id this unit owns in `docs/spec/owners.tsv`, with the upstream 4.8 data the W1 worker must reproduce. Registration statements are upstream's own Java (`init/upstream*.java`), condensed (namespaces such as `MapColor.` dropped); loot, recipe, tag, advancement and data rows are summarised from upstream's generated JSON (`src/generated/resources`). Loot notation: `self` = drops the block itself when it survives the explosion; `alternatives(a → b)` = first matching child; `[if ...]` = conditions; `{...}` = functions. In summaries `mc:` is `minecraft:` and ids without a namespace are `gloamingforest:`.

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

Item column: `BlockItem` = upstream registers a plain block item with the block (`GloamingBlocks.register`), the id is also a `block_item` row; `-` = no item of the same id (the block is placed by another item, or has none).

Block states come from upstream's blockstate JSON (`multipart:` lists only the property values the model parts test). Notes collect `RegistrationEvents` (fire, axe stripping, flower pots, jar lids), dispenser behaviours and the `neoforge:compostables` / `neoforge:furnace_fuels` data maps.

| Block | English name | upstream registration (`GloamingBlocks`) | Block states | Item | Loot (`blocks/<id>`) | Notes |
|---|---|---|---|---|---|---|
| `gloamingforest:uncrafting_table` | Uncrafting Table | `register("uncrafting_table", new UncraftingTableBlock(Properties.of().ignitedByLava().mapColor(FIRE).sound(WOOD).strength(2.5F)));` | powered: false/true | BlockItem | self | block tags: #c:player_workstations/crafting_tables, #mc:mineable/axe, #mineable_with_block_and_chain |

### Items (8)

| Item | English name | upstream registration (`GloamingItems`) | Notes |
|---|---|---|---|
| `gloamingforest:filled_magic_map` | Magic Map | `ITEMS.register("filled_magic_map", new MagicMapItem(new Item.Properties()));` |  |
| `gloamingforest:filled_maze_map` | Maze Map | `ITEMS.register("filled_maze_map", new MazeMapItem(false, new Item.Properties()));` |  |
| `gloamingforest:filled_ore_map` | Maze/Ore Map | `ITEMS.register("filled_ore_map", new MazeMapItem(true, new Item.Properties()));` |  |
| `gloamingforest:magic_map` | Blank Magic Map | `ITEMS.register("magic_map", new EmptyMagicMapItem(new Item.Properties()));` |  |
| `gloamingforest:magic_map_focus` | Magic Map Focus | `ITEMS.register("magic_map_focus", new Item(new Item.Properties()));` |  |
| `gloamingforest:maze_map` | Blank Maze Map | `ITEMS.register("maze_map", new EmptyMazeMapItem(false, new Item.Properties()));` |  |
| `gloamingforest:maze_map_focus` | Maze Map Focus | `ITEMS.register("maze_map_focus", new Item(new Item.Properties()));` |  |
| `gloamingforest:ore_map` | Blank Maze/Ore Map | `ITEMS.register("ore_map", new EmptyMazeMapItem(true, new Item.Properties()));` |  |

### Recipe serializers / types (3)

| Id | upstream registration (`GloamingRecipes.java`) |
|---|---|
| `gloamingforest:magic_map_cloning_recipe` | `RECIPE_SERIALIZERS.register("magic_map_cloning_recipe", new SimpleCraftingRecipeSerializer<>(MagicMapCloningRecipe::new));` |
| `gloamingforest:maze_map_cloning_recipe` | `RECIPE_SERIALIZERS.register("maze_map_cloning_recipe", new SimpleCraftingRecipeSerializer<>(MazeMapCloningRecipe::new));` |
| `gloamingforest:uncrafting` | `RECIPE_SERIALIZERS.register("uncrafting", UncraftingRecipe.Serializer::new); ;; RECIPE_TYPES.register("uncrafting", () -> RecipeType.simple(GloamingForestMod.prefix("uncrafting")));` |

### Loot condition types (1)

| Id | upstream registration (`GloamingLoot.java`) |
|---|---|
| `gloamingforest:uncrafting_table_enabled` | `CONDITIONS.register("uncrafting_table_enabled", new LootItemConditionType(UncraftingTableEnabledCondition.CODEC)); ;; CONDITIONALS.register("uncrafting_table_enabled", () -> UncraftingTableCondition.CODEC);` |

### Menu types (1)

| Id | upstream registration (`GloamingMenuTypes.java`) |
|---|---|
| `gloamingforest:uncrafting` | `CONTAINERS.register("uncrafting", new MenuType<>(UncraftingMenu::fromNetwork, FeatureFlags.REGISTRY.allFlags()));` |

### Stats (1)

| Id | upstream registration (`GloamingStats.java`) |
|---|---|
| `gloamingforest:uncrafting_table_interactions` (Interactions with Uncrafting Table) | `makeTFStat("uncrafting_table_interactions");` |

### Advancement triggers (1)

| Id | upstream registration (`GloamingAdvancements.java`) |
|---|---|
| `gloamingforest:uncraft_item` | `TRIGGERS.register("uncraft_item", UncraftItemTrigger::new);` |

### Data maps (2)

- `gloamingforest:magic_map_color` (registry `worldgen/biome`): `{"values":{"gloamingforest:clearing":{"brightness":2,"map_color":{"color":8368696,"id":1}},"gloamingforest:dark_forest":{"brightness":3,"map_color":{"color":6717235,"id":27}},"gloamingforest:dark_forest_center":{"brightness":3,"map_color":{"color":14188339,"id":15}},"gloamingforest:dense_forest":{"brightness":0,"map_color":{"color":31744,"id":7}},"gloamingforest:dense_mushroom_forest":{"brightness":0,"map_color":{"color":15892389,"id":20}},"gloamingforest:enchanted_forest":{"brightness":2,"map_color":{"color":5013401,"id":23}},"gloamingforest:final_plateau":{"brightness":2,"map_color":{"color":10066329,"id":22}},"gloamingforest:fire_swamp":{"brightness":1,"map_color":{"color":7340544,"id":35}},"gloamingforest:firefly_forest":{"brightness":1,"map_color":{"color":55610,"id":33}},"gloamingforest:forest":{"brightness":1,"map_color":{"color":31744,"id":7}},"gloamingforest:glacier":{"brightness":1,"map_color":{"color":10526975,"id":5}},"gloamingforest:highlands":{"brightness":0,"map_color":{"color":9923917,"id":10}},"gloamingforest:lake":{"brightness":3,"map_color":{"color":4210943,"id":12}},"gloamingforest:mushroom_forest":{"brightness":0,"map_color":{"color":14188339,"id":15}},"gloamingforest:oak_savannah":{"brightness":0,"map_color":{"color":8368696,"id":1}},"gloamingforest:snowy_forest":{"brightness":1,"map_color":{"color":16777215,"id":8}},"gloamingforest:spooky_forest":{"brightness":0,"map_color":{"color":8339378,"id":24}},"gloamingforest:stream":{"brightness":1,"map_color":{"color":4210943,"id":12}},"gloamingforest:swamp":{"brightness":3,"map_color":{"color":6085589,"id":31}},"gloamingforest:thornlands":{"brightness":3,"map_color":{"color":9402184,"id":13}}}}`
  Upstream registration: `GloamingForestMod.prefix("magic_map_color"), Registries.BIOME, MagicMapBiomeColor.CODEC).synced(MagicMapBiomeColor.CODEC, false).build();`
- `gloamingforest:ore_map_color` (registry `block`): `{"values":{"#minecraft:coal_ores":{"map_color":{"color":1644825,"id":29}},"#minecraft:copper_ores":{"map_color":{"color":14188339,"id":15}},"#minecraft:diamond_ores":{"map_color":{"color":6085589,"id":31}},"#minecraft:emerald_ores":{"map_color":{"color":55610,"id":33}},"#minecraft:gold_ores":{"map_color":{"color":16445005,"id":30}},"#minecraft:iron_ores":{"map_color":{"color":14200723,"id":60}},"#minecraft:lapis_ores":{"map_color":{"color":4882687,"id":32}},"#minecraft:redstone_ores":{"map_color":{"color":10040115,"id":28}},"minecraft:ancient_debris":{"map_color":{"color":4993571,"id":48}}}}`
  Upstream registration: `GloamingForestMod.prefix("ore_map_color"), Registries.BLOCK, OreMapOreColor.CODEC).synced(OreMapOreColor.CODEC, false).build();`

### Sound events (1)

`sounds.json` content (vanilla `event` references can be played with the matching Bedrock vanilla sound; upstream file names are listed only as names: the audio itself is All Rights Reserved and is re-synthesised by F3/the owner).

| Event | Subtitle | sounds.json entries |
|---|---|---|
| `block.gloamingforest.uncrafting_table.activate` |  | event block.end_portal.spawn |

### Tags (4)

| Tag | Members (upstream generated JSON) |
|---|---|
| `dimension_type/#gloamingforest:allows_magic_map_charting` | gloaming_forest_type |
| `item/#gloamingforest:banned_uncraftables` | (empty) |
| `item/#gloamingforest:banned_uncrafting_ingredients` | infested_towerwood, hollow_oak_sapling, time_sapling, transformation_sapling, mining_sapling, sorting_sapling, transformation_powder |
| `item/#gloamingforest:uncrafting_ignores_cost` | #c:rods/wooden |

### Loot tables (1)

Block loot tables summarised in the Loot column of the block table: `gloamingforest:blocks/uncrafting_table`

### Recipes (10)

Recipe-unlock advancements (generated by F1 into the recipe JSON, R21) are listed with their recipe; criteria are the `has_*` triggers besides `has_the_recipe`.

| Recipe | Type | Ingredients | Result | Unlock advancement (criteria) |
|---|---|---|---|---|
| `gloamingforest:magic_map` | mc:crafting_shaped | '###' / '#•#' / '###' ; #=#c:paper, •=magic_map_focus | magic_map | `gloamingforest:recipes/misc/magic_map` (has_item: has magic_map_focus) |
| `gloamingforest:magic_map_cloning_recipe` | magic_map_cloning_recipe | {} | - | - |
| `gloamingforest:magic_map_focus` | mc:crafting_shapeless | raven_feather + torchberries + #c:dusts/glowstone | magic_map_focus | `gloamingforest:recipes/misc/magic_map_focus` (has_berries: has torchberries; has_feather: has raven_feather) |
| `gloamingforest:maze_map` | mc:crafting_shaped | '###' / '#•#' / '###' ; #=#c:paper, •=maze_map_focus | maze_map | `gloamingforest:recipes/misc/maze_map` (has_item: has maze_map_focus) |
| `gloamingforest:maze_map_cloning_recipe` | maze_map_cloning_recipe | {} | - | - |
| `gloamingforest:ore_map` | mc:crafting_shapeless | maze_map + #c:storage_blocks/diamond + #c:storage_blocks/gold + #c:storage_blocks/iron | ore_map | `gloamingforest:recipes/misc/ore_map` (has_item: has maze_map) |
| `gloamingforest:relinquish_keepsakes` | uncrafting | {"cost":0,"input":{"item":"gloamingforest:keepsake_casket"},"key":{"C":{"item":"gloamingforest:skull_chest"}},"pattern":["C"]} | - | - |
| `gloamingforest:uncrafting_table` | mc:crafting_shaped | '###' / '#X#' / '###' ; #=mc:crafting_table, X=maze_map_focus [conditions [{"type":"gloamingforest:uncrafting_table_enabled"}]] | uncrafting_table | `gloamingforest:recipes/decorations/uncrafting_table` (has_uncrafting_table: has uncrafting_table) |
| `gloamingforest:uncrafting/tipped_arrow` | uncrafting | {"cost":4,"input":{"item":"minecraft:tipped_arrow"},"input_count":8,"key":{"A":{"item":"minecraft:arrow"}},"pattern":["AAA","A A","AAA"]} | - | - |
| `gloamingforest:uncrafting/written_book` | uncrafting | {"cost":0,"input":{"item":"minecraft:written_book"},"key":{"B":{"item":"minecraft:book"}},"pattern":["B"]} | - | - |

### Config keys (10)

See [config.md](config.md) for the section mapping; defaults and translation ids below are upstream's.

| Upstream key | Default | Translation id | English (config screen) |
|---|---|---|---|
| `common.Uncrafting Table.blacklistedUncraftingModIds` | `new ArrayList<>()` | `uncrafting_mod_ids` | Disabled Mod IDs — Here, you can disable all items from certain mods from being uncrafted. Input a valid mod id to disable all uncrafting recipes from that mod. Example: "gloamingforest" will disable all uncrafting recipes from this mod. |
| `common.Uncrafting Table.disableIngredientSwitching` | `false` | `ingredient_switching` | Disable Ingredient Switching — If true, the uncrafting table will no longer allow you to switch between ingredients if a recipe uses a tag for crafting. This will remove the functionality for ALL RECIPES! If you want to prevent certain ingredients from showing up in the first place, use the "gloamingforest:banned_uncrafting_ingredients" tag. |
| `common.Uncrafting Table.disableUncrafting` | `false` | `disable_uncrafting` | Disable Uncrafting — Disables the uncrafting function of the uncrafting table. Recommended as a last resort if there's too many things to change about its behavior (or you're just lazy, I dont judge). Do note that special uncrafting recipes are not disabled as the mod relies on them for other things. |
| `common.Uncrafting Table.disableUncraftingRecipes` | `List.of("gloamingforest:giant_log_to_oak_planks")` | `uncrafting_recipes` | Disabled Recipes — If you don't want to disable uncrafting altogether, and would rather disable certain recipes, this is for you. To add a recipe, add the mod id followed by the name of the recipe. You can check this in things like JEI. Example: "gloamingforest:firefly_particle_spawner" will disable uncrafting the particle spawner into a firefly jar, firefly, and poppy. If an item has multiple crafting recipes and you wish to disable them all, add the item to the "gloamingforest:banned_uncraftables" item tag. If you have a problematic ingredient, like infested towerwood for example, add the item to the "gloamingforest:banned_uncrafting_ingredients" item tag. |
| `common.Uncrafting Table.disableUncraftingTable` | `false` | `disable_uncrafting_table` | Disable Uncrafting Table — Disables any usage of the uncrafting table, as well as prevents it from showing up in loot or crafted. Please note that table has more uses than just uncrafting, you can read about them here! http://benimatic.com/gloamingwiki/index.php?title=Uncrafting_Table It is highly recommended to keep the table enabled as the mod has special uncrafting exclusive recipes, but the option remains for people that dont want the table to be functional at all. If you are looking to just prevent normal crafting recipes from being reversed, consider using the 'disableUncrafting' option instead. |
| `common.Uncrafting Table.enableShapelessCrafting` | `false` | `shapeless_uncrafting` | Shapeless Uncrafting — If true, the uncrafting table will also be allowed to uncraft shapeless recipes. The table was originally intended to only take shaped recipes, but this option remains for people who wish to keep the functionality. |
| `common.Uncrafting Table.flipIdList` | `false` | `uncrafting_mod_id_flip` | Mod ID Blacklist — If true, this will invert the above option from a blacklist to a whitelist. |
| `common.Uncrafting Table.flipRecipeList` | `false` | `uncrafting_recipes_flip` | Recipe Blacklist — If true, this will invert the above uncrafting recipe list from a blacklist to a whitelist. |
| `common.Uncrafting Table.repairingXpCostMultiplier` | `1.0D` | `repairing_xp_cost` | Repairing Cost Multiplier — Multiplies the total XP cost of repairing an item and rounds up. Higher values means the recipe will cost more to repair, lower means less. Set to 0 to disable the cost altogether. |
| `common.Uncrafting Table.uncraftingXpCostMultiplier` | `1.0D` | `uncrafting_xp_cost` | Uncrafting Cost Multiplier — Multiplies the total XP cost of uncrafting an item and rounds up. Higher values means the recipe will cost more to uncraft, lower means less. Set to 0 to disable the cost altogether. Note that this only affects reversed crafting recipes, uncrafting recipes will still use the same cost as they normally would. |

### subcommand (2)

`biomepng`, `map_locator`

## Appendix B: Strings (49 en_us keys owned)

Keys already shown above (block/item/entity names, subtitles) are not repeated. The 29 Bedrock locales are generated from upstream's lang files (R9); owners use these keys with the English text as fallback.

| Key | English |
|---|---|
| `block.gloamingforest.uncrafting_table.disabled` | This block has been disabled. |
| `commands.gloamingfeature.biomepng.counts_header` | Approximate biome-block counts within a %sx%s region |
| `commands.gloamingfeature.biomepng.progress` | %s%% Done mapping |
| `commands.gloamingfeature.biomepng.save_failed` | Could not save image! Please report this! |
| `commands.gloamingfeature.biomepng.save_success` | Image saved! |
| `config.gloamingforest.disable_uncrafting` | Disable Uncrafting |
| `config.gloamingforest.disable_uncrafting_table` | Disable Uncrafting Table |
| `config.gloamingforest.disable_uncrafting_table.tooltip` | Disables any usage of the uncrafting table, as well as prevents it from showing up in loot or crafted. Please note that table has more uses than just uncrafting, you can read about them here! http://benimatic.com/gloamingwiki/index.php?title=Uncrafting_Table It is highly recommended to keep the table enabled as the mod has special uncrafting exclusive recipes, but the option remains for people that dont want the table to be functional at all. If you are looking to just prevent normal crafting recipes from being reversed, consider using the 'disableUncrafting' option instead. |
| `config.gloamingforest.disable_uncrafting.tooltip` | Disables the uncrafting function of the uncrafting table. Recommended as a last resort if there's too many things to change about its behavior (or you're just lazy, I dont judge). Do note that special uncrafting recipes are not disabled as the mod relies on them for other things. |
| `config.gloamingforest.ingredient_switching` | Disable Ingredient Switching |
| `config.gloamingforest.ingredient_switching.tooltip` | If true, the uncrafting table will no longer allow you to switch between ingredients if a recipe uses a tag for crafting. This will remove the functionality for ALL RECIPES! If you want to prevent certain ingredients from showing up in the first place, use the "gloamingforest:banned_uncrafting_ingredients" tag. |
| `config.gloamingforest.repairing_xp_cost` | Repairing Cost Multiplier |
| `config.gloamingforest.repairing_xp_cost.tooltip` | Multiplies the total XP cost of repairing an item and rounds up. Higher values means the recipe will cost more to repair, lower means less. Set to 0 to disable the cost altogether. |
| `config.gloamingforest.shapeless_uncrafting` | Shapeless Uncrafting |
| `config.gloamingforest.shapeless_uncrafting.tooltip` | If true, the uncrafting table will also be allowed to uncraft shapeless recipes. The table was originally intended to only take shaped recipes, but this option remains for people who wish to keep the functionality. |
| `config.gloamingforest.uncrafting` | Uncrafting Table |
| `config.gloamingforest.uncrafting_mod_id_flip` | Mod ID Blacklist |
| `config.gloamingforest.uncrafting_mod_id_flip.tooltip` | If true, this will invert the above option from a blacklist to a whitelist. |
| `config.gloamingforest.uncrafting_mod_ids` | Disabled Mod IDs |
| `config.gloamingforest.uncrafting_mod_ids.tooltip` | Here, you can disable all items from certain mods from being uncrafted. Input a valid mod id to disable all uncrafting recipes from that mod. Example: "gloamingforest" will disable all uncrafting recipes from this mod. |
| `config.gloamingforest.uncrafting_recipes` | Disabled Recipes |
| `config.gloamingforest.uncrafting_recipes_flip` | Recipe Blacklist |
| `config.gloamingforest.uncrafting_recipes_flip.tooltip` | If true, this will invert the above uncrafting recipe list from a blacklist to a whitelist. |
| `config.gloamingforest.uncrafting_recipes.tooltip` | If you don't want to disable uncrafting altogether, and would rather disable certain recipes, this is for you. To add a recipe, add the mod id followed by the name of the recipe. You can check this in things like JEI. Example: "gloamingforest:firefly_particle_spawner" will disable uncrafting the particle spawner into a firefly jar, firefly, and poppy. If an item has multiple crafting recipes and you wish to disable them all, add the item to the "gloamingforest:banned_uncraftables" item tag. If you have a problematic ingredient, like infested towerwood for example, add the item to the "gloamingforest:banned_uncrafting_ingredients" item tag. |
| `config.gloamingforest.uncrafting_xp_cost` | Uncrafting Cost Multiplier |
| `config.gloamingforest.uncrafting_xp_cost.tooltip` | Multiplies the total XP cost of uncrafting an item and rounds up. Higher values means the recipe will cost more to uncraft, lower means less. Set to 0 to disable the cost altogether. Note that this only affects reversed crafting recipes, uncrafting recipes will still use the same cost as they normally would. |
| `config.gloamingforest.uncrafting.tooltip` | Settings for all things related to the Uncrafting Table. |
| `container.gloamingforest.uncrafting_table` | Uncrafting Table |
| `container.gloamingforest.uncrafting_table.cycle_back_ingredient` | Previous Uncrafting Ingredients |
| `container.gloamingforest.uncrafting_table.cycle_back_recipe` | Previous Crafting Recipe |
| `container.gloamingforest.uncrafting_table.cycle_back_uncraft` | Previous Uncrafting Recipe |
| `container.gloamingforest.uncrafting_table.cycle_next_ingredient` | Next Uncrafting Ingredients |
| `container.gloamingforest.uncrafting_table.cycle_next_recipe` | Next Crafting Recipe |
| `container.gloamingforest.uncrafting_table.cycle_next_uncraft` | Next Uncrafting Recipe |
| `container.gloamingforest.uncrafting_table.disabled_item` | Uncrafting this item is disabled. |
| `container.gloamingforest.uncrafting_table.uncrafting_disabled` | Uncrafting is disabled via config |
| `gui.gloamingforest.uncrafting_jei` | Uncrafting |
| `item.gloamingforest.maze_map.y_level` | Y level %s |
| `misc.gloamingforest.magic_map_fail` | The Magic faltered. Perhaps it doesn't work here? |

