# aurora (U28): Aurora palace, Snow Queen and mushroom tower

Behaviour spec of unit **U28** (`aurora`), written by F15 from the upstream 4.8 (`9e40a757`). Java paths are
relative to `src/main/java/gloamingforest/`; data paths to `src/generated/resources/`.

| Unit fact | Value |
|---|---|
| Selftests | `aurora.*`, testPos dx `2800-2899` (R12) |
| World data | `gf:aurora:*` (nothing global is required: the Snow Queen keeps her state in `gf::entity::data`; reserved for a per-palace debug flag) |
| Net channels | `aurora.*` (optional: Snow Queen phase / beam flag for companions when the F8 entity sync is not enough) |
| Contracts | landmark, structure, boss, entity, projectile, client.render, fx (+ loot, combat, progression by string) |
| Config | none (boss drop chests and the multiplayer adjuster live in the W0 `bosses` section) |

Unit record note: the content line counts "4 spawn eggs"; owners.tsv gives U28 five (the Snow Queen egg included),
all specified in section 11.

**Tag namespaces.** Tags are written `#<ns>:<path>`. `#gloamingforest:` tags and the `#c:` / `#neoforge:` tags that
owners.tsv lists (`tag` rows) are upstream data (`data/<ns>/tags/**`) imported by F1; `#minecraft:` tags are vanilla Java
1.21.1 tags (upstream adds members to some). Bedrock has no Java tags: every tag here resolves through `gf::tags` (F9,
`src/feature/tags/Tags.h`), and units add members only through union fragments (R6). Upstream's damage-type and tool-tier
tags are in the `neoforge` namespace (`#neoforge:is_physical`, `#neoforge:is_magic`, `#neoforge:is_environment`,
`#neoforge:needs_wood_tool`), never `#c:`.

## 1. Structure `gloamingforest:aurora_palace`

Sources: `world/components/structures/type/AuroraPalaceStructure.java`, `world/components/structures/icetower/*.java`
(9 piece / decorator classes), `world/components/structures/icetower/floordecorators/*.java` (13 floor-plan classes:
the enum `FloorTypesAuroraPalace` with its weight and class maps, the interface `IFloorDecorator` (default empty
required / blocked lists), the abstract `FloorWith3x3Map implements IFloorDecorator` (holds `REQUIRED_FLOOR_PARTS` /
`BLOCKED_FLOOR_PARTS`, returned as copies), the enum `FloorParts`, and the nine `FloorWith3x3Map` subclasses named per
row in the section 1.2 table: `FloorFarWallSteps`, `FloorPillarParkour`, `FloorPillarPlatforms`,
`FloorPillarPlatformsOutside`, `FloorPlatform`, `FloorQuadPillarStairs`, `FloorTop`, `FloorWraparoundWallSteps`,
`FloorWraparoundWallStepsPillars`),
`world/components/structures/selectors/IceTowerRandomBlockSelectorFactory.java`, F7 tower base (`bases/Tower.h`:
`TowerWingComponent`, `TowerRoofComponent`), `world/components/structures/UtilityPiece.java` (F7 `GloamingUtilityPiece`).

| Field | Value |
|---|---|
| Structure type | `gloamingforest:aurora_palace` (`AuroraPalaceStructure extends ControlledSpawningStructure`) |
| Structure set | `gloamingforest:aurora_palace`, placement `gloamingforest:landmark_grid` (`LandmarkGridPlacement.forceStructureForCenters()`), weight 1 |
| Biomes | `#gloamingforest:valid_aurora_palace_biomes` = `gloamingforest:glacier` (U1 tag); legacy placement table maps the glacier centre to this structure (F6) |
| Step | `surface_structures`, terrain adjustment `none` |
| Progression lock | `gloamingforest:progress_yeti` (advancement lock; landmark protection until the Alpha Yeti is beaten) |
| Restriction (U14 data) | `restrictions/gloamingforest:glacier`: structure aurora_palace, enforcement `frost`, multiplier 1.0, hint item `gloamingforest:alpha_yeti_fur`, advancement `gloamingforest:progress_yeti` |
| Hint | `gloamingforest:kobold` carrying book `gloamingforest.book.icetower` ("Notes on Auroral Fortification", 3 pages; F6 hint system) |
| Decoration clearance | chunk clearance radius 2, surface decorations false, underground decorations true, vegetation true, adjust elevation false |
| Centred in chunk | true |
| Controlled spawns label 0 | `gloamingforest:snow_guardian` 10 1-2, `gloamingforest:stable_ice_core` 10 1-2, `gloamingforest:unstable_ice_core` 5 1-2 |
| Spawn overrides | every mob category empty over the structure box (`STRUCTURE` box type) |
| Map icon | `gloamingforest:aurora_palace` (section 9) |
| Structure tag | member of `#gloamingforest:landmark` (structure tag) |
| Name | `structure.gloamingforest.aurora_palace` "Aurora Palace" |

Start position: `x = chunkX x 16 + 7`, `z = chunkZ x 16 + 7`, y = sea level (0) because the decoration config does not
adjust elevation; piece random `RandomSource.create(seed + chunkX x 25117 + chunkZ x 151121)`. First piece
`IceTowerMainComponent(rand, 0, x, y, z)`, which places itself at `(x + 11, y + 40, z + 11)` facing NORTH: the palace
floats 40 blocks above the start and its towers are filled to the ground (bases) or hang beards.

Decorator `IceTowerDecorator`: block `gloamingforest:aurora_block`, accent `minecraft:birch_planks`, fence
`minecraft:oak_fence`, stairs `minecraft:birch_stairs`, pillar `gloamingforest:aurora_pillar` (axis y), platform
`minecraft:birch_slab`, floor `minecraft:birch_planks`, random blocks = `SimpleRandomBlockSelector` with the single entry
`aurora_block` weight 1 (walls are plain aurora block; keep the selector so `generateBox(..., deco.randomBlocks)`
consumes the same random calls).

### 1.1 Piece types

| Id | Class | Summary |
|---|---|---|
| `gloamingforest:GloamingITMai` | IceTowerMainComponent | Main tower, `SIZE` 11, height `31 + rand(3) x 10` (31/41/51), at (x + 11, y + 40, z + 11), NORTH. `addChildren` **loops until the boss wing exists**: `rand.setSeed(rand.nextLong())`, clear openings, add opening (0, 1, 5) CW180, reset the piece list to the main tower alone; entrance: when the door x is 0 (always), bridge length `30 + rand(20)` built with `makeEntranceBridge` (bridge piece at `offsetTowerCCoords(x, y, z, 5, dir)`, extra z 4) and the door moved `length` blocks west; then `makeEntranceTower` (size 11, height 11, the main rotation); then the regular wing logic (`super.addChildren`). Saves `hasBossWing` |
| `gloamingforest:GloamingITWin` | IceTowerWingComponent | Wing tower (size 11). `addChildren`: door opening (0, 1, 5) CW180; `hasBase` = no other piece overlaps the wing's x/z rectangle and (depth 0 or `rand.nextBoolean()`); roof always (`IceTowerRoofComponent` through `tryToFitRoof`); no base -> beard, base -> a `gloamingforest:GloamingUtilityPiece` box from the lowest piece minY up to the wing minY (keeps features out). Depth < 5: start rotation `ROTATIONS[rand(4)]`, for each of the 4 rotations a valid opening; at depth 4 when the main tower has no boss wing yet -> `makeBossTowerWing` (size 15, height 41), else a child wing of height `(rand(2) + rand(2) + 2) x 10 + 1` (21/31/41). Child wings are refused when out of `RANGE` = 27 (x or z) of the first piece's centre, when the wing box extended by 16 (= round(11 x 1.414)) up and down collides with another piece, or when an entrance tower / bridge piece intersects the x/z rectangle of the **parent** wing (upstream tests `this.getBoundingBox()`, not the new wing; keep it). Treasure floor: `floors = height / 10`; if floors > 1, `treasureFloor = rand(floors - 1)`. Door height `getYByStairs = 11 + rand(floors - 1) x 10`. Door opening: F7 door + birch planks accent above when not air. Saves `hasBase`, `treasureFloor` |
| `gloamingforest:GloamingITBoss` | IceTowerBossWingComponent | Snow Queen tower: size 15, height 41, no controlled spawns (`spawnListIndex = -1`), never a base (always a beard). Floors: thick ice slabs per floor (each column `1 + rand(2) + rand(2) + rand(2)` thick, `minecraft:ice` 1/4 else `minecraft:packed_ice`) instead of planks; floors 0..2 decorated with packed-ice stairs: three levels (y = bottom, +3, +6) of two opposite stair sets each, rotating 90 degrees per level (`placeIceStairs`: solid 3 x 3 at (8..10, y + 1, 1..3), (11..13, y + 2, 1..3), (11..13, y + 3, 4..6) with 50 % random packed ice under each step); top floor (bottom 30, top 40): ceiling of glowstone (1/10) or packed ice, `rand(2) + rand(2)` thick, `gloamingforest:snow_queen_boss_spawner` at (7, top - 6, 7) = (7, 34, 7), air box (5..9, top - 3..top - 1, 5..9) |
| `gloamingforest:GloamingITEnt` | IceTowerEntranceComponent | Entrance tower: size 11, height 11, always a base, door (10, 1, 5) facing the bridge, three `GloamingITSt` stair pieces on the CW180, CCW90 and CW90 sides (each adds an opening), roof, no wings (`makeTowerWing` returns false); single floor = `decoratePillarsCornersHigh` (cross beams of x / z aurora pillars at y 5, the centre 3..7 cleared, four corner pillars (3/7, 5..10, 3/7) hanging from the ceiling) |
| `gloamingforest:GloamingITBri` | IceTowerBridgeComponent | Entrance bridge, length L (30..49); box = `getComponentToAddBoundingBox(x, y, z, 0, 0, -4, L, 6, 9, dir)`, i.e. world z - 4 .. z + 5 (local 0..L, 0..6, 0..9) in the bridge direction; contents in local coordinates: air (0..L, 1..5, 0..4), aurora-block floor (y 0) and ceiling (y 6) over z 4..8 (`extraZlength` = 4), aurora pillars on both sides every 3 blocks from x 2 (y 1..5 at z 4 and z 8). Saves `bridgeLength`, `extraZlength` |
| `gloamingforest:GloamingITSt` | IceTowerStairsComponent | Outside stairs of the entrance tower: `stairsLength` 5, box starts 4 below the door; stepped pyramid of aurora block (two blocks per step, only into replaceable blocks) widening with distance, plus one block at (0, 4, 5) |
| `gloamingforest:GloamingITRoof` | IceTowerRoofComponent | "Swoopy" roof cap on top of a wing: same size and orientation, height `round(sqrt(size^2 + size^2))` (16 for 11, 21 for 15); each column (x, z) is filled with aurora block from y 0 up to `round(sqrt(x^2 + z^2))` |
| `gloamingforest:GloamingITBea` | IceTowerBeardComponent | Hanging "beard" under a wing without base: same footprint, height `round(size x 1.414)` (16 / 21), box from wing minY - height to minY; each column hangs `round(sqrt(x^2 + z^2))` aurora blocks down from the top. Saves `beardSize`, `beardHeight` |

Wing `postProcess` (all wing types): `decoRNG = RandomSource.create(seed + minX x 321534781 ^ minZ x 756839)` (Java
precedence: `(seed + minX x 321534781) ^ (minZ x 756839)`); walls `generateBox(0, 0, 0, size - 1, height - 1, size - 1,
randomBlocks)`; hollow inside; with a base, every column is filled down with aurora block from y -1 (F7
`fillColumnDown`); floors (`makeFloorsForTower`, floor height 10); openings last.

`makeFloorsForTower` (non-boss wings): `floors = height / 10`, top floor = floors - 1 decorated first with
`decorateTopFloor` (50 %: corner pillars with the centre cross cleared, else pillar grid: cross beams at y 5 and four
full pillars at (3/7, 3/7)); a dead-end top floor (F7 `isDeadEnd`) gets a 4-high aurora pillar at (5, 1..4, 5) and the
**`gloamingforest:aurora_room`** chest on top at (5, bottom + 5, 5). The floor plan for the lower floors is chosen from
top to bottom with `decoRNG` (`createFloorPlan`), then each floor i from floors - 2 down to 0: birch-plank floor at
`(i + 1) x 10` (boss wing: thick ice), ladder rotation `CCW90` rotated CW `rotation` times, decoration by type.

### 1.2 Floor plan (`floordecorators`)

Each floor type declares 3 x 3 anchor zones of an 11 x 11 floor (`FloorParts`: LEFT_FRONT, FRONT, RIGHT_FRONT, LEFT, MID,
RIGHT, LEFT_BACK, BACK, RIGHT_BACK, PILLARS = the four 1 x 1 pillar cells; `rotateClockwise` maps LF->RF, F->R, RF->RB,
R->B, RB->LB, B->L, LB->LF, L->F, MID and PILLARS fixed). REQUIRED parts must be free on the floor above; BLOCKED parts
block the floor below. Doors block the zones whose rectangles contain a door between the floor's bottom and top
(`getPartsBlockedByDoors`: LB (0,0)-(3,3), L (4,0)-(6,3), LF (7,0)-(10,3), B (0,4)-(3,6), F (7,4)-(10,6), RB (0,7)-(3,10),
R (4,7)-(6,10), RF (7,7)-(10,10)).

| Type | Weight | Required | Blocked | Decoration (`IceTowerWingComponent`) | Treasure (`aurora_cache`) |
|---|---|---|---|---|---|
| FAR_WALL_STEPS (`FloorFarWallSteps`) | 1 | L, LB | LB, L, LF | two parallel step rows of pillar / slab along x 8 and 9, entry slab (7, 1, 1), hole in the ceiling (9, top, 2..6) | (1, bottom + 8, 5) on an x-axis pillar |
| PILLAR_PARKOUR (`FloorPillarParkour`) | 6 | L, MID, R, LF, F, RF, PILLARS | MID, R, PILLARS | 4 pillars, a centre stub and 9 short pillar beams at rising heights forming a jump course; ceiling holes (6..8, top, 6..8) and (2..5, top, 6..8) | (8, bottom + 8, 7) |
| PILLAR_PLATFORMS (`FloorPillarPlatforms`) | 6 | LB, B, RB, L, MID, R, PILLARS | LB, B, L, MID, LF, F, PILLARS | 3 x 3 plank platforms spiralling around the 4 pillars (y 1..9), ceiling cleared (2..8, top, 2..4) and (2..4, top, 2..6), slab tops on two pillars | (3, bottom + 5, 2) |
| PILLAR_PLATFORMS_OUTSIDE (`FloorPillarPlatformsOutside`) | 1 | RB, R, RF | LB, RB, L, LF, PILLARS | plank ledges (1..6, bottom, 1..3) on 4 sides with slabs on 3 of them, a 22-step slab stairway (`buildStairway`) to the ceiling plus 4 full-block landings, 4 pillars | (3, bottom + 5, 2) on a 2-long pillar |
| PLATFORM (`FloorPlatform`) | 1 | LB, L | RB, B, PILLARS | 4 pillars, 5 x 5 planks platform at y 5, two flights up to it and two flights from it to the ceiling hole | (5, bottom + 6, 5) |
| QUAD_PILLAR_STAIRS (`FloorQuadPillarStairs`) | 1 | L, LF | B, RB, PILLARS | 4 pillars, four slab flights around them to the ceiling | (3, bottom + 7, 5) on a 3-long pillar |
| WRAPAROUND_WALL_STEPS (`FloorWraparoundWallSteps`) | 1 | LB, L | RF | slab spiral along two walls (top / bottom slabs alternating), entry slabs (1, 1, 7..8), ceiling hole (9, top, 2..6) | (1, bottom + 5, 5) on a pillar |
| WRAPAROUND_WALL_STEPS_PILLARS (`FloorWraparoundWallStepsPillars`) | 1 | L, LB | LB, LF, RF, PILLARS | wraparound steps + 4 full pillars + 10 short pillar beams between them and the walls | (7, bottom + 7, 1) |
| TOP (`FloorTop`) | 0 | - | MID, PILLARS | the top floor (never chosen for lower floors) | - |

Algorithm: start with `topBlocked` = TOP's blocked parts + parts blocked by doors of the top floor and `bottomBlocked` =
parts blocked by doors of the floor below; repeat until the plan has `floors - 1` entries: collect every type except TOP
with its allowed rotations (0..3, `required` rotated disjoint from `topBlocked` and `blocked` rotated disjoint from
`bottomBlocked`); if none, restart the plan (`tries++`; once `tries > 10000`, every later iteration first clears both
current blocked sets and logs the error "Unable to generate floor plan for aurora palace at {}..."); else pick a type
by weight
(`WorldUtil.getRandomElementWithWeights`) and a rotation (`Util.getRandom`), then recompute the blocked sets for the
next floor down. All randoms come from `decoRNG` (translate exactly; the golden hash depends on it).

Candidate order (a GloamingForest decision): `getRandomElementWithWeights` sums the float weights, draws
`decoRNG.nextFloat() * total` and subtracts the weights **in list order**, returning the first type that takes the
value below 0, so the order of the candidates decides which type a given draw picks. Upstream builds the candidates with
`Arrays.stream(values()).collect(Collectors.toMap(...))` (IceTowerWingComponent.java:317-337), a `HashMap` keyed by
enum constants whose `hashCode` is the identity hash, and iterates its `keySet()`: upstream's own order depends on the JVM
run, so no upstream run is a reference for the pick. GloamingForest fixes the order to the enum declaration order with TOP
removed and the types that have no allowed rotation skipped: FAR_WALL_STEPS, PILLAR_PARKOUR, PILLAR_PLATFORMS,
PILLAR_PLATFORMS_OUTSIDE, PLATFORM, QUAD_PILLAR_STAIRS, WRAPAROUND_WALL_STEPS, WRAPAROUND_WALL_STEPS_PILLARS (the table
order above). The total is `(float)` of the double sum of the candidates' float weights; keep the float arithmetic.
The rotation is then `Util.getRandom(allowed, decoRNG)` over the allowed rotations in ascending order
(`IntStream.range(0, 4).filter(...)`), which is deterministic in upstream too.

Chests: `GloamingStructureComponentOld.placeTreasureRotated` places a vanilla `minecraft:chest` (not trapped) facing the
given direction with the loot table (F10 `loot::placeContainer`), skipped when a chest is already there.

### 1.3 Golden hash

R19: `aurora.palace_hash` generates the palace for a fixed seed and chunk with `landmark::forceStart` and hashes the
volume (F0 `snapshotVolume`); reviewed once with screenshots (aurora tint visible on walls). Because of the candidate
order decision in section 1.2, the hash is GloamingForest's own golden (recorded from the translation after the
screenshot review), not an oracle value: an upstream run can produce a different floor plan for the same seed.

## 2. Structure `gloamingforest:mushroom_tower`

Sources: `world/components/structures/type/MushroomTowerStructure.java`, `world/components/structures/mushroomtower/*.java`
(6 classes + package-info).

| Field | Value |
|---|---|
| Structure type | `gloamingforest:mushroom_tower` (`MushroomTowerStructure extends LandmarkStructure`, no controlled spawns, no lock, no hint) |
| Structure set | **none**: upstream's `GloamingStructureSets` registration is commented out and the legacy placement entry for `dense_mushroom_forest` too; the structure never generates naturally and is reachable only with `/place structure gloamingforest:mushroom_tower` (F6 `/gloamingforest place`) |
| Biomes | `#gloamingforest:valid_mushroom_tower_biomes` = `gloamingforest:dense_mushroom_forest` (U1 tag) |
| Step | `surface_structures`, terrain adjustment `none` |
| Decoration clearance | radius 2, surface true, underground true, vegetation true, adjust elevation true: start y = `clamp(firstOccupiedHeight(WORLD_SURFACE_WG), seaLevel + 1, seaLevel + 7)` |
| Map icon | none |
| Structure tag | member of `#gloamingforest:landmark` |
| Name | `structure.gloamingforest.mushroom_tower` "Mushroom Castle" |

Start: centred in chunk (`x = chunkX x 16 + 7`, `z = chunkZ x 16 + 7`), y from the elevation rule above, piece random as
the palace.

Decorator `MushroomTowerDecorator`: block and pillar `minecraft:mushroom_stem` (up=false, down=false, sides true),
accent `minecraft:red_mushroom_block` (default: all faces), fence `minecraft:oak_fence`, stairs `minecraft:spruce_stairs`,
floor `minecraft:oak_planks`. Ascender floors and bridges use `minecraft:jungle_planks`.

| Id | Class | Summary |
|---|---|---|
| `gloamingforest:GloamingMTMai` | MushroomTowerMainComponent | Main tower, `MAIN_SIZE` 15, height `8 + rand(3) x 4` (8/12/16), at (x + 15, y + 4, z + 15), NORTH; always a base. Depth < 3: up to 6 tries of `makeAscenderTower` (random side, child height `(height - doorY) + (rand(2) + rand(2) + 3) x 4 + 1`, an ascender bridge; logs "Main tower made a bridge to another tower" / "Main tower failed to branch off at index {}"), then a bridge (size `size - 4` = 11, height `(rand(2) + rand(2) + 2) x 4 + 1`) on every other side. Depth >= 3: a mushroom roof (hang 1.6). Door opening also clears the two blocks inside the door |
| `gloamingforest:GloamingMTWin` | MushroomTowerWingComponent | Round tower (sizes 11, 7, 3): trunk where `dist = (int)(max(|dx|, |dz|) + min(|dx|, |dz|) x 0.4) <= size / 2`, walls `dist > (int)(diameter x 0.8)`, planks floor and ceiling discs, floors every `FLOOR_HEIGHT` 4 (jungle planks when ascender), base columns down when `size > 3`. Children: an ascender wing adds a main bridge from (door at `height - 3`, CW180 side) with child height `(rand(3) + rand(3) + 2) x 4 + 1`; depth < 5 and size > 6: bridges of `size - 4` on each side except CW180 for non-main sizes; roof when it is the highest same-size tower in its column or has no base. Wings out of `RANGE` 200 or colliding (except with a mushroom roof) are refused; `adjustCoordinates` snaps a wing onto an existing same-size tower within 3 blocks. Door height `FLOOR_HEIGHT + 1 + rand(floors - 1) x 4`; openings at `offset + rand(wLength)` with `wLength = min(size / 3, 3)`. Saves `hasBase`, `isAscender` |
| `gloamingforest:GloamingMTBri` | MushroomTowerBridgeComponent | Bridge of `dSize` length, 3 wide: oak fences at z 0 and 2, floor at z 1 (jungle planks when ascender), walkway cleared; then builds the destination wing at (dSize - 1, 1, 1) (logs "Making tower wing failed when bridge was already made..." on failure). Saves `destSize`, `destHeight` |
| `gloamingforest:GloamingMTMB` | MushroomTowerMainBridgeComponent | 11-long bridge that ends in a new main tower (size 15, snapped with `adjustCoordinates`) |
| `gloamingforest:GloamingMTRoofMush` | TowerRoofMushroomComponent | Mushroom cap: height = wing size, overhang `(int)(height x hang)`, size `height + 2 x overhang`, box from wing maxY + 2 to maxY + height + 1; per layer y radius `(int)(sin((y + height / 1.2) / (height x 2.05) x 3.14) x size / 2)`, hollow `floor(radius x 0.9)` (solid in the top 3 layers), shell and inside both red mushroom block. Hang choice in `makeARoof` (wings): 1.6; if the 1.6 box does **not** collide with another mushroom roof try 1.0, and again 0.6 (Java logic kept: an isolated wing ends with 0.6) |

Golden hash `aurora.mushroom_tower_hash` (R19) from `/gloamingforest place gloamingforest:mushroom_tower` at a fixed
seed.

## 3. Blocks

Sources: `block/AuroraBrickBlock.java`, `block/AuroralizedGlassBlock.java`, `init/GloamingBlocks.java`,
`client/event/ColorHandler.java`, `client/model/block/aurorablock/NoiseVaryingModel*.java`, `util/SimplexNoiseHelper.java`,
`util/ColorUtil.java`.

| Id | Class | Properties | Behaviour |
|---|---|---|---|
| `gloamingforest:aurora_block` | AuroraBrickBlock | strength 10 / 6, map colour ICE, instrument CHIME, no `requiresCorrectToolForDrops`; tags `#minecraft:needs_diamond_tool` (not mineable/pickaxe) | `getDestroyProgress` = **0.1 per tick** (10 ticks to break) when the player has `gloamingforest:progress_glacier`, else vanilla progress for hardness 10. Model `NoiseVaryingModel`: 16 variants `aurora_block_0..15`, variant `calcVariant(pos, 16) = floor(positiveModulo(fractalNoise(3, 48, pos) x 120, 16))`. Tint: `hsvToRGB(rippleFractalNoise(2, 128, pos.above(128), 0.37, 0.67, 1.5), 1, 1)` (item / no level: hue 0.45) |
| `gloamingforest:aurora_pillar` | RotatedPillarBlock (`axis`) | strength 2 / 6, ICE, CHIME, requires correct tool; mineable/pickaxe, needs wood tool | tint = aurora tint converted to HSV, saturation x 0.5, value `min(v + 0.4, 0.9)` |
| `gloamingforest:aurora_slab` | SlabBlock (`type`, `waterlogged`) | strength 2 / 6, ICE, CHIME, requires correct tool; mineable/pickaxe, needs wood tool, `#minecraft:slabs` | pale tint as the pillar |
| `gloamingforest:auroralized_glass` | AuroralizedGlassBlock (TransparentBlock) | copy of `minecraft:glass`; `#c:glass_blocks`; item in `#gloamingforest:wip` (not in the creative tab) | pale tint as the pillar; beacon colour multiplier = full aurora tint at the glass position |

Noise: `rippleFractalNoise(it, size, pos, min, max, freq) = |((fractal(it, size, pos) x freq) mod (2 x (max - min))) -
(max - min)| + min`, `fractal(n) = n == 0 ? 0 : (simplex3((x + n x size) / size, (y + n x size) / size, (z + n x size)
/ size) + 1) x 0.5 + fractal(n - 1)` using upstream's `SimplexNoiseHelper` (Gustavson 3D simplex, public domain; translate
it into `src/core/aurora/` with gtests against values printed by the F13 oracle). `hsvToRGB` as `ColorUtil` (hue wrapped
to [0, 1), sector `(int)(h x 6)`, channels rounded `(int)(c x 255 + 0.5)`).

Block items: `gloamingforest:aurora_block`, `gloamingforest:aurora_pillar`, `gloamingforest:aurora_slab`,
`gloamingforest:auroralized_glass` (block items share the ids; item tint = the block colour with no position, hue 0.45).
Creative tab (F9 catalog): aurora block, pillar and slab in the blocks tab; glass hidden (WIP).

Block loot (F10 import): `gloamingforest:blocks/aurora_block` and `gloamingforest:blocks/aurora_pillar` drop
themselves (survives explosion); `gloamingforest:blocks/aurora_slab` drops 1 (2 for `type=double`, explosion decay);
`gloamingforest:blocks/auroralized_glass` drops itself only with Silk Touch.

## 4. Recipes and recipe advancements

| Recipe | Pattern | Result | Advancement |
|---|---|---|---|
| `gloamingforest:aurora_pillar` (shaped, building) | `#` over `#`, `#` = aurora_block | 2 aurora_pillar | `gloamingforest:recipes/building_blocks/aurora_pillar` (criterion `has_slab`: has aurora_slab, or recipe unlocked) |
| `gloamingforest:wood/aurora_slab_slab` (shaped, building) | `###`, `#` = aurora_block | 6 aurora_slab | `gloamingforest:recipes/building_blocks/wood/aurora_slab_slab` (criterion `has_item`: has aurora_block) |

R4: both are vanilla crafting recipes of U28's own outputs; the recipe-unlock advancements are generated by F1 (R21).

## 5. Mobs

Entity sizes and spawn eggs (`init/GloamingEntities.java`): all MONSTER category.

| Entity | Width x height (eye) | Egg colours |
|---|---|---|
| `gloamingforest:snow_queen` | 0.7 x 2.5 | 0xb1b2d4 / 0x87006e |
| `gloamingforest:snow_guardian` | 0.6 x 1.8 | 0xd3e7bc / 0xfefefe |
| `gloamingforest:stable_ice_core` | 0.8 x 1.8 (eye 1.35) | 0xa1bff3 / 0x7000f8 |
| `gloamingforest:unstable_ice_core` | 0.8 x 1.8 (eye 1.35) | 0x9aacf5 / 0x9b0fa5 |
| `gloamingforest:ice_crystal` | 0.6 x 1.8 | 0xdce9fe / 0xadcafb |
| `gloamingforest:ice_snowball` | 0.25 x 0.25, tracking 150, update 3, no egg | - |

Entity tags (owned by U17 / vanilla, membership noted for parity): `#gloamingforest:bosses` has the Snow Queen; stable /
unstable ice core, snow guardian and ice crystal are in `#minecraft:powder_snow_walkable_mobs`,
`#minecraft:freeze_immune_entity_types` and `#minecraft:fall_damage_immune` (the queen is fall-damage immune too) and in
`#gloamingforest:lifedrain_drops_no_flesh`; `ice_snowball` is in `#minecraft:impact_projectiles`. Snow queen, snow
guardian and both ice cores (not the ice crystal) are criteria of `gloamingforest:gloaming_hunter` (U14, kill any upstream mob
in the Gloaming Forest).

### 5.1 Common ice mob (`entity/monster/BaseIceMob.java`)

Base of stable / unstable ice core and snow guardian (`IceCrystal` extends it too).

- `aiStep`: while airborne and falling, `motion.y x 0.6` (slow fall); client: 3 `gloamingforest:snow_guardian`
  particles per tick at `(xOld + (r - r) x 0.3, yOld + eyeHeight + (r - r) x 0.5, zOld + (r - r) x 0.3)`; in a biome of
  `#minecraft:snow_golem_melts` (vanilla hot biomes + `gloamingforest:oak_savannah`, `gloamingforest:fire_swamp`) each
  of those also emits `minecraft:cloud` (1/4, velocity y 0.1) and `minecraft:falling_water` (1/2), and the server deals
  1 `minecraft:on_fire` damage every 20 ticks ("BURN!!!").
- `canRide` false (cannot be mounted on anything).

### 5.2 `gloamingforest:snow_guardian` (`entity/monster/SnowGuardian.java`)

| Field | Value |
|---|---|
| Attributes | max health 10, movement speed 0.23, attack damage 3 |
| Goals | 0 Float, 1 MeleeAttack 1.0 (no memory), 2 WaterAvoidingRandomStroll 1.0, 3 LookAtPlayer 8, 3 RandomLookAround; targets 1 HurtBy, 2 NearestAttackable Player |
| Sounds | `entity.gloamingforest.snow_guardian.ambient` / `.hurt` / `.death`, pitch `(r - r) x 0.2 + 0.8` |
| Equipment | `finalizeSpawn`: `type = rand(4)`; main hand ironwood / steeleaf / knightmetal / knightmetal sword (types 0..3); chest and head ironwood / steeleaf / knightmetal / arctic chestplate and helmet; then vanilla equipment enchantments by difficulty; default drop chances |
| Particles | 3 extra snow_guardian particles per tick (6 in total with the base) |
| Cluster | max 2 |
| Render | `SnowGuardianRenderer`: no body (noop model), only the armour layer (player inner / outer armour) and held item, bobbing `sin((tick + pt) x 0.2) x 0.15`, shadow 0.25 |
| Loot | `gloamingforest:entities/snow_guardian`: 0-2 snowballs, looting +0-1 |

### 5.3 `gloamingforest:stable_ice_core` (`entity/monster/StableIceCore.java`, RangedAttackMob)

| Field | Value |
|---|---|
| Attributes | max health 20 (monster default), speed 0.23, attack 3 |
| Goals | 0 Float, 1 RangedAttack (speed 1.25, interval 20, range 10), 2 WaterAvoidingRandomStroll 1.0, 3 LookAtPlayer 8, 3 RandomLookAround; targets 1 HurtBy, 2 Player |
| Attack | `IceSnowball` from (x, y + eyeHeight, z); aim at `target eye y - 1.4` plus `horizontalDist x 0.2`; `shoot(dx, dy + f, dz, 1.6, 6.0)`; sound `entity.gloamingforest.ice.shoot` volume 1, pitch `1 / (r x 0.4 + 0.8)` |
| Sounds | `entity.gloamingforest.ice.ambient` / `.hurt` / `.death` |
| Cluster | max 2 |
| Render | `StableIceCoreModel` (= unstable core model with its own texture) `iceshooter.png`, shadow 0.4, bobbing as the guardian |
| Loot | `gloamingforest:entities/stable_ice_core`: 0-2 snowballs, looting +0-1 |

### 5.4 `gloamingforest:unstable_ice_core` (`entity/monster/UnstableIceCore.java`)

| Field | Value |
|---|---|
| Attributes | max health 20, speed 0.23, attack 3 |
| Goals | 0 Float, 1 MeleeAttack 1.0, 2 Stroll, 3 Look, 3 RandomLook; targets HurtBy, Player |
| Death | `tickDeath`: the corpse stays **60 ticks** (3 s); at 60 the server explodes radius 1 (no fire, `ExplosionInteraction.MOB`: block damage only with mob griefing) and, when mob griefing, `transformBlocks`; then the vanilla removal (deathTime faked to 19 so the loot / xp / poof run once) |
| transformBlocks | every offset in the 9 x 9 x 9 cube with `distance < 4 + (r - r) x 2` (a new random per offset): if explosion resistance < 8 and the block is breakable: full-cube shape and (not solid, or leaves, or `minecraft:ice`, or `gloamingforest:aurora_block`) -> stained glass of the dye colour closest to the block's map colour; else, if a redstone conductor -> terracotta of that colour. Closest dye: Manhattan RGB distance to `DyeColor.getTextureDiffuseColor`, start WHITE with best 1024 |
| Sounds | `entity.gloamingforest.ice.*` (ambient / hurt / death) |
| Cluster | max 2 |
| Render | `UnstableIceCoreModel` (16 orbiting spikes `yRot = (tick + pt) / 5` etc., 16 cubes), `iceexploder.png`, shadow 0.4, bobbing; dying: creeper-like swell `(1 + f^4 x 0.4) x (1 + sin(t x 100) x t x 0.01)` and white flash every other 2 ticks with alpha `t x 0.2 x 255`; rotation fixed to `180 - yaw` (no death tilt) |
| Loot | `gloamingforest:entities/unstable_ice_core`: 0-2 snowballs, looting +0-1 |

### 5.5 `gloamingforest:ice_crystal` (`entity/monster/IceCrystal.java`)

| Field | Value |
|---|---|
| Attributes | max health 10, speed 0.23, attack 5 |
| Goals | as the unstable core (melee) |
| Lifetime | `setToDieIn30Seconds()` sets `maxCrystalAge = 600`; server `aiStep` counts `crystalAge` and **discards** (no loot) at 600; crystals spawned otherwise live forever |
| Sounds | `entity.gloamingforest.ice.*` |
| Cluster | max 4 |
| Render | `IceCrystalModel` (16 spikes), `icecrystal.png`, shadow 0.25, bobbing |
| Loot | `gloamingforest:entities/ice_crystal`: 0-2 snowballs, looting +0-1 |

### 5.6 Projectile `gloamingforest:ice_snowball` (`entity/projectile/IceSnowball.java`, GloamingThrowable)

Gravity 0.006; trail 2 `minecraft:item_snowball` (F8 `makeTrail`) per tick; on hitting a living entity 2 damage of
`gloamingforest:snowball_fight` (indirect: the snowball, owner = shooter); any hit or any damage taken kills it:
entity event 3 = 8 `item_snowball` particles, then discard. Renders as a thrown `minecraft:snowball` item. Registered
with F8 `projectile::registerType` (gravity 0.006, drag vanilla throwable 0.99).

### 5.7 Boss `gloamingforest:snow_queen` (`entity/boss/SnowQueen.java`, `SnowQueenIceShield.java`, F8 boss kit)

| Field | Value |
|---|---|
| Attributes | max health 200, movement speed 0.23, flying speed 0.23, attack damage 7, follow range 40, knockback resistance 0.75 |
| XP | 317 |
| Movement | `FlyingMoveControl(10, hovers = true)`, no gravity; every tick before `super.tick` motion y -= 0.05 (the hover goals push up) |
| Pushable | false |
| Goals | 1 `HoverSummonGoal`, 2 `HoverThenDropGoal(80, 20)`, 3 `HoverBeamGoal(80, 100)`, 5 AttemptToGoHome 1.25 (F8 restriction goal), 6 MeleeAttack 1.0 (memory), 8 LookAtPlayer 8, 8 RandomLookAround; targets 1 HurtBy, 2 NearestAttackable Player (must see) |
| Synced data | `BEAM_FLAG` (breathing), `PHASE_FLAG` (byte phase ordinal); phase, summons, drops and beam damage are **not saved**: a reloaded queen restarts in SUMMON with 6 summons (the constructor calls `setCurrentPhase(SUMMON)`) |
| Home | restriction point radius 20 in `gloamingforest:aurora_palace` (F8 `setHome`; F6 home structure) |
| Boss kit | `boss::registerBoss{entityId snow_queen, homeStructure aurora_palace, lootTable gloamingforest:entities/snow_queen, deathChestBlock gloaming_oak_chest, bar rgb 0x8CF0F0}`; spawner `boss::registerSpawner{blockId gloamingforest:snow_queen_boss_spawner (U5 block), entityId snow_queen, range 9, particle gloamingforest:snow}` with the extra condition: the nearest player within 9 must be above `spawnerY - 4` |
| Loot | `gloamingforest:entities/snow_queen`: pool 1 (`gloamingforest:multiplayer_rolls`, default 1, per player uniform -2..1) triple_bow / seeker_bow; pool 2 rolls 1-4: 7 packed ice (+0-1 looting); pool 3 rolls 2-5: 16 snowballs; pool 4: `gloamingforest:snow_queen_trophy` |
| Progression | kill / trophy / hurt grants `gloamingforest:progress_glacier` (U14 criteria `queen`, `trophy`, `was_in_fight` + previous `progress_yeti`); which is also part of `progress_merge` |

**Phases** (`Phase` SUMMON, DROP, BEAM; `customServerAiStep` checks every tick):

- `setCurrentPhase(SUMMON)`: `summonsRemaining = MAX_SUMMONS` (6). SUMMON -> DROP when `summonsRemaining == 0` and
  `countMyMinions() <= 0` (ice crystals in the queen's 1 x 1 x 1 box inflated by 32 / 16 / 32).
- `setCurrentPhase(DROP)`: `successfulDrops = 0`, `maxDrops = 2 + rand(3)`. DROP -> BEAM when `successfulDrops >= maxDrops`.
- `setCurrentPhase(BEAM)`: `damageWhileBeaming = 0`. BEAM -> SUMMON when `damageWhileBeaming >= 25`
  (`MAX_DAMAGE_WHILE_BEAMING`; `hurt` adds `(int) damage` of every successful hit while in BEAM).

**Hover goals** (`entity/ai/goal/HoverBaseGoal.java` + subclasses). `makeNewHoverSpot(target)`: up to 100 tries of
`(target.x + (r - r) x radius, target.y + height, target.z + (r - r) x radius)` that is unobstructed with no collision
for the queen's box and with line of sight; else the last try (debug "Found no spots, giving up").

| Goal | Height / radius | Behaviour |
|---|---|---|
| HoverSummonGoal | 6 / 6 | Use: target alive, phase SUMMON, line of sight. Every tick: seek timer +1 (stops after 80); push towards the spot (`offset / dist x (0.05, 0.1, 0.05)`, y + 0.05), look at target 30 / 30. Within distance^2 3 of the spot: if summons remain and fewer than 4 minions (`MAX_MINIONS_AT_ONCE`) -> `summonMinionAt(target)`; new spot |
| HoverThenDropGoal | 6 / 0 (80 hover, 20 drop) | Use: phase DROP. Seek to the spot (max 80 ticks, push as the summon goal, y + 0.05, look 30 / 30); `canContinueToUse` counts hover ticks while within distance^2 1 of the spot; after 80 hover ticks drop: no push (gravity -0.05), while above `dropY = target.y - 1` destroy `#minecraft:ice` blocks in the box inflated (1, 0.5, 1) (mob griefing only, no drops); after 20 drop ticks `incrementSuccessfulDrops()` and stop |
| HoverBeamGoal | 3 / 4 (80 hover, 100 beam) | Use: phase BEAM, target alive. Seek (max 80 ticks), in position at distance^2 <= 1; after 80 hover ticks: beam timer +1, `setBreathing(true)`, ray attack, `hoverPosY -= 0.05` down to `beamY` = target y at spot choice; push towards the spot when farther than 0.5 (`+0.045` gravity offset); look at target with tracking 1 (in position) or 20. Ray: from (x, y + 0.25, z) along the view vector, range 20; candidates in the box moved 10 along the look and inflated 20; every pickable non-part entity whose box (inflated by its pick radius) contains the source or is clipped by the ray, nearer than the previous hit, takes `doBreathAttack`: 4 `gloamingforest:chilling_breath` damage (U17 damage type, attributed to the queen). Stop resets the timers and breathing |

`summonMinionAt(target)`: a new `gloamingforest:ice_crystal` at the queen, then up to 100 `randomTeleport` tries at
`home + gaussian x (3, 2, 3)` (when the home point is valid in this dimension) or `target + gaussian x (6, 8, 6)`; game
event ENTITY_PLACE; the crystal targets the queen's target and dies in 30 seconds (600 ticks); `summonsRemaining--`.

**Melee / contact**: `doHurtTarget` uses `gloamingforest:squish` (U6 damage type, attributed to the queen) during DROP,
else the mob attack (7); applied with `EntityUtil.properlyApplyCustomDamageSource` (F10 `combat::hurt` with the
attacker's damage and enchantments).

**Ice shields** (`SnowQueenIceShield extends GloamingPart`, 7 parts, 0.75 x 0.75): parts 0..5 orbit at angle
`60 x i + tickCount x 5` degrees, distance 1, y + 0.1 (`getShieldYOffset`); part 6 sits under the queen at y - 1; yaw =
the angle. Server: each tick every part collides with pushable entities in its box deflated by 0.2: `push(part)`; a
living entity that `doHurtTarget` hurts is lifted `+0.4` y and the queen plays `entity.gloamingforest.snow_queen.attack`
(1, 1). Part `hurt`: an arrow with pierce level > 0 passes (returns true, no effect); anything else is blocked
(returns false) with `entity.gloamingforest.snow_queen.break` volume 1, pitch `((r - r) x 0.7 + 1) x 2`. Shields
render as `minecraft:packed_ice` block models (`SnowQueenIceShieldRenderer`, `BakedMultiPartRenderers`).

**Client effects** (`aiStep` on the client): 3 `gloamingforest:snow_guardian` particles per tick around the eyes (as
the ice mobs); in DROP one `gloamingforest:snow_warning` per shield at `(r - r) x 0.5` around it; while breathing and
alive 10 `gloamingforest:ice_beam` per tick from `(x + look.x x 0.5, y + 1.7 + look.y x 0.5, z + look.z x 0.5)` with
velocity `(look.x + g x 0.0075 x spread, g x 0.0075 x spread, look.z + g x 0.0075 x spread) x (2 + r x 0.15)`,
`spread = 2 + r x 2.5`. Death (deathTime > 0): 5 particles per tick, `minecraft:explosion` or `minecraft:poof` 50/50,
random in the box doubled in width, gaussian x 0.02 velocity.

**Render** (`SnowQueenRenderer`, `SnowQueenModel`): humanoid model, texture `snowqueen.png`, scale 1.2, shadow 0.625;
BEAM phase: breathing -> both arms forward (`xRot = -pi/2` with the attack swing offsets and `bobArms`), not breathing
-> both arms raised (`xRot += pi`). F2 converts `SnowQueenModel`, `IceCrystalModel`, `UnstableIceCoreModel`,
`StableIceCoreModel` into `assets/gen/geo/aurora/**`.

**Sounds**: `entity.gloamingforest.snow_queen.ambient` / `.hurt` / `.death` (default pitch), `.attack`, `.break`.

## 6. Loot tables (chests)

| Table | Pools |
|---|---|
| `gloamingforest:aurora_cache` (wing treasure floors) | 4 rolls: useless table (weight 25), stick / coal / arrow 1-12, ice / packed ice 1-4, ironwood ingot 1-2, maze wafer 1-9 (each 75); 2 rolls: charm_of_keeping_1, ironwood ingot 1-6, firefly 1-5, aurora_block 1-12, arctic fur 1-3; 1 roll: arctic fur 1-8, ice_bow, ender_bow, ice_sword (75 each), enchanted books sharpness IV / power IV / punch II / unbreaking II / infinity (25 each), a book enchanted randomly with `gloamingforest:chill_aura` (25) |
| `gloamingforest:aurora_room` (dead-end top floor) | 4 rolls: maze wafer 1-12, ice bomb 1-4, firefly 1-5, ice / packed ice 1-4; 2 rolls: arctic fur 1-6, arctic helmet / chestplate / leggings / boots, knightmetal chestplate, knightmetal sword, charm_of_life_1; 1 roll: ice_bow (levels 30), ender_bow (5), ice_sword (25), chill_aura book, glass_sword (20) (`enchant_with_levels`, `#minecraft:on_random_loot`) |

R5: both are chest tables of the unit that places the chest (U28). Entity tables are in section 5.

## 7. Damage types (owned by U28)

| Id | message_id | Effects | Scaling | Exhaustion | Tags | Users |
|---|---|---|---|---|---|---|
| `gloamingforest:frozen` | `gloamingforest.frozen` | freezing | when caused by living non-player | 0.1 | `#neoforge:is_magic`, `#minecraft:bypasses_wolf_armor` | ice bomb (`gloamingforest:thrown_ice`, U8; "%1$s was frozen by %2$s using an Ice Bomb", `.item` variant) |
| `gloamingforest:snowball_fight` | `gloamingforest.snowballFight` | freezing | when caused by living non-player | 0.1 | `#minecraft:is_projectile`, `#neoforge:is_magic` | ice snowball ("%1$s lost a snowball fight to %2$s", `.item` variant) |

Death messages are F10's lang keys (R9); F10 pre-registers both types from import, U28 only uses them by id.

## 8. Particles (owned by U28)

All use the sprite set `gloamingforest:snow_0..3` (imported textures), opaque sheet, full bright (light 240 / 240),
physics on. Client mod: F12 `particles::registerNative`; vanilla clients: a Bedrock particle JSON with the same
lifetime / drag approximations (`assets/pack/rp/particles/aurora/`).

| Id | Class | Behaviour | Emitted by |
|---|---|---|---|
| `gloamingforest:snow` | `SnowParticle` | initial velocity `0.1 x random + 0.4 x v`, white, size x 0.75, lifetime `(int)(6 / (rand x 0.8 + 0.6))`; tick: motion x 0.7, y -= 0.02, ground x 0.7 | Snow Queen spawner (U28 spawner spec), sinister spawner (U22), Alpha Yeti (U27) |
| `gloamingforest:snow_warning` | `SnowWarningParticle.SimpleFactory` | snow particle, lifetime 50, extra y -= 0.02 per tick | Snow Queen DROP phase; trophy effects (U5, by id) |
| `gloamingforest:extended_snow_warning` | `SnowWarningParticle.ExtendedFactory` | as snow_warning, lifetime 100 | registered only (no emitter in upstream 4.8); keep the id |
| `gloamingforest:snow_guardian` | `SnowGuardianParticle` | snow particle, scale 0.75, lifetime `10 + rand(15)`, grey level `0.75 + rand x 0.25` | ice mobs, Snow Queen |
| `gloamingforest:ice_beam` | `IceBeamParticle` | scale 0.75, lifetime 50, drag 0.96, no gravity, ground x 0.7 | Snow Queen beam |

## 9. Map decoration

`gloamingforest:aurora_palace` (manual): the structure icon of the palace on upstream maps (`GloamingMapDecorations`), drawn by the
conquered-aware renderer (`ConqueredMapIconRenderer`: crossed out once conquered); U13 draws it through F12
`maps::setIcon` with U28's texture id.

## 10. Tags (owned by U28)

| Tag | Members | Consumer |
|---|---|---|
| `block/#gloamingforest:ice_bomb_replaceables` | `gloamingforest:mayapple`, `gloamingforest:fiddlehead`, `minecraft:short_grass`, `minecraft:tall_grass`, `minecraft:fern`, `minecraft:large_fern`, `#minecraft:flowers` | ice bomb freezing (`IceBomb`, U8): a member in the freeze area becomes a snow layer when snow can survive there, else air |
| `block/#gloamingforest:incorrect_for_ice_tool` | `#minecraft:incorrect_for_wooden_tool` | `GloamingToolMaterials.ICE` tier (ice sword, U8): the ice tier mines like wood |

## 11. Sound events (owned by U28)

All synthesized by F3 from `tools/sounds/recipes/aurora.json` (never from upstream audio); upstream's vanilla-event references are
mapped to the Bedrock vanilla sound with the same role.

| Event | Subtitle (key = `subtitles.gloamingforest.entity.<path>`) | upstream source |
|---|---|---|
| `entity.gloamingforest.ice.ambient` | "Ice Core crackles" | ice crackle x 2 |
| `entity.gloamingforest.ice.death` | "Ice Core dies" | ice death x 2 |
| `entity.gloamingforest.ice.hurt` | "Ice Core hurts" | ice hurt x 2 |
| `entity.gloamingforest.ice.shoot` | "Ice Core shoots snowball" | event `entity.snowball.throw` |
| `entity.gloamingforest.snow_guardian.ambient` | "Snow Guardian crackles" | ice crackle x 2 |
| `entity.gloamingforest.snow_guardian.death` | "Snow Guardian dies" | ice death x 2 |
| `entity.gloamingforest.snow_guardian.hurt` | "Snow Guardian hurts" | ice hurt x 2 |
| `entity.gloamingforest.snow_queen.ambient` | "Snow Queen crackles" | ice crackle x 2 |
| `entity.gloamingforest.snow_queen.attack` | "Snow Queen attacks" | event `entity.iron_golem.attack` |
| `entity.gloamingforest.snow_queen.break` | "Snow Queen deflects attack" | event `entity.item.break` |
| `entity.gloamingforest.snow_queen.death` | "Snow Queen dies" | ice death x 2 |
| `entity.gloamingforest.snow_queen.hurt` | "Snow Queen hurts" | ice hurt x 2 |

## 12. Spawn eggs and placements

Eggs (F1 skeleton, colours in section 5): `gloamingforest:snow_queen_spawn_egg`, `gloamingforest:snow_guardian_spawn_egg`,
`gloamingforest:stable_ice_core_spawn_egg`, `gloamingforest:unstable_ice_core_spawn_egg`,
`gloamingforest:ice_crystal_spawn_egg`. Placements (`events/RegistrationEvents.java`, F5 `spawn::registerPlacement`):
snow_guardian, stable_ice_core, unstable_ice_core, snow_queen and ice_crystal = ON_GROUND,
`MOTION_BLOCKING_NO_LEAVES`, `Monster::checkMonsterSpawnRules` (REPLACE). Natural spawns only through the palace's
controlled spawns (section 1).

## 13. Bedrock adaptation notes

- Aurora tint: client mod = F12 `tint::registerBlock` for the four blocks with the exact noise; the 16 noise variants
  of `aurora_block` are material-instance swaps chosen by the same `calcVariant` in the block renderer. Vanilla
  clients: no per-position tint for custom blocks; they see the untinted variant 0 texture multiplied by the item tint
  (hue 0.45) baked into a `upstream-derived` texture (listed in the README deviations).
- `aurora_block` break speed: Bedrock computes destroy time client side; with the client mod the companion applies the
  0.1 per tick progress for players with `progress_glacier` (`progression::client::has`); for vanilla players the
  server uses `interactions::onStartDestroy` + a mining-speed effect equivalent (F9 helper) so the 10-tick break holds.
- Snow Queen: F8 multipart (`entity::addPart` x 7, 0.75 boxes, offset functions from section 5.7) with
  `entity::setMoveControl(Mob&, flying)` and no gravity; phases kept in `entity::data` and synced with
  `entity::setProperty(snow_queen, "gloamingforest:phase", 0..2)` + `"gloamingforest:beam"` for animations (vanilla
  clients animate from the properties too). Pierce arrows: query the arrow's pierce level through F10's projectile
  info.
- Unstable ice core: Bedrock removes dead mobs after 20 ticks; F8 `Behaviour.die` keeps the body for 60 ticks
  (invulnerable, AI off, flashing property), then explodes and removes it with loot. Map colours for
  `transformBlocks` come from a generated vanilla block -> map colour table (F13 oracle) since Bedrock map colours
  differ.
- Snow Guardian invisible body: geometry with only armour / item bones; vanilla clients render the armour through
  the vanilla armour layer.
- Mushroom tower: registered with F6 `registerGenerator` but no structure set / placement; `/gloamingforest place`
  and the selftest are the only ways to build it (as upstream).

## 14. Acceptance (selftests `aurora.*`, dx 2800-2899)

Per the unit record: golden hashes of the aurora palace (`aurora.palace_hash`) and the mushroom tower
(`aurora.mushroom_tower_hash`); Snow Queen fight: spawner at (7, 34, 7) of the boss wing spawns her only with a player
above y - 4 within 9; SUMMON spawns up to 6 ice crystals (at most 4 alive) that vanish after 600 ticks; DROP breaks
ice and lands 2-4 drops; BEAM deals 4 chilling_breath per hit and ends after 25 damage; shields block a sword hit (break
sound) but let a piercing arrow through and lift a touching player by 0.4; death places the gloaming oak death chest
and grants `progress_glacier`; unstable ice core explodes 60 ticks after death and turns a stone wall into terracotta
and ice into stained glass; stable ice core snowball deals 2 `snowball_fight`; ice mobs burn in a fire swamp; aurora
block breaks in 10 ticks with `progress_glacier`; client screenshot of the aurora tint on palace walls.
