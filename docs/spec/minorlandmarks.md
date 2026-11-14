# U18 `minorlandmarks`: Minor landmarks and decorative features

Behaviour spec translated from the upstream 4.8 source (commit `9e40a757`). Checklist for the
U18 worker and the W2 audit (A1). Numbers are upstream's; "Bedrock:" paragraphs mark adaptation notes. The appendix lists
every owned configured feature, placed feature and loot table in compact form.

| | |
|---|---|
| Unit | U18 `minorlandmarks` (W1 batch 2) |
| Selftests | `minorlandmarks.*`, testPos dx `1800-1899` (owners rows `selftest minorlandmarks.*`, `selftest_dx 1800-1899`) |
| World DB | `gf:minorlandmarks:*` |
| Net channels | `minorlandmarks.*` |
| Config | none |
| Contracts | landmark (F6), structure + bases HollowHill / Maze (F7), features (F5), loot (F10), spawn (F5) |
| Java sources | `block/HedgeBlock.java`, `world/components/structures/{HollowHillComponent,HedgeMazeComponent,TFMaze}.java`, `world/components/structures/type/{HollowHillStructure,HedgeMazeStructure,HollowTreeStructure,FallenTrunkStructure}.java`, `world/components/structures/hollowtree/*.java`, `world/components/structures/fallentrunk/*.java`, `world/components/feature/{BlockSpikeFeature,DarkForestFeature,FallenHollowLogFeature,SmallFallenLogFeature,FoundationFeature,LampostFeature,MonolithFeature,CheckAbovePatchFeature,TFSmallLakeFeature,WebFeature,WoodRootFeature}.java`, `world/components/feature/trees/HollowStumpFeature.java`, `init/{TFConfiguredFeatures,TFPlacedFeatures,TFStructures,TFStructureSets,TFMapDecorations}.java` |

Not owned here: the landmark grid placement types and `avoid_landmark_grid` (F6), the TFMaze / HollowHill base
classes and the speleothem registry (F7), the trunk logs, hollow logs, moss patch, fireflies, cicadas, roots, liveroot,
canopy fence, jack-o'-lanterns usage (blocks of U4/U6), hedge spiders, swarm spiders, hostile wolves, redcaps, kobolds,
wraiths, beetles (U16/U17), ravens (U16).

## 1. Block `gloamingforest:hedge`

Map colour PLANT, push DESTROY, grass sound, strength 2.0 / 6.0; block item `gloamingforest:hedge`, loot
`gloamingforest:blocks/hedge` (itself; see appendix). Collision box 0..0.9375 high (so entities touch it).
- Contact damage 3 (`minecraft:cactus` damage source) when an entity is inside / steps on it, unless the entity is a
  spider (any `Spider` subclass), an item entity, ignores block triggers, or rides something exempt.
- Pathfinding: `DANGER_OTHER` for mobs it would hurt.
- Left-clicking (attack) hurts the player 3 and schedules a tick in 10; each tick, players within 8 that are swinging
  and looking at this block take 3 more and the tick is rescheduled (keeps hurting while you keep punching).
- Breaking it hurts the player 3.
- Lang: `block.gloamingforest.hedge` "Hedge".

Bedrock: entity-inside / step-on through `blockevents::onEntityInside` / `onSteppedOn` (F11); the punch damage through
`interactions::onStartDestroy` with a 10-tick repeat while the player keeps mining (no swing state on Bedrock).

## 2. Hollow hills (`gloamingforest:small_hollow_hill`, `gloamingforest:medium_hollow_hill`, `gloamingforest:large_hollow_hill`)

Structure type `gloamingforest:hollow_hill` (`HollowHillStructure`, codec field `hill_size` 1..3, controlled spawns,
speleothem config), piece `gloamingforest:TFHill` (`HollowHillComponent`, F7 base `bases/HollowHill.h`). Structure sets
`gloamingforest:small_hollow_hill`, `gloamingforest:medium_hollow_hill`, `gloamingforest:large_hollow_hill`: landmark grid
with their own grid lock. Biomes `#gloamingforest:valid_hollow_hill_biomes`; step `surface_structures`; terrain
adaptation none; centre in chunk.

| | Small (size 1) | Medium (2) | Large (3) |
|---|---|---|---|
| first piece offset | (x-3, y-2, z-3) | (x-7, y-5, z-7) | (x-11, y-5, z-11) |
| radius = `(2s+1)*8 - 6` | 18 | 34 | 50 |
| `hdiam = (2s+1)*16` | 48 | 80 | 112 |
| decoration clearance | radius 1, surface decorations allowed, no underground, vegetation allowed, elevation not adjusted | radius 2 | radius 3 |
| controlled spawns (label 0) | spider w10 4, zombie w10 4, redcap w10 4, swarm spider w10 4, kobold w10 4-8 | redcap w10 1-2, redcap sapper w2 1-2, kobold w10 2-4, skeleton w10 2-3, swarm spider w10 2-4, spider w10 1-3, creeper w5 1-2, fire beetle w5 1, slime beetle w5 1, witch w1 1 | redcap w10 2-4, sapper w2 1-2, skeleton w10 2-3, cave spider w10 1-2, creeper w10 1, enderman w1 1, wraith w2 1-2, fire / slime / pinch beetle w10 1-2, witch w1 1 |
| spawn shape | `canSpawnMob`: normalised `(hx, hy, hz)` in the start box (x/z mapped to -1..1, y 0..1) with length < 0.975 |
| chest loot | `gloamingforest:hill_1` | `gloamingforest:hill_2` | `gloamingforest:hill_3` |
| spawner mobs (`nextInt(10)`) | 3-5 spider, 6-7 zombie, 8 silverfish, 9 redcap, else swarm spider | 3-5 zombie, 6-7 skeleton, 8 swarm spider, 9 cave spider, else redcap | `nextInt(11)`: 0 slime beetle, 1 fire beetle, 2 pinch beetle, 3-5 skeleton, 6-8 cave spider, 9 creeper, else wraith |
| map icon | `gloamingforest:small_hollow_hill` | `gloamingforest:medium_hollow_hill` | `gloamingforest:large_hollow_hill` |
| speleothem config | `small_hill` | `medium_hill` | `large_hill` (F7 `structure_speleothem_settings`) |

Component (`HollowHillComponent`, translate exactly):
- Box `(-r, -(3 + s), -r, 2r, r / (s == 1 ? 2 : s), 2r)`, orientation SOUTH.
- `postProcess`: drain water (radius `16.5 s`, depth `3s + 2` below sea level: water -> cave air, dirt/sand under the
  floor -> stone); then for every lattice position of the speleothem config within `r^2 * 0.85` (`setFeatures`),
  in this order, all on the one piece `rand`:
  1. reseed `rand.setSeed(rand.nextLong() ^ pos.asLong())`;
  2. ceiling feature first (`placeCeilingFeature`): if `speleothemConfig.shouldDoAStalactite(rand)`
     (`rand.nextFloat() < stalactiteChance`), `getStalactite(rand)` and a hanging spike (`startSpike`, no height cap)
     at `ceilingY = ceil(cos(d / hdiam * PI) * hdiam / 4)`; otherwise nothing more is drawn for the ceiling;
  3. then the floor feature (`placeFloorFeature`) at `floorY = floor(2s - cos(d/hdiam*PI) * hdiam/20 + 1 + 0.25)`:
     one `floatChance = rand.nextFloat()`; `< 0.025` spawner (`getMobID(rand)`), `< 0.05` treasure chest (both moved
     by `sqrt(2)` in a random direction drawn after `floatChance`, set at `floorY + 1` on two cobblestone blocks at
     `floorY` and `floorY - 1`); else, if `speleothemConfig.shouldDoAStalagmite(rand)` (`rand.nextFloat() <
     stalagmiteChance`; this roll consumes RNG and can fail, leaving the point bare), `getStalagmite(rand)` and a
     standing spike at `floorY` capped to `ceilingY - floorY + 4`.
- These formulas are `float` with `Mth` (definitions in terrain.md section 3; bit-exact for the volume-hash goldens).
  - `radius = (2s + 1) * 8 - 6` and `hdiam = (2s + 1) * 16` are ints.
  - `distSq = dx*dx + dz*dz`, with `dx = (float)(x - cx) - 0.5f` (`getDistSqFromCenter`, float); `shortenedRadiusSq =
    radius * radius * 0.85f`.
  - `ceilingY = getWorldY(Mth.ceil(Mth.cos(Mth.sqrt(distSq) / hdiam * Mth.PI) * (hdiam / 4f)))` and `floorY =
    getWorldY(Mth.floor((2s) - Mth.cos(dist / hdiam * Mth.PI) * (hdiam / 20f) + 1 + 0.25f))`, where `dist =
    Mth.sqrt(distSq)` and `getWorldY` is the piece-relative to world y conversion of `StructurePiece` (F7 base). Both
    use the table cosine.
  - Chest offset: `angle = rand.nextFloat() * Mth.TWO_PI`, then `x + Math.round(Mth.cos(angle) * Mth.SQRT_OF_TWO)`
    (likewise z with `Mth.sin`). Java's float `Math.round` returns the nearest int with ties toward positive
    infinity. C's `roundf` rounds ties away from zero, so it is not a substitute.
- Terraformer (`getStructureTerraformer`): `radius = (4s + 0.8) * 8`, inner `radius - 8`; mound
  `HollowHillFunction(centre at minY + 10, radius, 0.7).clamp(0,1)`; inner ceiling `-HollowHill(minY + 6, inner,
  0.675)`; inner floor `HollowHill(minY + s + s/2, 2 - inner, 0.1)`; interior `max(ceiling, floor)` masked by
  `Focused(minY, inner * 0.52, -inner, 1)`; result `8 * min(mound, max(mask, interior))` times a masking sphere
  `Focused(centre - ceil(radius * 0.1), width/2 + 5, width/4, 0).clamp(0,1)` (density types from U2).

Loot (appendix): `gloamingforest:hill_1`, `gloamingforest:hill_2`, `gloamingforest:hill_3`.

## 3. Hedge maze `gloamingforest:hedge_maze`

Structure type `gloamingforest:hedge_maze` (`HedgeMazeStructure`), piece `gloamingforest:TFHedge`
(`HedgeMazeComponent`). Structure set `gloamingforest:hedge_maze` (landmark grid). Biomes
`#gloamingforest:valid_hedge_maze_biomes`; step surface; terrain BEARD_BOX; decoration clearance radius 2, no
surface decorations, underground and vegetation allowed, elevation adjusted; map icon `gloamingforest:hedge_maze`.
First piece at `(x + 1, y + 4, z + 1)`; height `adjustForTerrain(24, 4)`; terraformer inverted pyramid (0, 4).

Component: `MSIZE` 16 cells of 3 blocks, `RADIUS = 16/2*3 + 1 = 25`, diameter 50; box `(-25, -3, -25, 50, 10, 50)`.
`postProcess`: seed `worldSeed + minX * minZ`; TFMaze 16x16 with odd bias 2, torch block `gloamingforest:firefly`,
wall `gloamingforest:hedge`, type 4, tall 3, roots 3; grass floor at y -1 over 0..50; 8 jack-o'-lanterns at the four
entrances (x 0/50 at z 24, 29; z 0/50 at x 24, 29) facing inward; until every cell is non-zero: reset, carve
`16/3 = 5` 1-cell rooms (not at (1,1), at least 3 cells apart), recursive backtracker from (0,0); add 4 exits; copy to
the world at (1, 0, 1). Each room (centre `cell * 3 + 3`, RNG `seed ^ x + z`): a jack-o'-lantern (random facing),
25 % a second one, a spawner (`nextInt(3)`: 1 swarm spider, 2 hostile wolf, else hedge spider) and a chest
`gloamingforest:hedge_maze`, 25 % a second chest `gloamingforest:hedge_cloth`; positions random within 8 of the
centre.

## 4. Hollow trees `gloamingforest:hollow_tree`, `gloamingforest:swamp_hollow_tree`

Structure type `gloamingforest:hollow_tree` (`HollowTreeStructure`, not a landmark; also started by the hollow oak
sapling, trees.md 2). Structure set `gloamingforest:hollow_tree`: `gloamingforest:avoid_landmark_grid` frequency 0.5,
salt 34481210, spacing 7, separation 5, triangular; both structures weight 1.

| Field | `hollow_tree` | `swamp_hollow_tree` |
|---|---|---|
| biomes | `#gloamingforest:valid_hollow_tree_biomes` | `gloamingforest:swamp` |
| allow in water | false | true |
| height | uniform 32..95 | same |
| radius | uniform 1..4 | same |
| log / wood / leaves | gloaming oak log / wood / leaves | same |
| root | `gloamingforest:root` | same |
| vine, bug | vine; firefly or cicada (1:1) | same |
| leaf dungeon | gloaming oak wood shell, air inside, chest facing west with `gloamingforest:tree_cache`, `gloamingforest:swarm_spider` spawner | same |
| decoration clearance | radius 0.5, no surface decorations, underground and vegetation allowed, elevation adjusted | same |

`findGenerationPoint`: chunk RNG `seed + cx * 25117 + cz * 151121`; random x/z in the chunk; height capped to the build
limit and at least 16; refuses water (sea floor below the surface) unless allowed; noise biome must match; box
`(x - r - 1, y, z - r - 1)..(x + r + 1, y + height, z + r + 1)`. Sapling: `checkSaplingClearance` requires every block in
x/z +-3, y -3..+128 (except the sapling) to be tree-replaceable.

Pieces (translate exactly; `HollowTreePiece` base):
- `gloamingforest:TFHTTr` (HollowTreeTrunk): hollow trunk of the radius; `nextInt(3) + 3` with an inclusive loop =
  4..6 **small** branches (length 4, angle 0.35, leafy) at `height * rand * 0.9 + height / 10`; then `buildFullCrown`
  adds the large, medium and small crown branches and roots (counts from its min/max branch arguments, inclusive
  loops); vines and bugs on the trunk.
- `gloamingforest:TFHTLB` (HollowTreeLargeBranch): 1/8 (`LEAF_DUNGEON_CHANCE` 8) a leaf dungeon at the tip;
  `nextInt(length/6) + length/8 + 1` medium branches (inclusive loop); 5 reinforcement logs (`i <= 4`).
- `gloamingforest:TFHTMB` (HollowTreeMedBranch): `min(nextInt(3) + 1, length / 5)` shoots and leaf balls radius 2 or 3.
- `gloamingforest:TFHTSB` (HollowTreeSmallBranch): leaf ball radius 1-2 at the tip.
- `gloamingforest:TFHTRo` (HollowTreeRoot): root strands into the ground.
- `gloamingforest:TFHTLD` (HollowTreeLeafDungeon): radius 4 sphere of wood + leaves, hollow, chest with the dungeon loot
  table (random facing) and a spawner of the dungeon monster at the centre.

Loot: `gloamingforest:tree_cache` (appendix).

## 5. Fallen trunks `gloamingforest:fallen_trunk`

Structure type `gloamingforest:fallen_trunk` (`FallenTrunkStructure`), piece `gloamingforest:TFFallenTrunk`
(`FallenTrunkPiece`, with `Hole` and `TrunkUnderDensityFunction`). Structure set `gloamingforest:fallen_trunk`:
`gloamingforest:avoid_landmark_grid` frequency 0.8, salt 1275623845, spacing 7, separation 5, triangular, exclusion zone
1 chunk around `gloamingforest:hollow_tree`. Biomes `#gloamingforest:valid_hollow_tree_biomes`; step surface; no terrain
adaptation (custom terraformer).

- Config: length uniform 17..24, big trunk length 22..28, log `gloamingforest:gloaming_oak_log`, chest loot
  `gloamingforest:fallen_trunk_loot`.
- Generation point: chunk RNG `seed + cx * 14413411 + cz * 43387781`; radius one of 1, 2, 4 (equal); length (big for
  radius 4); refuses when the noise biome is invalid or any invalid biome lies within `length.max` horizontally;
  random horizontal orientation; box `xySize = radius > 1 ? 2r + 1 : 4`, z length - 1; radius > 1 trunks adjust their
  height to the terrain (`adjustForTerrain(box, 4, WORLD_SURFACE_WG)`).
- Children: two `UtilityPiece`s (box inflated 10 for terraforming, inflated 3 for feature clearance; F7
  `gloamingforest:TFUtilityPiece`).
- `postProcess` (RNG from the position): radius 1 -> small trunk (50 % hole); radius 2 -> trunk (50 % hole); radius 4
  -> trunk without hole, 50 % with a spawner at (r, 2, (length-1)/2) (swarm spider, hostile wolf or cave spider) and one
  `gloamingforest:gloaming_oak_chest` with `gloamingforest:fallen_trunk_loot` at a random offset beside it. Logs are
  placed (axis Z) where the world block is replaceable-by-trees, a flower, empty, or 50 %; eroded ends 2 blocks long;
  44 % moss patch (`gloamingforest:moss_patch`) on top of each placed log.
- Terraformer: beard-thin rigid box plus mounds under the trunk (`TrunkUnderDensityFunction`, more for big trunks).

## 6. Feature types (owned)

| Id | Java class | Behaviour summary (translate exactly) |
|---|---|---|
| `gloamingforest:block_spike` | BlockSpikeFeature | stalactite/stalagmite of a weighted block list: length `between(max * sizeVariation, max)`, clearance scan, base diameter `length / 4.5`, per column length `nextInt(length / (dist + 0.25))` with octagonal distance; also used by hollow hills and caves (`startSpike`). |
| `gloamingforest:dark_forest_placer` | DarkForestFeature (RandomPatchConfiguration) | finds the ground by scanning down to sea level, then `tries` attempts within `xz_spread` / `y_spread` (`nextInt(s+1) - nextInt(s+1)`) placing the inner feature (dark forest mushrooms, ferns, grass, bushes, pumpkins). |
| `gloamingforest:fallen_hollow_log` | FallenHollowLogFeature | 4x4 hollow gloaming oak log along X or Z (50/50) with jagged ends (`nextInt(3)`), moss patches, persistent leaves, grass and fireflies; needs flat ground. |
| `gloamingforest:fallen_small_log` | SmallFallenLogFeature (HollowLogConfig `normal`, `hollow`) | length 3-6 along X or Z; 50 % all hollow; 1/5 refuses / sinks in liquid; 2/3 of positions get mushrooms or moss; one branch 2/3. |
| `gloamingforest:foundation` | FoundationFeature (RuinedFoundationConfig) | ruined stone foundation: walls of sampled width/height with decay (`nextInt(decayRarity + 1) >= 1 ? main : decay`), optional basement of sampled depth with a floor (`placeFloorTest`), posts (oak fence) and a loot container (chest) at a random edge coordinate with `gloamingforest:foundation_basement` (seed `worldSeed * x + y ^ z`); basement depth 3 or none (50/50). |
| `gloamingforest:hollow_stump` | HollowStumpFeature (TFTreeFeatureConfig) | stump radius 2-3, hollow `diameter >> 1`, roots 4 deep, column heights `2 + nextInt(3) + nextInt(2)`. |
| `gloamingforest:lampposts` | LampostFeature (BlockStateConfiguration) | on grass only: `gloamingforest:canopy_fence` post 1-4 high topped by the configured state with a random rotation (`pumpkin_lamppost`: jack-o'-lantern; `lamppost_placer` picks firefly lampposts, 10 % cicada lampposts, both configured by U6). |
| `gloamingforest:monolith` | MonolithFeature | 2x2 obsidian pillar of height 10-19 with uneven corners (`dir` picks the tall corner), a lapis block at the top of the tallest column; spawns 2 ravens within 8 blocks. |
| `gloamingforest:mycelium_blob` | CheckAbovePatchFeature (DiskConfiguration) | disk replacement like vanilla `disk` but only where the block above is not solid (mycelium patches). |
| `gloamingforest:small_lake` | TFSmallLakeFeature | vanilla-style lake (4-7 ellipsoids in a 16x8x16 box) with the configured fluid / barrier, cave air above, freezes / surfaces as configured (water, lava, frozen lakes). |
| `gloamingforest:webs` | WebFeature | picks a random y above the origin; under leaves or a log places a cobweb (50 % in the leaves themselves). |
| `gloamingforest:wood_roots` | WoodRootFeature (RootConfig) | in stone: root strand of length `rand*6 + rand*6 + 4`, tilt 0.6-0.9, branching 2/3 with new angle `+0.25..0.75`, 1/4 ore-root (liveroot) blocks. |

## 7. Configured / placed features (overview)

Configured (31): `gloamingforest:birch_fallen_log`, `default_fallen_logs`, `oak_fallen_log`, `spruce_fallen_log`,
`tf_oak_fallen_log`, `mangrove_fallen_log` (fallen small logs), `hollow_log`, `hollow_stump`, `foundation`,
`lamppost_placer`, `pumpkin_lamppost`, `monolith`, `mycelium_blob`, `outside_stalagmite`, `lava_lake`, `water_lake`,
`water_frozen`, `torch_berries`, `uberous_soil_patch_big`, `uberous_soil_patch_small`, `small_andesite`,
`small_diorite`, `small_granite`, `vanilla_roots`, `webs`, `dark_dead_bushes`, `dark_ferns`, `dark_grass`,
`dark_pumpkins`, `dense_ferns`, `dense_large_ferns` (all `gloamingforest:`). Placed (35) add `dense_water_lake`,
`frozen_lake`, `wood_roots`, and the vanilla-grass replacements `patch_grass_badlands`, `patch_grass_forest`,
`patch_grass_jungle`, `patch_grass_savanna`, `patch_grass_taiga`, `patch_grass_taiga_2`. Biome placement lists are U1's.

## 8. Loot tables (owned)

`gloamingforest:blocks/hedge`, `gloamingforest:chests/basement` (rolled by U19's druid hut basement), `gloamingforest:foundation_basement`,
`gloamingforest:fallen_trunk_loot`, `gloamingforest:hedge_maze`, `gloamingforest:hedge_cloth`, `gloamingforest:hill_1`,
`gloamingforest:hill_2`, `gloamingforest:hill_3`, `gloamingforest:tree_cache` (contents in the appendix).

## 9. Map decorations, piece types, strings

- Map decorations: `gloamingforest:hedge_maze`, `gloamingforest:small_hollow_hill`, `gloamingforest:medium_hollow_hill`,
  `gloamingforest:large_hollow_hill` (magic map icons; vanilla fallback via F12 / F6 mapping).
- Piece types: `gloamingforest:TFHill`, `gloamingforest:TFHedge`, `gloamingforest:TFFallenTrunk`,
  `gloamingforest:TFHTTr`, `gloamingforest:TFHTLB`, `gloamingforest:TFHTMB`, `gloamingforest:TFHTSB`,
  `gloamingforest:TFHTRo`, `gloamingforest:TFHTLD`.
- Structure types: `gloamingforest:hollow_hill`, `gloamingforest:hedge_maze`, `gloamingforest:hollow_tree`,
  `gloamingforest:fallen_trunk`.
- Lang: `block.gloamingforest.hedge`.

## 10. Selftests (`minorlandmarks.*`, dx 1800-1899)

`minorlandmarks.hedge_damage` (3 cactus damage, spiders immune), `minorlandmarks.hollow_hill_layout` (seeded golden
hash per size), `minorlandmarks.hedge_maze_layout`, `minorlandmarks.hollow_tree_sapling`, `minorlandmarks.fallen_trunk`,
`minorlandmarks.monolith_ravens`, feature smoke tests for every feature type in a flat area.

## Appendix: upstream data (compact, generated from upstream 4.8 data JSON)

Notation: `block[state=value]`; `weighted{a:w}`; loot `xN` count, `wN` weight, `[...]` conditions; placement modifiers joined by `>`. The imported JSON (F1) is authoritative; this appendix is the review checklist.

### Configured features (31)

- `gloamingforest:birch_fallen_log`: type `gloamingforest:fallen_small_log` {hollow=gloamingforest:hollow_birch_log_horizontal[axis=x,variant=empty], normal=birch_log[axis=y]}
- `gloamingforest:dark_dead_bushes`: type `gloamingforest:dark_forest_placer` {feature={feature=simple_block{config={to_place=dead_bush}}, placement=[block_predicate_filter{predicate=all_of{predicates=[replaceable, matching_block_tag{offset=[0, -1, 0], tag=dirt}]}}]}, tries=50, xz_spread=7, y_spread=3}
- `gloamingforest:dark_ferns`: type `gloamingforest:dark_forest_placer` {feature={feature=simple_block{config={to_place=fern}}, placement=[block_predicate_filter{predicate=all_of{predicates=[matching_blocks{blocks=air}, matching_block_tag{offset=[0, -1, 0], tag=dirt}]}}]}, tries=64, xz_spread=7, y_spread=3}
- `gloamingforest:dark_grass`: type `gloamingforest:dark_forest_placer` {feature={feature=simple_block{config={to_place=short_grass}}, placement=[block_predicate_filter{predicate=all_of{predicates=[matching_blocks{blocks=air}, matching_block_tag{offset=[0, -1, 0], tag=dirt}]}}]}, tries=64, xz_spread=7, y_spread=3}
- `gloamingforest:dark_pumpkins`: type `gloamingforest:dark_forest_placer` {feature={feature=simple_block{config={to_place=pumpkin}}, placement=[block_predicate_filter{predicate=all_of{predicates=[replaceable, matching_block_tag{offset=[0, -1, 0], tag=dirt}]}}]}, tries=50, xz_spread=7, y_spread=3}
- `gloamingforest:default_fallen_logs`: type `minecraft:random_selector` {default={feature=gloamingforest:tf_oak_fallen_log, placement=[]}, features=[{chance=0.1, feature={feature=gloamingforest:birch_fallen_log, placement=[]}}, {chance=0.2, feature={feature=gloamingforest:oak_fallen_log, placement=[]}}, {chance=0.4, feature={feature=gloamingforest:canopy_fallen_log, placement=[]}}]}
- `gloamingforest:dense_ferns`: type `minecraft:random_patch` {feature={feature=simple_block{config={to_place=fern}}, placement=[block_predicate_filter{predicate=all_of{predicates=[matching_blocks{blocks=air}, matching_block_tag{offset=[0, -1, 0], tag=dirt}]}}]}, tries=64, xz_spread=7, y_spread=3}
- `gloamingforest:dense_large_ferns`: type `minecraft:random_patch` {feature={feature=simple_block{config={to_place=large_fern[half=lower]}}, placement=[block_predicate_filter{predicate=all_of{predicates=[matching_blocks{blocks=air}, matching_block_tag{offset=[0, -1, 0], tag=dirt}]}}]}, tries=64, xz_spread=7, y_spread=3}
- `gloamingforest:foundation`: type `gloamingforest:foundation` {basement_height=weighted_list{distribution=[{data=3, weight=1}, {data=0, weight=1}]}, basement_posts=oak_fence[east=false,north=false,south=false,waterlogged=false,west=false], decayed_wall_block=mossy_cobblestone, decayed_wall_top_block=weighted{mossy_cobblestone:5, mossy_cobblestone_slab[type=bottom,waterlogged=false]:1, mossy_cobblestone_stairs[facing=east,half=bottom,shape=straight,waterlogged=false]:2, mossy_cobblestone_stairs[facing=west,half=bottom,shape=straight,waterlogged=false]:2}, floor=weighted{oak_planks:39, oak_slab[type=bottom,waterlogged=false]:1, oak_stairs[facing=north,half=bottom,shape=straight,waterlogged=false]:6, oak_stairs[facing=east,half=bottom,shape=straight,waterlogged=false]:2, oak_stairs[facing=south,half=bottom,shape=straight,waterlogged=false]:6, oak_stairs[facing=west,half=bottom,shape=straight,waterlogged=false]:2}, loot_container=chest[type=single,facing=north,waterlogged=false], loot_table=gloamingforest:foundation_basement, random_floor_chance=uniform{max_exclusive=1, min_inclusive=-2}, wall_block=cobblestone, wall_heights=uniform(1..5), wall_top_block=weighted{cobblestone:5, cobblestone_slab[type=bottom,waterlogged=false]:1, cobblestone_stairs[facing=east,half=bottom,shape=straight,waterlogged=false]:2, cobblestone_stairs[facing=west,half=bottom,shape=straight,waterlogged=false]:2}, wall_width=uniform(5..9)}
- `gloamingforest:hollow_log`: type `gloamingforest:fallen_hollow_log` {}
- `gloamingforest:hollow_stump`: type `gloamingforest:hollow_stump` {add_first_five_chance=1, add_second_five_chance=1, branch_provider=gloamingforest:gloaming_oak_wood[axis=y], check_water=False, decorators=[], has_leaves=False, leaves_provider=gloamingforest:gloaming_oak_leaves[distance=7,persistent=false,waterlogged=false], minimum_size=0, roots_provider=gloamingforest:root, trunk_provider=gloamingforest:gloaming_oak_log[axis=y]}
- `gloamingforest:lamppost_placer`: type `minecraft:random_selector` {default={feature=gloamingforest:firefly_lamppost, placement=[]}, features=[{chance=0.1, feature={feature=gloamingforest:cicada_lamppost, placement=[]}}]}
- `gloamingforest:lava_lake`: type `gloamingforest:small_lake` {barrier=stone, fluid=lava[level=0]}
- `gloamingforest:mangrove_fallen_log`: type `gloamingforest:fallen_small_log` {hollow=gloamingforest:hollow_mangrove_log_horizontal[axis=x,variant=empty], normal=gloamingforest:mangrove_log[axis=y]}
- `gloamingforest:monolith`: type `gloamingforest:monolith` {}
- `gloamingforest:mycelium_blob`: type `gloamingforest:mycelium_blob` {half_height=3, radius=uniform(4..6), state_provider={fallback=mycelium[snowy=false], rules=[]}, target=matching_blocks{blocks=grass_block}}
- `gloamingforest:oak_fallen_log`: type `gloamingforest:fallen_small_log` {hollow=gloamingforest:hollow_oak_log_horizontal[axis=x,variant=empty], normal=oak_log[axis=y]}
- `gloamingforest:outside_stalagmite`: type `gloamingforest:block_spike` {}
- `gloamingforest:pumpkin_lamppost`: type `gloamingforest:lampposts` {state=jack_o_lantern[facing=north]}
- `gloamingforest:small_andesite`: type `minecraft:ore` {discard_chance_on_air_exposure=0, size=16, targets=[{state=andesite, target={predicate_type=tag_match, tag=stone_ore_replaceables}}]}
- `gloamingforest:small_diorite`: type `minecraft:ore` {discard_chance_on_air_exposure=0, size=16, targets=[{state=diorite, target={predicate_type=tag_match, tag=stone_ore_replaceables}}]}
- `gloamingforest:small_granite`: type `minecraft:ore` {discard_chance_on_air_exposure=0, size=16, targets=[{state=granite, target={predicate_type=tag_match, tag=stone_ore_replaceables}}]}
- `gloamingforest:spruce_fallen_log`: type `gloamingforest:fallen_small_log` {hollow=gloamingforest:hollow_spruce_log_horizontal[axis=x,variant=empty], normal=spruce_log[axis=y]}
- `gloamingforest:tf_oak_fallen_log`: type `gloamingforest:fallen_small_log` {hollow=gloamingforest:hollow_gloaming_oak_log_horizontal[axis=x,variant=empty], normal=gloamingforest:gloaming_oak_log[axis=y]}
- `gloamingforest:torch_berries`: type `gloamingforest:underground_plants` {state=gloamingforest:torchberry_plant[has_torchberries=true]}
- `gloamingforest:uberous_soil_patch_big`: type `gloamingforest:mycelium_blob` {half_height=1, radius=uniform(4..8), state_provider={fallback=gloamingforest:uberous_soil, rules=[]}, target=matching_blocks{blocks=[podzol, coarse_dirt, dirt]}}
- `gloamingforest:uberous_soil_patch_small`: type `gloamingforest:mycelium_blob` {half_height=0, radius=uniform(2..3), state_provider={fallback=gloamingforest:uberous_soil, rules=[]}, target=matching_blocks{blocks=[podzol, coarse_dirt, dirt]}}
- `gloamingforest:vanilla_roots`: type `gloamingforest:underground_plants` {state=hanging_roots[waterlogged=false]}
- `gloamingforest:water_frozen`: type `gloamingforest:small_lake` {fluid=water[level=0], ice=ice}
- `gloamingforest:water_lake`: type `gloamingforest:small_lake` {fluid=water[level=0]}
- `gloamingforest:webs`: type `gloamingforest:webs` {}

### Placed features (35)

- `gloamingforest:dark_dead_bushes`: feature `gloamingforest:dark_dead_bushes`; placement: rarity 1/3 > in_square > heightmap WORLD_SURFACE_WG > biome
- `gloamingforest:dark_ferns`: feature `gloamingforest:dark_ferns`; placement: rarity 1/3 > in_square > heightmap WORLD_SURFACE_WG > biome
- `gloamingforest:dark_grass`: feature `gloamingforest:dark_grass`; placement: count 20 > in_square > heightmap WORLD_SURFACE_WG > biome
- `gloamingforest:dark_pumpkins`: feature `gloamingforest:dark_pumpkins`; placement: rarity 1/30 > in_square > heightmap WORLD_SURFACE_WG > biome
- `gloamingforest:default_fallen_logs`: feature `gloamingforest:default_fallen_logs`; placement: rarity 1/40 > in_square > heightmap OCEAN_FLOOR > gloamingforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=True, occupies_vegetation=False, structures_allowed=[]} > biome
- `gloamingforest:dense_ferns`: feature `gloamingforest:dense_ferns`; placement: count weighted_list{distribution=[{data=3, weight=1}, {data=4, weight=1}]} > in_square > heightmap WORLD_SURFACE_WG > biome > gloamingforest:no_structure{additional_clearance=0, occupies_surface=False, occupies_underground=False, occupies_vegetation=True, structures_allowed=[]}
- `gloamingforest:dense_large_ferns`: feature `gloamingforest:dense_large_ferns`; placement: count 2 > in_square > heightmap WORLD_SURFACE_WG > biome > gloamingforest:no_structure{additional_clearance=0, occupies_surface=False, occupies_underground=False, occupies_vegetation=True, structures_allowed=[]}
- `gloamingforest:dense_water_lake`: feature `gloamingforest:water_lake`; placement: rarity 1/4 > in_square > heightmap OCEAN_FLOOR > gloamingforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=True, occupies_vegetation=False, structures_allowed=[]} > biome
- `gloamingforest:foundation`: feature `gloamingforest:foundation`; placement: rarity 1/90 > in_square > heightmap OCEAN_FLOOR > gloamingforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > biome
- `gloamingforest:frozen_lake`: feature `gloamingforest:water_frozen`; placement: rarity 1/4 > in_square > heightmap OCEAN_FLOOR > gloamingforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=True, occupies_vegetation=False, structures_allowed=[]} > biome
- `gloamingforest:hollow_log`: feature `gloamingforest:hollow_log`; placement: rarity 1/85 > in_square > heightmap OCEAN_FLOOR > gloamingforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > biome
- `gloamingforest:hollow_stump`: feature `gloamingforest:hollow_stump`; placement: rarity 1/80 > in_square > heightmap OCEAN_FLOOR > gloamingforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > biome
- `gloamingforest:lamppost_placer`: feature `gloamingforest:lamppost_placer`; placement: rarity 1/2 > in_square > heightmap OCEAN_FLOOR > gloamingforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=True, occupies_vegetation=False, structures_allowed=[]} > biome
- `gloamingforest:lava_lake`: feature `gloamingforest:lava_lake`; placement: rarity 1/10 > in_square > heightmap OCEAN_FLOOR > gloamingforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=True, occupies_vegetation=False, structures_allowed=[]} > biome
- `gloamingforest:mangrove_fallen_log`: feature `gloamingforest:mangrove_fallen_log`; placement: rarity 1/40 > in_square > heightmap OCEAN_FLOOR > gloamingforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > biome
- `gloamingforest:monolith`: feature `gloamingforest:monolith`; placement: rarity 1/90 > in_square > heightmap OCEAN_FLOOR > gloamingforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > biome
- `gloamingforest:mycelium_blob`: feature `gloamingforest:mycelium_blob`; placement: rarity 1/3 > in_square > heightmap OCEAN_FLOOR > gloamingforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > biome
- `gloamingforest:outside_stalagmite`: feature `gloamingforest:outside_stalagmite`; placement: rarity 1/77 > in_square > heightmap OCEAN_FLOOR > gloamingforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > biome
- `gloamingforest:patch_grass_badlands`: feature `minecraft:patch_grass`; placement: in_square > heightmap WORLD_SURFACE_WG > biome > gloamingforest:no_structure{additional_clearance=0, occupies_surface=False, occupies_underground=False, occupies_vegetation=True, structures_allowed=[]}
- `gloamingforest:patch_grass_forest`: feature `minecraft:patch_grass`; placement: count 2 > in_square > heightmap WORLD_SURFACE_WG > biome > gloamingforest:no_structure{additional_clearance=0, occupies_surface=False, occupies_underground=False, occupies_vegetation=True, structures_allowed=[]}
- `gloamingforest:patch_grass_jungle`: feature `minecraft:patch_grass_jungle`; placement: count 25 > in_square > heightmap WORLD_SURFACE_WG > biome > gloamingforest:no_structure{additional_clearance=0, occupies_surface=False, occupies_underground=False, occupies_vegetation=True, structures_allowed=[]}
- `gloamingforest:patch_grass_savanna`: feature `minecraft:patch_grass`; placement: count 20 > in_square > heightmap WORLD_SURFACE_WG > biome > gloamingforest:no_structure{additional_clearance=0, occupies_surface=False, occupies_underground=False, occupies_vegetation=True, structures_allowed=[]}
- `gloamingforest:patch_grass_taiga`: feature `minecraft:patch_taiga_grass`; placement: count 7 > in_square > heightmap WORLD_SURFACE_WG > biome > gloamingforest:no_structure{additional_clearance=0, occupies_surface=False, occupies_underground=False, occupies_vegetation=True, structures_allowed=[]}
- `gloamingforest:patch_grass_taiga_2`: feature `minecraft:patch_taiga_grass`; placement: in_square > heightmap WORLD_SURFACE_WG > biome > gloamingforest:no_structure{additional_clearance=0, occupies_surface=False, occupies_underground=False, occupies_vegetation=True, structures_allowed=[]}
- `gloamingforest:pumpkin_lamppost`: feature `gloamingforest:pumpkin_lamppost`; placement: rarity 1/10 > in_square > heightmap OCEAN_FLOOR > gloamingforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > biome
- `gloamingforest:small_andesite`: feature `gloamingforest:small_andesite`; placement: height trapezoid{max_inclusive=y=64, min_inclusive=y=-64} > rarity 1/10 > in_square > count 5 > biome
- `gloamingforest:small_diorite`: feature `gloamingforest:small_diorite`; placement: height trapezoid{max_inclusive=y=64, min_inclusive=y=-64} > rarity 1/10 > in_square > count 5 > biome
- `gloamingforest:small_granite`: feature `gloamingforest:small_granite`; placement: height trapezoid{max_inclusive=y=64, min_inclusive=y=-64} > rarity 1/10 > in_square > count 5 > biome
- `gloamingforest:spruce_fallen_log`: feature `gloamingforest:spruce_fallen_log`; placement: rarity 1/40 > in_square > heightmap OCEAN_FLOOR > gloamingforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > biome
- `gloamingforest:tf_oak_fallen_log`: feature `gloamingforest:tf_oak_fallen_log`; placement: rarity 1/40 > in_square > heightmap OCEAN_FLOOR > gloamingforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=False, occupies_vegetation=False, structures_allowed=[]} > biome
- `gloamingforest:torch_berries`: feature `gloamingforest:torch_berries`; placement: heightmap WORLD_SURFACE_WG > height uniform bottom+0..y=60 > count 8 > in_square > gloamingforest:no_structure{additional_clearance=0, occupies_surface=False, occupies_underground=True, occupies_vegetation=False, structures_allowed=[]} > biome
- `gloamingforest:vanilla_roots`: feature `gloamingforest:vanilla_roots`; placement: count 16 > height uniform bottom+0..y=0 > block_predicate_filter{predicate=would_survive{state=gloamingforest:torchberry_plant[has_torchberries=false]}} > rarity 1/1 > in_square > heightmap MOTION_BLOCKING > gloamingforest:no_structure{additional_clearance=0, occupies_surface=False, occupies_underground=True, occupies_vegetation=False, structures_allowed=[]} > biome
- `gloamingforest:water_lake`: feature `gloamingforest:water_lake`; placement: rarity 1/32 > in_square > heightmap OCEAN_FLOOR > gloamingforest:no_structure{additional_clearance=0, occupies_surface=True, occupies_underground=True, occupies_vegetation=False, structures_allowed=[]} > biome
- `gloamingforest:webs`: feature `gloamingforest:webs`; placement: heightmap WORLD_SURFACE_WG > count 60 > in_square > biome
- `gloamingforest:wood_roots`: feature `gloamingforest:ore/wood_roots_spread`; placement: height uniform bottom+0..y=0 > rarity 1/40 > in_square > heightmap MOTION_BLOCKING > gloamingforest:no_structure{additional_clearance=0, occupies_surface=False, occupies_underground=True, occupies_vegetation=False, structures_allowed=[]} > biome

### Loot tables (10)

- `gloamingforest:blocks/hedge` (block)
  - pool 1: rolls 1 if silk touch: gloamingforest:hedge
- `gloamingforest:chests/basement` (chest)
  - pool 1: rolls 4: table gloamingforest:useless w25; potion {function=set_potion, id=water} w75; rotten_flesh x1-6 w75; poisonous_potato x1-2 w75; wheat x1-6 w75; potato x1-6 w75; carrot x1-6 w75; melon_slice x1-6 w75; water_bucket w75; torch x1-12 w75; mushroom_stew w75; milk_bucket w75; sweet_berries x2-7 w75; melon_seeds x1-5 w75
  - pool 2: rolls 2: bread x1-8; gloamingforest:shika_senbei x1-4; cooked_beef x1-6; cooked_porkchop x1-8; baked_potato x1-8; cooked_chicken x1-10; cooked_cod x1-8
  - pool 3: rolls 1: glistering_melon_slice x1-12 w75; apple x1-12 w75; map w75; gloamingforest:charm_of_keeping_1 w75; golden_apple x1-2 w25; golden_carrot x1-2 w25; cake w25; oak_boat w25; gloamingforest:hollow_oak_sapling w25; gloamingforest:music_disc_home w15; gloamingforest:music_disc_radiance w15
- `gloamingforest:fallen_trunk_loot` (chest)
  - pool 1: rolls 4: table gloamingforest:useless w25; poisonous_potato x1-2 w75; wheat x1-6 w75; potato x1-6 w75; carrot x1-6 w75; melon_slice x1-6 w75; melon_seeds x1-5 w75; gloamingforest:shika_senbei x2-6 w75; water_bucket w75; milk_bucket w75
  - pool 2: rolls 2: gloamingforest:gloaming_oak_sapling x1-4; gloamingforest:canopy_sapling x1-4; gloamingforest:mangrove_sapling x1-4; gloamingforest:darkwood_sapling x1-4; gloamingforest:firefly x1-12
  - pool 3: rolls 1: pumpkin_pie x1-12 w75; apple x1-12 w75; gloamingforest:charm_of_keeping_1 w75; gloamingforest:charm_of_life_1 w75; gloamingforest:hollow_oak_sapling w25; gloamingforest:music_disc_superstitious w25
- `gloamingforest:foundation_basement` (chest)
  - pool 1: rolls 4: table gloamingforest:useless w25; potion {function=set_potion, id=water} w75; rotten_flesh x1-6 w75; poisonous_potato x1-2 w75; water_bucket w75; torch x1-12 w75; milk_bucket w75
  - pool 2: rolls 2: wheat x1-6; potion {function=set_potion, id=awkward}; potion {function=set_potion, id=mundane}; potion {function=set_potion, id=thick}; rotten_flesh x1-6; melon_seeds x1-5; pumpkin_seeds x1-5
  - pool 3: rolls 1: glistering_melon_slice x1-12 w75; map w75; gloamingforest:charm_of_keeping_1 w75; gloamingforest:music_disc_thread w50; golden_apple x1-2 w25; golden_carrot x1-2 w25; oak_boat w25; gloamingforest:hollow_oak_sapling w25
- `gloamingforest:hedge_cloth` (chest)
  - pool 1: rolls 4-6: white_carpet x2-3; light_gray_carpet x2-3; gray_carpet x2-3; black_carpet x2-3; red_carpet x2-3; orange_carpet x2-3; yellow_carpet x2-3; green_carpet x2-3; lime_carpet x2-3; blue_carpet x2-3; cyan_carpet x2-3; light_blue_carpet x2-3; purple_carpet x2-3; magenta_carpet x2-3; pink_carpet x2-3; brown_carpet x2-3
  - pool 2: rolls 2-3: string x8-17; gold_nugget x1-3
  - pool 3: rolls 1: gloamingforest:emperors_cloth
- `gloamingforest:hedge_maze` (chest)
  - pool 1: rolls 4: table gloamingforest:useless w25; oak_planks x1-4 w75; brown_mushroom x1-4 w75; red_mushroom x1-4 w75; wheat x1-4 w75; string x1-4 w75; stick x1-6 w75
  - pool 2: rolls 2: melon_slice x1-4; melon_seeds x1-4; pumpkin_seeds x1-4; arrow x1-12; gloamingforest:firefly x1-4
  - pool 3: rolls 1: cobweb x1-4 w75; apple x1-2 w75; shears w75; saddle w75; bow w75; golden_apple w25; mushroom_stew w25; diamond w25; diamond_hoe w25; gloamingforest:music_disc_motion w15
- `gloamingforest:hill_1` (chest)
  - pool 1: rolls 4: table gloamingforest:useless w25; iron_ingot x1-4 w75; wheat x1-4 w75; string x1-4 w75; bucket w75
  - pool 2: rolls 2: torch x1-12; arrow x1-12; gunpowder x1-4; bread; gloamingforest:ore_magnet
  - pool 3: rolls 1: gloamingforest:liveroot x1-3 w75; gold_ingot x1-3 w75; iron_pickaxe w75; diamond w25; gloamingforest:ore_meter w25; gloamingforest:transformation_powder x1-12 w25; gloamingforest:steeleaf_ingot x1-3 w25; gloamingforest:music_disc_findings w25
- `gloamingforest:hill_2` (chest)
  - pool 1: rolls 4: table gloamingforest:useless w25; iron_ingot x1-4 w75; carrot x1-4 w75; ladder x1-6 w75; bucket w75
  - pool 2: rolls 2: baked_potato x1-2; arrow x1-12; torch x1-12; gloamingforest:ore_magnet; gloamingforest:ironwood_ingot x1-4
  - pool 3: rolls 1: gloamingforest:magic_map w75; gloamingforest:transformation_powder x1-12 w75; gloamingforest:uncrafting_table w75 [{condition=gloamingforest:uncrafting_table_enabled}]; diamond w25; emerald x1-6 w25; gloamingforest:peacock_feather_fan w25; gloamingforest:charm_of_life_1 w25; gloamingforest:pocket_watch w25; gloamingforest:music_disc_wayfarer w25
- `gloamingforest:hill_3` (chest)
  - pool 1: rolls 4: table gloamingforest:useless w25; gold_nugget x1-9 w75; potato x1-4 w75; cod x1-4 w75; gloamingforest:torchberries x1-5 w75
  - pool 2: rolls 2: torch x1-12; arrow x1-12; gunpowder x1-4; gloamingforest:steeleaf_ingot x1-4; pumpkin_pie; gloamingforest:ore_magnet
  - pool 3: rolls 1: gloamingforest:transformation_powder x1-12 w75; gloamingforest:ironwood_pickaxe {add=False, enchantments={minecraft:efficiency=1, minecraft:fortune=1}, function=set_enchantments} w75; gloamingforest:magic_map w75; diamond x1-2 w25; gloamingforest:moonworm_queen w25; gloamingforest:charm_of_life_1 w25; gloamingforest:charm_of_keeping_1 w25; gloamingforest:ironwood_block w25; gloamingforest:music_disc_maker w20
- `gloamingforest:tree_cache` (chest)
  - pool 1: rolls 4: table gloamingforest:useless w25; poisonous_potato x1-2 w75; wheat x1-6 w75; potato x1-6 w75; carrot x1-6 w75; melon_slice x1-6 w75; melon_seeds x1-5 w75; water_bucket w75; milk_bucket w75
  - pool 2: rolls 2: gloamingforest:gloaming_oak_sapling x1-4; gloamingforest:canopy_sapling x1-4; gloamingforest:mangrove_sapling x1-4; gloamingforest:darkwood_sapling x1-4; gloamingforest:firefly x1-12
  - pool 3: rolls 1: pumpkin_pie x1-12 w75; apple x1-12 w75; gloamingforest:charm_of_keeping_1 w75; gloamingforest:charm_of_life_1 w75; gloamingforest:hollow_oak_sapling w25; gloamingforest:time_sapling w25; gloamingforest:transformation_sapling w25; gloamingforest:mining_sapling w25; gloamingforest:sorting_sapling w25
