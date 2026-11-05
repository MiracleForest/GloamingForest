# U3 `trees`: implementation notes

Spec: [docs/spec/trees.md](../spec/trees.md). What is where:

| Part | Files |
|---|---|
| TF's tree features (MegaOak, MegaCanopy, LargeWinter, MiningTree, TimeTree, DarkCanopy, SnowTree, SnowUnderTrees) | `src/feature/trees/TreeFeatures.cpp` |
| Tree placers and decorators (branching trunk, trunk mover, spheroid foliage, core placer, trunkside, roots, dangle) | `src/feature/trees/TreePlacers.cpp` |
| FeatureLogic / FeaturePlacers / Bresenham helpers | `src/feature/trees/TreeUtil.{h,cpp}` |
| Canopy mushrooms | `src/feature/trees/Mushrooms.cpp` |
| Minecraft's huge brown / red mushroom feature types (not in F5's vanilla set) | `src/feature/ext/trees/HugeMushrooms.{h,cpp}` (contract extension) |
| Magic log cores (toggle, 20-tick cycle, time / transformation / mining / sorting) | `src/feature/trees/Cores.cpp` |
| Leaf runes (server fallback), hardened dark leaves, hollow oak sapling | `src/feature/trees/Leaves.cpp` |
| Leaf runes drawn by the client mod | `src/client/trees/LeafRunes.cpp` |
| Pick block on hardened dark leaves | `src/feature/ext/trees/PickBlock.{h,cpp}` (contract extension) |
| Java HashSet iteration order (TreeFeature#updateLeaves) | `src/core/trees/JavaHashSet.{h,cpp}`, `tests/trees/` |
| Module and config section `trees` | `src/feature/trees/Module.cpp`, `Trees.h` |
| Selftests, e2e | `src/feature/trees/SelfTests.cpp`, `tools/e2e/{server,client}/scenarios/trees.ps1` |
| Pack: hardened dark leaves (foliage tint), particles | `assets/pack/bp/blocks/trees/`, `assets/pack/rp/particles/trees/` |
| Import overrides (mangrove trunk defaults, leaf mining speeds) | `tools/import/overrides/trees.json` |
| Core sounds | `tools/sounds/recipes/trees.json` -> `assets/gen/audio/trees/` |

## Magic log cores

A core's 20-tick scheduled tick is a module timer per active core (`gf:trees:cores` in the world
database; cores placed active by structures or commands start through F11's `onPlaced`). Ranges are
clamped to TF's `defineInRange(0, 128)`; a range of 0 disables the core.

- **Time**: 480 random positions per cycle; F11's random tick dispatch for TF blocks, the game's
  `Block::randomTick` for the vanilla blocks that tick randomly in Java 1.21.1 (a list by Bedrock id),
  the block actor tick for Java's server tickers. The particle sits on the top of the block's outline
  (Java: the occlusion shape's top; leaves and glass have none).
- **Transformation**: the enchanted forest is U1's behaviour pack biome
  `twilightforest:enchanted_forest`; without it (U1 not merged) the core warns once and does
  nothing, and `trees.transformation_core_biome` reports NOT RUN for the real biome and checks the
  mechanism with a stand-in biome. The chunk is marked for saving by touching its biome data
  counter the way the engine touches its dirty counters (`LevelChunk::mFullChunkDirtyTicksCounters`,
  measured: a clean counter counts from INT_MIN, a change sets `lastChange = totalTime`), and resent
  to every player whose view holds it through the player's `NetworkChunkPublisher::_sendQueuedChunk`
  (a LevelChunkPacket; the client asks for the sub-chunks again). No block is edited.
- **Mining**: `OreMagnetItem.doMagnet` translated (U8 owns the magnet item). The ore map is
  `ToolEvents#refreshOreMagnetCache`: every `c:ores_in_ground/<g>` tag with a matching
  `c:ore_bearing_ground/<g>` tag (the data's and F9's vanilla bridge), minus
  `#twilightforest:mining_tree_excluded` (coal), plus ancient debris -> netherrack; rebuilt when the
  data reloads.
- **Sorting**: NeoForge's item handlers per face: a WorldlyContainer (furnace family, brewing stand,
  shulker box) gets a SidedInvWrapper per face with its slot rules (`getSlotsForFace`,
  `canPlaceItemThroughFace`, `canTakeItemThroughFace`; Bedrock's brewing stand slot order mapped),
  any other container one handler; TF containers go through the inventory contract's insert /
  extract.

## Leaves

- Tints: dark and hardened dark leaves take the biome foliage tint (`default_foliage`; hardened dark
  leaves through the manual BP file, the families kit only tints its `leaves` family); the magic
  leaves' position colours are F11's `leafcolors` (checked against ColorHandler in
  `tests/trees/LeafTintsTest.cpp`) drawn by F11's client tint.
- Mining speeds: Java's (`speed / hardness / 30` per tick; shears 15 and swords 1.5 on `#leaves`,
  hoes by tier) through `item_specific_speeds` in the import override. Measured in Bedrock on the
  ground: an entry of `s` seconds gives `toolSpeed / (30 s)` per tick (shears 5, hoes their tier's
  speed, others 1), so shears get `h / 3` and swords `h / 1.5`; hoes and every other item already
  match. `trees.leaf_tool_speeds` checks 16 items on the 10 leaves.
- Hardened dark leaves: no natural spawn stands on them (an empty spawn list there through
  `gf::spawn::onSpecialSpawn`); pick block gives dark leaves (`PickBlock` extension).
- Leaf runes: the client mod runs ClientLevel#animateTick's sampling itself; the server does it only
  for players without the client mod and sends each rune to that player alone (at most 16 a tick),
  and only while leaves of transformation are within the samples' reach (a block palette look over
  the sub-chunks within 31 blocks, refreshed every 20 ticks per player).

## Tree features

`TFTreeFeature#place` runs `TreeFeature.updateLeaves` (TF passes its leaf set as the sources; the
seven work sets iterate in Java's HashSet order, `core::trees::JavaHashSet`).
`StructureTemplate.updateShapeAtEdge` has no Bedrock counterpart (the game connects block shapes;
F11 recomputes leaf distances on neighbour changes). The decorators get their position sets in
Java's HashSet order, then sorted by y. `dark_canopy_tree` checks DarkCanopyTreeFeature's own free
height (logs are not free) before the tree interpreter, which replays the two trunk height draws.
`dark_canopy_tree` and `anywhere_tree` (SnowTreeFeature) then run their own copy of the older
`TreeFeature.updateLeaves` (distance 1 next to the trunk, five rounds lowering to `l + 1`, not
bounded by the tree's box): a decorator `gf_trees:known_shape_update_leaves` appended to the
interpreter's decorator list runs it last, with the interpreter's trunk set (F5's `minecraft:tree`
itself writes no leaf distances).

### Java oracle comparison

`trees.golden_*` and `trees.sapling_hashes` also compare their box block by block with TF 4.8 on
NeoForge 1.21.1 (`data/gf/java_goldens/trees.json`, from `assets/data/java_goldens/trees.json`), when
the oracle placed the tree at the same absolute box (the positions follow the world's spawn: the
server e2e world's). The dump comes from a `trees` kind of the F13 oracle dumper
(`tests/trees/oracle/`: the Java source, the spec section and how to run it): each case clears the
flat area as `flatArea` does, places the configured feature with `LegacyRandomSource(seed)` or plants
the sapling and calls `SaplingBlock#advanceTree` twice with one, and dumps the box.

What it showed and what was fixed: F5's `minecraft:tree` writes no leaf distances (Java's
`TreeFeature.updateLeaves`): TF's vanilla-type trees now run it through `gf_trees:update_leaves`, a
decorator `tools/import/overrides/trees.json` appends to their configured features; the dark canopy and
snow trees run their own older copy. Every golden tree, mega canopy tree and sapling (the tree of time
aside) then matches block for block; the golden hashes are those runs'. Known differences (at most this
many blocks, `PARITY GAP (known)` in the log, more fails):
- `features_a` (120; 105 or 106 measured): the firefly canopy tree's trunkside / dangle decorators draw differently (some
  fireflies, a jar on a rope); F5's `leave_vine` writes vine bits 1 where Java's vine faces east (8);
  one mega oak leaf keeps distance 2 where Java keeps 7.
- `features_b` (21) and `sapling_time_sapling` (10): the tree of time's exposed roots at its base.
- `features_c` (19): the oracle places in a live level, where `HugeMushroomBlock#updateShape` clears
  the faces between cap blocks; worldgen keeps them, as Bedrock does.

## Client e2e probes

`trees.showcase` (client builds) builds every tree and block for the screenshots, plus the game's own
brown and red cap blocks (`huge_mushroom_bits` 14) to compare the brown canopy mushroom's caps with.
`trees.particles_probe` (client builds) drives a sortingwood engine between two chests and a heart of
transformation (range 4, the stand-in biome `minecraft:mesa` without U1) every 10 / 20 ticks for a
minute: the sorting and transformation particles in flight, and the chunk resend reaching the real
client (`chunk resent to 1 players`, the brown mesa cell on screen). `trees.vanilla_probe` (both
builds: the remote modes run it on the host) stands 250 blocks east and 64 north of the unit's row and
puts itself back after 2400 ticks (blocks in unloaded chunks are tried again every 600 ticks), so a
`full` run or a glob with only the probe leaves nothing behind; `trees.zz_showcase_restore` puts
everything (blocks, biomes, the stand-in) back at once.

## Known gaps (other units)

- Hollow oak sapling: the clearance (HollowTreeStructure#checkSaplingClearance) runs here; the hollow
  tree itself is U18's landmark generator (`twilightforest:hollow_tree`); until it lands the sapling
  stays, as TF's grower leaves it without a valid start. The landmark contract has no validate-only
  step, so the sapling stays in place while `generateAt` builds the start: a failed start changes
  nothing (no block update, no F11 events); after a valid one the sapling goes unless a piece put
  something else there (TF removes it before placing; its pieces overwrite that air).
- Transformation core: needs U1's enchanted forest biome.
- F11 recomputes leaf distances when leaves are placed (Java's tree placement uses
  UPDATE_KNOWN_SHAPE, which does not); TF's distances from `updateLeaves` are therefore replaced by
  the true distances a tick later. `trees.leaf_distance_after_growth` measures it on a sapling-grown
  mega canopy tree (right after growth 3911 of 4008 leaves at distance 1 and 97 decay-bound, as TF
  keeps them; 40 ticks later 1153 at distance 1 and none decay-bound) and logs `PARITY GAP (F11)`.
- Vanilla clients see the magic leaves (time, transformation, mining, sorting, rainbow oak) in the
  biome's foliage colour: the families kit gives them `tint_method: default_foliage` so the client
  mod's position tint (F12's `PositionDependentTintResolver` hook, which the game calls only for
  tinted materials) can draw ColorHandler's colours; Bedrock's tint methods are all biome colours, and
  a pre-tinted texture would be multiplied again by that tint. The fix belongs to F11 / F12 (see the
  PR's routing notes).
- F5's natural spawner does not check `BlockState#isValidSpawn`; hardened dark leaves use the
  special spawn list instead (a landmark's own list registered before it wins there).
