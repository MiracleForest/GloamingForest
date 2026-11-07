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
| A particle to one player (the mining core's per-player particle packets) | `src/feature/ext/trees/ParticleTo.{h,cpp}` (contract extension of fx) |
| A chunk's biome-only change marked for saving | `src/feature/ext/trees/ChunkDirty.{h,cpp}` (contract extension of worldgen) |
| Java HashSet iteration order (TreeFeature#updateLeaves) | `src/core/trees/JavaHashSet.{h,cpp}`, `tests/trees/` |
| Module and config section `trees` | `src/feature/trees/Module.cpp`, `Trees.h` |
| Selftests, e2e | `src/feature/trees/SelfTests.cpp`, `tools/e2e/{server,client}/scenarios/trees.ps1` |
| Pack: hardened dark leaves (foliage tint), particles | `assets/pack/bp/blocks/trees/`, `assets/pack/rp/particles/trees/` |
| Import overrides (mangrove trunk defaults, leaf mining speeds, dark leaves' light dampening) | `tools/import/overrides/trees.json` |
| Core sounds | F3's mapping in `assets/gen/audio/trees/rp/sounds.d/trees.json`: TF's `sounds.json` makes `block.twilightforest.core.time` an event reference to vanilla `block.comparator.click` (`sounds/random/click`) and `core.transformation` one to `block.note_block.harp` (`sounds/note/harp`); `Cores.cpp` plays them at TF's volume / pitch (0.35 / 0.5 and 0.1 / random 0..2). No synth recipe: TF ships no audio for them. |

## Magic log cores

A core's 20-tick scheduled tick is a module timer per active core (`gf:trees:cores` in the world
database; cores placed active by structures or commands start through F11's `onPlaced`). Ranges are
clamped to TF's `defineInRange(0, 128)`; a range of 0 disables the core.

- **Time**: 480 random positions per cycle; F11's random tick dispatch for TF blocks, the game's
  `Block::randomTick` for the vanilla blocks that tick randomly in Java 1.21.1 (a list by Bedrock id),
  the block actor tick for Java's server tickers. Like Java's `BlockState#isRandomlyTicking`, the
  state decides, not only the type: leaves tick only at distance 7 and not persistent (TF's leaves by
  their `distance` state; Bedrock's vanilla leaves keep none, so their LeavesBlock distance is walked
  through the leaves to a `#minecraft:logs` block), crops below their maximum age, cocoa / nether wart /
  sweet berries / chorus flowers below their caps, bamboo at stage 0, the heads of kelp and of
  weeping / twisting / cave vines below age 25 (their bodies never), attached stems never
  (`trees.time_core_ticks`). The particle sits on the top of the block's outline
  (Java: the occlusion shape's top; leaves and glass have none).
- **Transformation**: the enchanted forest is U1's behaviour pack biome
  `twilightforest:enchanted_forest`; without it (U1 not merged) the core warns once and does
  nothing, and `trees.transformation_core_biome` reports NOT RUN for the real biome and checks the
  mechanism with a stand-in biome. The chunk is marked for saving by touching its biome data
  counter the way the engine touches its dirty counters (`LevelChunk::mFullChunkDirtyTicksCounters`,
  measured: a clean counter counts from INT_MIN, a change sets `lastChange = totalTime`), and resent
  to every player whose view holds it through the player's `NetworkChunkPublisher::_sendQueuedChunk`
  (a LevelChunkPacket; the client asks for the sub-chunks again). No block is edited. The counter
  touch lives in the `ChunkDirty` extension with its measured semantics: Bedrock exports no setter,
  touch or dirty mark for a chunk without a block edit.
- **Mining**: `OreMagnetItem.doMagnet` translated (U8 owns the magnet item). The ore map is
  `ToolEvents#refreshOreMagnetCache`: every `c:ores_in_ground/<g>` tag with a matching
  `c:ore_bearing_ground/<g>` tag (the data's and F9's vanilla bridge), minus
  `#twilightforest:mining_tree_excluded` (coal), plus ancient debris -> netherrack; rebuilt when the
  data reloads. Its particles go as TF sends them: one packet per player within 64 blocks, each with
  its own 16 offsets drawn from the level's random (`ParticleTo` extension), so the random's
  consumption follows the number of players nearby as in TF.
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
- Spawning: no natural spawn stands on hardened dark leaves (`isValidSpawn(noSpawning)`) nor on the
  `LeavesBlock` leaves (twilight oak, canopy, mangrove, rainbow oak, time, transformation, mining,
  sorting: the default `isValidSpawn`, `isFaceSturdy(UP)`, fails on LeavesBlock's empty support
  shape): an empty spawn list there through `gf::spawn::onSpecialSpawn`. Dark leaves
  (DarkLeavesBlock, a full support shape) take the biome's list. `trees.hardened_dark_leaves`
  checks all ten. Pick block on hardened dark leaves gives dark leaves (`PickBlock` extension).
- Light: dark leaves block 15 levels of light (DarkLeavesBlock#getLightBlock: the Dark Forest
  canopy shuts the sky out) through a `params.components` patch of their family entry; the other
  LeavesBlock leaves block 1 (the families kit), hardened dark leaves are a full block (15).
  `trees.hardened_dark_leaves` checks the four values.
- Leaf runes: the client mod runs ClientLevel#animateTick's sampling itself; the server does it only
  for players without the client mod and sends each rune to that player alone (at most 16 a tick),
  and only while leaves of transformation are within the samples' reach (a block palette look over
  the sub-chunks within 31 blocks, refreshed every 20 ticks per player).

## Tree features

`TFTreeFeature#place` runs `TreeFeature.updateLeaves` (TF passes its leaf set as the sources, the
decorations and the trunk as filled, every set in the box; the seven work sets iterate in Java's
HashSet order, `core::trees::JavaHashSet`). TF's own tree features have no decorators in TF's data,
but `runDecorator` records what decorators place (set3) for them anyway.

**Temporary data patch (for A8):** `tools/import/overrides/trees.json` appends the decorator
`gf_trees:update_leaves` to TF's 16 `minecraft:tree` configured features, because F5's interpreter
writes no leaf distances. Once F5 runs `TreeFeature.updateLeaves` itself, the decorator turns into a
no-op on its own (`interpreterUpdatesLeaves`: a probe tree placed once in an in-memory world shows
whether the interpreter wrote distances), and A8 should drop the patch. Running both would be
harmless anyway (the walk is idempotent), only slower.

Roots: `FeatureLogic.ROOT_SHOULD_SKIP` is the tag `twilightforest:tree_roots_skip` (logs, roots,
liveroot, mangrove root, time wood, `#features_cannot_replace`). An earlier build asked for a
non-existent `root_trace_skip`, so traced roots replaced the roots of earlier strands (root is
worldgen-replaceable through `#moss_replaceable`): more roots under the trees (outside the
comparison boxes) and shifted random draws for every decorator after `tree_roots` (the firefly
canopy tree's fireflies and lanterns) and the tree of time's exposed roots overwriting its time wood.
Not ported: `StructureTemplate.updateShapeAtEdge` has no Bedrock counterpart (the game connects block shapes;
F11 recomputes leaf distances on neighbour changes). The decorators get their position sets in
Java's HashSet order, then sorted by y. `dark_canopy_tree` checks DarkCanopyTreeFeature's own free
height (logs are not free) before the tree interpreter, which replays the two trunk height draws.
`dark_canopy_tree` and `anywhere_tree` (SnowTreeFeature) then run their own copy of the older
`TreeFeature.updateLeaves` (distance 1 next to the trunk, five rounds lowering to `l + 1`, not
bounded by the tree's box): a decorator `gf_trees:legacy_update_leaves` appended to the
interpreter's decorator list runs it last, with the interpreter's trunk set (F5's `minecraft:tree`
itself writes no leaf distances).

### Java oracle comparison

`trees.golden_*` and `trees.sapling_hashes` also compare their box block by block with TF 4.8 on
NeoForge 1.21.1 (`data/gf/java_goldens/trees.json`, from `assets/data/java_goldens/trees.json`), when
the oracle placed the tree at the same absolute box (the positions follow the world's spawn: the
server e2e world's). The dump comes from a `trees` kind of the F13 oracle dumper
(`tests/trees/oracle/`: the Java source, the spec section and how to run it): each case clears the
flat area as `flatArea` does, places the configured feature with `LegacyRandomSource(seed)` or plants
the sapling and calls `SaplingBlock#advanceTree` twice with one, and dumps the box. The `mega_*`
cases plant a 2x2 of saplings (`trees.mega_2x2_java`): the mega canopy tree and the twilight oak's two
mega trees (seed 78001 draws 0.245, the forest mega oak; 83748 draws 0.016 < 0.1, the savannah mega oak).

What it showed and what was fixed:
- F5's `minecraft:tree` writes no leaf distances (Java's `TreeFeature.updateLeaves`): TF's
  vanilla-type trees run it through `gf_trees:update_leaves` (the temporary data patch above); the
  dark canopy and snow trees run their own older copy.
- Root tracing asked for a tag that does not exist (`root_trace_skip` instead of TF's
  `tree_roots_skip`): fixed, which brought the firefly canopy tree (its fireflies, jars and ropes
  after `tree_roots`) and the tree of time's base to Java's blocks.
- `trees.sapling_growers` grows the same saplings around the same spots through the game's setters,
  whose underground roots flatArea does not put back; `trees.sapling_hashes` resets the ground under
  each cell to the flat world's layers first, as the oracle does (an earlier root would stop the
  next tree's roots and shift its draws).

Every tree feature, golden tree and sapling now matches TF 4.8 block for block; the golden hashes are
those runs'. The comparison counts differences per feature (a placeAll cell or the case) and allows
differences only where they are explained (`kAllowances`, logged as `PARITY GAP (known)`, more
fails), all of them Java oracle artifacts: the oracle places in a live level, where
`HugeMushroomBlock#updateShape` clears the faces between cap blocks of the same kind, and worldgen
(WorldGenRegion) keeps them as Bedrock's features do: `canopy_red_spheroid_mushroom` (19 blocks)
and `canopy_red_smooth_mushroom` (8). The server scenario requires all 16 comparisons to run (each
logs `JAVA PARITY <case>: ...`); a world whose spawn differs from the oracle's logs `not compared`
and fails the scenario.

## Client e2e probes

`trees.showcase` (client builds) builds every tree and block for the screenshots, plus the game's own
brown and red cap blocks (`huge_mushroom_bits` 14) to compare the brown canopy mushroom's caps with.
`trees.particles_probe` (client builds) drives a sortingwood engine between two chests and a heart of
transformation (range 4, the stand-in biome `minecraft:mesa` without U1) every 10 / 20 ticks for a
minute: the sorting and transformation particles in flight, and the chunk resend reaching the real
client (`chunk resent to 1 players`, the brown mesa cell on screen). `trees.vanilla_probe` (both
builds: the remote modes run it on the host) changes nothing in a world without a real player (the
server `full` run); for a real client it stands 250 blocks east and 64 north of the unit's row with
its timewood clock placed inactive (its cycles run from the probe's own coroutine, so no active core
ever stands outside the unit's rows) and puts itself back after 2400 ticks (blocks in unloaded chunks
are tried again every 600 ticks); `trees.zz_showcase_restore` puts
everything (blocks, biomes, the stand-in) back at once.

## Vanilla-client deviations (best effort)

- Leaf runes: TF's ClientLevel#animateTick draws one rune for every sampled leaf of transformation,
  with no cap. A vanilla client gets the server's samples as packets, at most 16 runes per player per
  tick, so in a dense transformation tree a vanilla client sees fewer runes than TF shows. With the
  client mod the runes are drawn on the client, uncapped, as in TF.
- The magic leaves' colours on vanilla clients (see Known gaps).

## Known gaps (other units)

- Hollow oak sapling: the clearance (HollowTreeStructure#checkSaplingClearance) runs here; the hollow
  tree itself is U18's landmark generator (`twilightforest:hollow_tree`); until it lands the sapling
  stays, as TF's grower leaves it without a valid start. The landmark contract has no validate-only
  step, so the sapling stays in place while `generateAt` builds the start: a failed start changes
  nothing (no block update, no F11 events); after a valid one the sapling goes unless a piece put
  something else there (TF removes it before placing; its pieces overwrite that air). A valid start
  normally leaves the sapling's cell untouched: the hollow trunk is hollow (HollowTreeTrunk fills only
  `radius / 2 < dist <= radius`), so its center is the air TF's grower left; the grower then clears
  the sapling (a debug line, no warning).
- Hollow oak sapling at a chunk corner: U18 tells a placed hollow tree start from a natural one by its
  center (a natural start arrives at its chunk's corner, +7 or the point it found), so a sapling at
  exactly `(16 cx, 16 cz)` or `(16 cx + 7, 16 cz + 7)` grows the tree at a random point of its chunk
  instead of at the sapling until F6 records `forced` in the start's data (see U18's audit notes).
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
- F5's natural spawner does not check `BlockState#isValidSpawn`; TF's leaves (hardened dark leaves
  and the eight LeavesBlock leaves) use the special spawn list instead (a landmark's own list registered before it wins there).

## Audit round 1

Checked against TF 4.8 and the goldens (A1, W2 round 1):

- **`trees.features_a` flake** (one mega canopy leaf `distance 7` in Java, `2` here, at `89 -35 19`
  in some full runs): the features leave the leaves TF's `updateLeaves` does not reach at distance 0
  ("not computed"; the comparison takes 0 for Java's 7), and F11's random tick computes the true
  distance of a distance-0 leaf. A random tick landing on one while `placeAll` waits for the next
  cell's chunks changed that leaf. Not a generation difference: the Java oracle dumps its box without
  ticking. Fixed in the test: `placeAll` stops random ticks (`gamerule randomtickspeed 0`, the world's
  value put back afterwards) until its comparison is done.
- **Vanilla configured features TF references**: `tree/selector/highlands_trees` (25 % `minecraft:spruce`,
  10 % `minecraft:pine`) and `tree/selector/vanilla/vanilla_mushrooms` (`minecraft:huge_brown_mushroom`
  / `huge_red_mushroom`) named vanilla configured features F5's vanilla set lacks
  (`worldgen: unknown configured feature ...` in every full run): the highlands lost a third of
  their trees and the vanilla mushroom selector placed nothing. `tools/import/overrides/trees.json`
  now inlines Minecraft 1.21.1's values (TreeFeatures SPRUCE / PINE with `gf_trees:update_leaves`,
  HUGE_BROWN_MUSHROOM / HUGE_RED_MUSHROOM). F5 should add them to its vanilla set (then the patch
  is redundant and A8 can drop it).
- **Sapling warning**: the hollow oak sapling's `did not fill the ... cell` warning fired on every
  hollow tree (its trunk is hollow, see Known gaps): now a debug line.

Still open: the hollow oak sapling at a chunk corner (F6's `forced` flag, Known gaps); plants placed by
the tree features and the canopy mushrooms are to be re-checked once F5's `canSurvive` fix lands
(nothing floats in `trees.features_*`, which compare with Java block for block on flat ground).

## Audit round 2

Checked (W2 round 2): the vanilla feature overrides against F5's vanilla set, the plants the tree
features place, and the `trees.features_a` flake in the full runs since round 1.

Dependency: F5's vanilla set never reached the feature data on main. `vanillaFeatures()`
(src/feature/features/Features.cpp:82) loops over `Json::parse(kVanillaFeaturesExtra).items()`, whose
temporary dies before the C++20 range-for body runs, so the five extra features (spruce, pine, the huge
mushrooms, jungle grass) were never merged ("worldgen data: 150 configured features" instead of 155).
The inline copies hid this. Without them, spruce, pine and the huge mushrooms warn `unknown configured feature` until F5's fix (a
named variable) is merged. With that fix applied locally: 155 configured features and no warnings.
This change merges after it.

Fixed:

- **Vanilla features**: F5's vanilla set now has `minecraft:spruce`, `pine`, `huge_brown_mushroom` and
  `huge_red_mushroom` (`src/feature/features/VanillaFeatures.inc`, the same Java 1.21.1 values), so
  the inline copies in `tools/import/overrides/trees.json` (highlands tree selector, vanilla mushroom
  selector) are gone; the selectors name the vanilla ids again, as TF's data does. The inline spruce
  and pine carried `gf_trees:update_leaves`, which does nothing on Bedrock's spruce leaves (no
  distance state), so nothing placed changes.
- **`trees.features_a` flake** (`mega_canopy_tree ... Java distance 7, here 2`, also after round 1's
  random tick stop): the cells' trees reach into each other (the mega canopy tree and the forest mega
  oak next to it). A tree placed next to another's leaves fires F11's neighbour update (Java
  `LeavesBlock#updateShape`, scheduled one tick later), which computes the true distance of a leaf TF's
  `updateLeaves` left "not computed" (Java keeps 7: world generation sends no shape updates).
  `placeAll` waited for each cell's chunks between placements, so under load that tick could run
  before the comparison. It now loads every cell's chunks first, then places all the features and
  compares in the same tick.

Checked, unchanged:

- **Floating plants**: `trees.features_a/b/c` compare with the Java oracle block for block, so a plant
  a tree feature placed where Java's survival check refuses it would show as a difference; none does.
  The tree features place no small mushrooms or oreberries, so F5's brightness limit for mushroom /
  oreberry survival in generation (round 2) does not change them.

Still open: the hollow oak sapling at a chunk corner until F6 records `forced` in a start's data
(Known gaps).
