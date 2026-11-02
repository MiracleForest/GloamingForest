# U2 `terrain`: Terrain, carvers and chunk blankets

Behaviour spec translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ `9e40a757`). Checklist for the
U2 worker and the W2 audit (A1). Numbers are TF's; "Bedrock:" paragraphs mark adaptation notes. The appendix lists
every owned configured/placed feature in compact form.

| | |
|---|---|
| Unit | U2 `terrain` (W1 batch 1) |
| Selftests | `terrain.*`, testPos dx `200-299` (owners rows `selftest terrain.*`, `selftest_dx 200-299`) |
| World DB | `gf:terrain:*` |
| Net channels | `terrain.*` |
| Config | section `terrain` (config_section `terrain`): no TF key; adds the GloamingForest option `skylight` (bool, default false) replacing TF's "Skylight" world preset (config.md / not-ported.md) |
| Contracts | worldgen (F5: `setTerrain`, `setSurface`, `registerCarver`, `registerBlanket`, `addStage`), features (F5), landmark (F6: terraformers, `decorationAllowed`) |
| Java sources | `init/{TFDensityFunctions,TFDimensionData,TFCaveCarvers}.java`, `init/custom/ChunkBlanketProcessors.java`, `world/components/chunkgenerators/{TerrainDensityRouter,NoiseDensityRouter,FocusedDensityFunction,HollowHillFunction,AbsoluteDifferenceFunction,SqrtDensityFunction,BoxDensityFunction,TanhHillFunction,TerrainColumn}.java`, `world/components/TFCavesCarver.java`, `world/components/chunkblanketing/*.java`, `world/components/placements/ChunkBlanketingModifier.java`, `world/registration/surface_rules/TFSurfaceRules.java`, `asmhooks/WorldgenHooks.java` (beardifier custom density, blanket hook), `world/components/feature/UndergroundPlantFeature.java` (oreberries) |
| Data | `worldgen/noise_settings/{twilight_noise_gen,skylight_noise_gen}.json`, `worldgen/density_function/*.json`, `worldgen/configured_carver/*.json`, `twilight/chunk_blanket_processors/*.json`, owned configured/placed features, tags below |

Not owned here: the biome source and `biome_grid` (U1), the dimension type and dimension JSON (F4: height -32..256,
fixed time), structure terraformers' shapes (owned by each landmark unit; F6 combines them), ore blocks and
oreberry plants (U6/U5), the `#twilightforest:antibuilder_ignores` consumers (U26 antibuilder) and
`ore_magnet/ignored_ores` consumers (U8 ore magnet), `carminite_reactor_ores` consumer (U26).

## 1. Noise settings (generator)

| | `twilightforest:twilight_noise_gen` | `twilightforest:skylight_noise_gen` |
|---|---|---|
| noise | min y -32, height 256, size horizontal 2, vertical 2 | same |
| sea level | 0 | 0 |
| default block / fluid | stone / water | stone / **air** |
| aquifers, ore veins | disabled | disabled |
| final density (and initial density without jaggedness) | `twilightforest:forested_terrain` | `twilightforest:skylight_terrain` |
| other router fields | 0 | 0 |
| surface rule | `TFSurfaceRules.tfSurface()` (2) | same |
| spawn target | none | none |

Bedrock: F5's `TerrainProvider` evaluates the density graph (F5 `noise` density evaluator) per 8 x 8 cell:
`cellWidth = QuartPos.toBlock(size_horizontal) = 2 << 2 = 8`, `cellHeight = QuartPos.toBlock(size_vertical) = 2 << 2 =
8`. This is not vanilla overworld's 4 x 8 (`size_horizontal 1`). Interpolation follows `NoiseChunk` with those cell sizes:
`cellCountXZ = 16 / 8 = 2` cells per chunk axis and `cellCountY = 256 / 8 = 32` cells from `cellNoiseMinY = -32 / 8 =
-4`. `interpolated` inputs are evaluated at the 3 x 33 x 3 cell corners of the chunk and interpolated trilinearly
across each 8 x 8 x 8 cell. `flat_cache` stays at quart (4-block) resolution, and `cache_2d` / `cache_once` keep their
vanilla semantics. A 4 x 8 grid changes every terrain height, and the F13 height goldens would not match. The dimension
is -32..256 (S1). The skylight variant is chosen by the `terrain`
config option at world creation and stored in `gf:terrain:skylight`.

## 2. Density functions (owned, data)

- `twilightforest:raw_biome_terrain`: `twilightforest:biome_driven_terrain {terrain_source twilightforest:biome_grid,
  lower -31, upper 64, depth_scalar 1, base_factor 8, base_offset -1.25}`.
- `twilightforest:raw_biome_noise`: `twilightforest:biome_driven_noise {terrain_source biome_grid, lower -31, upper 64,
  depth_scalar 1}`.
- `twilightforest:forested_terrain`: `clamp(-0.1, 0.5)` of `(raw_biome_terrain + y_clamped_gradient(-31 -> 31, 256 ->
  -256)) / 6 + raw_biome_noise * interpolated(max(0, flat_cache(ridgeMix)))` where `ridgeMix = (0.5 + 0.5 *
  ridge(xz 1, y 0)) * (1 - S) + (0.5 + 0.5 * ridge(xz 4, y 0)) * S` and `S = cache_once(clamp(0.5 + 0.5 * surface(xz 1),
  0, 1))` (noise ids `minecraft:ridge`, `minecraft:surface`).
- `twilightforest:skylight_terrain`: `clamp(-0.1, 0.5)` of `sqrt(clamp(interpolated(-0.5 + 5 * (-0.5 +
  flat_cache(ridgeMix))), 0, 2)) - 0.25 * |half_negative(-(raw_biome_terrain + gradient))|` (floating islands).
  Exact JSON trees in the imported data (`worldgen/density_function/*.json`).

## 3. Density function types (owned code)

| Id | Class | compute |
|---|---|---|
| `twilightforest:biome_driven_terrain` | TerrainDensityRouter | `d = BiomeDensitySource.sampleTerrain(x, z)` (U1 blending); returns `baseOffset + d.depth * baseFactor + d.depth`; min/max = lower/upper bound |
| `twilightforest:biome_driven_noise` | NoiseDensityRouter | returns `d.scale`; a chunk-cached variant (16 x 16 column cache) replaces it in `mapAll` |
| `twilightforest:focused` | FocusedDensityFunction(centerX, bottomY, centerZ, radius, nearValue, farValue), all `float` | all `float`: `dX = centerX - (float) blockX` (same for y, z); `dist = Mth.sqrt(dX*dX + dY*dY + dZ*dZ)`; returns the float overload `Mth.clampedMap(dist, 0f, radius, near, far)`, widened to double. Centre at block + 0.5f (`fromPos`) |
| `twilightforest:hollow_hill` | HollowHillFunction(centerX, bottomY, centerZ, radius, heightScale), all `float` | all `float`: `dX = (float) blockX - centerX`, `dY = (float) blockY - bottomY`, `dZ = (float) blockZ - centerZ`; `dist = Mth.sqrt(dX*dX + dZ*dZ)`; `height = Mth.cos(dist / radius * Mth.PI) * radius * 0.3333333334f` (table cosine, not `/ 3`); `n = Mth.clamp(dist / Mth.abs(radius), 0f, 1f)`; `n >= 1` -> 0; else `Mth.clamp(height * heightScale - dY, -1f, 1f)`, widened to double. A negative radius gives the bowl-shaped floor. Centre at block + 0.5f (`fromPos`) |
| `twilightforest:coord_min` | AbsoluteDifferenceFunction.Min(max, cx, cz) | `min(min(|x - cx|, |z - cz|), max)` |
| `twilightforest:coord_max` | AbsoluteDifferenceFunction.Max(max, cx, cz) | `min(max(|x - cx|, |z - cz|), max)` |
| `twilightforest:sqrt` | SqrtDensityFunction(input) | `sqrt(input)`; bounds = input bounds |

Also used by landmark terraformers (hollow hills, labyrinth, lich tower, courtyard): `BoxDensityFunction` and
`TanhHillFunction` (no registry ids; F6/F7 helpers).
- `BoxDensityFunction` (int box, double min/max -4 / 4) is vanilla `Beardifier` arithmetic in double. BEARD_THIN / BURY
  use `yDist = blockY - minY`; BEARD_BOX / ENCAPSULATE use the distance outside `[minY, maxY]`. The density is
  BEARD_THIN / BEARD_BOX `getBeardContribution(xDist, yDist, zDist, blockY - minY) * 0.8`, BURY
  `getBuryContribution(xDist, yDist * 0.5, zDist)`, or ENCAPSULATE `getBuryContribution(xDist * 0.5, yDist * 0.5,
  zDist * 0.5) * 0.8`. It is then clamped to [-4, 4].
- `TanhHillFunction` has `float` deltas. The perpendicular axis is scaled by `1.3F`. The constructor stores
  `Mth.cos(angle)` / `Mth.sin(angle)` (table values).
  - `double distSquared = dX * dX + dZ * dZ` is summed in float and then widened, and `9 * radius * radius` is float.
  - The rest is `double` with `Math.sqrt` / `Math.exp`.

`Mth` arithmetic (bit-exact parity for the hollow hill, labyrinth mound and fallen-trunk volume hashes). Where TF calls
`Mth`, translate the 1.21.1 definitions, not `<cmath>`:
- `SIN[i] = (float) Math.sin(i * Math.PI * 2.0 / 65536.0)` for `i` in 0..65535, built in double and stored as float.
- `Mth.sin(float v) = SIN[(int)(v * 10430.378f) & 65535]` and `Mth.cos(float v) = SIN[(int)(v * 10430.378f + 16384.0f) &
  65535]`, with float multiply and add. Java `f2i` truncates toward zero and saturates; NaN becomes 0. The C++
  translation must clamp before the cast, because an out-of-range cast is undefined behaviour.
- `Mth.PI = 3.1415927f`; `Mth.sqrt(float v) = (float) Math.sqrt((double) v)`; `Mth.abs(float)`.
- `Mth.clamp(float v, float lo, float hi) = v < lo ? lo : Math.min(v, hi)`. The double overload is the same in double.
  Integer literals are promoted to the overload's type.
- `Mth.clampedMap(float...) = clampedLerp(outMin, outMax, inverseLerp(v, inMin, inMax))`, all in float. `clampedLerp`
  returns `outMin` when `t < 0`, `outMax` when `t > 1`, else `outMin + t * (outMax - outMin)`.
- Overloads resolve as in Java (most specific applicable method). `FocusedDensityFunction.compute` calls
  `Mth.clampedMap(dist, 0, this.radius, this.nearValue, this.farValue)` with `float` arguments and the `int` literal
  `0`. Both the float and the double overload apply (int widens to either), and the float one is more specific, so
  the whole map runs in float and only the returned value is widened to double. Translate it as the float overload
  with `0.0f`, never with a double `0.0`.
- Keep Java's float/double split exactly as the source declares it. Do not evaluate a `float` expression in double,
  keep float temporaries, and do not let the compiler fuse `a * b + c` into an FMA, because Java never contracts. Build
  these translation units without `-ffast-math` and with `-ffp-contract=off` (or `#pragma clang fp contract(off)`).

Structure terraforming hook (`WorldgenHooks.getCustomDensity`): the Beardifier's value gets the sum of every
intersecting structure's custom density function (F6 `landmark::addTerraformer`); U2's terrain provider must add it
exactly where Java's `Beardifier.compute` is evaluated.

## 4. Surface rules (`TFSurfaceRules.tfSurface`)

Sequence (first match wins):
1. Bedrock floor: `vertical_gradient("bedrock_floor", bottom -> bottom + 5)` -> bedrock.
2. `twilightforest:highlands`, on floor: surface noise above `2.25 / 8.25` -> coarse dirt; above `-2.25 / 8.25` -> podzol
   when no water above (`water(-1, 0)`), else dirt.
3. `twilightforest:thornlands`, `twilightforest:final_plateau`: on floor weathered deadrock; under floor (water start
   check -6..-1) cracked deadrock; else deadrock (`twilightforest:weathered_deadrock`, `cracked_deadrock`, `deadrock`).
4. `twilightforest:snowy_forest`: on floor snow block (no water above) else dirt; under floor dirt.
5. `twilightforest:glacier`: on floor gravel; under floor gravel.
6. Overworld-like floor: lakes and streams: ceiling sandstone, floor grass (no water) else sand; swamps and fire
   swamps: grass (no water) else dirt; elsewhere grass above y -4 (y start check -4, multiplier 1) when no water above,
   dirt under water below y -4; under floor dirt above y -4.

Bedrock: F5 `setSurface(SurfaceProvider)` with the same evaluation order and the `minecraft:surface` noise.

## 5. Carvers

| Carver | Type | Config (configured carver of the same id) |
|---|---|---|
| `twilightforest:tf_caves` | TFCavesCarver(highlands false, walls = noise provider seed 6972119253061020355, NormalNoise(0, 1.0), scale 0.5, states dirt, dirt, rooted dirt, dirt, dirt, coarse dirt, dirt, dirt) | probability 0.1, y uniform bottom + 16 .. -8, yScale 0.6, horizontal radius x1.05, vertical x1.05, floor level -0.7, lava level bottom, replaceable `#twilightforest:carver_replaceables` |
| `twilightforest:highland_caves` | TFCavesCarver(highlands true, walls weighted trollsteinn 1 / stone 3) | probability 1, y biased-to-bottom 8 .. 32 (inner 16), yScale 0.6, horizontal x1.1..1.3, vertical x1.1, floor level -0.9..-0.65, lava level bottom, same replaceable tag |

Carver logic (translate exactly):
- `isStartChunk`: highlands always, else `rand <= probability`.
- Highland caves skip chunks where `clamp(manhattanDistanceFromLandmarkCenter(cx, cz), 0, 3) & 1 == 1` (a binary grid
  around landmark centres).
- Caves per chunk: highlands `nextInt(2)`, else `nextInt(4)`; per cave a room (always for highlands, else 1/4) of
  radius `1 + rand * 6` with `tunnelCount += nextInt(4)`; tunnels as vanilla (`getThickness`, branch count `i - nextInt(i/4)`).
- `carveBlock`: grass / mycelium / podzol / dirt path marks the surface; nothing below `minY + 6`; never exposes
  water (skips positions next to water inside the chunk); places rooted or coarse dirt (50/50) instead of air when fluid
  is 2 blocks above; restores the surface top material below.
- `postCarveBlock`: highlands: 1/4 neighbours that are replaceable become wall blocks; forest caves: non-down
  neighbours that are overworld base stone or water become wall blocks (dirt roofs) when facing up, under air, or where
  `ImprovedNoise(seed 6972119253061020355)` at 0.25 scale `> 0.5`.

Tag `block/#twilightforest:carver_replaceables` = `#minecraft:overworld_carver_replaceables`, `minecraft:snow_block`.

## 6. Chunk blankets (registry `chunk_blanket_processors`)

Hook: after surface generation of each chunk, every processor whose biome mask intersects the chunk's biomes runs
(`ChunkBlanketProcessors.chunkBlanketing`, F5 `registerBlanket`).

- `chunk_blanket_processors/twilightforest:dark_forest_canopy` (type `canopy`): biomes dark forest and dark forest
  centre, block `twilightforest:hardened_dark_leaves`, height 14, avoid structures `twilightforest:dark_tower`. Thickness
  per 4x4 cell = number of the 3x3 neighbouring cells in the mask, bilinearly interpolated per column minus 4; within
  24 blocks of an avoided structure's landmark centre subtract `24 - dist`; where thickness > 1 and the surface is not
  liquid, fill from `surface + 14 - thickness/2` for `thickness` blocks (RNG `Xoroshiro(overworldSeed, seed(chunk))`).
- `chunk_blanket_processors/twilightforest:snowy_forest_glacier` (type `glacier`): biome glacier, body packed ice, top
  ice, height 32: each column from the first free block up 32 blocks of packed ice capped with ice.

Placement modifier `twilightforest:chunk_blanketing` (`ChunkBlanketingModifier{integrity, heightmap, biomes?}`): one
position per column of the chunk with probability `integrity`, at `heightmap + 1`, filtered by the optional biome set
(`addThorns`: 0.7, OCEAN_FLOOR_WG, thornlands; used by U30's thorns feature).

## 7. Ores and oreberries (owned features; data in the appendix)

- Legacy ores (vanilla `ore` feature, replaceables `#minecraft:stone_ore_replaceables`): `twilightforest:legacy_coal_ore`,
  `twilightforest:legacy_iron_ore` (size 9, count 20, uniform bottom..63), `twilightforest:legacy_gold_ore`,
  `twilightforest:legacy_redstone_ore`, `twilightforest:legacy_diamond_ore`, `twilightforest:legacy_lapis_ore`,
  `twilightforest:legacy_copper_ore` (each configured + placed with the same id).
- Oreberries (`twilightforest:oreberry_bushes` underground-plant feature): configured
  `twilightforest:iron_oreberries`, `twilightforest:gold_oreberries`, `twilightforest:copper_oreberries`,
  `twilightforest:essence_oreberries`; placed `twilightforest:iron_oreberries`, `twilightforest:gold_oreberries`,
  `twilightforest:copper_oreberries` and enchanted-forest variants `twilightforest:iron_oreberries_enchanted_forest`,
  `twilightforest:gold_oreberries_enchanted_forest`, `twilightforest:copper_oreberries_enchanted_forest`,
  `twilightforest:essence_oreberries_enchanted_forest`.
- `twilightforest:ore/wood_roots_spread` (configured): `twilightforest:wood_roots` (U18 feature type), root block
  weighted `twilightforest:root` 6 / `twilightforest:liveroot_block` 1, ore root `twilightforest:liveroot_block`.

## 8. Block tags (owned by owners.tsv; consumers elsewhere)

- `block/#twilightforest:carver_replaceables` (section 5).
- `block/#twilightforest:antibuilder_ignores` = redstone lamp, TNT, water, `twilightforest:antibuilder`,
  `carminite_builder`, `built_block`, `reactor_debris`, `carminite_reactor`, `reappearing_block`, `ghast_trap`,
  `fake_diamond`, `fake_gold`, `#twilightforest:common_protections`, optional `gravestone:gravestone`.
- `block/#twilightforest:carminite_reactor_ores` = nether quartz ore, nether gold ore.
- `block/#twilightforest:ore_magnet/ignored_ores` = `#minecraft:coal_ores`.

## 9. Registries owned

`registry chunk_blanket_processors` (types `canopy`, `glacier`) with its two entries; the density function types
above; `carver` ids `twilightforest:tf_caves`, `twilightforest:highland_caves`; configured carvers
`twilightforest:tf_caves`, `twilightforest:highland_caves`; noise settings `twilightforest:twilight_noise_gen`,
`twilightforest:skylight_noise_gen`; density functions `twilightforest:forested_terrain`,
`twilightforest:raw_biome_noise`, `twilightforest:raw_biome_terrain`, `twilightforest:skylight_terrain`.

## 10. Bedrock adaptation notes

- Performance: the density graph runs on generator threads (F5); cache `sampleTerrain` per column (the chunk-cached
  noise router) and per quart cell; budget per chunk is measured by A1.
- Java parity: noise seeds (`minecraft:ridge`, `minecraft:surface`) come from the world seed exactly as Java's
  `RandomState` (F5 `jrand` / `noise`); F13 goldens check heights for fixed seeds.
- The canopy blanket runs after F6's landmark list is known for the chunk (needs structure references).

## 11. Selftests (`terrain.*`, dx 200-299)

`terrain.height_golden` (surface heights of a seeded area equal F13 goldens), `terrain.surface_rules` (highlands
podzol/coarse dirt, deadrock in thornlands, snow in snowy forest), `terrain.carvers` (no water exposure, dirt roofs),
`terrain.canopy_blanket` (hardened dark leaves at surface + 14), `terrain.glacier_blanket` (32 packed ice + ice),
`terrain.skylight_option`, `terrain.density_types` (unit tests of each type in gtest under `tests/terrain/`).

## Appendix: TF data (compact, generated from TF 4.8 data JSON)

Notation: `block[state=value]`; `weighted{a:w}`; loot `xN` count, `wN` weight, `[...]` conditions; placement modifiers joined by `>`. The imported JSON (F1) is authoritative; this appendix is the review checklist.

### Configured features (12)

- `twilightforest:copper_oreberries`: type `twilightforest:oreberry_bushes` {state=twilightforest:copper_oreberry[age=3,layers=0]}
- `twilightforest:essence_oreberries`: type `twilightforest:oreberry_bushes` {state=twilightforest:essence_oreberry[age=3,layers=0]}
- `twilightforest:gold_oreberries`: type `twilightforest:oreberry_bushes` {state=twilightforest:gold_oreberry[age=3,layers=0]}
- `twilightforest:iron_oreberries`: type `twilightforest:oreberry_bushes` {state=twilightforest:iron_oreberry[age=3,layers=0]}
- `twilightforest:legacy_coal_ore`: type `minecraft:ore` {discard_chance_on_air_exposure=0, size=16, targets=[{state=coal_ore, target={predicate_type=tag_match, tag=stone_ore_replaceables}}]}
- `twilightforest:legacy_copper_ore`: type `minecraft:ore` {discard_chance_on_air_exposure=0, size=10, targets=[{state=copper_ore, target={predicate_type=tag_match, tag=stone_ore_replaceables}}]}
- `twilightforest:legacy_diamond_ore`: type `minecraft:ore` {discard_chance_on_air_exposure=0, size=8, targets=[{state=diamond_ore, target={predicate_type=tag_match, tag=stone_ore_replaceables}}]}
- `twilightforest:legacy_gold_ore`: type `minecraft:ore` {discard_chance_on_air_exposure=0, size=9, targets=[{state=gold_ore, target={predicate_type=tag_match, tag=stone_ore_replaceables}}]}
- `twilightforest:legacy_iron_ore`: type `minecraft:ore` {discard_chance_on_air_exposure=0, size=9, targets=[{state=iron_ore, target={predicate_type=tag_match, tag=stone_ore_replaceables}}]}
- `twilightforest:legacy_lapis_ore`: type `minecraft:ore` {discard_chance_on_air_exposure=0, size=7, targets=[{state=lapis_ore, target={predicate_type=tag_match, tag=stone_ore_replaceables}}]}
- `twilightforest:legacy_redstone_ore`: type `minecraft:ore` {discard_chance_on_air_exposure=0, size=8, targets=[{state=redstone_ore[lit=false], target={predicate_type=tag_match, tag=stone_ore_replaceables}}]}
- `twilightforest:ore/wood_roots_spread`: type `twilightforest:wood_roots` {root_block=weighted{twilightforest:root:6, twilightforest:liveroot_block:1}, root_ore=twilightforest:liveroot_block}

### Placed features (14)

- `twilightforest:copper_oreberries`: feature `twilightforest:copper_oreberries`; placement: twilightforest:no_structure{additional_clearance=0, occupies_surface=False, occupies_underground=True, occupies_vegetation=False, structures_allowed=[]} > rarity 1/3 > in_square > height uniform y=-10..y=-8
- `twilightforest:copper_oreberries_enchanted_forest`: feature `twilightforest:copper_oreberries`; placement: twilightforest:no_structure{additional_clearance=0, occupies_surface=False, occupies_underground=True, occupies_vegetation=False, structures_allowed=[]} > rarity 1/5 > in_square > height uniform y=-10..y=-8
- `twilightforest:essence_oreberries_enchanted_forest`: feature `twilightforest:essence_oreberries`; placement: twilightforest:no_structure{additional_clearance=0, occupies_surface=False, occupies_underground=True, occupies_vegetation=False, structures_allowed=[]} > rarity 1/5 > in_square > height uniform y=-10..y=-8
- `twilightforest:gold_oreberries`: feature `twilightforest:gold_oreberries`; placement: twilightforest:no_structure{additional_clearance=0, occupies_surface=False, occupies_underground=True, occupies_vegetation=False, structures_allowed=[]} > rarity 1/3 > in_square > height uniform y=-10..y=-8
- `twilightforest:gold_oreberries_enchanted_forest`: feature `twilightforest:gold_oreberries`; placement: twilightforest:no_structure{additional_clearance=0, occupies_surface=False, occupies_underground=True, occupies_vegetation=False, structures_allowed=[]} > rarity 1/5 > in_square > height uniform y=-10..y=-8
- `twilightforest:iron_oreberries`: feature `twilightforest:iron_oreberries`; placement: twilightforest:no_structure{additional_clearance=0, occupies_surface=False, occupies_underground=True, occupies_vegetation=False, structures_allowed=[]} > rarity 1/3 > in_square > height uniform y=-10..y=-8
- `twilightforest:iron_oreberries_enchanted_forest`: feature `twilightforest:iron_oreberries`; placement: twilightforest:no_structure{additional_clearance=0, occupies_surface=False, occupies_underground=True, occupies_vegetation=False, structures_allowed=[]} > rarity 1/5 > in_square > height uniform y=-10..y=-8
- `twilightforest:legacy_coal_ore`: feature `twilightforest:legacy_coal_ore`; placement: height uniform bottom+0..y=127 > in_square > count 20 > biome
- `twilightforest:legacy_copper_ore`: feature `twilightforest:legacy_copper_ore`; placement: height uniform bottom+0..y=96 > in_square > count 6 > biome
- `twilightforest:legacy_diamond_ore`: feature `twilightforest:legacy_diamond_ore`; placement: height uniform bottom+0..y=15 > in_square > biome
- `twilightforest:legacy_gold_ore`: feature `twilightforest:legacy_gold_ore`; placement: height uniform bottom+0..y=31 > in_square > count 2 > biome
- `twilightforest:legacy_iron_ore`: feature `twilightforest:legacy_iron_ore`; placement: height uniform bottom+0..y=63 > in_square > count 20 > biome
- `twilightforest:legacy_lapis_ore`: feature `twilightforest:legacy_lapis_ore`; placement: height uniform bottom+0..y=30 > in_square > count 2 > biome
- `twilightforest:legacy_redstone_ore`: feature `twilightforest:legacy_redstone_ore`; placement: height uniform bottom+0..y=15 > in_square > count 8 > biome
