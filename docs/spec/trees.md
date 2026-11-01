# U3 `trees`: Trees, leaves, saplings and magic cores

Behaviour spec translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ `9e40a757`). Checklist for the
U3 worker and the W2 audit (A1). Numbers are TF's; "Bedrock:" paragraphs mark adaptation notes. The appendix lists
every owned configured feature, placed feature and loot table in compact form (generated from TF's data JSON).

| | |
|---|---|
| Unit | U3 `trees` (W1 batch 1) |
| Selftests | `trees.*`, testPos dx `300-399` (owners rows `selftest trees.*`, `selftest_dx 300-399`) |
| World DB | `gf:trees:*` |
| Net channels | `trees.*` |
| Config | section `trees` (config_section `trees`): `common.Magic Trees.timeCoreRange`, `common.Magic Trees.transformationCoreRange`, `common.Magic Trees.miningCoreRange`, `common.Magic Trees.sortingCoreRange` (each default 16, range 0..128; a value <= 0 disables that core; translations `time_range`, `transformation_range`, `mining_range`, `sorting_range`) |
| Contracts | features + worldgen (F5), landmark (F6: `no_structure` placement), families + blockevents (F11), inventory (F10, sorting core), client.render (F12 tints), fx (F10) |
| Java sources | `init/{TFBlocks,TFFeatures,TFFeatureModifiers,TFConfiguredFeatures,TFPlacedFeatures}.java`, `block/{DarkLeavesBlock,HardenedDarkLeavesBlock,TransformationLeavesBlock,MangroveSaplingBlock,SpecialMagicLogBlock,TimeLogCoreBlock,TransLogCoreBlock,MineLogCoreBlock,SortLogCoreBlock}.java`, `world/components/feature/trees/*.java`, `world/components/feature/trees/treeplacers/*.java`, `world/components/feature/trees/growers/*.java`, `world/components/feature/config/TFTreeFeatureConfig.java`, `world/registration/TreeConfigurations.java`, `world/registration/TreeDecorators.java`, `util/FeaturePlacers.java`, `util/FeatureLogic.java`, `client/event/ColorHandler.java`, `client/particle/{LeafRune,Sorting,Transformation,LogCore}Particle.java` |

Not owned here: the logs, woods, stripped variants, planks and every other wood-set block (U4 woodwork), roots and
liveroot, fireflies, mushgloom, troll-cave mushrooms' placement, hollow logs (U6 flora / U4), the hollow tree
structure (U18), the ore magnet logic (U8 `OreMagnetItem.doMagnet`, called by the mining core), the
`#twilightforest:sortable_entities` tag and `time_core_excluded` tag (F1/F9 data), biomes that place these features
(U1).

## 1. Leaves

All leaves: map colour PLANT, ignited by lava, push DESTROY, azalea-leaves sound, not suffocating / view-blocking /
redstone-conducting; vanilla `LeavesBlock` decay (`distance` 1-7, `persistent`, `waterlogged`) unless stated.

| Block | Strength | Special | Tint (client) |
|---|---|---|---|
| `twilightforest:twilight_oak_leaves` | 0.2, random ticks, no occlusion | | biome foliage colour |
| `twilightforest:canopy_leaves` | 0.2 | | `0xFF000000 | ((foliage & 0xFEFEFE) + 0x469A66) / 2`; evergreen default |
| `twilightforest:mangrove_leaves` | 0.2 | | `((foliage & 0xFEFEFE) + 0xC0E694) / 2`; birch default |
| `twilightforest:rainbow_oak_leaves` | 0.2 | | rainbow: `r = x*32 + y*16`, `g = y*32 + z*16`, `b = x*16 + z*32`; for each: if bit 256 set -> `255 - (v & 255)`; then r and b `&= 255`, g `^= 255` |
| `twilightforest:time_leaves` | 0.2 | | fade `x*16 + y*16 + z*16` (mirror at 256, `& 255`), spring (106,156,23) -> fall (251,108,27) |
| `twilightforest:transformation_leaves` | 0.2 | `animateTick`: one `twilightforest:leaf_rune` particle on the bottom face (velocity 0.5) | fade `x*27 + y*63 + z*39`, (108,204,234) -> (96,107,121) |
| `twilightforest:mining_leaves` | 0.2 | | fade `x*31 + y*33 + z*32`, (252,241,68) -> (237,172,9) |
| `twilightforest:sorting_leaves` | 0.2 | | fade `x*63 + y*63 + z*63`, (54,76,3) -> (168,199,43) |
| `twilightforest:dark_leaves` | **2.0 / 10.0**; no `randomTicks()` in its properties, but `LeavesBlock.isRandomlyTicking` (distance 7 and not persistent) still makes it decay like other leaves | full support shape, light block 15 (fully opaque to light) | biome foliage colour |
| `twilightforest:hardened_dark_leaves` | 2.0 / 10.0, a plain block (no decay, no states) | mobs never spawn on it; pick block gives dark leaves; no block item (owners: block only) | biome foliage colour |

Item tints: the leaf items use the default foliage / the same algorithms with no position (time leaves item
`(106,156,23)`, transformation `(108,204,234)`, mining `(252,241,68)`, sorting `(54,76,3)`).

Leaf loot (all: shears or silk touch -> the leaves; else sapling with fortune table, sticks 1-2 with chance
`[0.02, 0.0222, 0.025, 0.0333, 0.1]`, explosion decay):
- `twilightforest:blocks/twilight_oak_leaves` -> `twilightforest:twilight_oak_sapling` `[0.05, 0.0625, 0.0833, 0.1]`
- `twilightforest:blocks/canopy_leaves` -> `twilightforest:canopy_sapling` (same chances)
- `twilightforest:blocks/mangrove_leaves` -> `twilightforest:mangrove_sapling`
- `twilightforest:blocks/rainbow_oak_leaves` -> `twilightforest:rainbow_oak_sapling` (see appendix)
- `twilightforest:blocks/dark_leaves` and `twilightforest:blocks/hardened_dark_leaves` -> `twilightforest:darkwood_sapling`
  `[0.025, 0.0278, 0.0312, 0.0417, 0.1]` (hardened drops dark leaves under shears / silk touch)
- `twilightforest:blocks/time_leaves`, `twilightforest:blocks/transformation_leaves`,
  `twilightforest:blocks/mining_leaves`, `twilightforest:blocks/sorting_leaves`: no sapling (sticks only).

Bedrock: leaves come from F11's `leaves` family; tints are client-mod tint functions (`tint::registerBlock`), vanilla
clients get the default foliage map colour (magic leaves pre-coloured textures as the vanilla fallback).

## 2. Saplings and pots

All saplings: PLANT, push DESTROY, instabreak, grass sound, no collision, random ticks, vanilla sapling growth
(`stage` 0-1, bone meal 45 %, light >= 9).

| Sapling | Grower (`TFTreeGrowers`) | Potted |
|---|---|---|
| `twilightforest:twilight_oak_sapling` ("Sickly Twilight Oak Sapling") | `twilight_oak`: 2x2 mega -> `twilightforest:tree/forest_mega_oak_tree`, secondary mega `tree/savannah_mega_oak_tree`; tree `tree/twilight_oak_tree`, 10 % secondary `tree/large_twilight_oak_tree` | `twilightforest:potted_twilight_oak_sapling` |
| `twilightforest:canopy_sapling` | `canopy`: 2x2 -> `tree/mega_canopy_tree`, else `tree/canopy_tree` | `twilightforest:potted_canopy_sapling` |
| `twilightforest:mangrove_sapling` | `mangrove`: `tree/mangrove_tree`; MangroveSaplingBlock is waterloggable (`waterlogged` + `stage`), placed waterlogged in a full water source, unwaterlogs when no water touches it, survives on its normal soils | `twilightforest:potted_mangrove_sapling` |
| `twilightforest:darkwood_sapling` | `dark`: `tree/homegrown_darkwood_tree` | `twilightforest:potted_darkwood_sapling` |
| `twilightforest:hollow_oak_sapling` ("Robust Twilight Oak Sapling") | `StructureTreeGrower("hollow_oak")`: starts the `twilightforest:hollow_tree` structure (U18) at the sapling if `checkSaplingClearance` passes and every chunk of its box is loaded, removes the sapling and places every chunk of the start | `twilightforest:potted_hollow_oak_sapling` |
| `twilightforest:time_sapling` | `time`: `tree/time_tree` | `twilightforest:potted_time_sapling` |
| `twilightforest:transformation_sapling` | `transformation`: `tree/transformation_tree` | `twilightforest:potted_transformation_sapling` |
| `twilightforest:mining_sapling` | `mining`: `tree/mining_tree` | `twilightforest:potted_mining_sapling` |
| `twilightforest:sorting_sapling` | `sorting`: `tree/sorting_tree` | `twilightforest:potted_sorting_sapling` |
| `twilightforest:rainbow_oak_sapling` | `rainbow_oak`: `tree/rainbow_oak`, 10 % secondary `tree/large_rainbow_oak` | `twilightforest:potted_rainbow_oak_sapling` |

Pots are vanilla flower pots (copy of `minecraft:flower_pot`). Sapling loot: itself if it survives the explosion;
pot loot: flower pot + sapling (`twilightforest:blocks/potted_*`).

## 3. Magic log cores (`SpecialMagicLogBlock`)

Blocks `twilightforest:time_log_core` ("Timewood Clock"), `twilightforest:transformation_log_core` ("Heart of
Transformation"), `twilightforest:mining_log_core` ("Minewood Core"), `twilightforest:sorting_log_core` ("Sortingwood
Engine"): rotated pillars (`axis`) with `active` (bool), strength 2.0, wood sound, light 15 when active, map colours
of their wood. `onPlace` schedules a tick in 20 ticks.

Using the block toggles `active` (activating also schedules a tick). TF implements this in `useWithoutItem`, which in
1.21 runs when no item interaction consumes the use. It is not limited to an empty hand:
- The block does not override `useItemOn`, so `BlockBehaviour.useItemOn` returns `PASS_TO_DEFAULT_BLOCK_INTERACTION`
  for any held item, and `useWithoutItem` runs for the **main hand** with whatever it holds.
- It is skipped only when the player is sneaking while holding an item in either hand, because secondary use bypasses
  block interactions.
- `useWithoutItem` always returns SUCCESS (its PASS branch is unreachable). The held item's own `useOn` therefore never
  runs on a core: no block placement against it, no axe stripping. The off hand is never processed.
- It runs on both logical sides (no `isClientSide` check).

When the core is disabled by config, it is set inactive and the player gets the action-bar message
`misc.twilightforest.core_disabled` ("%s is disabled via config"), whose argument is the block's name component
(`this.getName()`, e.g. "Timewood Clock"), styled RED.

Bedrock: register `interactions::onUseOn({.block = <core id>})` with no item filter and return `Consume`, so the
held item is not used. Pass when the player is sneaking with an item in either hand. Debounce the repeated use-on
events (`firstEvent`). Every 20 ticks while active and enabled: play the core sound,
perform the effect, reschedule. Loot: the matching plain log (e.g. `twilightforest:blocks/mining_log_core` drops
`twilightforest:mining_log`, see appendix).

| Core | Effect per 20-tick cycle |
|---|---|
| Time | `8 * 3 * 20 = 480` random positions within `timeCoreRange` (cube, `nextInt(2r + 1) - r` per axis); blocks not in `#twilightforest:time_core_excluded` (nether portal) get a random tick if they tick randomly and a block-entity tick if they have a ticker; each success sends a `twilightforest:log_core_particle` (colour 0.953, 0.698, 0.0) at the block's top to players within 64. Sound `block.twilightforest.core.time` (BLOCKS, 0.35, 0.5). |
| Transformation | up to 16 tries of a random x/z offset within `transformationCoreRange` (y offset 0), skipping offsets farther than 16 blocks (distSq > 256) and columns already `twilightforest:enchanted_forest`; the first other column has every 4x4x4 biome cell in its chunk column (x/z quart of that position, all sections) set to enchanted forest, the chunk marked unsaved and the biomes resent; 9 `twilightforest:transformation_particle` flying from a 2-block ring to the column centre. Sound `block.twilightforest.core.transformation` (0.1, random 0-2). |
| Mining | one random position within `miningCoreRange`; `OreMagnetItem.doMagnet(level, core, target, true)` pulls ores toward the core; when something moved, play the magnet grab sound (U8's `twilightforest:magnet_grab` event, 0.1, 1.0). No core sound. |
| Sorting | inputs: item handlers of block entities within 2 blocks of the core (all faces) and `#twilightforest:sortable_entities` within 2 blocks; outputs: handlers within `sortingCoreRange` (cube) outside that inner box and sortable entities within 16; for each input, take the first extractable single item and move it into the output that already holds the most of that item (first non-full matching stack, else first empty valid slot); one item per input per cycle; a `twilightforest:sorting_particle` flies from the output toward the input. No sound. |

Particles (client, owned here): `twilightforest:log_core_particle` (rising, colour from velocity, alpha fades in
`min(age/lifetime * 1.35, 1)`, size shrinks `1 - f^2 / 2`); `twilightforest:transformation_particle` (size `0.5 *
(0.2..0.5)`, no physics, lifetime 20-38, colour (0.38, 0.82, 0.82), alpha fades); `twilightforest:sorting_particle`
(size `0.4 * (0.2..0.5)`, no physics, lifetime 19-28, colour (0, 0.9, 0.1)); `twilightforest:leaf_rune` (size
0-0.25, lifetime 40-49, gravity 0.15-0.25, tinted `(0.9f, 0.9f, f)`).

Sounds owned: `block.twilightforest.core.time`, `block.twilightforest.core.transformation` (re-synthesised through
`tools/sounds/recipes/trees.json`).

Bedrock: the time core uses LeviLamina's random-tick and block-actor tick calls; the transformation core needs the
F5 / U1 biome-override storage (Bedrock biome storage per 4x4x4 cell) plus a client chunk resend; the sorting core
uses `inventory::find` for containers; scheduled ticks through `blockevents` (no vanilla scheduled tick for custom
blocks; use a 20-tick module timer keyed by BlockData).

## 4. Other owned block

`twilightforest:mangrove_root` (block item `twilightforest:mangrove_root`): plain block, ignited by lava, bass, map
colour STONE, wood sound, strength 2.0. Loot `twilightforest:blocks/mangrove_root`: itself with silk touch, else 3-5
sticks (explosion decay). Placed as the exposed-roots provider of the mangrove tree.

## 5. Feature types (owned)

| Id | Java class | Config | Summary (translate exactly) |
|---|---|---|---|
| `twilightforest:mega_oak` | MegaOakTreeFeature (extends CanopyTreeFeature) | TFTreeFeatureConfig | 2x2 trunk of height `minHeight (+ nextInt(h/2) if 1/chanceFirst) (+ nextInt(5) if 1/chanceSecond)`; up to 7 fireflies on the trunk (1/3 each); 12-20 branches (tilt 0.15-0.5, length 5, from `height - 10 + b/2`, spread clamped to +-5) ending in 2.5 x 2.5 leaf spheroids; roots at the 4 trunk columns. Needs soil that sustains a canopy sapling. |
| `twilightforest:mega_canopy` | MegaCanopyTreeFeature | TFTreeFeatureConfig | 2x2 trunk `minHeight + nextInt(h/2) (1/chance1) + nextInt(h/4) (1/chance2)`; 7 fireflies (1/3); 6-8 branches length 15-18 tilt 0.25 from `height - 15 - (b + offset)`, each ending in a wood star and leaf discs radius 3 / 6 / 8 / 7 / 4.5 (odd circles, y +2..-2). |
| `twilightforest:dark_canopy_tree` | DarkCanopyTreeFeature | vanilla TreeConfiguration | vanilla-tree style placement that searches down to the ground, refuses water/unsuitable soil and needs 4 free blocks of trunk; foliage, decorators and roots per config (`tree/darkwood_tree`, `homegrown_darkwood_tree`, `darkwood_lantern_tree`, `dark_forest_oak_tree`, `dark_forest_birch_tree`, `dark_oak_bush`). |
| `twilightforest:large_winter_tree` | LargeWinterTreeFeature | TFTreeFeatureConfig | height 35, 1/3 +nextInt(10), then 1/8 +nextInt(10); 2x2 spruce trunk on dirt; leaf rings of radius by function per level (pine branches); 4-6 roots. |
| `twilightforest:miners_tree` | MiningTreeFeature | TFTreeFeatureConfig | trunk 0..9 tall, `twilightforest:mining_log_core` one above the base (axis y, tick scheduled 20) if the spot is a log or replaceable; branches, a 3x3x3 leaf box, 3-4 roots. |
| `twilightforest:tree_of_time` | TimeTreeFeature | TFTreeFeatureConfig | tree with a `twilightforest:time_log_core` placed one above the base and scheduled. |
| `twilightforest:anywhere_tree` | SnowTreeFeature | vanilla TreeConfiguration | a vanilla tree placer that can grow on any sturdy surface (snowy forest spruces). |
| `twilightforest:snow_under_trees` | SnowUnderTreeFeature | none | for every column of the chunk whose top motion-blocking block is leaves: place `minecraft:snow` on the highest non-leaf motion-blocking surface if it is air above a sturdy top face, and set `snowy = true` on the block below. |
| `twilightforest:canopy_brown_mushroom` | BrownCanopyMushroomFeature | HugeMushroomFeatureConfiguration | huge brown mushroom with a canopy-style branching stem. |
| `twilightforest:canopy_red_vanilla_mushroom` | RedCanopyMushroomFeature(type 0) | HugeMushroomFeatureConfiguration | red cap, vanilla shape |
| `twilightforest:canopy_red_smooth_mushroom` | RedCanopyMushroomFeature(type 1) | same | smooth dome cap |
| `twilightforest:canopy_red_spheroid_mushroom` | RedCanopyMushroomFeature(type 2) | same | spheroid cap |
| `twilightforest:canopy_red_flat_mushroom` | RedCanopyMushroomFeature(type 3) | same | flat cap |

All `TFTreeFeature`s record trunk / leaf / root / decoration positions, run the config's decorators and then update
leaf distances (`TreeFeature.updateLeaves`) and shapes at the edge (vanilla post-processing).

## 6. Tree placers and decorators (owners kind `placement_modifier`)

| Id | Registry | Codec fields | Behaviour |
|---|---|---|---|
| `twilightforest:branching_trunk_placer` | trunk placer | base height + rand a/b, `branch_start_offset_down` 0..24, `branch_config {branch_provider, count_minimum 0..16, random_add_count, length 1..24, random_add_length, spacing_yaw 0..0.5 (0.3), downwards_pitch 0..1 (0.2)}`, `perpendicular_branches`, `prevent_exposed_root` | straight trunk built upward until blocked (height shortened), then `count + nextInt(randomAdd + 1)` branches drawn with a voxel Bresenham line from `height - offsetDown`; perpendicular mode draws an L (horizontal then vertical); foliage attaches at branch ends and the top. |
| `twilightforest:trunk_mover_upper` | trunk placer (TrunkRiser) | `offset_up` 0..16, `trunk_placer` | delegates to the wrapped placer `offset_up` blocks higher (stilted trees). |
| `twilightforest:spheroid_foliage_placer` | foliage placer | `horizontal_radius`, `vertical_radius` 0..16, `offset`, `random_add_horizontal`, `random_add_vertical`, `vertical_filler_bias` -0.5..0.5, `shag_factor` 0..256 | `FeaturePlacers.placeSpheroid` of `radiusOffset + horizontal + nextInt(randomH + 1)` by `vertical + nextInt(randomV + 1)` with bias; plus `shag_factor` random rays (yaw 0..2pi, pitch -1..1) of leaves out of the spheroid. |
| `twilightforest:core_placer` | tree decorator | `core_position` 0..20, `deco_provider` | sets the block at the first log + `core_position` up (magic cores). |
| `twilightforest:trunkside_decorator` | tree decorator | `placement_count` 0..64, `probability_of_placement` 0..1, `deco_provider` | per attempt (with probability) a random log + random horizontal direction; places the deco in air, rotating `facing` to that direction (fireflies). |
| `twilightforest:tree_roots` | tree decorator | `base_strand_count`, `additional_random_strands`, `root_length` 0..32, `y_offset`, `exposed_roots_provider` (optional), `ground_roots_provider`, `rootPenetrability` | `strands + nextInt(extra + 1)` roots spread around the trunk base (angle offset random), traced with Bresenham; with an exposed provider the root starts above ground and becomes the ground provider when underground (`traceExposedRoot`), else `traceRoot`. |
| `twilightforest:dangle_from_tree_decorator` | tree decorator | `attempts_minimum`, `random_add_attempts`, `minimum_required_length`, `base_length`, `random_add_length`, `rope_provider`, `baggage_provider` | from random leaves hang a rope of `base + nextInt(add + 1)` downward through air (needs at least the minimum free length) ending in a baggage block (darkwood lantern trees: lanterns). |

## 7. Configured and placed features (overview)

Owned configured features (54) and placed features (28) are listed in full in the appendix. Groups:
- Trees: `twilightforest:tree/twilight_oak_tree`, `tree/large_twilight_oak_tree`, `tree/swampy_oak_tree`,
  `tree/canopy_tree`, `tree/firefly_canopy_tree`, `tree/dead_canopy_tree`, `tree/mega_canopy_tree`,
  `tree/forest_mega_oak_tree`, `tree/savannah_mega_oak_tree`, `tree/mangrove_tree`, `tree/darkwood_tree`,
  `tree/homegrown_darkwood_tree`, `tree/darkwood_lantern_tree`, `tree/dark_forest_oak_tree`,
  `tree/dark_forest_birch_tree`, `tree/dark_oak_bush`, `tree/oak_bush`, `tree/rainbow_oak`, `tree/large_rainbow_oak`,
  `tree/time_tree`, `tree/transformation_tree`, `tree/mining_tree`, `tree/sorting_tree`, `tree/snowy_spruce_tree`,
  `tree/mega_spruce_tree`, `tree/large_winter_tree`, `tree/smaller_jungle_tree`, `tree/vanilla_oak_tree`,
  `tree/vanilla_birch_tree`, `tree/dummy` (no-op).
- Selectors: `tree/selector/canopy_trees`, `dense_canopy_trees`, `dark_forest_trees`, `enchanted_forest_trees`,
  `firefly_forest_trees`, `highlands_trees`, `snowy_forest_trees`, `vanilla_trees`, `vanilla/vanilla_mushrooms`.
- Mushrooms: `mushroom/brown_canopy_mushroom`, `mushroom/red_canopy_mushroom` (weighted 33 each of the four red
  variants), `mushroom/canopy_red_{vanilla,smooth,spheroid,flat}_mushroom`, `mushroom/canopy_mushrooms_dense` (67.5 %
  brown, 22.5 % red), `mushroom/canopy_mushrooms_sparse` (15 % / 5 %), `mushroom/big_mushgloom`,
  `dark_brown_mushrooms` / `dark_red_mushrooms` (dark-forest placer, 50 tries, spread 7 / 3),
  `troll_huge_brown_mushrooms`, `troll_huge_red_mushrooms`.
- Misc: `twilightforest:canopy_fallen_log` (fallen small log, canopy), `twilightforest:snow_under_trees`.

Placed features use TF placement modifiers `twilightforest:no_structure` (avoid landmarks, F6),
`twilightforest:chunk_centerer` (mega canopy trees) and vanilla ones. They are referenced from U1's biomes.

## 8. Strings

Lang keys owned (47): every block / item name above (e.g. "Sickly Twilight Oak Sapling", "Robust Twilight Oak
Sapling", "Timewood Clock", "Heart of Transformation", "Minewood Core", "Sortingwood Engine", "Leaves of
Transformation"), `misc.twilightforest.core_disabled` ("%s is disabled via config", `%s` = the core block's name), the
two core subtitles and the four config keys
(`config.twilightforest.time_range` ... with tooltips).

## 9. Selftests (`trees.*`, dx 300-399)

`trees.sapling_growers` (each sapling grows its configured feature on bone meal), `trees.mega_2x2`,
`trees.time_core_ticks` (a crop grows faster), `trees.transformation_core_biome`, `trees.mining_core_magnet`,
`trees.sorting_core_moves_item`, `trees.core_disabled_config` (message argument is the core's name),
`trees.core_toggle_held_item` (using a core while holding a block toggles it and places nothing), `trees.leaf_decay`,
`trees.leaf_loot`, `trees.snow_under_trees`, golden volume hashes of seeded `tree/canopy_tree`, `tree/mega_canopy_tree`,
`tree/darkwood_tree` and `tree/mangrove_tree` placements.

## Appendix: TF data (compact, generated from TF 4.8 data JSON)

Notation: `block[state=value]`; `weighted{a:w}`; loot `xN` count, `wN` weight, `[...]` conditions; placement modifiers joined by `>`. The imported JSON (F1) is authoritative; this appendix is the review checklist.

### Configured features (54)

- `twilightforest:canopy_fallen_log`: type `twilightforest:fallen_small_log` {hollow=twilightforest:hollow_canopy_log_horizontal[axis=x,variant=empty], normal=twilightforest:canopy_log[axis=y]}
- `twilightforest:dark_brown_mushrooms`: type `twilightforest:dark_forest_placer` {feature={feature=simple_block{config={to_place=brown_mushroom}}, placement=[block_predicate_filter{predicate=all_of{predicates=[replaceable, matching_block_tag{offset=[0, -1, 0], tag=dirt}]}}]}, tries=50, xz_spread=7, y_spread=3}
- `twilightforest:dark_red_mushrooms`: type `twilightforest:dark_forest_placer` {feature={feature=simple_block{config={to_place=red_mushroom}}, placement=[block_predicate_filter{predicate=all_of{predicates=[replaceable, matching_block_tag{offset=[0, -1, 0], tag=dirt}]}}]}, tries=50, xz_spread=7, y_spread=3}
- `twilightforest:mushroom/big_mushgloom`: type `twilightforest:big_mushgloom` {cap_provider=twilightforest:huge_mushgloom[down=false,east=true,north=true,south=true,up=true,west=true], foliage_radius=1, stem_provider=twilightforest:huge_mushgloom_stem[down=false,east=true,north=true,south=true,up=false,west=true]}
- `twilightforest:mushroom/brown_canopy_mushroom`: type `twilightforest:canopy_brown_mushroom` {cap_provider=brown_mushroom_block[down=false,east=true,north=true,south=true,up=true,west=true], foliage_radius=3, stem_provider=mushroom_stem[down=false,east=true,north=true,south=true,up=false,west=true]}
- `twilightforest:mushroom/canopy_mushrooms_dense`: type `minecraft:random_selector` {default={feature=twilightforest:tree/dummy, placement=[]}, features=[{chance=0.675, feature={feature=twilightforest:mushroom/brown_canopy_mushroom, placement=[]}}, {chance=0.225, feature={feature=twilightforest:mushroom/red_canopy_mushroom, placement=[]}}]}
- `twilightforest:mushroom/canopy_mushrooms_sparse`: type `minecraft:random_selector` {default={feature=twilightforest:tree/dummy, placement=[]}, features=[{chance=0.15, feature={feature=twilightforest:mushroom/brown_canopy_mushroom, placement=[]}}, {chance=0.05, feature={feature=twilightforest:mushroom/red_canopy_mushroom, placement=[]}}]}
- `twilightforest:mushroom/canopy_red_flat_mushroom`: type `twilightforest:canopy_red_flat_mushroom` {cap_provider=red_mushroom_block[down=false,east=true,north=true,south=true,up=true,west=true], foliage_radius=3, stem_provider=mushroom_stem[down=false,east=true,north=true,south=true,up=false,west=true]}
- `twilightforest:mushroom/canopy_red_smooth_mushroom`: type `twilightforest:canopy_red_smooth_mushroom` {cap_provider=red_mushroom_block[down=false,east=true,north=true,south=true,up=true,west=true], foliage_radius=3, stem_provider=mushroom_stem[down=false,east=true,north=true,south=true,up=false,west=true]}
- `twilightforest:mushroom/canopy_red_spheroid_mushroom`: type `twilightforest:canopy_red_spheroid_mushroom` {cap_provider=red_mushroom_block[down=false,east=true,north=true,south=true,up=true,west=true], foliage_radius=3, stem_provider=mushroom_stem[down=false,east=true,north=true,south=true,up=false,west=true]}
- `twilightforest:mushroom/canopy_red_vanilla_mushroom`: type `twilightforest:canopy_red_vanilla_mushroom` {cap_provider=red_mushroom_block[down=false,east=true,north=true,south=true,up=true,west=true], foliage_radius=3, stem_provider=mushroom_stem[down=false,east=true,north=true,south=true,up=false,west=true]}
- `twilightforest:mushroom/red_canopy_mushroom`: type `twilightforest:weighted_list` [{data={feature=twilightforest:mushroom/canopy_red_vanilla_mushroom, placement=[]}, weight=33}, {data={feature=twilightforest:mushroom/canopy_red_smooth_mushroom, placement=[]}, weight=33}, {data={feature=twilightforest:mushroom/canopy_red_spheroid_mushroom, placement=[]}, weight=33}, {data={feature=twilightforest:mushroom/canopy_red_flat_mushroom, placement=[]}, weight=1}]
- `twilightforest:snow_under_trees`: type `twilightforest:snow_under_trees` {}
- `twilightforest:tree/canopy_tree`: type `minecraft:tree` {decorators=[twilightforest:trunkside_decorator{deco_provider=twilightforest:firefly[facing=up], placement_count=2, probability_of_placement=1}, twilightforest:tree_roots{additional_random_strands=1, base_strand_count=3, ground_roots_provider=weighted{twilightforest:root:6, twilightforest:liveroot_block:1}, rootPenetrability=1, root_length=5, y_offset=0}], dirt_provider=dirt, foliage_placer=twilightforest:spheroid_foliage_placer{horizontal_radius=4.123106, offset=0, random_add_horizontal=0, random_add_vertical=0, shag_factor=24, vertical_filler_bias=-0.2, vertical_radius=1.5}, foliage_provider=twilightforest:canopy_leaves[distance=7,persistent=false,waterlogged=false], force_dirt=False, ignore_vines=True, minimum_size=two_layers_feature_size{limit=20, lower_size=0, upper_size=5}, trunk_placer=twilightforest:branching_trunk_placer{base_height=20, branch_config={branch_provider=twilightforest:canopy_wood[axis=y], count_minimum=3, downwards_pitch=0.2, length=10, random_add_count=1, random_add_length=1, spacing_yaw=0.3}, branch_start_offset_down=12, height_rand_a=5, height_rand_b=5, perpendicular_branches=False, prevent_exposed_root=True}, trunk_provider=twilightforest:canopy_log[axis=y]}
- `twilightforest:tree/dark_forest_birch_tree`: type `twilightforest:dark_canopy_tree` {decorators=[], dirt_provider=dirt, foliage_placer=blob_foliage_placer{height=3, offset=0, radius=2}, foliage_provider=birch_leaves[distance=7,persistent=false,waterlogged=false], force_dirt=False, ignore_vines=True, minimum_size=two_layers_feature_size{limit=1, lower_size=0, upper_size=1}, trunk_placer=straight_trunk_placer{base_height=5, height_rand_a=2, height_rand_b=0}, trunk_provider=birch_log[axis=y]}
- `twilightforest:tree/dark_forest_oak_tree`: type `twilightforest:dark_canopy_tree` {decorators=[], dirt_provider=dirt, foliage_placer=blob_foliage_placer{height=3, offset=0, radius=2}, foliage_provider=oak_leaves[distance=7,persistent=false,waterlogged=false], force_dirt=False, ignore_vines=True, minimum_size=two_layers_feature_size{limit=1, lower_size=0, upper_size=1}, trunk_placer=straight_trunk_placer{base_height=4, height_rand_a=2, height_rand_b=0}, trunk_provider=oak_log[axis=y]}
- `twilightforest:tree/dark_oak_bush`: type `twilightforest:dark_canopy_tree` {decorators=[], dirt_provider=dirt, foliage_placer=bush_foliage_placer{height=2, offset=1, radius=2}, foliage_provider=oak_leaves[distance=7,persistent=false,waterlogged=false], force_dirt=False, ignore_vines=True, minimum_size=two_layers_feature_size{limit=0, lower_size=0, upper_size=0}, trunk_placer=straight_trunk_placer{base_height=1, height_rand_a=0, height_rand_b=0}, trunk_provider=oak_log[axis=y]}
- `twilightforest:tree/darkwood_lantern_tree`: type `twilightforest:dark_canopy_tree` {decorators=[twilightforest:tree_roots{additional_random_strands=1, base_strand_count=3, ground_roots_provider=weighted{twilightforest:root:6, twilightforest:liveroot_block:1}, rootPenetrability=1, root_length=5, y_offset=0}, twilightforest:dangle_from_tree_decorator{attempts_minimum=0, baggage_provider={entries=[{data=lantern[hanging=true,waterlogged=false], weight=1}]}, base_length=4, minimum_required_length=2, random_add_attempts=1, random_add_length=2, rope_provider={entries=[{data=chain[axis=y,waterlogged=false], weight=1}]}}], dirt_provider=dirt, foliage_placer=twilightforest:spheroid_foliage_placer{horizontal_radius=4.5, offset=0, random_add_horizontal=1, random_add_vertical=0, shag_factor=36, vertical_filler_bias=0.45, vertical_radius=2.25}, foliage_provider=twilightforest:dark_leaves[distance=7,persistent=false,waterlogged=false], force_dirt=False, ignore_vines=True, minimum_size=two_layers_feature_size{limit=4, lower_size=1, upper_size=1}, trunk_placer=twilightforest:branching_trunk_placer{base_height=9, branch_config={branch_provider=twilightforest:dark_wood[axis=y], count_minimum=4, downwards_pitch=0.23, length=8, random_add_count=0, random_add_length=2, spacing_yaw=0.23}, branch_start_offset_down=6, height_rand_a=1, height_rand_b=1, perpendicular_branches=False, prevent_exposed_root=False}, trunk_provider=twilightforest:dark_log[axis=y]}
- `twilightforest:tree/darkwood_tree`: type `twilightforest:dark_canopy_tree` {decorators=[twilightforest:tree_roots{additional_random_strands=1, base_strand_count=3, ground_roots_provider=weighted{twilightforest:root:6, twilightforest:liveroot_block:1}, rootPenetrability=1, root_length=5, y_offset=0}], dirt_provider=dirt, foliage_placer=twilightforest:spheroid_foliage_placer{horizontal_radius=4.5, offset=0, random_add_horizontal=1, random_add_vertical=0, shag_factor=36, vertical_filler_bias=0.45, vertical_radius=2.25}, foliage_provider=twilightforest:hardened_dark_leaves, force_dirt=False, ignore_vines=True, minimum_size=two_layers_feature_size{limit=4, lower_size=1, upper_size=1}, trunk_placer=twilightforest:branching_trunk_placer{base_height=9, branch_config={branch_provider=twilightforest:dark_wood[axis=y], count_minimum=4, downwards_pitch=0.23, length=8, random_add_count=0, random_add_length=2, spacing_yaw=0.23}, branch_start_offset_down=6, height_rand_a=1, height_rand_b=1, perpendicular_branches=False, prevent_exposed_root=False}, trunk_provider=twilightforest:dark_log[axis=y]}
- `twilightforest:tree/dead_canopy_tree`: type `minecraft:tree` {decorators=[twilightforest:trunkside_decorator{deco_provider=twilightforest:firefly[facing=up], placement_count=2, probability_of_placement=1}, twilightforest:tree_roots{additional_random_strands=1, base_strand_count=3, ground_roots_provider=weighted{twilightforest:root:6, twilightforest:liveroot_block:1}, rootPenetrability=1, root_length=5, y_offset=0}], dirt_provider=dirt, foliage_placer=twilightforest:spheroid_foliage_placer{horizontal_radius=0, offset=0, random_add_horizontal=0, random_add_vertical=0, shag_factor=0, vertical_filler_bias=0, vertical_radius=0}, foliage_provider=air, force_dirt=False, ignore_vines=True, minimum_size=two_layers_feature_size{limit=20, lower_size=0, upper_size=5}, trunk_placer=twilightforest:branching_trunk_placer{base_height=20, branch_config={branch_provider=twilightforest:canopy_wood[axis=y], count_minimum=3, downwards_pitch=0.2, length=10, random_add_count=1, random_add_length=1, spacing_yaw=0.3}, branch_start_offset_down=12, height_rand_a=5, height_rand_b=5, perpendicular_branches=False, prevent_exposed_root=True}, trunk_provider=twilightforest:canopy_log[axis=y]}
- `twilightforest:tree/dummy`: type `minecraft:no_op` {}
- `twilightforest:tree/firefly_canopy_tree`: type `minecraft:tree` {decorators=[twilightforest:tree_roots{additional_random_strands=1, base_strand_count=3, ground_roots_provider=weighted{twilightforest:root:6, twilightforest:liveroot_block:1}, rootPenetrability=1, root_length=5, y_offset=0}, twilightforest:trunkside_decorator{deco_provider=twilightforest:firefly[facing=up], placement_count=2, probability_of_placement=1}, twilightforest:trunkside_decorator{deco_provider=twilightforest:firefly[facing=north], placement_count=4, probability_of_placement=0.5}, twilightforest:dangle_from_tree_decorator{attempts_minimum=1, baggage_provider={entries=[{data=twilightforest:firefly_jar[waterlogged=false], weight=10}, {data=twilightforest:cicada_jar[waterlogged=false], weight=1}]}, base_length=5, minimum_required_length=2, random_add_attempts=1, random_add_length=15, rope_provider={entries=[{data=twilightforest:rope[waterlogged=false,x=false,y=true,z=false], weight=3}, {data=chain[axis=y,waterlogged=false], weight=1}]}}], dirt_provider=dirt, foliage_placer=twilightforest:spheroid_foliage_placer{horizontal_radius=4.123106, offset=0, random_add_horizontal=0, random_add_vertical=0, shag_factor=24, vertical_filler_bias=-0.2, vertical_radius=1.5}, foliage_provider=twilightforest:canopy_leaves[distance=7,persistent=false,waterlogged=false], force_dirt=False, ignore_vines=True, minimum_size=two_layers_feature_size{limit=20, lower_size=1, upper_size=5}, trunk_placer=twilightforest:branching_trunk_placer{base_height=20, branch_config={branch_provider=twilightforest:canopy_wood[axis=y], count_minimum=3, downwards_pitch=0.2, length=10, random_add_count=1, random_add_length=1, spacing_yaw=0.3}, branch_start_offset_down=12, height_rand_a=5, height_rand_b=5, perpendicular_branches=False, prevent_exposed_root=True}, trunk_provider=twilightforest:canopy_log[axis=y]}
- `twilightforest:tree/forest_mega_oak_tree`: type `twilightforest:mega_oak` {add_first_five_chance=1, add_second_five_chance=1, branch_provider=twilightforest:twilight_oak_wood[axis=y], check_water=False, decorators=[], has_leaves=False, leaves_provider=twilightforest:twilight_oak_leaves[distance=7,persistent=false,waterlogged=false], minimum_size=24, roots_provider=twilightforest:root, trunk_provider=twilightforest:twilight_oak_log[axis=y]}
- `twilightforest:tree/homegrown_darkwood_tree`: type `twilightforest:dark_canopy_tree` {decorators=[twilightforest:tree_roots{additional_random_strands=1, base_strand_count=3, ground_roots_provider=weighted{twilightforest:root:6, twilightforest:liveroot_block:1}, rootPenetrability=1, root_length=5, y_offset=0}], dirt_provider=dirt, foliage_placer=twilightforest:spheroid_foliage_placer{horizontal_radius=4.5, offset=0, random_add_horizontal=1, random_add_vertical=0, shag_factor=36, vertical_filler_bias=0.45, vertical_radius=2.25}, foliage_provider=twilightforest:dark_leaves[distance=7,persistent=false,waterlogged=false], force_dirt=False, ignore_vines=True, minimum_size=two_layers_feature_size{limit=4, lower_size=1, upper_size=1}, trunk_placer=twilightforest:branching_trunk_placer{base_height=9, branch_config={branch_provider=twilightforest:dark_wood[axis=y], count_minimum=4, downwards_pitch=0.23, length=8, random_add_count=0, random_add_length=2, spacing_yaw=0.23}, branch_start_offset_down=6, height_rand_a=1, height_rand_b=1, perpendicular_branches=False, prevent_exposed_root=False}, trunk_provider=twilightforest:dark_log[axis=y]}
- `twilightforest:tree/large_rainbow_oak`: type `minecraft:tree` {decorators=[twilightforest:tree_roots{additional_random_strands=1, base_strand_count=3, ground_roots_provider=weighted{twilightforest:root:6, twilightforest:liveroot_block:1}, rootPenetrability=1, root_length=5, y_offset=0}], dirt_provider=dirt, foliage_placer=fancy_foliage_placer{height=4, offset=4, radius=2}, foliage_provider=twilightforest:rainbow_oak_leaves[distance=7,persistent=false,waterlogged=false], force_dirt=False, ignore_vines=False, minimum_size=two_layers_feature_size{limit=4, lower_size=1, min_clipped_height=4, upper_size=0}, trunk_placer=fancy_trunk_placer{base_height=3, height_rand_a=11, height_rand_b=0}, trunk_provider=twilightforest:twilight_oak_log[axis=y]}
- `twilightforest:tree/large_twilight_oak_tree`: type `minecraft:tree` {decorators=[twilightforest:tree_roots{additional_random_strands=1, base_strand_count=3, ground_roots_provider=weighted{twilightforest:root:6, twilightforest:liveroot_block:1}, rootPenetrability=1, root_length=5, y_offset=0}], dirt_provider=dirt, foliage_placer=fancy_foliage_placer{height=4, offset=4, radius=2}, foliage_provider=twilightforest:twilight_oak_leaves[distance=7,persistent=false,waterlogged=false], force_dirt=False, ignore_vines=False, minimum_size=two_layers_feature_size{limit=4, lower_size=1, min_clipped_height=4, upper_size=0}, trunk_placer=fancy_trunk_placer{base_height=3, height_rand_a=11, height_rand_b=0}, trunk_provider=twilightforest:twilight_oak_log[axis=y]}
- `twilightforest:tree/large_winter_tree`: type `twilightforest:large_winter_tree` {add_first_five_chance=1, add_second_five_chance=1, branch_provider=spruce_log[axis=y], check_water=False, decorators=[], has_leaves=False, leaves_provider=spruce_leaves[distance=7,persistent=false,waterlogged=false], minimum_size=0, roots_provider=twilightforest:root, trunk_provider=spruce_log[axis=y]}
- `twilightforest:tree/mangrove_tree`: type `minecraft:tree` {decorators=[twilightforest:trunkside_decorator{deco_provider=twilightforest:firefly[facing=up], placement_count=2, probability_of_placement=1}, twilightforest:tree_roots{additional_random_strands=1, base_strand_count=3, exposed_roots_provider=twilightforest:mangrove_root, ground_roots_provider=weighted{twilightforest:root:4, twilightforest:liveroot_block:1}, rootPenetrability=1, root_length=12, y_offset=0}, leave_vine{probability=0.125}], dirt_provider=dirt, foliage_placer=twilightforest:spheroid_foliage_placer{horizontal_radius=2.5, offset=0, random_add_horizontal=2, random_add_vertical=0, shag_factor=15, vertical_filler_bias=-0.25, vertical_radius=1.5}, foliage_provider=twilightforest:mangrove_leaves[distance=7,persistent=false,waterlogged=false], force_dirt=False, ignore_vines=False, minimum_size=two_layers_feature_size{limit=4, lower_size=1, upper_size=1}, trunk_placer=twilightforest:trunk_mover_upper{offset_up=4, trunk_placer=twilightforest:branching_trunk_placer{base_height=7, branch_config={branch_provider=twilightforest:mangrove_wood[axis=y], count_minimum=0, downwards_pitch=0.25, length=6, random_add_count=3, random_add_length=2, spacing_yaw=0.3}, branch_start_offset_down=6, height_rand_a=4, height_rand_b=0, perpendicular_branches=False, prevent_exposed_root=False}}, trunk_provider=twilightforest:mangrove_log[axis=y]}
- `twilightforest:tree/mega_canopy_tree`: type `twilightforest:mega_canopy` {add_first_five_chance=2, add_second_five_chance=3, branch_provider=twilightforest:canopy_wood[axis=y], check_water=False, decorators=[], has_leaves=False, leaves_provider=twilightforest:canopy_leaves[distance=7,persistent=false,waterlogged=false], minimum_size=30, roots_provider=twilightforest:root, trunk_provider=twilightforest:canopy_log[axis=y]}
- `twilightforest:tree/mega_spruce_tree`: type `minecraft:tree` {decorators=[], dirt_provider=dirt, foliage_placer=mega_pine_foliage_placer{crown_height=uniform(13..17), offset=0, radius=0}, foliage_provider=spruce_leaves[distance=7,persistent=false,waterlogged=false], force_dirt=False, ignore_vines=False, minimum_size=two_layers_feature_size{limit=4, lower_size=1, upper_size=2}, trunk_placer=giant_trunk_placer{base_height=13, height_rand_a=2, height_rand_b=14}, trunk_provider=spruce_log[axis=y]}
- `twilightforest:tree/mining_tree`: type `twilightforest:miners_tree` {add_first_five_chance=1, add_second_five_chance=1, branch_provider=twilightforest:mining_wood[axis=y], check_water=False, decorators=[], has_leaves=False, leaves_provider=twilightforest:mining_leaves[distance=7,persistent=false,waterlogged=false], minimum_size=0, roots_provider=twilightforest:root, trunk_provider=twilightforest:mining_log[axis=y]}
- `twilightforest:tree/oak_bush`: type `minecraft:tree` {decorators=[], dirt_provider=dirt, foliage_placer=bush_foliage_placer{height=2, offset=1, radius=2}, foliage_provider=oak_leaves[distance=7,persistent=false,waterlogged=false], force_dirt=False, ignore_vines=True, minimum_size=two_layers_feature_size{limit=0, lower_size=0, upper_size=0}, trunk_placer=straight_trunk_placer{base_height=1, height_rand_a=0, height_rand_b=0}, trunk_provider=oak_log[axis=y]}
- `twilightforest:tree/rainbow_oak`: type `minecraft:tree` {decorators=[twilightforest:tree_roots{additional_random_strands=1, base_strand_count=3, ground_roots_provider=weighted{twilightforest:root:6, twilightforest:liveroot_block:1}, rootPenetrability=1, root_length=5, y_offset=0}], dirt_provider=dirt, foliage_placer=blob_foliage_placer{height=3, offset=0, radius=2}, foliage_provider=twilightforest:rainbow_oak_leaves[distance=7,persistent=false,waterlogged=false], force_dirt=False, ignore_vines=False, minimum_size=two_layers_feature_size{limit=1, lower_size=1, upper_size=1}, trunk_placer=straight_trunk_placer{base_height=4, height_rand_a=2, height_rand_b=0}, trunk_provider=twilightforest:twilight_oak_log[axis=y]}
- `twilightforest:tree/savannah_mega_oak_tree`: type `twilightforest:mega_oak` {add_first_five_chance=1, add_second_five_chance=1, branch_provider=twilightforest:twilight_oak_wood[axis=y], check_water=False, decorators=[], has_leaves=False, leaves_provider=twilightforest:twilight_oak_leaves[distance=7,persistent=false,waterlogged=false], minimum_size=16, roots_provider=twilightforest:root, trunk_provider=twilightforest:twilight_oak_log[axis=y]}
- `twilightforest:tree/selector/canopy_trees`: type `minecraft:random_selector` {default={feature=twilightforest:tree/twilight_oak_tree, placement=[]}, features=[{chance=0.6, feature={feature=twilightforest:tree/canopy_tree, placement=[]}}]}
- `twilightforest:tree/selector/dark_forest_trees`: type `minecraft:random_selector` {default={feature=twilightforest:tree/darkwood_tree, placement=[]}, features=[{chance=0.2, feature={feature=twilightforest:tree/dark_forest_birch_tree, placement=[]}}, {chance=0.2, feature={feature=twilightforest:tree/dark_forest_oak_tree, placement=[]}}, {chance=0.4, feature={feature=twilightforest:tree/dark_oak_bush, placement=[]}}]}
- `twilightforest:tree/selector/dense_canopy_trees`: type `minecraft:random_selector` {default={feature=twilightforest:tree/twilight_oak_tree, placement=[]}, features=[{chance=0.7, feature={feature=twilightforest:tree/canopy_tree, placement=[]}}]}
- `twilightforest:tree/selector/enchanted_forest_trees`: type `minecraft:random_selector` {default={feature=twilightforest:tree/rainbow_oak, placement=[]}, features=[{chance=0.15, feature={feature=twilightforest:tree/vanilla_oak_tree, placement=[]}}, {chance=0.15, feature={feature=twilightforest:tree/vanilla_birch_tree, placement=[]}}, {chance=0.15, feature={feature=twilightforest:tree/large_rainbow_oak, placement=[]}}]}
- `twilightforest:tree/selector/firefly_forest_trees`: type `minecraft:random_selector` {default={feature=twilightforest:tree/twilight_oak_tree, placement=[]}, features=[{chance=0.33, feature={feature=twilightforest:tree/canopy_tree, placement=[]}}, {chance=0.45, feature={feature=twilightforest:tree/firefly_canopy_tree, placement=[]}}]}
- `twilightforest:tree/selector/highlands_trees`: type `minecraft:random_selector` {default={feature=twilightforest:tree/mega_spruce_tree, placement=[]}, features=[{chance=0.25, feature={feature=twilightforest:tree/vanilla_birch_tree, placement=[]}}, {chance=0.25, feature={feature=spruce, placement=[]}}, {chance=0.1, feature={feature=pine, placement=[]}}]}
- `twilightforest:tree/selector/snowy_forest_trees`: type `minecraft:random_selector` {default={feature=twilightforest:tree/snowy_spruce_tree, placement=[]}, features=[{chance=0.33, feature={feature=twilightforest:tree/mega_spruce_tree, placement=[]}}, {chance=0.125, feature={feature=twilightforest:tree/large_winter_tree, placement=[]}}]}
- `twilightforest:tree/selector/vanilla_trees`: type `minecraft:random_selector` {default={feature=twilightforest:tree/twilight_oak_tree, placement=[]}, features=[{chance=0.25, feature={feature=twilightforest:tree/vanilla_birch_tree, placement=[]}}, {chance=0.25, feature={feature=twilightforest:tree/vanilla_oak_tree, placement=[]}}]}
- `twilightforest:tree/selector/vanilla/vanilla_mushrooms`: type `minecraft:random_boolean_selector` {feature_false={feature=huge_brown_mushroom, placement=[]}, feature_true={feature=huge_red_mushroom, placement=[]}}
- `twilightforest:tree/smaller_jungle_tree`: type `minecraft:tree` {decorators=[cocoa{probability=0.7}, trunk_vine, leave_vine{probability=0.25}], dirt_provider=dirt, foliage_placer=blob_foliage_placer{height=3, offset=0, radius=2}, foliage_provider=jungle_leaves[distance=7,persistent=false,waterlogged=false], force_dirt=False, ignore_vines=True, minimum_size=two_layers_feature_size{limit=1, lower_size=0, upper_size=1}, trunk_placer=straight_trunk_placer{base_height=3, height_rand_a=2, height_rand_b=2}, trunk_provider=jungle_log[axis=y]}
- `twilightforest:tree/snowy_spruce_tree`: type `twilightforest:anywhere_tree` {decorators=[], dirt_provider=dirt, foliage_placer=spruce_foliage_placer{offset=uniform(0..2), radius=uniform(2..3), trunk_height=uniform(1..2)}, foliage_provider=spruce_leaves[distance=7,persistent=false,waterlogged=false], force_dirt=False, ignore_vines=True, minimum_size=two_layers_feature_size{limit=2, lower_size=0, upper_size=2}, trunk_placer=straight_trunk_placer{base_height=5, height_rand_a=2, height_rand_b=1}, trunk_provider=spruce_log[axis=y]}
- `twilightforest:tree/sorting_tree`: type `minecraft:tree` {decorators=[twilightforest:core_placer{core_position=2, deco_provider=twilightforest:sorting_log_core[active=false,axis=y]}], dirt_provider=dirt, foliage_placer=twilightforest:spheroid_foliage_placer{horizontal_radius=1.5, offset=0, random_add_horizontal=1, random_add_vertical=0, shag_factor=0, vertical_filler_bias=0.5, vertical_radius=2.25}, foliage_provider=twilightforest:sorting_leaves[distance=7,persistent=false,waterlogged=false], force_dirt=True, ignore_vines=True, minimum_size=two_layers_feature_size{limit=1, lower_size=1, upper_size=1}, trunk_placer=straight_trunk_placer{base_height=3, height_rand_a=0, height_rand_b=0}, trunk_provider=twilightforest:sorting_log[axis=y]}
- `twilightforest:tree/swampy_oak_tree`: type `minecraft:tree` {decorators=[twilightforest:tree_roots{additional_random_strands=1, base_strand_count=3, ground_roots_provider=weighted{twilightforest:root:6, twilightforest:liveroot_block:1}, rootPenetrability=1, root_length=5, y_offset=0}, leave_vine{probability=0.125}], dirt_provider=dirt, foliage_placer=blob_foliage_placer{height=3, offset=0, radius=2}, foliage_provider=twilightforest:twilight_oak_leaves[distance=7,persistent=false,waterlogged=false], force_dirt=False, ignore_vines=False, minimum_size=two_layers_feature_size{limit=1, lower_size=0, upper_size=1}, trunk_placer=straight_trunk_placer{base_height=4, height_rand_a=2, height_rand_b=0}, trunk_provider=twilightforest:twilight_oak_log[axis=y]}
- `twilightforest:tree/time_tree`: type `twilightforest:tree_of_time` {add_first_five_chance=1, add_second_five_chance=1, branch_provider=twilightforest:time_wood[axis=y], check_water=False, decorators=[], has_leaves=False, leaves_provider=twilightforest:time_leaves[distance=7,persistent=false,waterlogged=false], minimum_size=0, roots_provider=twilightforest:root, trunk_provider=twilightforest:time_log[axis=y]}
- `twilightforest:tree/transformation_tree`: type `minecraft:tree` {decorators=[twilightforest:core_placer{core_position=3, deco_provider=twilightforest:transformation_log_core[active=false,axis=y]}], dirt_provider=dirt, foliage_placer=twilightforest:spheroid_foliage_placer{horizontal_radius=4.5, offset=0, random_add_horizontal=1, random_add_vertical=0, shag_factor=0, vertical_filler_bias=-0.25, vertical_radius=1.5}, foliage_provider=twilightforest:transformation_leaves[distance=7,persistent=false,waterlogged=false], force_dirt=False, ignore_vines=True, minimum_size=two_layers_feature_size{limit=4, lower_size=1, upper_size=5}, trunk_placer=twilightforest:branching_trunk_placer{base_height=6, branch_config={branch_provider=twilightforest:transformation_wood[axis=y], count_minimum=3, downwards_pitch=0.2, length=10, random_add_count=1, random_add_length=1, spacing_yaw=0.3}, branch_start_offset_down=7, height_rand_a=5, height_rand_b=5, perpendicular_branches=False, prevent_exposed_root=False}, trunk_provider=twilightforest:transformation_log[axis=y]}
- `twilightforest:tree/twilight_oak_tree`: type `minecraft:tree` {decorators=[twilightforest:tree_roots{additional_random_strands=1, base_strand_count=3, ground_roots_provider=weighted{twilightforest:root:6, twilightforest:liveroot_block:1}, rootPenetrability=1, root_length=5, y_offset=0}], dirt_provider=dirt, foliage_placer=blob_foliage_placer{height=3, offset=0, radius=2}, foliage_provider=twilightforest:twilight_oak_leaves[distance=7,persistent=false,waterlogged=false], force_dirt=False, ignore_vines=False, minimum_size=two_layers_feature_size{limit=1, lower_size=0, upper_size=1}, trunk_placer=straight_trunk_placer{base_height=4, height_rand_a=2, height_rand_b=0}, trunk_provider=twilightforest:twilight_oak_log[axis=y]}
- `twilightforest:tree/vanilla_birch_tree`: type `minecraft:tree` {decorators=[], dirt_provider=dirt, foliage_placer=blob_foliage_placer{height=3, offset=0, radius=2}, foliage_provider=birch_leaves[distance=7,persistent=false,waterlogged=false], force_dirt=False, ignore_vines=True, minimum_size=two_layers_feature_size{limit=1, lower_size=0, upper_size=1}, trunk_placer=straight_trunk_placer{base_height=5, height_rand_a=2, height_rand_b=0}, trunk_provider=birch_log[axis=y]}
- `twilightforest:tree/vanilla_oak_tree`: type `minecraft:tree` {decorators=[], dirt_provider=dirt, foliage_placer=blob_foliage_placer{height=3, offset=0, radius=2}, foliage_provider=oak_leaves[distance=7,persistent=false,waterlogged=false], force_dirt=False, ignore_vines=True, minimum_size=two_layers_feature_size{limit=1, lower_size=0, upper_size=1}, trunk_placer=straight_trunk_placer{base_height=4, height_rand_a=2, height_rand_b=0}, trunk_provider=oak_log[axis=y]}
- `twilightforest:troll_huge_brown_mushrooms`: type `twilightforest:troll_cave_huge_brown_mushroom` {cap_provider=brown_mushroom_block[down=false,east=true,north=true,south=true,up=true,west=true], foliage_radius=3, stem_provider=mushroom_stem[down=false,east=true,north=true,south=true,up=false,west=true]}
- `twilightforest:troll_huge_red_mushrooms`: type `twilightforest:troll_cave_huge_red_mushroom` {cap_provider=red_mushroom_block[down=false,east=true,north=true,south=true,up=true,west=true], foliage_radius=2, stem_provider=mushroom_stem[down=false,east=true,north=true,south=true,up=false,west=true]}

### Placed features (28)

- `twilightforest:canopy_fallen_log`: feature `twilightforest:canopy_fallen_log`; placement: rarity 1/40 > in_square > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > biome
- `twilightforest:dark_brown_mushrooms`: feature `twilightforest:dark_brown_mushrooms`; placement: rarity 1/3 > in_square > heightmap WORLD_SURFACE_WG > biome
- `twilightforest:dark_red_mushrooms`: feature `twilightforest:dark_red_mushrooms`; placement: rarity 1/3 > in_square > heightmap WORLD_SURFACE_WG > biome
- `twilightforest:mushroom/canopy_mushrooms_dense`: feature `twilightforest:mushroom/canopy_mushrooms_dense`; placement: count weighted_list{distribution=[{data=5, weight=9}, {data=6, weight=1}]} > in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > block_predicate_filter{predicate=would_survive{state=twilightforest:canopy_sapling[stage=0]}} > biome
- `twilightforest:mushroom/canopy_mushrooms_sparse`: feature `twilightforest:mushroom/canopy_mushrooms_sparse`; placement: count weighted_list{distribution=[{data=3, weight=9}, {data=4, weight=1}]} > in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > block_predicate_filter{predicate=would_survive{state=twilightforest:canopy_sapling[stage=0]}} > biome
- `twilightforest:snow_under_trees`: feature `twilightforest:snow_under_trees`; placement: heightmap WORLD_SURFACE_WG > biome
- `twilightforest:tree/darkwood_tree`: feature `twilightforest:tree/darkwood_tree`; placement: count weighted_list{distribution=[{data=5, weight=9}, {data=6, weight=1}]} > in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=16, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > block_predicate_filter{predicate=would_survive{state=twilightforest:darkwood_sapling[stage=0]}} > biome
- `twilightforest:tree/dead_canopy_tree`: feature `twilightforest:tree/dead_canopy_tree`; placement: count weighted_list{distribution=[{data=2, weight=9}, {data=3, weight=1}]} > in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=twilightforest:lich_tower} > block_predicate_filter{predicate=would_survive{state=twilightforest:canopy_sapling[stage=0]}} > biome
- `twilightforest:tree/forest_mega_oak_tree`: feature `twilightforest:tree/forest_mega_oak_tree`; placement: count weighted_list{distribution=[{data=7, weight=9}, {data=8, weight=1}]} > in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > block_predicate_filter{predicate=would_survive{state=twilightforest:twilight_oak_sapling[stage=0]}} > biome
- `twilightforest:tree/large_twilight_oak_tree`: feature `twilightforest:tree/large_twilight_oak_tree`; placement: count weighted_list{distribution=[{data=1, weight=9}, {data=2, weight=1}]} > in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=twilightforest:lich_tower} > block_predicate_filter{predicate=would_survive{state=twilightforest:twilight_oak_sapling[stage=0]}} > biome
- `twilightforest:tree/mangrove_tree`: feature `twilightforest:tree/mangrove_tree`; placement: count weighted_list{distribution=[{data=3, weight=9}, {data=4, weight=1}]} > in_square > surface_water_depth_filter{max_water_depth=6} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > block_predicate_filter{predicate=would_survive{state=twilightforest:mangrove_sapling[stage=0,waterlogged=false]}} > biome > heightmap WORLD_SURFACE_WG
- `twilightforest:tree/mega_canopy_tree`: feature `twilightforest:tree/mega_canopy_tree`; placement: twilightforest:chunk_centerer > rarity 1/20 > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > block_predicate_filter{predicate=would_survive{state=twilightforest:canopy_sapling[stage=0]}} > biome
- `twilightforest:tree/oak_bush`: feature `twilightforest:tree/oak_bush`; placement: count weighted_list{distribution=[{data=1, weight=2}, {data=2, weight=1}]} > in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > block_predicate_filter{predicate=would_survive{state=oak_sapling[stage=0]}} > biome
- `twilightforest:tree/oak_bush_dense`: feature `twilightforest:tree/oak_bush`; placement: count weighted_list{distribution=[{data=2, weight=2}, {data=4, weight=1}]} > in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > block_predicate_filter{predicate=would_survive{state=oak_sapling[stage=0]}} > biome
- `twilightforest:tree/savannah_mega_oak_tree`: feature `twilightforest:tree/savannah_mega_oak_tree`; placement: count weighted_list{distribution=[{data=0, weight=9}, {data=1, weight=1}]} > in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > block_predicate_filter{predicate=would_survive{state=twilightforest:twilight_oak_sapling[stage=0]}} > biome
- `twilightforest:tree/savannah_oak_tree`: feature `twilightforest:tree/twilight_oak_tree`; placement: count weighted_list{distribution=[{data=1, weight=9}, {data=2, weight=1}]} > in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=twilightforest:lich_tower} > block_predicate_filter{predicate=would_survive{state=twilightforest:twilight_oak_sapling[stage=0]}} > biome
- `twilightforest:tree/selector/canopy_trees`: feature `twilightforest:tree/selector/canopy_trees`; placement: count weighted_list{distribution=[{data=7, weight=9}, {data=8, weight=1}]} > in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=twilightforest:lich_tower} > block_predicate_filter{predicate=would_survive{state=twilightforest:canopy_sapling[stage=0]}} > biome
- `twilightforest:tree/selector/dark_forest_tree_mix`: feature `twilightforest:tree/selector/dark_forest_trees`; placement: count weighted_list{distribution=[{data=3, weight=9}, {data=4, weight=1}]} > in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=10, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > biome
- `twilightforest:tree/selector/dark_forest_trees`: feature `twilightforest:tree/darkwood_tree`; placement: count weighted_list{distribution=[{data=8, weight=9}, {data=9, weight=1}]} > in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=10, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > biome
- `twilightforest:tree/selector/dense_canopy_trees`: feature `twilightforest:tree/selector/dense_canopy_trees`; placement: count weighted_list{distribution=[{data=5, weight=9}, {data=6, weight=1}]} > in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=twilightforest:lich_tower} > block_predicate_filter{predicate=would_survive{state=twilightforest:canopy_sapling[stage=0]}} > biome
- `twilightforest:tree/selector/enchanted_forest_trees`: feature `twilightforest:tree/selector/enchanted_forest_trees`; placement: count weighted_list{distribution=[{data=5, weight=9}, {data=6, weight=1}]} > in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > block_predicate_filter{predicate=would_survive{state=twilightforest:rainbow_oak_sapling[stage=0]}} > biome
- `twilightforest:tree/selector/firefly_forest_trees`: feature `twilightforest:tree/selector/firefly_forest_trees`; placement: count weighted_list{distribution=[{data=3, weight=9}, {data=4, weight=1}]} > in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=twilightforest:lich_tower} > block_predicate_filter{predicate=would_survive{state=twilightforest:canopy_sapling[stage=0]}} > biome
- `twilightforest:tree/selector/highlands_trees`: feature `twilightforest:tree/selector/highlands_trees`; placement: count weighted_list{distribution=[{data=3, weight=9}, {data=4, weight=1}]} > in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > block_predicate_filter{predicate=would_survive{state=spruce_sapling[stage=0]}} > biome
- `twilightforest:tree/selector/snowy_forest_trees`: feature `twilightforest:tree/selector/snowy_forest_trees`; placement: count weighted_list{distribution=[{data=10, weight=9}, {data=11, weight=1}]} > in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > environment_scan{direction_of_search=up, max_steps=8, target_condition=not{predicate=matching_blocks{blocks=powder_snow}}} > block_predicate_filter{predicate=matching_blocks{blocks=[snow_block, powder_snow], offset=[0, -1, 0]}} > biome
- `twilightforest:tree/selector/vanilla_mushrooms`: feature `twilightforest:tree/selector/vanilla/vanilla_mushrooms`; placement: in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > block_predicate_filter{predicate=would_survive{state=twilightforest:canopy_sapling[stage=0]}} > biome
- `twilightforest:tree/selector/vanilla_trees`: feature `twilightforest:tree/selector/vanilla_trees`; placement: in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=twilightforest:lich_tower} > block_predicate_filter{predicate=would_survive{state=twilightforest:twilight_oak_sapling[stage=0]}} > biome
- `twilightforest:tree/swampy_oak_tree`: feature `twilightforest:tree/swampy_oak_tree`; placement: count weighted_list{distribution=[{data=4, weight=9}, {data=5, weight=1}]} > in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > block_predicate_filter{predicate=would_survive{state=twilightforest:twilight_oak_sapling[stage=0]}} > biome
- `twilightforest:tree/twilight_oak_tree`: feature `twilightforest:tree/twilight_oak_tree`; placement: count weighted_list{distribution=[{data=1, weight=9}, {data=2, weight=1}]} > in_square > surface_water_depth_filter{max_water_depth=0} > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=twilightforest:lich_tower} > block_predicate_filter{predicate=would_survive{state=twilightforest:twilight_oak_sapling[stage=0]}} > biome

### Loot tables (35)

- `twilightforest:blocks/canopy_leaves` (block)
  - pool 1: rolls 1: alternatives(twilightforest:canopy_leaves [ANY(tool can shears_dig | silk touch)] ; twilightforest:canopy_sapling [survives explosion & fortune table [0.05, 0.0625, 0.0833, 0.1]])
  - pool 2: rolls 1 if NOT(ANY(tool can shears_dig | silk touch)): stick x1-2, explosion decay [fortune table [0.02, 0.0222, 0.025, 0.0333, 0.1]]
- `twilightforest:blocks/canopy_sapling` (block)
  - pool 1: rolls 1 if survives explosion: twilightforest:canopy_sapling
- `twilightforest:blocks/dark_leaves` (block)
  - pool 1: rolls 1: alternatives(twilightforest:dark_leaves [ANY(tool can shears_dig | silk touch)] ; twilightforest:darkwood_sapling [survives explosion & fortune table [0.025, 0.0278, 0.0312, 0.0417, 0.1]])
  - pool 2: rolls 1 if NOT(ANY(tool can shears_dig | silk touch)): stick x1-2, explosion decay [fortune table [0.02, 0.0222, 0.025, 0.0333, 0.1]]
- `twilightforest:blocks/darkwood_sapling` (block)
  - pool 1: rolls 1 if survives explosion: twilightforest:darkwood_sapling
- `twilightforest:blocks/hardened_dark_leaves` (block)
  - pool 1: rolls 1: alternatives(twilightforest:dark_leaves [ANY(tool can shears_dig | silk touch)] ; twilightforest:darkwood_sapling [survives explosion & fortune table [0.025, 0.0278, 0.0312, 0.0417, 0.1]])
  - pool 2: rolls 1 if NOT(ANY(tool can shears_dig | silk touch)): stick x1-2, explosion decay [fortune table [0.02, 0.0222, 0.025, 0.0333, 0.1]]
- `twilightforest:blocks/hollow_oak_sapling` (block)
  - pool 1: rolls 1 if survives explosion: twilightforest:hollow_oak_sapling
- `twilightforest:blocks/mangrove_leaves` (block)
  - pool 1: rolls 1: alternatives(twilightforest:mangrove_leaves [ANY(tool can shears_dig | silk touch)] ; twilightforest:mangrove_sapling [survives explosion & fortune table [0.05, 0.0625, 0.0833, 0.1]])
  - pool 2: rolls 1 if NOT(ANY(tool can shears_dig | silk touch)): stick x1-2, explosion decay [fortune table [0.02, 0.0222, 0.025, 0.0333, 0.1]]
- `twilightforest:blocks/mangrove_root` (block)
  - pool 1: rolls 1: alternatives(twilightforest:mangrove_root [silk touch] ; stick x3-5, explosion decay)
- `twilightforest:blocks/mangrove_sapling` (block)
  - pool 1: rolls 1 if survives explosion: twilightforest:mangrove_sapling
- `twilightforest:blocks/mining_leaves` (block)
  - pool 1: rolls 1: alternatives(twilightforest:mining_leaves [ANY(tool can shears_dig | silk touch)] ; stick x1-2, explosion decay [fortune table [0.02, 0.0222, 0.025, 0.0333, 0.1]])
- `twilightforest:blocks/mining_log_core` (block)
  - pool 1: rolls 1 if survives explosion: twilightforest:mining_log
- `twilightforest:blocks/mining_sapling` (block)
  - pool 1: rolls 1 if survives explosion: twilightforest:mining_sapling
- `twilightforest:blocks/potted_canopy_sapling` (block)
  - pool 1: rolls 1 if survives explosion: flower_pot
  - pool 2: rolls 1 if survives explosion: twilightforest:canopy_sapling
- `twilightforest:blocks/potted_darkwood_sapling` (block)
  - pool 1: rolls 1 if survives explosion: flower_pot
  - pool 2: rolls 1 if survives explosion: twilightforest:darkwood_sapling
- `twilightforest:blocks/potted_hollow_oak_sapling` (block)
  - pool 1: rolls 1 if survives explosion: flower_pot
  - pool 2: rolls 1 if survives explosion: twilightforest:hollow_oak_sapling
- `twilightforest:blocks/potted_mangrove_sapling` (block)
  - pool 1: rolls 1 if survives explosion: flower_pot
  - pool 2: rolls 1 if survives explosion: twilightforest:mangrove_sapling
- `twilightforest:blocks/potted_mining_sapling` (block)
  - pool 1: rolls 1 if survives explosion: flower_pot
  - pool 2: rolls 1 if survives explosion: twilightforest:mining_sapling
- `twilightforest:blocks/potted_rainbow_oak_sapling` (block)
  - pool 1: rolls 1 if survives explosion: flower_pot
  - pool 2: rolls 1 if survives explosion: twilightforest:rainbow_oak_sapling
- `twilightforest:blocks/potted_sorting_sapling` (block)
  - pool 1: rolls 1 if survives explosion: flower_pot
  - pool 2: rolls 1 if survives explosion: twilightforest:sorting_sapling
- `twilightforest:blocks/potted_time_sapling` (block)
  - pool 1: rolls 1 if survives explosion: flower_pot
  - pool 2: rolls 1 if survives explosion: twilightforest:time_sapling
- `twilightforest:blocks/potted_transformation_sapling` (block)
  - pool 1: rolls 1 if survives explosion: flower_pot
  - pool 2: rolls 1 if survives explosion: twilightforest:transformation_sapling
- `twilightforest:blocks/potted_twilight_oak_sapling` (block)
  - pool 1: rolls 1 if survives explosion: flower_pot
  - pool 2: rolls 1 if survives explosion: twilightforest:twilight_oak_sapling
- `twilightforest:blocks/rainbow_oak_leaves` (block)
  - pool 1: rolls 1: alternatives(twilightforest:rainbow_oak_leaves [ANY(tool can shears_dig | silk touch)] ; twilightforest:rainbow_oak_sapling [survives explosion & fortune table [0.025, 0.0278, 0.0312, 0.0417, 0.1]])
  - pool 2: rolls 1 if NOT(ANY(tool can shears_dig | silk touch)): stick x1-2, explosion decay [fortune table [0.02, 0.0222, 0.025, 0.0333, 0.1]]
- `twilightforest:blocks/rainbow_oak_sapling` (block)
  - pool 1: rolls 1 if survives explosion: twilightforest:rainbow_oak_sapling
- `twilightforest:blocks/sorting_leaves` (block)
  - pool 1: rolls 1: alternatives(twilightforest:sorting_leaves [ANY(tool can shears_dig | silk touch)] ; stick x1-2, explosion decay [fortune table [0.02, 0.0222, 0.025, 0.0333, 0.1]])
- `twilightforest:blocks/sorting_log_core` (block)
  - pool 1: rolls 1 if survives explosion: twilightforest:sorting_log
- `twilightforest:blocks/sorting_sapling` (block)
  - pool 1: rolls 1 if survives explosion: twilightforest:sorting_sapling
- `twilightforest:blocks/time_leaves` (block)
  - pool 1: rolls 1: alternatives(twilightforest:time_leaves [ANY(tool can shears_dig | silk touch)] ; stick x1-2, explosion decay [fortune table [0.02, 0.0222, 0.025, 0.0333, 0.1]])
- `twilightforest:blocks/time_log_core` (block)
  - pool 1: rolls 1 if survives explosion: twilightforest:time_log
- `twilightforest:blocks/time_sapling` (block)
  - pool 1: rolls 1 if survives explosion: twilightforest:time_sapling
- `twilightforest:blocks/transformation_leaves` (block)
  - pool 1: rolls 1: alternatives(twilightforest:transformation_leaves [ANY(tool can shears_dig | silk touch)] ; stick x1-2, explosion decay [fortune table [0.02, 0.0222, 0.025, 0.0333, 0.1]])
- `twilightforest:blocks/transformation_log_core` (block)
  - pool 1: rolls 1 if survives explosion: twilightforest:transformation_log
- `twilightforest:blocks/transformation_sapling` (block)
  - pool 1: rolls 1 if survives explosion: twilightforest:transformation_sapling
- `twilightforest:blocks/twilight_oak_leaves` (block)
  - pool 1: rolls 1: alternatives(twilightforest:twilight_oak_leaves [ANY(tool can shears_dig | silk touch)] ; twilightforest:twilight_oak_sapling [survives explosion & fortune table [0.05, 0.0625, 0.0833, 0.1]])
  - pool 2: rolls 1 if NOT(ANY(tool can shears_dig | silk touch)): stick x1-2, explosion decay [fortune table [0.02, 0.0222, 0.025, 0.0333, 0.1]]
- `twilightforest:blocks/twilight_oak_sapling` (block)
  - pool 1: rolls 1 if survives explosion: twilightforest:twilight_oak_sapling
