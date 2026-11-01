# U19 `questcamps`: Quest grove, camps and template features

Behaviour spec translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ `9e40a757`). Checklist for the
U19 worker and the W2 audit (A1). Numbers are TF's; "Bedrock:" paragraphs mark adaptation notes. The appendix lists
the owned features and loot tables in compact form.

| | |
|---|---|
| Unit | U19 `questcamps` (W1 batch 2) |
| Selftests | `questcamps.*`, testPos dx `1900-1999` (owners rows `selftest questcamps.*`, `selftest_dx 1900-1999`) |
| World DB | `gf:questcamps:*` |
| Net channels | `questcamps.*` |
| Config | section `questcamps` (config_section `questcamps`): `client.questRamWoolIndicator` (client, default `true`, translation `ram_indicator`) |
| Contracts | landmark (F6), structure + template + marker handlers (F7), features (F5), entity (F8), loot (F10), progression (F10 trigger bus), client.ui (F12 HUD layer) |
| Java sources | `entity/passive/QuestRam.java`, `entity/ai/goal/QuestRamEatWoolGoal.java`, `entity/passive/quest/**`, `client/overlay/QuestingRamIndicatorOverlay.java`, `client/renderer/entity/QuestRamRenderer.java`, `world/components/structures/QuestGrove.java`, `world/components/structures/type/{QuestGroveStructure,CampStructure}.java`, `world/components/structures/camp/CampPieces.java`, `world/components/structures/markerhandler/*.java` (F7), `world/components/feature/templates/{TemplateFeature,DruidHutFeature,SimpleWellFeature,FancyWellFeature,GraveyardFeature,GroveRuinsFeature,StoneCircleFeature}.java`, `util/features/{FeatureLogic,FeaturePlacers}.java`, `world/components/structures/TwilightTemplateStructurePiece.java` (`placePieceAdjusted`), `world/components/structures/util/{LandmarkStructure,DecorationClearance}.java` (generation point, `adjustForTerrain`), `advancements/QuestRamCompletionTrigger.java` |
| Data | `twilight/quests/questing_ram.json`, `twilight/template_definition/camp/**` (35), `twilight/template_marker_handler_list/camp_marker_handlers.json` (F7), `worldgen/structure/{quest_grove,camp}.json`, `worldgen/structure_set/{quest_grove,camp}.json`, loot tables below |

Not owned here: the drying racks, jerkies, tannin, tanned leather (U4/U10), wraith, skeleton druid, rising zombie
(U17/U21), the quest ram trophy and crumble horn (U5/U8), the marker-handler types and wood palettes (F7/F9), the
advancement `twilightforest:quest_ram` that listens to the trigger (U14).

## 1. Entity `twilightforest:quest_ram` (Questing Ram)

CREATURE 1.25 x 2.9, tracking 80, interval 3; egg `twilightforest:quest_ram_spawn_egg` (`0xfefeee` / `0x33aadd`,
"Questing Ram Spawn Egg"). Placement ON_GROUND / MOTION_BLOCKING_NO_LEAVES / `Animal.checkAnimalSpawnRules`.
Attributes: max health 70, speed 0.23 (mob defaults otherwise). Not breedable (no food, no offspring), cannot ride.
Home: restriction point synced (`HOME_POINT`), home radius 13 (set where it is placed by the grove).

Goals: Float 0; Panic(1.38) 1; QuestRamEatWoolGoal 2; Tempt(1.0, quest items it still needs, not scared by movement)
3; restriction goals; WaterAvoidingRandomStroll(1.0) 5; RandomLookAround 6.

Quest (`twilight/quests/questing_ram.json`, registry `quests`, entry `quests/twilightforest:questing_ram`): items map
dye colour -> ingredient (the 16 `minecraft:<colour>_wool`), reward table
`twilightforest:entities/questing_ram_rewards`.
- Synced `DATA_COLOR` (16-bit flag set, saved `ColorFlags`) and `DATA_REWARDED` (saved `Rewarded`).
- Feeding: interacting with a quest item of a missing colour consumes one, sets the flag, shows 50 ENTITY_EFFECT
  particles in that colour around the ram (to players within 64) and plays the ambient sound.
- `QuestRamEatWoolGoal` (MOVE, LOOK): the nearest tempting item entity within 16 (on ground or in water, in sight):
  path to it at 1.0; within 2.5 blocks (distSq < 6.25) accept it, discard the item, game event EAT.
- Every `70 + nextInt(50)` ticks: with all 16 colours and not yet rewarded, roll the reward table (PIGLIN_BARTER
  params, THIS_ENTITY) and drop the items, fire the trigger `twilightforest:complete_quest_ram` for every server player
  within 16, mark the `twilightforest:quest_grove` landmark conquered, set rewarded. While complete and unrewarded each
  tick shows 5 particles of a random colour and plays the ambient sound.
- Voice pitch `(r1 - r2) * 0.2 + 0.7`; step sound (0.15, 1.0).
- Loot `twilightforest:entities/quest_ram`: empty. Rewards (`twilightforest:entities/questing_ram_rewards`):
  `twilightforest:crumble_horn` + a bundle containing `twilightforest:quest_ram_trophy` and one each of coal, iron,
  copper, lapis, gold, diamond and emerald blocks (`twilightforest:entities/questing_ram_reward_blocks`).

| Event | Subtitle |
|---|---|
| `entity.twilightforest.quest_ram.ambient` | "Questing Ram bleats" |
| `entity.twilightforest.quest_ram.hurt` | "Questing Ram hurts" |
| `entity.twilightforest.quest_ram.death` | "Questing Ram dies" |
| `entity.twilightforest.quest_ram.step` | generic footsteps |

Rendering (client mod): texture `questram.png`; the model has 16 wool segments, each visible only when its colour is
collected and tinted with the sheep colour of that dye; a glowing-lines layer (`questram_lines.png`, translucent,
full bright, scaled 1.025). Vanilla clients: drive the 16 segment visibilities from one packed int actor property.

Crosshair indicator (client mod, `questRamWoolIndicator`): in first person, not hidden GUI, looking at a quest ram and
holding a quest item: draw sprite `twilightforest:questing_ram_x` when that colour is still missing, else
`twilightforest:questing_ram_check`, 7 x 7, at `x = (guiHeight - 1) / 2 - 3`, `y = (guiHeight - 1) / 2 - 11`
(TF uses the GUI height for both coordinates), with inverted blending. Registered as an F12 HUD layer.

Trigger `twilightforest:complete_quest_ram` (no conditions besides the player).

## 2. Quest grove `twilightforest:quest_grove`

Structure type `twilightforest:quest_grove` (`QuestGroveStructure`, conquerable), piece `twilightforest:TFQuest1`
(`QuestGrove`, a template piece). Structure set `twilightforest:quest_grove` (landmark grid). Biomes
`#twilightforest:valid_quest_grove_biomes`; surface; BEARD_THIN; decoration clearance radius 2, no surface decorations,
underground and vegetation allowed, elevation adjusted; map icon `twilightforest:quest_grove`; centre in chunk.
- Generation point (`LandmarkStructure.findGenerationPoint`, centre in chunk): `x = chunkMinX + 7`, `z = chunkMinZ +
  7`. `y = adjustForTerrain(x, z) = clamp(getFirstOccupiedHeight(x, z, WORLD_SURFACE_WG), seaLevel + 1, seaLevel + 7)`,
  which is the y of the top non-air block clamped to `1..7` (TF sea level 0; `adjust_structure_elevation` true).
- Piece: `QuestGroveStructure.getFirstPiece` creates the template piece `twilightforest:quest_grove` with
  `templatePosition = (x - LENGTH / 2 + 1, y + 2, z - LENGTH / 2 + 1) = (x - 12, y + 2, z - 12)` (`LENGTH` 27; Java
  int division 27 / 2 = 13).
  - Rotation NONE, mirror NONE.
  - Settings `makeSettings(NONE)`: `BlockIgnoreProcessor.STRUCTURE_BLOCK`, then `TargetedRotProcessor({mossy_stone_bricks},
    0.5f)`, then `StoneBricksVariants`. Known shape; flag UPDATE_CLIENTS. The rot processor is vanilla `BlockRotProcessor`
    limited to mossy stone bricks: such a block is not placed when `settings.getRandom(pos).nextFloat() > 0.5f`. The
    settings have no random, so that is `RandomSource.create(Mth.getSeed(pos))`, seeded per position.
  - A piece deserialised from a save rebuilds its settings as `readSettings(tag)` + `StoneBricksVariants` only, so the
    rot processor is dropped for chunks generated after a reload. This is a TF quirk; keep it.
- Vertical placement: `postProcess` calls `placePieceAdjusted(-2)`, so the template is placed at
  `templatePosition.above(-2) = (x - 12, y, z - 12)`. The stored piece box keeps `minY = y + 2`. Net offset: template
  layer 0 is at the generation y, 2 below the stored box.
- Ground line: BEARD_THIN shapes the terrain around the stored box toward its ground level `box.minY = y + 2`. The
  vanilla beard adds density below `minY` and removes it from `minY` up, within the 24-block kernel. The expected
  surface (top solid block) is therefore about `y + 1`:
  - template layers 0 (`y`) and 1 (`y + 1`) are ground layers (foundation and floor, flush with the terrain);
  - layer 2 (`y + 2`) is the first open-air layer.
  The beard is a smooth density bias, so the edges may deviate by a block.
- Footprint: 27 x 27, template x/z 0..26 = world `x - 12 .. x + 14`. The grove centre (template x/z 13) is at
  `(chunkMinX + 8, chunkMinZ + 8)`, the chunk centre. The height is free: no code reads it, and BEARD_THIN uses only
  the box's `minY`.
- Data markers are handled only inside the chunk being generated (`chunkBounds.isInside(pos)`):
  - `quest_ram`: `FeaturePlacers.placeEntity`. It spawns a quest ram at the block centre (`moveTo(pos)` = x + 0.5, y, z
    + 0.5) with rotation 0, sets it persistent, calls `finalizeMobSpawn(STRUCTURE)`, sets its home restriction point to
    the marker position (GlobalPos), adds it, and sets the marker block to air.
  - `dispenser`: a `minecraft:dropper` facing `rotation.rotate(NORTH)` (= NORTH), flags `16 | 4 | 2`, filled from
    `twilightforest:quest_grove_dropper` (4 random wool blocks) with loot seed `random.nextLong()`.
- Template re-design (R11): a 27 x 27 grove (mossy stone brick ring and pillars) with free height and layers 0-1 at
  ground level, as above. It has exactly one `quest_ram` marker, where the ram should stand: an air block above the
  floor, since the marker becomes air. The droppers are marked `dispenser`: each marker becomes a dropper facing north,
  so put it where a north-facing dropper reads well. A marker may sit anywhere inside the 27 x 27 footprint: each
  marker is processed once, when the chunk that contains it generates.

## 3. Camps `twilightforest:camp`

Structure type `twilightforest:camp` (`CampStructure`, not a landmark, `DecorationClearance`: radius 1, no surface
decorations, underground and vegetation allowed, elevation not adjusted). Structure set `twilightforest:camp`:
`twilightforest:avoid_landmark_grid`, spacing 18, separation 14, salt 701432212, triangular, avoiding
`twilightforest:fallen_trunk` (2 chunks) and `twilightforest:hollow_tree` (1). Biomes `#twilightforest:valid_camp_biomes`;
surface; BEARD_BOX; no spawn overrides.
- Generation point: random x/z inside the chunk (`lerpDiscrete(rand)`), y `adjustForTerrain(12, 3)`, random rotation
  of SOUTH; start template from pool `twilightforest:camp/structure_start` joined at a junction named after the pool;
  then the generic jigsaw expansion of `TwilightJigsawPiece` following each template's jigsaw `pool` / `name` /
  `target`. Its piece type is `twilightforest:TFJigsawTemplate`; owner F7 is proposed, pending an owners.tsv fix by
  F0/A8 ([issue #4](https://github.com/MiracleForest/GloamingForest/issues/4)).
- Pools (from `CampPieces`): `twilightforest:camp/structure_start`, `camp/tent`, `camp/main_path`, `camp/path`,
  `camp/rack_path` (aliases deco -> rack), `camp/deco`, `camp/rack`.
- Marker handlers `twilightforest:camp_marker_handlers` (rotation-aware switch): `birch_drying_rack` (birch drying rack
  with `twilightforest:camp_drying_rack`), `camp_armor_rack` (canopy drying rack with `twilightforest:camp_armor_rack`),
  `painting` (random `#minecraft:placeable` painting), `tent_pot` (decorated pot with `twilightforest:camp_pot`),
  `twilight_oak_slab` (25 % twilight oak bottom slab, 75 % air).
- `twilightforest:camp_tent` loot is the tent chest table (tannin, jerkies, leather, drying rack, armour or shears).

### 3.1 Camp templates (owned registry entries; re-designs keep pool, weight, placement)

| Template definition | Pool (weight) and placement |
|---|---|
| `template_definition/twilightforest:camp/campfire_east`, `template_definition/twilightforest:camp/campfire_south`, `template_definition/twilightforest:camp/campfire_west` | `camp/structure_start` 100; height WORLD_SURFACE_WG +1; beard_box; ignore world waterlog; camp markers |
| `template_definition/twilightforest:camp/tent/solo_tent` (100), `template_definition/twilightforest:camp/tent/duo_tent` (75), `template_definition/twilightforest:camp/tent/open_tent` (75), `template_definition/twilightforest:camp/tent/luxury_tent` (50) | `camp/tent`; height +1, junction clamp 0; beard_box; camp markers; random swaps: light grey wool -> white / light grey / grey / brown / green / blue / red (10 each) or lime (1); red bed -> red / light blue / lime / orange / light grey bed (10 each) |
| `template_definition/twilightforest:camp/path/intersection_left`, `template_definition/twilightforest:camp/path/intersection_right`, `template_definition/twilightforest:camp/path/intersection_short`, `template_definition/twilightforest:camp/path/j_path`, `template_definition/twilightforest:camp/path/l_path` | `camp/main_path` 25; height +2 clamp 0; gravity processor (WORLD_SURFACE_WG, offset -2) |
| `template_definition/twilightforest:camp/path/path_2x4`, `template_definition/twilightforest:camp/path/path_3x4` | `camp/main_path` 50, `camp/path` 50, `camp/rack_path` 100 (alias deco -> rack); gravity |
| `template_definition/twilightforest:camp/path/path_2x6`, `template_definition/twilightforest:camp/path/path_2x7` | `camp/main_path` 50, `camp/path` 50; gravity |
| `template_definition/twilightforest:camp/deco/compost_large`, `template_definition/twilightforest:camp/deco/compost_small`, `template_definition/twilightforest:camp/deco/garden_1x3`, `template_definition/twilightforest:camp/deco/garden_2x4`, `template_definition/twilightforest:camp/deco/garden_straight`, `template_definition/twilightforest:camp/deco/garden_u`, `template_definition/twilightforest:camp/deco/pen`, `template_definition/twilightforest:camp/deco/repair_station` | `camp/deco` 100; height +2 clamp 0; beard_box; camp markers |
| `template_definition/twilightforest:camp/deco/berries_staked`, `template_definition/twilightforest:camp/deco/berries_support`, `template_definition/twilightforest:camp/deco/berries_trellis`, `template_definition/twilightforest:camp/deco/blackberry_wall`, `template_definition/twilightforest:camp/deco/blueberry_wall`, `template_definition/twilightforest:camp/deco/raspberry_wall` | `camp/deco` 75; same placement |
| `template_definition/twilightforest:camp/deco/double_drying_rack`, `template_definition/twilightforest:camp/deco/long_drying_rack` | `camp/deco` 100 and `camp/rack` 100 |
| `template_definition/twilightforest:camp/deco/lumber1`, `template_definition/twilightforest:camp/deco/lumber2`, `template_definition/twilightforest:camp/deco/lumber3` | `camp/deco` 100; height +1 clamp 0; random swap of twilight oak logs (axis y) -> twilight oak 100 / canopy 90 / oak 80 / birch 70 |

Each re-designed template keeps the jigsaw connectors its pool expects (start -> tent / main path; paths -> path /
deco / rack), the marker names above, and a footprint matching its name (e.g. `path_2x6` is 2 x 6).

## 4. Template features (21 original templates under `assets/structures/questcamps/feature/**`)

Two placement models. The druid hut, simple well, fancy well, grove ruins and stone circle extend `TemplateFeature`
(4.1). The graveyard does **not**: `GraveyardFeature` extends `Feature<NoneFeatureConfiguration>` directly and has
its own snapping, RNG, mirror distribution and offsets (4.2). `SwizzleConfig` features (the hut and both wells) apply
weighted wood palette swaps (`#twilightforest:common` 8, `uncommon` 4, `rare` 3, `treasure` 1) and 50 % mossy
cobblestone / cobblestone-stair preprocessing.

### 4.1 `TemplateFeature.place` (druid hut, wells, grove ruins, stone circle; translate exactly)

1. RNG is `world.getRandom()` (the WorldGenRegion's random), **not** the feature context random.
2. `template = getTemplate(random)`. `rotation = Rotation.getRandom(random)` (`nextInt(4)`: NONE, CW_90, CW_180,
   CCW_90). `mirror = Util.getRandom(Mirror.values(), random)` (`nextInt(3)`: NONE, LEFT_RIGHT, FRONT_BACK, 1/3 each).
3. The structure mask is the origin's chunk at full build height (`chunk minX..maxX`, `minBuildHeight..maxBuildHeight`,
   `chunk minZ..maxZ`). Every template placement of the feature, including the hut basement and the well bottoms, is
   clipped to it: blocks outside the origin's 16 x 16 chunk are silently not placed.
4. Snap: `size = template.getSize(rotation)`; `dx = nextInt(16 - size.x)`, then `dz = nextInt(16 - size.z)`;
   `start = chunkOrigin + (dx, origin.y, dz)`.
   **Size constraint**: `nextInt(bound)` throws for `bound <= 0`, and the rotation is random. Every template returned by
   `getTemplate` must therefore be **at most 15 x 15** in x and z (both axes <= 15, since either may become x). A
   re-designed template of 16 or more crashes the feature.
5. Ground (`offsetToAverageGroundLevel(start, size)`):
   - For every column `dx in [0, size.x)`, `dz in [0, size.z)` (x outer), start at `y = heightmap
     MOTION_BLOCKING_NO_LEAVES` and scan down while `y >= 0`.
   - A liquid, bedrock, a giant block, `#twilightforest:clouds` or hardened dark leaves aborts the feature
     (`FeatureLogic.isBlockNotOk`). A solid block (`isSolid()`) ends the scan. Reaching `y < 0` aborts.
   - It aborts when the population standard deviation of the heights is above 2.0.
   - `start.y = baseY = (int)(mean + 0.5)`, and `maxY = max height`.
   - It aborts unless every block in the closed box `[start.above(maxY - baseY + 1), start + size]` is
     `canBeReplaced()`.
6. `start.y += yLevelOffset()`: simple well +1, the others 0.
7. `placementPos = template.getZeroPositionWithTransform(start, mirror, rotation)`. The settings hold the mirror,
   rotation, mask and random, plus `modifySettings` (the SwizzleConfig processors for the hut and wells). The
   template is placed with flag UPDATE_CLIENTS.
8. DATA-mode structure blocks of the template (`filterBlocks(placementPos, settings, STRUCTURE_BLOCK)`) go to
   `processMarkers`, then `postPlacement` runs.

| Feature type / configured | Templates | Markers and contents |
|---|---|---|
| `twilightforest:druid_hut` (configured `twilightforest:druid_hut`, placed 1/105, no-structure clearance 16) | hut `feature/druid_hut/druid_hut`, `druid_sideways` or `druid_doubledeck` (1/3 each); `postPlacement`: `nextBoolean()` -> a basement (`basement_study`, `basement_shelves` or `basement_gallery` uniformly, then `nextBoolean()` -> its `_trap` variant) at `placementPos.below(12).relative(rotation.rotate(mirror.mirror(NORTH)), 1).relative(rotation.rotate(mirror.mirror(EAST)), 1)`, processors cleared and rebuilt (SwizzleConfig re-rolled, CobbleVariants, StoneBricksVariants) | `spawner` -> remove, then a `minecraft:spawner` with `twilightforest:skeleton_druid`. `loot<F><K>...` (at least 6 characters): chest, or trapped chest when the name ends in `T`; facing `F` = W / E / S (anything else N), rotated and mirrored; type `K` = L / R / S (left / right swapped when mirror != NONE); loot `twilightforest:chests/hut_junk` when the name ends in `J`, else `twilightforest:chests/basement`. `barrel<F>` -> barrel facing D / W / E / N / S (anything else UP) with hut junk. `painting<F><w>` -> a painting `w` wide (digit, else 1), 2 high when `w` is 2 or 4, else 1, hung at the marker or one block clockwise of it when mirrored |
| `twilightforest:simple_well` (configured `twilightforest:well_placer`: simple, 5 % fancy; placed 1/80) | `feature/well/simple_well_top` (y offset +1) and `simple_well_bottom`, placed `bottom.size.y` below the top's `placementPos` with `SmartGrassProcessor` added | `loot<F>`: `nextBoolean()` -> cobblestone or mossy cobblestone (`nextBoolean()`) instead; otherwise a barrel facing `F` (rotated and mirrored; default N) with `twilightforest:well`, then `nextBoolean()` -> nothing more, else a hopper at `barrel.relative(F)` facing back into the barrel |
| `twilightforest:fancy_well` | `feature/well/fancy_well_top` and `fancy_well_bottom` (same stacking, no y offset) | `loot*` -> barrel facing UP with `twilightforest:fancy_well` |
| `twilightforest:grove_ruins` (placed 1/110) | `nextBoolean()` ? `feature/ruins/grove_pillar` : `grove_arch` | none |
| `twilightforest:stone_circle` (placed 1/105) | `feature/ruins/stone_circle` | none |

Every template above must be at most 15 x 15 (step 4); markers in both the top and bottom well templates are handled.

### 4.2 `twilightforest:graveyard` (`GraveyardFeature`, placed 1/70; translate exactly)

Templates: base `feature/graveyard/graveyard`, trap `feature/graveyard/grave_trap`, and graves `GraveType` in the order
`[Full = grave_full, Upper = grave_upper, Lower = grave_lower]`. If any is missing, the feature returns false. RNG is
`rand = ctx.random()` (the feature random, unlike 4.1). Flags are `16 | 2 | 1 = 19` unless noted. `BlockPos.rotate(r)`
is vanilla's: CW_90 `(-z, y, x)`, CW_180 `(-x, y, -z)`, CCW_90 `(z, y, -x)`.

1. `rotation = Rotation.values()[nextInt(4)]`. `mirror = Mirror.values()[nextInt(4) % 3]`, which gives **NONE 1/2**,
   LEFT_RIGHT 1/4 and FRONT_BACK 1/4.
2. `transformedSize = base.getSize(rotation)` and `transformedGraveSize = grave_full.getSize(rotation)`. The grave
   offsets assume that the three graves share one size.
3. Snap (not `nextInt(16 - size)`; there is no 15-block limit, and the base may cross chunk borders):
   - `chunkpos = ChunkPos(origin - (8, 0, 8))` and `chunkendpos = ChunkPos(origin - (8, 0, 8) + transformedSize)`;
   - mask `BoundingBox(chunkpos.minX + 8, 0, chunkpos.minZ + 8, chunkendpos.maxX + 8, 255, chunkendpos.maxZ + 8)`. It
     reaches to the chunk of the far corner, and y 0..255, so nothing is placed below y 0;
   - `startPos = chunkpos.worldPosition + (8, origin.y - 1, 8)`.
4. Ground: `offsetToAverageGroundLevel(startPos, transformedSize)` is the column scan and the standard-deviation
   limit of 4.1 step 5, with two differences:
   - `baseY = Math.round(mean)`, which gives the same result for heights >= 0;
   - the clearance test is `FeatureLogic.isAreaClear`. It aborts only on a block that is `!canBeReplaced() &&
     isSolid() && !liquid()` in `[start.above(maxY - baseY + 1), start + size]`.
5. `placementPos = base.getZeroPositionWithTransform(startPos, mirror, rotation) + (1, -1, 0)`, `size =
   transformedSize - (1, 0, 1)` and `graveSize = transformedGraveSize - (1, 0, 1)`.
6. Base: `settings.addProcessor(WebTemplateProcessor)`. This mutates the shared settings, so the web processor also
   applies to every grave and trap placed later.
   - The processor turns every stored template block other than `grass_block` into a cobweb when
     `settings.getRandom(pos).nextInt(5) == 0`. Stored air entries count, as in vanilla's `processBlockInfos`;
     structure voids are not stored. The settings carry `rand`, so it draws from the feature random.
   - The base is placed at `placementPos`, and its data markers (`filterBlocks(placementPos, settings,
     STRUCTURE_BLOCK)`) are collected.
   - The base and the graves are placed at y `baseY - 1`. Template layer 0 is therefore one below the averaged
     ground, layer 1 is the ground line, and layer 2 is the open floor.
7. Floor cobwebs: `start = startPos + (1, 1, 0)` and `end = start + (size.x, 0, size.z)`. For `x in 1..size.x-1` (outer)
   and `z in 1..size.z-1`, an empty `start + (x, 0, z)` becomes a cobweb when `nextInt(12) == 0`. The RNG is drawn
   only for empty positions (`&&` short-circuit).
8. Grave grid:
   - `inner = start + (2, 0, 2)`, `bound = end - (2, 0, 2)`, `innerSize = bound - inner` = `(size.x - 4, 0, size.z -
     4)`.
   - `fixed = inner + (fx, 0, fz)`, with
     `fx = (rotation == CW_180 ? graveSize.x : 0) + (mirror == FRONT_BACK ? transformedGraveSize.x - 1 : 0) *
     (rotation == CW_180 ? -1 : 1)` and
     `fz = (rotation == CCW_90 ? graveSize.z : 0) + (mirror == FRONT_BACK ? transformedGraveSize.z - 1 : 0) *
     (rotation == CCW_90 ? -1 : 1)`.
   - `fixedSize = innerSize - (graveSize.x, 0, graveSize.z)`.
   - `chestloc = BlockPos(nextInt(2) - (mirror == FRONT_BACK ? 1 : 0), 1, 0).rotate(rotation)` is drawn once, before
     the loop.
   - Loop `for (x = 0; x <= fixedSize.x; x += (rotation is CW_90 or CCW_90) ? 2 : 5)` and `for (z = 0; z <= fixedSize.z;
     z += (rotation is NONE or CW_180) ? 2 : 5)`. Graves are 2 apart along one axis and 5 apart along the other, and
     the rotation decides which.
   - Skip the cell when `x == innerSize.x / 2 || z == innerSize.z / 2` (Java int division). This is the middle row
     and column, but only where a loop value lands exactly on that half.
   - For each cell, `placement = fixed + (x, -2, z)` and `grave = graves[nextInt(3)]`. The grave is placed at
     `placement` with the shared settings (web processor included), and its data markers are collected.
   - If the grave is `Full` and `nextBoolean()`:
     1. if `nextInt(3) == 0`: `placement += BlockPos(mirror == FRONT_BACK ? 1 : -1, 0, mirror == LEFT_RIGHT ? 1 : -1)
        .rotate(rotation)`, and the trap template is placed at the new `placement`;
     2. quirk: `data.addAll(trap.filterBlocks(placementPos, ...))` runs in this branch whether or not the trap was
        placed. It uses the **base's** `placementPos`, so data markers of the trap template are processed (step 9) at
        their offsets from the base origin. Reproduce it, or keep data markers out of the re-designed `grave_trap`,
        which makes it a no-op;
     3. `setBlock(placement + chestloc, trapped_chest[facing=WEST].rotate(rotation).mirror(mirror), 19)`. If the set
        succeeds, the chest is filled with `twilightforest:graveyard` and `placement + chestloc - (0, 1, 0)` becomes
        mossy cobblestone (UPDATE_ALL);
     4. a `twilightforest:wraith` is created at `(placement.x, placement.y, placement.z)`. That is the integer block
        corner, not the centre, and includes the trap shift. It gets `finalizeMobSpawn(STRUCTURE)` and is added.
     The other half of the full graves get neither a chest nor a wraith.
9. Every collected data marker (mode DATA) named `spawner` is removed; then `nextInt(4) == 0` places a
   `minecraft:spawner` with `twilightforest:rising_zombie` (UPDATE_ALL). The feature returns true.

Re-design constraints: the base may be wider than 15, since its snap is not limited. The three graves must share one
footprint. Graves are placed relative to the base's floor line as above. `spawner` markers may sit in the base and the
graves.

## 5. Loot tables (owned; contents in the appendix)

`twilightforest:camp_armor_rack`, `twilightforest:camp_drying_rack`, `twilightforest:camp_pot`, `twilightforest:camp_tent`,
`twilightforest:chests/hut_junk`, `twilightforest:entities/quest_ram`, `twilightforest:entities/questing_ram_rewards`,
`twilightforest:entities/questing_ram_reward_blocks`, `twilightforest:fancy_well`, `twilightforest:graveyard`,
`twilightforest:quest_grove_dropper`, `twilightforest:well`.

## 6. Ids and strings

Configured features: `twilightforest:druid_hut`, `twilightforest:fancy_well`, `twilightforest:graveyard`,
`twilightforest:grove_ruins`, `twilightforest:simple_well`, `twilightforest:stone_circle`, `twilightforest:well_placer`.
Placed: `twilightforest:druid_hut`, `twilightforest:graveyard`, `twilightforest:grove_ruins`,
`twilightforest:stone_circle`, `twilightforest:well_placer`. Structure types `twilightforest:quest_grove`,
`twilightforest:camp`; structures `twilightforest:quest_grove`, `twilightforest:camp`; map decoration
`twilightforest:quest_grove`. Lang (7): `entity.twilightforest.quest_ram` "Questing Ram", the spawn egg, three
subtitles, `config.twilightforest.ram_indicator` (+ tooltip).

## 7. Bedrock adaptation notes

- The 16-colour tempt check and the item-entity goal are server-side; the crosshair indicator needs the client mod
  (F12 HUD); vanilla clients get no indicator.
- The reward bundle (`minecraft:bundle` with contents) maps to Bedrock bundles (1.21 parity); if bundles are
  unavailable to vanilla clients, drop the contents directly.
- Wood palette swizzles and marker handlers are F7 (`palettes::apply`, `registerMarkerHandler`).

## 8. Selftests (`questcamps.*`, dx 1900-1999)

`questcamps.ram_accepts_wool` (each colour once), `questcamps.ram_eats_dropped_wool`, `questcamps.ram_reward`
(16 colours -> rewards + trigger + conquered grove), `questcamps.grove_layout`, `questcamps.camp_assembly` (seeded
camp has a start, tents and paths), `questcamps.druid_hut_markers`, `questcamps.graveyard`, `questcamps.wells`.

## Appendix: TF data (compact, generated from TF 4.8 data JSON)

Notation: `block[state=value]`; `weighted{a:w}`; loot `xN` count, `wN` weight, `[...]` conditions; placement modifiers joined by `>`. The imported JSON (F1) is authoritative; this appendix is the review checklist.

### Configured features (7)

- `twilightforest:druid_hut`: type `twilightforest:druid_hut` {palette_choices=[{data=#twilightforest:common, weight=8}, {data=#twilightforest:uncommon, weight=4}, {data=#twilightforest:rare, weight=3}, {data=#twilightforest:treasure, weight=1}], preprocessing_rules=[{input_predicate={block=cobblestone, predicate_type=random_block_match, probability=0.5}, location_predicate={predicate_type=always_true}, output_state=mossy_cobblestone}, {input_predicate={block=cobblestone_stairs, predicate_type=random_block_match, probability=0.5}, location_predicate={predicate_type=always_true}, output_state=mossy_cobblestone_stairs[facing=north,half=bottom,shape=straight,waterlogged=false]}, {input_predicate={block=cobblestone_slab, predicate_type=random_block_match, probability=0.5}, location_predicate={predicate_type=always_true}, output_state=mossy_cobblestone_slab[type=bottom,waterlogged=false]}, {input_predicate={block=cobblestone_wall, predicate_type=random_block_match, probability=0.5}, location_predicate={predicate_type=always_true}, output_state=mossy_cobblestone_wall[east=none,north=none,south=none,up=true,waterlogged=false,west=none]}, {input_predicate={block=stone_bricks, predicate_type=random_block_match, probability=0.5}, location_predicate={predicate_type=always_true}, output_state=mossy_stone_bricks}, {input_predicate={block=stone_brick_stairs, predicate_type=random_block_match, probability=0.5}, location_predicate={predicate_type=always_true}, output_state=mossy_stone_brick_stairs[facing=north,half=bottom,shape=straight,waterlogged=false]}, {input_predicate={block=stone_brick_slab, predicate_type=random_block_match, probability=0.5}, location_predicate={predicate_type=always_true}, output_state=mossy_stone_brick_slab[type=bottom,waterlogged=false]}, {input_predicate={block=stone_brick_wall, predicate_type=random_block_match, probability=0.5}, location_predicate={predicate_type=always_true}, output_state=mossy_stone_brick_wall[east=none,north=none,south=none,up=true,waterlogged=false,west=none]}], target_palettes=#twilightforest:druid_hut_swizzle_mask}
- `twilightforest:fancy_well`: type `twilightforest:fancy_well` {palette_choices=[{data=#twilightforest:common, weight=8}, {data=#twilightforest:uncommon, weight=4}, {data=#twilightforest:rare, weight=3}, {data=#twilightforest:treasure, weight=1}], preprocessing_rules=[{input_predicate={block=cobblestone, predicate_type=random_block_match, probability=0.5}, location_predicate={predicate_type=always_true}, output_state=mossy_cobblestone}, {input_predicate={block=cobblestone_stairs, predicate_type=random_block_match, probability=0.5}, location_predicate={predicate_type=always_true}, output_state=mossy_cobblestone_stairs[facing=north,half=bottom,shape=straight,waterlogged=false]}, {input_predicate={block=cobblestone_slab, predicate_type=random_block_match, probability=0.5}, location_predicate={predicate_type=always_true}, output_state=mossy_cobblestone_slab[type=bottom,waterlogged=false]}, {input_predicate={block=cobblestone_wall, predicate_type=random_block_match, probability=0.5}, location_predicate={predicate_type=always_true}, output_state=mossy_cobblestone_wall[east=none,north=none,south=none,up=true,waterlogged=false,west=none]}, {input_predicate={block=stone_bricks, predicate_type=random_block_match, probability=0.5}, location_predicate={predicate_type=always_true}, output_state=mossy_stone_bricks}, {input_predicate={block=stone_brick_stairs, predicate_type=random_block_match, probability=0.5}, location_predicate={predicate_type=always_true}, output_state=mossy_stone_brick_stairs[facing=north,half=bottom,shape=straight,waterlogged=false]}, {input_predicate={block=stone_brick_slab, predicate_type=random_block_match, probability=0.5}, location_predicate={predicate_type=always_true}, output_state=mossy_stone_brick_slab[type=bottom,waterlogged=false]}, {input_predicate={block=stone_brick_wall, predicate_type=random_block_match, probability=0.5}, location_predicate={predicate_type=always_true}, output_state=mossy_stone_brick_wall[east=none,north=none,south=none,up=true,waterlogged=false,west=none]}], target_palettes=#twilightforest:well_swizzle_mask}
- `twilightforest:graveyard`: type `twilightforest:graveyard` {}
- `twilightforest:grove_ruins`: type `twilightforest:grove_ruins` {}
- `twilightforest:simple_well`: type `twilightforest:simple_well` {palette_choices=[{data=#twilightforest:common, weight=8}, {data=#twilightforest:uncommon, weight=4}, {data=#twilightforest:rare, weight=3}, {data=#twilightforest:treasure, weight=1}], preprocessing_rules=[{input_predicate={block=cobblestone, predicate_type=random_block_match, probability=0.5}, location_predicate={predicate_type=always_true}, output_state=mossy_cobblestone}, {input_predicate={block=cobblestone_stairs, predicate_type=random_block_match, probability=0.5}, location_predicate={predicate_type=always_true}, output_state=mossy_cobblestone_stairs[facing=north,half=bottom,shape=straight,waterlogged=false]}, {input_predicate={block=cobblestone_slab, predicate_type=random_block_match, probability=0.5}, location_predicate={predicate_type=always_true}, output_state=mossy_cobblestone_slab[type=bottom,waterlogged=false]}, {input_predicate={block=cobblestone_wall, predicate_type=random_block_match, probability=0.5}, location_predicate={predicate_type=always_true}, output_state=mossy_cobblestone_wall[east=none,north=none,south=none,up=true,waterlogged=false,west=none]}], target_palettes=#twilightforest:well_swizzle_mask}
- `twilightforest:stone_circle`: type `twilightforest:stone_circle` {}
- `twilightforest:well_placer`: type `minecraft:random_selector` {default={feature=twilightforest:simple_well, placement=[]}, features=[{chance=0.05, feature={feature=twilightforest:fancy_well, placement=[]}}]}

### Placed features (5)

- `twilightforest:druid_hut`: feature `twilightforest:druid_hut`; placement: rarity 1/105 > in_square > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=16, occupies_surface=True, occupies_underground=True, occupies_vegetation=False, structures_allowed=[]} > biome
- `twilightforest:graveyard`: feature `twilightforest:graveyard`; placement: rarity 1/70 > in_square > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > biome
- `twilightforest:grove_ruins`: feature `twilightforest:grove_ruins`; placement: rarity 1/110 > in_square > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > biome
- `twilightforest:stone_circle`: feature `twilightforest:stone_circle`; placement: rarity 1/105 > in_square > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > biome
- `twilightforest:well_placer`: feature `twilightforest:well_placer`; placement: rarity 1/80 > in_square > heightmap OCEAN_FLOOR > twilightforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=True, occupies_vegetation=False, structures_allowed=[]} > biome

### Loot tables (12)

- `twilightforest:camp_armor_rack` (chest)
  - pool 1: rolls 1: leather_helmet; leather_chestplate; leather_leggings; leather_boots; chainmail_helmet; chainmail_chestplate; chainmail_leggings; chainmail_boots
- `twilightforest:camp_drying_rack` (chest)
  - pool 1: rolls 1: empty w20; twilightforest:chicken_jerky w8; twilightforest:mutton_jerky w8; twilightforest:pork_jerky w8; twilightforest:venison_jerky w8; twilightforest:beef_jerky w4; twilightforest:tanned_leather w4; twilightforest:rabbit_jerky w4; leather w2; table {pools=[{bonus_rolls=0, entries=[item{name=leather_helmet}, item{name=leather_chestplate}, item{name=leather_leggings}, item{name=leather_boots}], rolls=1}]} w2; twilightforest:gelatinous_slime_drop
- `twilightforest:camp_pot` (chest)
  - pool 1: rolls 1: twilightforest:chicken_jerky x2-4 w8; twilightforest:mutton_jerky x2-4 w8; twilightforest:pork_jerky x2-4 w8; twilightforest:venison_jerky x2-4 w8; twilightforest:beef_jerky x2-3 w4; twilightforest:tanned_leather x2-3 w4; twilightforest:rabbit_jerky x2-3 w4; twilightforest:gelatinous_slime_drop x1-2
- `twilightforest:camp_tent` (chest)
  - pool 1: rolls 1: twilightforest:tannin x2-4
  - pool 2: rolls 1: twilightforest:venison_jerky x2-4; twilightforest:mutton_jerky x2-4; twilightforest:pork_jerky x2-4
  - pool 3: rolls 1-2: leather x1-2; twilightforest:birch_drying_rack x1; stick x1-2; apple x1-2
  - pool 4: rolls 1: leather_helmet; leather_chestplate; leather_leggings; leather_boots; shears
- `twilightforest:chests/hut_junk` (chest)
  - pool 1: rolls 9: cobweb x1-4 w3; bowl x1-3 w3; glass_bottle x2-4 w3; potion {function=set_potion, id=water}; coarse_dirt x2-5; paper x3-9; sand x1-4 w3; flint x1-2 w3; wheat_seeds x2-5; bone_meal; stick x2-8 w3
  - pool 2: rolls 1: table twilightforest:chests/suspicious_stew w8; suspicious_stew w2; mushroom_stew w4; bell; iron_nugget x2-5; gold_nugget x1-3
- `twilightforest:entities/quest_ram` (entity): empty
- `twilightforest:entities/questing_ram_reward_blocks` (entity)
  - pool 1: rolls 1: coal_block
  - pool 2: rolls 1: iron_block
  - pool 3: rolls 1: copper_block
  - pool 4: rolls 1: lapis_block
  - pool 5: rolls 1: gold_block
  - pool 6: rolls 1: diamond_block
  - pool 7: rolls 1: emerald_block
- `twilightforest:entities/questing_ram_rewards` (entity)
  - pool 1: rolls 1: twilightforest:crumble_horn
  - pool 2: rolls 1: bundle {component=bundle_contents, entries=[item{name=twilightforest:quest_ram_trophy}, loot_table{value=twilightforest:entities/questing_ram_reward_blocks}], function=set_contents}
- `twilightforest:fancy_well` (chest)
  - pool 1: rolls 4: gold_nugget x5-15; iron_nugget x6-12; string x4-7; stick x5-20
  - pool 2: rolls 2: bucket; water_bucket; twilightforest:raw_ironwood x3-7; iron_ingot x3-5; gold_ingot x2-4; copper_ingot x4-8
  - pool 3: rolls 1: experience_bottle x5-9 w75; twilightforest:charm_of_life_1 w75; twilightforest:music_disc_steps w50; emerald x4-9 w25; diamond x3-5 w25; twilightforest:charm_of_keeping_2 w25; twilightforest:transformation_powder x5-17 w25
- `twilightforest:graveyard` (chest)
  - pool 1: rolls 4: pumpkin_seeds x1-32; pumpkin_pie x1-6; twilightforest:torchberries x1-16
  - pool 2: rolls 2: diamond x1-6; twilightforest:moon_dial
  - pool 3: rolls 1: twilightforest:transformation_powder x1-12; twilightforest:uncrafting_table [{condition=twilightforest:uncrafting_table_enabled}]; golden_apple
- `twilightforest:quest_grove_dropper` (chest)
  - pool 1: rolls 4: white_wool; orange_wool; magenta_wool; light_blue_wool; yellow_wool; lime_wool; pink_wool; gray_wool; light_gray_wool; cyan_wool; purple_wool; blue_wool; brown_wool; green_wool; red_wool; black_wool
- `twilightforest:well` (chest)
  - pool 1: rolls 4: gold_nugget x3-5 w75; iron_nugget x1-3 w75; string x2-6 w75; stick x3-7 w75; empty w25
  - pool 2: rolls 2: bucket; melon_seeds x1-4; pumpkin_seeds x1-4; ink_sac x1-2; water_bucket; bowl; twilightforest:raw_ironwood; iron_ingot x1-2; gold_ingot x1-2; copper_ingot x1-2
  - pool 3: rolls 1: shears; saddle; diamond; experience_bottle x2-4
