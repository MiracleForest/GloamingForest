# U1 `biomes`: Biomes and biome layers

Behaviour spec translated from the upstream 4.8 source (commit `9e40a757`). Checklist for the
U1 worker and the W2 audit (A1). Numbers are upstream's; "Bedrock:" paragraphs mark adaptation notes. The data tables in
section 6 are generated from upstream's biome JSON (`data/gloamingforest/worldgen/biome/*.json`), which F1 imports.

| | |
|---|---|
| Unit | U1 `biomes` (W1 batch 1) |
| Selftests | `biomes.*`, testPos dx `100-199` (owners rows `selftest biomes.*`, `selftest_dx 100-199`) |
| World DB | `gf:biomes:*` |
| Net channels | `biomes.*` |
| Config | section `biomes` (config_section `biomes`); upstream has no biome key (0 keys) |
| Contracts | worldgen (F5: `setBiomeSource`, `biomeAt`, `mainBiomeAt`), features + spawn (F5), client.render (F12 tints), client.world (F12 fog/sky providers shared with U15), data, import |
| Java sources | `world/components/layer/*.java` (BiomeDensitySource, RandomBiomeLayer, KeyBiomesLayer, CompanionBiomesLayer, BorderLayer, StabilizeLayer, SeamLayer, FilteredBiomeLayer, MedianLayer, IThornsTransformer), `world/components/layer/vanillalegacy/**` (Area, LazyArea, ZoomLayer, SmoothLayer, contexts, transformers), `world/components/biomesources/GloamingBiomeProvider.java`, `world/components/chunkgenerators/TerrainColumn.java`, `init/custom/{BiomeLayerStack,BiomeLayerTypes}.java`, `init/GloamingBiomes.java`, `world/registration/biomes/{BiomeMaker,BiomeHelper}.java`, `world/components/BiomeColorAlgorithms.java`, `client/FoliageColorHandler.java`, `GloamingEnumExtensions.java` (grass colour modifiers), `data/tags/BiomeTagGenerator.java` |

Not owned here: terrain shaping from the depth/scale values (U2's density function types `biome_driven_*`), the
features the biomes list (U2, U3, U6, U18, U19 and others own them), sky/fog/weather rendering and the aurora (U15),
biome progression locks and toasts (U14), the music event `gloamingforest:music.gloamingforest.gloaming_forest` (U10),
the landmark grid that picks which landmark sits in which biome (F6, using the `valid_*` tags below).

## 1. The 22 biomes

Biome ids (all owned): `gloamingforest:clearing`, `gloamingforest:dark_forest`, `gloamingforest:dark_forest_center`,
`gloamingforest:dense_forest`, `gloamingforest:dense_mushroom_forest`, `gloamingforest:enchanted_forest`,
`gloamingforest:final_plateau`, `gloamingforest:fire_swamp`, `gloamingforest:firefly_forest`, `gloamingforest:forest`,
`gloamingforest:glacier`, `gloamingforest:highlands`, `gloamingforest:highlands_underground`, `gloamingforest:lake`,
`gloamingforest:mushroom_forest`, `gloamingforest:oak_savannah`, `gloamingforest:snowy_forest`,
`gloamingforest:spooky_forest`, `gloamingforest:stream`, `gloamingforest:swamp`, `gloamingforest:thornlands`,
`gloamingforest:underground`. Lang keys `biome.gloamingforest.<id>` (22) are owned here.

Climate, colours, music, mood sound, particles, natural spawns, carvers and every feature list per generation step
are in section 6 (exact values). Every biome plays `gloamingforest:music.gloamingforest.gloaming_forest` (min 1200,
max 12000 ticks) and the cave mood sound (6000 ticks, extent 8, offset 2.0).

## 2. Biome source (`GloamingBiomeProvider` + `BiomeDensitySource`)

- Codec field `terrain_data` -> registry `biome_terrain_data` entry `biome_terrain_data/gloamingforest:biome_grid`
  (`BiomeDensitySource`: a list of `TerrainColumn`s + `biome_layer_config` = `gloamingforest:biomes_along_streams`).
- Horizontal biome: the layer stack's `LazyArea` (cache 25, per-layer salt) sampled at quart coordinates
  `(x >> 2, z >> 2)` gives the key biome; `getMainBiome` returns that column's main biome.
- Vertical biome (`getNoiseBiome(qx, qy, qz)`): the key biome's `TerrainColumn.getBiome(qy)` returns the layer whose
  key is nearest to the **absolute** quart y (`|key - qy|`, keys reduced in ascending order, ties keep the lower key; no
  layers -> the key biome). E.g. clearing (keys -3, -1): quart y >= -1 -> clearing, quart y <= -2 -> underground
  (block y < -4); the highlands use `gloamingforest:highlands_underground`; final plateau and thornlands have one
  layer. Keys are in the grid table (section 5).
- Terrain blending (`BiomeDensitySource.sampleTerrain(blockX, blockZ, ctx)`, used by U2's `biome_driven_terrain` /
  `biome_driven_noise`). Translate exactly; F5/U2 call it through `worldgen::setBiomeSource`. All arithmetic is in
  `double` unless marked:
  - Constants: `BLEND_RADIUS = 8.75`, `BLEND_RADIUS_INT = Mth.floor(8.75 + 1.0) = 9` (blocks), `BLOCK_XYZ_OFFSET =
    QuartPos.SIZE / 2 = 2`.
  - `bxo = blockX - 2` and `bzo = blockZ - 2` (ints). `xQuartStart = (bxo - 9) >> 2`, `xQuartEnd = (bxo + 9) >> 2`,
    likewise for z (arithmetic shifts). `xCount = xQuartEnd - xQuartStart + 1` (5 or 6), likewise `zCount`.
  - `xQuartDelta = (bxo - (xQuartStart << 2)) * 0.25` and `zQuartDelta` likewise. These are in **quart units**, and
    so is everything after them.
  - Loop order (x outer, z inner; it matters for the floating-point sums): `cx = 0..xCount-1`, `cz = 0..zCount-1`.
    `dX = xQuartDelta - cx`, `dZ = zQuartDelta - cz`, `distSq = dX*dX + dZ*dZ` (quart units).
  - A cell counts only if `distSq < 8.75 * 8.75` (= 76.5625, compared in quart units). With these bounds `|dX|, |dZ|
    <= 3`, so `distSq <= 18` and the test never rejects a cell; keep it anyway. The column is
    `getTerrainColumn(cx + xQuartStart, cz + zQuartStart)` at quart coordinates; skip the cell if absent.
  - With `w = column.weight(ctx)`, `d = column.depth(ctx)` and `s = column.scale(ctx)` (density functions evaluated
    at the caller's context, not at the cell):
    `falloff = 76.5625 * w * Math.exp((distSq * 2f + d) * -0.4f)` and `scaleFalloff = 76.5625 * w *
    Math.exp((distSq * 2f + s) * -0.4f)`.
    The literals are Java floats: `2f` is exactly 2.0, but `-0.4f` widens to `-0.4000000059604645`, not `-0.4`. Use
    `(double) -0.4f` for bit-exact parity. `Math.exp` is HotSpot's x64 intrinsic, which is not guaranteed to match the
    C runtime's `exp` in the last ulp. Check against the F13 height goldens; if they differ, use an `exp` that
    reproduces HotSpot's.
  - The multiplication order is `76.5625 * w`, then `*= exp(...)`.
  - Accumulate `totalDepth += d * falloff`, `totalContribution += falloff`, `totalScale += s * scaleFalloff` and
    `totalScaleContribution += scaleFalloff`.
  - Result: `DensityData(totalDepth / totalContribution, totalScale / totalScaleContribution)`.

Bedrock: implement as F5's `BiomeSourceFn` returning upstream biome ids per 4x4x4 cell; vanilla clients receive the biome
ids through Bedrock biome definitions shipped in the BP/RP (client biome JSON for colours and fog).

## 3. Layer stacks (registry `biome_layer_stack`)

Two registry entries (both owned): `biome_layer_stack/gloamingforest:random_forest_biomes` and
`biome_layer_stack/gloamingforest:biomes_along_streams`. Layer types (upstream registry `biome_layer_type`): `random_biomes`,
`key_biomes`, `companion_biomes`, `zoom`, `stabilize`, `border`, `seam`, `smooth`, `filtered`, `median`.

`gloamingforest:random_forest_biomes` (innermost first; salt in brackets):
1. `random_biomes` [1]: per pixel `nextRandom(15) == 0` -> rare biome (uniform from lake, dense mushroom forest,
   enchanted forest, clearing, spooky forest) else common (uniform from forest, dense forest, mushroom forest, oak
   savannah, firefly forest).
2. `key_biomes` [1000]: key biomes in list order `[0] glacier, [1] fire swamp, [2] dark forest center, [3] final
   plateau`. The parent is sampled at `(x, z)`: upstream's `AreaTransformer1.run` seeds the pixel RNG at `(x, z)`, and
   `getParentX/Y = x | 3` is not used for sampling. Per pixel, with a `java.util.Random`, not the layer context:
   - `rand = new Random(overworldSeed + (x & -4) * 25117L + (z & -4) * 151121L)`; `ox = nextInt(2) + 1`, then `oz =
     nextInt(2) + 1`.
   - `rand.setSeed(overworldSeed + (x / 8) * 25117L + (z / 8) * 151121L)`; `offset = nextInt(3)`. Here `x / 8` and
     `z / 8` are Java int division, which truncates toward zero (`-1 / 8 == 0`, `-9 / 8 == -1`). It is not floor or
     `>> 3`, so the quadrants next to the axes differ for negative coordinates. Products are in 64-bit.
   - If `(x & 3) == ox && (z & 3) == oz`: return `keyBiomes[(offset + quadrant) & 3]` with `quadrant = ((x & 4) != 0 ?
     2 : 0) + ((z & 4) != 0 ? 1 : 0)`. So (x&4 = 0, z&4 = 0) gets `offset`, (0, 4) gets `offset + 1`, (4, 0) gets
     `offset + 2` and (4, 4) gets `offset + 3`. Otherwise return the parent biome at `(x, z)`.
3. `companion_biomes` [1000]: a pixel not itself the key but touching it (4-neighbours) becomes the companion: fire
   swamp -> swamp, glacier -> snowy forest, dark forest center -> dark forest, final plateau -> highlands.
4. `zoom` [1000], `zoom` [1001] (not fuzzy; vanilla legacy zoom).
5. `stabilize` [700]: within the 3x3 around each 4x4 centre `((x + off + 1) & -4) - off`, copy the centre's biome
   (`off = (x << 4) & 3`).
6. `border` [500]: pixels that are not final plateau but touch it (4-neighbours, then diagonals) become thornlands.
7. `zoom` [1002], [1003], [1004], [1005].

`gloamingforest:biomes_along_streams`:
- `filtered` [100]: output = the stream biome where the "river" input is `gloamingforest:stream`, else the
  `fallback_layer` (`random_forest_biomes`).
- River input = `smooth` [7000] of `seam` [1] over `random_forest_biomes`: a pixel becomes `gloamingforest:stream`
  when any 4-neighbour differs, unless either side is lake, thornlands, clearing or oak savannah, or the pair is one of
  (snowy forest, glacier), (mushroom forest, dense mushroom forest), (swamp, fire swamp), (dark forest, dark forest
  center), (highlands, final plateau).
- `median` (`MedianLayer`, a registered layer type that the shipped stacks do not use): sample `biomes[pos] =
  parent(x + pos % 3, z + pos / 3)` for `pos` 0..8. Return the first biome, in index order, that occurs at least 5
  times of 9; otherwise return `biomes[0] = parent(x, z)`. The code's tie branch ("prefer index 5, else
  `nextRandom(2)`") never runs, because `biomeRecordCount` starts at 0 and every count is at least 1, so both
  comparisons are always false. The layer therefore consumes **no** context RNG. Translate this behaviour, not the
  intent of the comments.
- Vanilla legacy layer RNG (`LazyAreaContext`): world-seed mixing as Java's `LinearCongruentialGenerator`; keep it
  bit-exact (F5 `jrand` provides it) so biome maps match Java seeds.

Bedrock: the whole stack runs inside the F5 biome source; F13 goldens compare biome maps for fixed seeds.

## 4. Biome colours (client mod tints; F12 `tint::registerBlock` for grass/foliage-tinted blocks)

Grass colour modifiers (`BiomeSpecialEffects.GrassColorModifier` extensions) and foliage handler:

| Biome | Grass | Foliage |
|---|---|---|
| `gloamingforest:enchanted_forest` | `enchanted(color, x, z)`: bands by the distance to the nearest landmark centre (the quest grove); exact formula below the table | same (`enchanted(foliageColor, x, z)`) |
| `gloamingforest:swamp` | `((GrassColor.get(0.8, 0.9) & 0xFEFEFE) + 0x4E0E4E) / 2` | same with FoliageColor |
| `gloamingforest:dark_forest` | `((GrassColor.get(0.7, 0.8) & 0xFEFEFE) + 0x1E0E4E) / 2` | same with FoliageColor |
| `gloamingforest:dark_forest_center` | temperature noise `n = TEMPERATURE_NOISE(x * 0.0225, z * 0.0225)`: `n < -0.2 ? 0x667540 : 0x554114` | `(n + 1) / 2 < -0.1 ? 0xF9821E : 0xE94E14` |
| `gloamingforest:spooky_forest` | `blend(0xC43323, 0x5BC423, n' > 0.6 ? n' * 0.1 : n')` with `n' = (n + 1) / 2` | `blend(0xFF0101, 0x49FF01, n' > 0.6 ? n' * 0.2 : n')` |

Enchanted forest, exactly (`BiomeColorAlgorithms.enchanted(originalColor, x, z)`; both the grass colour modifier and
the foliage handler pass `(int) x`, `(int) z` of the block position, and the original grass or foliage colour):

```
center = LegacyLandmarkPlacements.getNearestCenterXZ(x / 16, z / 16)  // Java int division: truncates toward zero
dist   = (int) Mth.sqrt((float) ((cx - x) * (cx - x) + (cz - z) * (cz - z)))  // int sum -> float; Mth.sqrt =
                                                                      // (float) Math.sqrt(double); (int) truncates
c = dist * 16 % 512
if (c > 255) c = 511 - c
c = 255 - c
return (originalColor & 0xFFFF00) + c                                 // replaces the blue byte
```

- `getNearestCenterXZ` is F6's pure `gf::core::landmark::nearestCenter(chunkX, chunkZ)`. Its argument is `x / 16`
  with truncating division (C++ `int / int` truncates the same way). It is NOT `x >> 4` and not a floor division:
  for negative `x` or `z` that is not a multiple of 16 those give a chunk one lower. Near a region border that
  chunk belongs to the next landmark region, so the colour bands would be centred on another grid cell than in upstream.
- The squared distance is an `int` sum. `|cx - x|` and `|cz - z|` are at most 200 (the truncated chunk reaches one
  chunk past the region in negative regions, whose centres are offset by +9). Per region R: blocks span
  `[256R - 143, 256R + 112]` and centres `[256R - 39, 256R + 57]` for R < 0 (max 200); blocks `[-143, 127]` and
  centres `[-40, 56]` for R = 0 (max 199); blocks `[256R - 128, 256R + 127]` and centres `[256R - 40, 256R + 56]`
  for R > 0 (max 184). So nothing overflows and the sum (at most 80000) is converted to `float` exactly (below
  2^24). The square root is taken in double and rounded to float before truncation.

All other biomes use the JSON grass/foliage colours (or the vanilla climate colour maps). Vanilla clients see the
static `grass_color` / `foliage_color` from the client biome JSON.

## 5. Terrain grid (`biome_terrain_data/gloamingforest:biome_grid`)

Per key biome: `depth`, `scale`, `weight` (terrain blending) and the vertical biome layers. Values exactly as upstream:

| Key biome | depth | scale | weight | biome layers (offset -> biome) |
|---|---|---|---|---|
| `gloamingforest:clearing` | 0.05 | 1.15 | 1.0 | -1.0 -> `gloamingforest:clearing`, -3.0 -> `gloamingforest:underground` |
| `gloamingforest:dark_forest` | 0.1 | 1.25 | 0.5 | -1.0 -> `gloamingforest:dark_forest`, -3.0 -> `gloamingforest:underground` |
| `gloamingforest:dark_forest_center` | 0.1 | 1.125 | 1.0 | -1.0 -> `gloamingforest:dark_forest_center`, -3.0 -> `gloamingforest:underground` |
| `gloamingforest:dense_forest` | -0.8 | 3.9 | 1.0 | -1.8 -> `gloamingforest:dense_forest`, -3.8 -> `gloamingforest:underground` |
| `gloamingforest:dense_mushroom_forest` | 0.0 | 1.75 | 1.0 | -1.0 -> `gloamingforest:dense_mushroom_forest`, -3.0 -> `gloamingforest:underground` |
| `gloamingforest:enchanted_forest` | -0.5 | 4.0 | 1.0 | -1.5 -> `gloamingforest:enchanted_forest`, -3.5 -> `gloamingforest:underground` |
| `gloamingforest:final_plateau` | 12.0 | 0.75 | 1.0 | 0.0 -> `gloamingforest:final_plateau` |
| `gloamingforest:fire_swamp` | 0.2 | 1.25 | 1.0 | -1.0 -> `gloamingforest:fire_swamp`, -3.0 -> `gloamingforest:underground` |
| `gloamingforest:firefly_forest` | -0.75 | 4.15 | 1.0 | -1.75 -> `gloamingforest:firefly_forest`, -3.75 -> `gloamingforest:underground` |
| `gloamingforest:forest` | -0.7 | 4.2 | 1.0 | -1.7 -> `gloamingforest:forest`, -3.7 -> `gloamingforest:underground` |
| `gloamingforest:glacier` | -0.05 | 1.75 | 1.0 | -1.05 -> `gloamingforest:glacier`, -3.05 -> `gloamingforest:underground` |
| `gloamingforest:highlands` | 3.0 | 2.25 | 0.135 | -1.0 -> `gloamingforest:highlands`, -3.0 -> `gloamingforest:highlands_underground` |
| `gloamingforest:lake` | -2.2 | 2.0 | 1.0 | -3.2 -> `gloamingforest:lake`, -5.2 -> `gloamingforest:underground` |
| `gloamingforest:mushroom_forest` | 0.0 | 2.0 | 1.0 | -1.0 -> `gloamingforest:mushroom_forest`, -3.0 -> `gloamingforest:underground` |
| `gloamingforest:oak_savannah` | -0.05 | 2.0 | 1.0 | -1.05 -> `gloamingforest:oak_savannah`, -3.05 -> `gloamingforest:underground` |
| `gloamingforest:snowy_forest` | 0.0 | 2.45 | 0.5 | -1.0 -> `gloamingforest:snowy_forest`, -3.0 -> `gloamingforest:underground` |
| `gloamingforest:spooky_forest` | 0.0 | 2.25 | 1.0 | -1.0 -> `gloamingforest:spooky_forest`, -3.0 -> `gloamingforest:underground` |
| `gloamingforest:stream` | -0.1 | 0.001 | 1.35 | -1.1 -> `gloamingforest:stream`, -3.1 -> `gloamingforest:underground` |
| `gloamingforest:swamp` | -0.6 | 1.7 | 1.0 | -1.6 -> `gloamingforest:swamp`, -3.6 -> `gloamingforest:underground` |
| `gloamingforest:thornlands` | 5.5 | 1.75 | 1.15 | 0.0 -> `gloamingforest:thornlands` |

Other fields: {"biome_layer_config": "gloamingforest:biomes_along_streams"}

## 6. Biome data (generated from upstream's biome JSON)

#### Climate and effects

| Biome | temp | downfall | precip | sky | fog | water | water fog | grass | foliage | grass modifier | music | ambient / mood / additions | particle |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `gloamingforest:clearing` | 0.8 | 0.4 | yes | #20224A | #C0FFD8 | #3F76E4 | #050533 | - | - | - | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | - |
| `gloamingforest:dark_forest` | 0.7 | 0.8 | yes | #000000 | #000000 | #3F76E4 | #050533 | #4B6754 | #3B5E3F | gloamingforest:dark_forest | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | - |
| `gloamingforest:dark_forest_center` | 0.5 | 0.5 | yes | #000000 | #493000 | #3F76E4 | #050533 | #667540 | #F9821E | gloamingforest:dark_forest_center | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | - |
| `gloamingforest:dense_forest` | 0.7 | 0.8 | yes | #20224A | #C0FFD8 | #005522 | #005522 | - | - | - | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | gloamingforest:wandering_firefly p0.00025 |
| `gloamingforest:dense_mushroom_forest` | 0.8 | 1.0 | yes | #20224A | #C0FFD8 | #3F76E4 | #050533 | - | - | - | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | gloamingforest:wandering_firefly p0.00025 |
| `gloamingforest:enchanted_forest` | 0.5 | 0.5 | no | #20224A | #C0FFD8 | #3F76E4 | #050533 | #00FFFF | #00FFFF | gloamingforest:enchanted_forest | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | gloamingforest:wandering_firefly p0.00025 |
| `gloamingforest:final_plateau` | 1.0 | 0.2 | yes | #20224A | #C0FFD8 | #3F76E4 | #050533 | - | - | - | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | - |
| `gloamingforest:fire_swamp` | 1.0 | 0.4 | no | #20224A | #380A00 | #6C2C2C | #6C2C2C | #572E23 | #64260F | - | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | white_ash p0.05 |
| `gloamingforest:firefly_forest` | 0.5 | 1.0 | yes | #20224A | #C0FFD8 | #3F76E4 | #050533 | - | - | - | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | gloamingforest:wandering_firefly p0.001 |
| `gloamingforest:forest` | 0.5 | 0.5 | yes | #20224A | #C0FFD8 | #3F76E4 | #050533 | - | - | - | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | gloamingforest:wandering_firefly p0.00025 |
| `gloamingforest:glacier` | 0.08 | 0.1 | yes | #130D28 | #361F88 | #3F76E4 | #050533 | - | - | - | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | - |
| `gloamingforest:highlands` | 0.4 | 0.7 | yes | #20224A | #C0FFD8 | #3F76E4 | #050533 | - | - | - | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | - |
| `gloamingforest:highlands_underground` | 0.35 | 0.0 | yes | #20224A | #C0FFD8 | #3F76E4 | #050533 | - | - | - | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | - |
| `gloamingforest:lake` | 0.66 | 1.0 | yes | #20224A | #C0FFD8 | #3F76E4 | #050533 | - | - | - | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | - |
| `gloamingforest:mushroom_forest` | 0.8 | 0.8 | yes | #20224A | #C0FFD8 | #3F76E4 | #050533 | - | - | - | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | gloamingforest:wandering_firefly p0.00025 |
| `gloamingforest:oak_savannah` | 0.9 | 0.0 | yes | #20224A | #C0FFD8 | #3F76E4 | #050533 | - | - | - | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | - |
| `gloamingforest:snowy_forest` | 0.09 | 0.9 | yes | #808080 | #FFFFFF | #3F76E4 | #050533 | #FFFFFF | #FFFFFF | - | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | - |
| `gloamingforest:spooky_forest` | 0.5 | 1.0 | yes | #20224A | #827391 | #BC8857 | #BC8857 | #C45123 | #FF8501 | gloamingforest:spooky_forest | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | - |
| `gloamingforest:stream` | 0.5 | 0.1 | yes | #20224A | #C0FFD8 | #3F76E4 | #050533 | - | - | - | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | - |
| `gloamingforest:swamp` | 0.8 | 0.9 | yes | #002112 | #003F21 | #95B55F | #95B55F | #5C694E | #496137 | gloamingforest:swamp | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | - |
| `gloamingforest:thornlands` | 0.3 | 0.2 | yes | #20224A | #C0FFD8 | #3F76E4 | #050533 | - | - | - | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | - |
| `gloamingforest:underground` | 0.7 | 0.0 | yes | #20224A | #C0FFD8 | #3F76E4 | #050533 | - | - | - | gloamingforest:music.gloamingforest.gloaming_forest (1200-12000) | mood ambient.cave (6000, 8, 2.0) | - |

#### Mob spawns (natural spawning; landmarks override with controlled spawns)

| Biome | Spawners (`type weight min-max`) | Spawn costs | Creature probability |
|---|---|---|---|
| `gloamingforest:clearing` | **creature**: gloamingforest:bighorn_sheep 12 4-4, gloamingforest:boar 10 4-4, chicken 10 4-4, gloamingforest:deer 15 4-5, wolf 5 4-4, gloamingforest:tiny_bird 15 4-8, gloamingforest:squirrel 10 2-4, gloamingforest:dwarf_rabbit 10 4-5, gloamingforest:raven 10 1-2 | - | 0.15 |
| `gloamingforest:dark_forest` | **monster**: enderman 2 1-2, zombie 5 1-2, skeleton 5 1-2, gloamingforest:mist_wolf 5 1-1, gloamingforest:skeleton_druid 5 1-1, gloamingforest:king_spider 1 1-1, gloamingforest:kobold 10 1-3, witch 2 1-1 | enderman 0.15/0.75, skeleton 0.15/0.8, witch 0.15/0.75, zombie 0.15/0.7, gloamingforest:king_spider 0.25/0.85, gloamingforest:kobold 0.15/0.7, gloamingforest:mist_wolf 0.2/0.75, gloamingforest:skeleton_druid 0.2/0.8 | 0.05 |
| `gloamingforest:dark_forest_center` | - | - | - |
| `gloamingforest:dense_forest` | **creature**: gloamingforest:bighorn_sheep 12 4-4, gloamingforest:boar 10 4-4, chicken 10 4-4, gloamingforest:deer 15 4-5, wolf 5 4-4, gloamingforest:tiny_bird 15 4-8, gloamingforest:squirrel 10 2-4, gloamingforest:dwarf_rabbit 10 4-5, gloamingforest:raven 10 1-2 | - | 0.15 |
| `gloamingforest:dense_mushroom_forest` | **creature**: gloamingforest:bighorn_sheep 12 4-4, gloamingforest:boar 10 4-4, chicken 10 4-4, gloamingforest:deer 15 4-5, wolf 5 4-4, gloamingforest:tiny_bird 15 4-8, gloamingforest:squirrel 10 2-4, gloamingforest:dwarf_rabbit 10 4-5, gloamingforest:raven 10 1-2 | - | 0.15 |
| `gloamingforest:enchanted_forest` | **creature**: gloamingforest:bighorn_sheep 12 4-4, gloamingforest:boar 10 4-4, chicken 10 4-4, gloamingforest:deer 15 4-5, wolf 5 4-4, gloamingforest:tiny_bird 15 4-8, gloamingforest:squirrel 10 2-4, gloamingforest:dwarf_rabbit 10 4-5, gloamingforest:raven 10 1-2 | - | 0.15 |
| `gloamingforest:final_plateau` | **creature**: gloamingforest:raven 10 4-4 | - | 0.3 |
| `gloamingforest:fire_swamp` | - | - | - |
| `gloamingforest:firefly_forest` | **creature**: gloamingforest:bighorn_sheep 12 4-4, gloamingforest:boar 10 4-4, chicken 10 4-4, gloamingforest:deer 15 4-5, wolf 5 4-4, gloamingforest:tiny_bird 15 4-8, gloamingforest:squirrel 10 2-4, gloamingforest:dwarf_rabbit 10 4-5, gloamingforest:raven 10 1-2 | - | 0.15 |
| `gloamingforest:forest` | **creature**: gloamingforest:bighorn_sheep 12 4-4, gloamingforest:boar 10 4-4, chicken 10 4-4, gloamingforest:deer 15 4-5, wolf 5 4-4, gloamingforest:tiny_bird 15 4-8, gloamingforest:squirrel 10 2-4, gloamingforest:dwarf_rabbit 10 4-5, gloamingforest:raven 10 1-2 | - | 0.15 |
| `gloamingforest:glacier` | **creature**: gloamingforest:penguin 10 2-4 | - | 0.15 |
| `gloamingforest:highlands` | **creature**: gloamingforest:bighorn_sheep 12 4-4, gloamingforest:boar 10 4-4, chicken 10 4-4, gloamingforest:deer 15 4-5, wolf 5 4-4, gloamingforest:tiny_bird 15 4-8, gloamingforest:squirrel 10 2-4, gloamingforest:dwarf_rabbit 10 4-5, gloamingforest:raven 10 1-2 | - | 0.15 |
| `gloamingforest:highlands_underground` | **ambient**: bat 10 1-1; **monster**: spider 10 2-3, zombie 10 1-2, skeleton 10 1-1, creeper 1 1-1, slime 10 2-4, enderman 1 1-2, gloamingforest:kobold 10 1-3 | creeper 0.15/0.35, enderman 0.15/0.4, skeleton 0.15/0.3, slime 0.15/0.2, spider 0.15/0.2, zombie 0.15/0.2, gloamingforest:kobold 0.15/0.2 | - |
| `gloamingforest:lake` | **creature**: gloamingforest:bighorn_sheep 12 4-4, gloamingforest:boar 10 4-4, chicken 10 4-4, gloamingforest:deer 15 4-5, wolf 5 4-4, gloamingforest:tiny_bird 15 4-8, gloamingforest:squirrel 10 2-4, gloamingforest:dwarf_rabbit 10 4-5, gloamingforest:raven 10 1-2 | - | 0.15 |
| `gloamingforest:mushroom_forest` | **creature**: gloamingforest:bighorn_sheep 12 4-4, gloamingforest:boar 10 4-4, chicken 10 4-4, gloamingforest:deer 15 4-5, wolf 5 4-4, gloamingforest:tiny_bird 15 4-8, gloamingforest:squirrel 10 2-4, gloamingforest:dwarf_rabbit 10 4-5, gloamingforest:raven 10 1-2 | - | 0.15 |
| `gloamingforest:oak_savannah` | **creature**: gloamingforest:bighorn_sheep 12 4-4, gloamingforest:boar 10 4-4, chicken 10 4-4, gloamingforest:deer 15 4-5, wolf 5 4-4, gloamingforest:tiny_bird 15 4-8, gloamingforest:squirrel 10 2-4, gloamingforest:dwarf_rabbit 10 4-5, gloamingforest:raven 10 1-2 | - | 0.15 |
| `gloamingforest:snowy_forest` | **monster**: gloamingforest:winter_wolf 5 1-1, gloamingforest:yeti 5 1-1 | gloamingforest:winter_wolf 0.15/0.6, gloamingforest:yeti 0.15/0.6 | 0.05 |
| `gloamingforest:spooky_forest` | **ambient**: bat 20 2-4; **monster**: spider 50 1-2, skeleton 20 1-2, gloamingforest:skeleton_druid 5 1-1 | skeleton 0.25/0.85, spider 0.25/0.75, gloamingforest:skeleton_druid 0.25/0.95 | - |
| `gloamingforest:stream` | **creature**: gloamingforest:bighorn_sheep 12 4-4, gloamingforest:boar 10 4-4, chicken 10 4-4, gloamingforest:deer 15 4-5, wolf 5 4-4, gloamingforest:tiny_bird 15 4-8, gloamingforest:squirrel 10 2-4, gloamingforest:dwarf_rabbit 10 4-5, gloamingforest:raven 10 1-2 | - | 0.15 |
| `gloamingforest:swamp` | **monster**: zombie 10 1-2, gloamingforest:mosquito_swarm 10 1-1 | zombie 0.15/0.7, gloamingforest:mosquito_swarm 0.15/0.7 | - |
| `gloamingforest:thornlands` | - | - | - |
| `gloamingforest:underground` | **ambient**: bat 10 1-1; **monster**: spider 10 2-3, zombie 10 1-2, skeleton 10 1-1, creeper 1 1-1, slime 10 2-4, enderman 1 1-2, gloamingforest:kobold 10 1-3 | creeper 0.15/0.35, enderman 0.15/0.4, skeleton 0.15/0.3, slime 0.15/0.2, spider 0.15/0.2, zombie 0.15/0.2, gloamingforest:kobold 0.15/0.2 | - |

#### Carvers and features by generation step

- `gloamingforest:clearing`: carvers gloamingforest:gf_caves; lakes: gloamingforest:water_lake | surface_structures: gloamingforest:druid_hut, gloamingforest:well_placer, gloamingforest:foundation | underground_ores: disk_sand, disk_clay, disk_gravel, gloamingforest:wood_roots, gloamingforest:legacy_coal_ore, gloamingforest:legacy_iron_ore, gloamingforest:legacy_gold_ore, gloamingforest:legacy_redstone_ore, gloamingforest:legacy_diamond_ore, gloamingforest:legacy_lapis_ore, gloamingforest:legacy_copper_ore, gloamingforest:small_andesite, gloamingforest:small_diorite, gloamingforest:small_granite | underground_decoration: gloamingforest:plant_roots, gloamingforest:torch_berries, gloamingforest:iron_oreberries, gloamingforest:gold_oreberries, gloamingforest:copper_oreberries | vegetal_decoration: patch_tall_grass, gloamingforest:patch_grass_badlands, gloamingforest:patch_grass_savanna, patch_sugar_cane, gloamingforest:raspberry_bushes, gloamingforest:blueberry_bushes, gloamingforest:patch_grass_forest, gloamingforest:mayapple, gloamingforest:flower_placer, gloamingforest:grove_ruins, gloamingforest:stone_circle, gloamingforest:outside_stalagmite, gloamingforest:monolith, gloamingforest:hollow_stump, gloamingforest:hollow_log, flower_forest_flowers, gloamingforest:default_fallen_logs | top_layer_modification: freeze_top_layer
- `gloamingforest:dark_forest`: carvers gloamingforest:gf_caves; underground_ores: gloamingforest:legacy_coal_ore, gloamingforest:legacy_iron_ore, gloamingforest:legacy_gold_ore, gloamingforest:legacy_redstone_ore, gloamingforest:legacy_diamond_ore, gloamingforest:legacy_lapis_ore, gloamingforest:legacy_copper_ore | underground_decoration: gloamingforest:plant_roots, gloamingforest:torch_berries | vegetal_decoration: patch_grass_normal, gloamingforest:tree/selector/dark_forest_tree_mix, gloamingforest:tree/selector/dark_forest_trees, gloamingforest:tree/darkwood_tree, gloamingforest:dark_grass, gloamingforest:dark_dead_bushes, gloamingforest:dark_pumpkins, gloamingforest:dark_mushglooms, gloamingforest:dark_brown_mushrooms, gloamingforest:dark_red_mushrooms
- `gloamingforest:dark_forest_center`: carvers gloamingforest:gf_caves; underground_ores: gloamingforest:legacy_coal_ore, gloamingforest:legacy_iron_ore, gloamingforest:legacy_gold_ore, gloamingforest:legacy_redstone_ore, gloamingforest:legacy_diamond_ore, gloamingforest:legacy_lapis_ore, gloamingforest:legacy_copper_ore | underground_decoration: gloamingforest:plant_roots, gloamingforest:torch_berries | vegetal_decoration: patch_grass_normal, gloamingforest:tree/selector/dark_forest_tree_mix, gloamingforest:tree/selector/dark_forest_trees, gloamingforest:tree/darkwood_tree, gloamingforest:dark_grass, gloamingforest:dark_dead_bushes, gloamingforest:dark_pumpkins, gloamingforest:dark_mushglooms, gloamingforest:dark_brown_mushrooms, gloamingforest:dark_red_mushrooms
- `gloamingforest:dense_forest`: carvers gloamingforest:gf_caves; lakes: gloamingforest:water_lake | surface_structures: gloamingforest:druid_hut, gloamingforest:well_placer, gloamingforest:foundation | underground_ores: disk_sand, disk_clay, disk_gravel, gloamingforest:wood_roots, gloamingforest:legacy_coal_ore, gloamingforest:legacy_iron_ore, gloamingforest:legacy_gold_ore, gloamingforest:legacy_redstone_ore, gloamingforest:legacy_diamond_ore, gloamingforest:legacy_lapis_ore, gloamingforest:legacy_copper_ore, gloamingforest:small_andesite, gloamingforest:small_diorite, gloamingforest:small_granite | underground_decoration: gloamingforest:plant_roots, gloamingforest:torch_berries, gloamingforest:iron_oreberries, gloamingforest:gold_oreberries, gloamingforest:copper_oreberries | vegetal_decoration: patch_tall_grass, gloamingforest:patch_grass_badlands, gloamingforest:patch_grass_savanna, patch_sugar_cane, gloamingforest:raspberry_bushes, gloamingforest:blueberry_bushes, gloamingforest:blackberry_bushes, gloamingforest:patch_grass_jungle, patch_large_fern, gloamingforest:mayapple, gloamingforest:flower_placer_alt, gloamingforest:grove_ruins, gloamingforest:stone_circle, gloamingforest:outside_stalagmite, gloamingforest:monolith, gloamingforest:hollow_stump, gloamingforest:hollow_log, gloamingforest:tree/selector/dense_canopy_trees, gloamingforest:tree/selector/vanilla_trees, gloamingforest:tree/oak_bush_dense, gloamingforest:default_fallen_logs, gloamingforest:tree/forest_mega_oak_tree, gloamingforest:tree/mega_canopy_tree | top_layer_modification: freeze_top_layer
- `gloamingforest:dense_mushroom_forest`: carvers gloamingforest:gf_caves; lakes: gloamingforest:water_lake | surface_structures: gloamingforest:druid_hut, gloamingforest:well_placer, gloamingforest:foundation | underground_ores: disk_sand, disk_clay, disk_gravel, gloamingforest:wood_roots, gloamingforest:legacy_coal_ore, gloamingforest:legacy_iron_ore, gloamingforest:legacy_gold_ore, gloamingforest:legacy_redstone_ore, gloamingforest:legacy_diamond_ore, gloamingforest:legacy_lapis_ore, gloamingforest:legacy_copper_ore, gloamingforest:small_andesite, gloamingforest:small_diorite, gloamingforest:small_granite | underground_decoration: gloamingforest:plant_roots, gloamingforest:torch_berries, gloamingforest:iron_oreberries, gloamingforest:gold_oreberries, gloamingforest:copper_oreberries | vegetal_decoration: patch_tall_grass, gloamingforest:patch_grass_badlands, gloamingforest:patch_grass_savanna, patch_sugar_cane, gloamingforest:blackberry_bushes, gloamingforest:patch_grass_jungle, gloamingforest:mayapple, gloamingforest:flower_placer_alt, gloamingforest:grove_ruins, gloamingforest:stone_circle, gloamingforest:outside_stalagmite, gloamingforest:monolith, gloamingforest:hollow_stump, gloamingforest:hollow_log, gloamingforest:mycelium_blob, gloamingforest:tree/selector/vanilla_trees, gloamingforest:tree/gloaming_oak_tree, gloamingforest:tree/large_gloaming_oak_tree, brown_mushroom_normal, red_mushroom_normal, brown_mushroom_taiga, red_mushroom_taiga, gloamingforest:tree/selector/vanilla_mushrooms, gloamingforest:mushroom/canopy_mushrooms_dense, gloamingforest:mushgloom_cluster, gloamingforest:tree/selector/canopy_trees, gloamingforest:default_fallen_logs, gloamingforest:tree/mega_canopy_tree | top_layer_modification: freeze_top_layer
- `gloamingforest:enchanted_forest`: carvers gloamingforest:gf_caves; lakes: gloamingforest:dense_water_lake | surface_structures: gloamingforest:druid_hut, gloamingforest:well_placer, gloamingforest:foundation | underground_ores: disk_sand, disk_clay, disk_gravel, gloamingforest:wood_roots, gloamingforest:legacy_coal_ore, gloamingforest:legacy_iron_ore, gloamingforest:legacy_gold_ore, gloamingforest:legacy_redstone_ore, gloamingforest:legacy_diamond_ore, gloamingforest:legacy_lapis_ore, gloamingforest:legacy_copper_ore, gloamingforest:small_andesite, gloamingforest:small_diorite, gloamingforest:small_granite | underground_decoration: gloamingforest:plant_roots, gloamingforest:torch_berries, gloamingforest:iron_oreberries_enchanted_forest, gloamingforest:gold_oreberries_enchanted_forest, gloamingforest:copper_oreberries_enchanted_forest, gloamingforest:essence_oreberries_enchanted_forest | vegetal_decoration: patch_grass_badlands, patch_sugar_cane, gloamingforest:dense_ferns, gloamingforest:dense_large_ferns, gloamingforest:flower_placer, gloamingforest:grove_ruins, gloamingforest:stone_circle, gloamingforest:outside_stalagmite, gloamingforest:monolith, gloamingforest:hollow_stump, gloamingforest:hollow_log, gloamingforest:tree/selector/enchanted_forest_trees, gloamingforest:tree/selector/dense_canopy_trees, gloamingforest:fiddlehead, gloamingforest:tree/selector/canopy_trees, gloamingforest:default_fallen_logs | top_layer_modification: freeze_top_layer, gloamingforest:enchanted_forest_vines
- `gloamingforest:final_plateau`: carvers none; 
- `gloamingforest:fire_swamp`: carvers gloamingforest:gf_caves; lakes: gloamingforest:lava_lake | underground_ores: disk_sand, disk_clay, disk_gravel, gloamingforest:wood_roots, gloamingforest:legacy_coal_ore, gloamingforest:legacy_iron_ore, gloamingforest:legacy_gold_ore, gloamingforest:legacy_redstone_ore, gloamingforest:legacy_diamond_ore, gloamingforest:legacy_lapis_ore, gloamingforest:legacy_copper_ore, gloamingforest:small_andesite, gloamingforest:small_diorite, gloamingforest:small_granite | underground_decoration: gloamingforest:plant_roots, gloamingforest:torch_berries | vegetal_decoration: patch_tall_grass, gloamingforest:patch_grass_badlands, gloamingforest:patch_grass_savanna, patch_sugar_cane, patch_grass_taiga_2, gloamingforest:stone_circle, gloamingforest:outside_stalagmite, gloamingforest:monolith, gloamingforest:hollow_stump, gloamingforest:hollow_log, gloamingforest:tree/swampy_oak_tree, gloamingforest:tree/oak_bush, gloamingforest:fire_jet, gloamingforest:smoker, patch_sugar_cane_swamp, vines, brown_mushroom_swamp | top_layer_modification: freeze_top_layer
- `gloamingforest:firefly_forest`: carvers gloamingforest:gf_caves; lakes: gloamingforest:water_lake | surface_structures: gloamingforest:druid_hut, gloamingforest:well_placer, gloamingforest:foundation | underground_ores: disk_sand, disk_clay, disk_gravel, gloamingforest:wood_roots, gloamingforest:legacy_coal_ore, gloamingforest:legacy_iron_ore, gloamingforest:legacy_gold_ore, gloamingforest:legacy_redstone_ore, gloamingforest:legacy_diamond_ore, gloamingforest:legacy_lapis_ore, gloamingforest:legacy_copper_ore, gloamingforest:small_andesite, gloamingforest:small_diorite, gloamingforest:small_granite | underground_decoration: gloamingforest:plant_roots, gloamingforest:torch_berries, gloamingforest:iron_oreberries, gloamingforest:gold_oreberries, gloamingforest:copper_oreberries | vegetal_decoration: patch_tall_grass, gloamingforest:patch_grass_badlands, gloamingforest:patch_grass_savanna, patch_sugar_cane, gloamingforest:blackberry_bushes, gloamingforest:patch_grass_jungle, gloamingforest:mayapple, gloamingforest:flower_placer_alt, gloamingforest:grove_ruins, gloamingforest:stone_circle, gloamingforest:outside_stalagmite, gloamingforest:monolith, gloamingforest:hollow_stump, gloamingforest:hollow_log, gloamingforest:tree/selector/firefly_forest_trees, gloamingforest:tree/selector/vanilla_trees, gloamingforest:tree/gloaming_oak_tree, gloamingforest:tree/large_gloaming_oak_tree, gloamingforest:lamppost_placer, gloamingforest:mushgloom_cluster, patch_pumpkin, flower_forest_flowers, gloamingforest:default_fallen_logs | top_layer_modification: freeze_top_layer
- `gloamingforest:forest`: carvers gloamingforest:gf_caves; lakes: gloamingforest:water_lake | surface_structures: gloamingforest:druid_hut, gloamingforest:well_placer, gloamingforest:foundation | underground_ores: disk_sand, disk_clay, disk_gravel, gloamingforest:wood_roots, gloamingforest:legacy_coal_ore, gloamingforest:legacy_iron_ore, gloamingforest:legacy_gold_ore, gloamingforest:legacy_redstone_ore, gloamingforest:legacy_diamond_ore, gloamingforest:legacy_lapis_ore, gloamingforest:legacy_copper_ore, gloamingforest:small_andesite, gloamingforest:small_diorite, gloamingforest:small_granite | underground_decoration: gloamingforest:plant_roots, gloamingforest:torch_berries, gloamingforest:iron_oreberries, gloamingforest:gold_oreberries, gloamingforest:copper_oreberries | vegetal_decoration: patch_tall_grass, gloamingforest:patch_grass_badlands, gloamingforest:patch_grass_savanna, patch_sugar_cane, gloamingforest:blueberry_bushes, gloamingforest:patch_grass_jungle, patch_large_fern, gloamingforest:mayapple, gloamingforest:flower_placer_alt, gloamingforest:grove_ruins, gloamingforest:stone_circle, gloamingforest:outside_stalagmite, gloamingforest:monolith, gloamingforest:hollow_stump, gloamingforest:hollow_log, gloamingforest:tree/selector/vanilla_trees, gloamingforest:tree/gloaming_oak_tree, gloamingforest:tree/large_gloaming_oak_tree, gloamingforest:tree/selector/canopy_trees, gloamingforest:default_fallen_logs, gloamingforest:tree/mega_canopy_tree | top_layer_modification: freeze_top_layer
- `gloamingforest:glacier`: carvers gloamingforest:gf_caves; underground_ores: gloamingforest:legacy_coal_ore, gloamingforest:legacy_iron_ore, gloamingforest:legacy_gold_ore, gloamingforest:legacy_redstone_ore, gloamingforest:legacy_diamond_ore, gloamingforest:legacy_lapis_ore, gloamingforest:legacy_copper_ore | underground_decoration: gloamingforest:plant_roots, gloamingforest:torch_berries | top_layer_modification: freeze_top_layer
- `gloamingforest:highlands`: carvers gloamingforest:highland_caves; surface_structures: gloamingforest:druid_hut, gloamingforest:well_placer, gloamingforest:foundation | underground_ores: gloamingforest:legacy_coal_ore, gloamingforest:legacy_iron_ore, gloamingforest:legacy_gold_ore, gloamingforest:legacy_redstone_ore, gloamingforest:legacy_diamond_ore, gloamingforest:legacy_lapis_ore, gloamingforest:legacy_copper_ore, gloamingforest:small_andesite, gloamingforest:small_diorite, gloamingforest:small_granite | underground_decoration: gloamingforest:troll_roots | vegetal_decoration: gloamingforest:blueberry_bushes, patch_grass_taiga, patch_large_fern, gloamingforest:grove_ruins, gloamingforest:stone_circle, gloamingforest:outside_stalagmite, gloamingforest:monolith, gloamingforest:hollow_stump, gloamingforest:hollow_log, gloamingforest:tree/selector/highlands_trees, forest_rock, gloamingforest:sparse_mushglooms, gloamingforest:spruce_fallen_log, gloamingforest:dark_ferns, gloamingforest:troll_mushglooms
- `gloamingforest:highlands_underground`: carvers gloamingforest:highland_caves; underground_ores: gloamingforest:legacy_coal_ore, gloamingforest:legacy_iron_ore, gloamingforest:legacy_gold_ore, gloamingforest:legacy_redstone_ore, gloamingforest:legacy_diamond_ore, gloamingforest:legacy_lapis_ore, gloamingforest:legacy_copper_ore | underground_decoration: gloamingforest:troll_roots | vegetal_decoration: gloamingforest:troll_mushglooms
- `gloamingforest:lake`: carvers none; underground_ores: gloamingforest:legacy_coal_ore, gloamingforest:legacy_iron_ore, gloamingforest:legacy_gold_ore, gloamingforest:legacy_redstone_ore, gloamingforest:legacy_diamond_ore, gloamingforest:legacy_lapis_ore, gloamingforest:legacy_copper_ore, gloamingforest:small_andesite, gloamingforest:small_diorite, gloamingforest:small_granite | underground_decoration: gloamingforest:iron_oreberries, gloamingforest:gold_oreberries, gloamingforest:copper_oreberries | vegetal_decoration: seagrass_deep, seagrass_simple | top_layer_modification: freeze_top_layer
- `gloamingforest:mushroom_forest`: carvers gloamingforest:gf_caves; lakes: gloamingforest:water_lake | surface_structures: gloamingforest:druid_hut, gloamingforest:well_placer, gloamingforest:foundation | underground_ores: disk_sand, disk_clay, disk_gravel, gloamingforest:wood_roots, gloamingforest:legacy_coal_ore, gloamingforest:legacy_iron_ore, gloamingforest:legacy_gold_ore, gloamingforest:legacy_redstone_ore, gloamingforest:legacy_diamond_ore, gloamingforest:legacy_lapis_ore, gloamingforest:legacy_copper_ore, gloamingforest:small_andesite, gloamingforest:small_diorite, gloamingforest:small_granite | underground_decoration: gloamingforest:plant_roots, gloamingforest:torch_berries, gloamingforest:iron_oreberries, gloamingforest:gold_oreberries, gloamingforest:copper_oreberries | vegetal_decoration: patch_tall_grass, gloamingforest:patch_grass_badlands, gloamingforest:patch_grass_savanna, patch_sugar_cane, gloamingforest:raspberry_bushes, gloamingforest:blueberry_bushes, gloamingforest:blackberry_bushes, gloamingforest:patch_grass_jungle, gloamingforest:mayapple, gloamingforest:flower_placer_alt, gloamingforest:grove_ruins, gloamingforest:stone_circle, gloamingforest:outside_stalagmite, gloamingforest:monolith, gloamingforest:hollow_stump, gloamingforest:hollow_log, gloamingforest:mycelium_blob, gloamingforest:tree/selector/vanilla_trees, gloamingforest:tree/gloaming_oak_tree, gloamingforest:tree/large_gloaming_oak_tree, brown_mushroom_normal, red_mushroom_normal, brown_mushroom_taiga, red_mushroom_taiga, gloamingforest:tree/selector/vanilla_mushrooms, gloamingforest:mushroom/canopy_mushrooms_sparse, gloamingforest:mushgloom_cluster, gloamingforest:tree/selector/canopy_trees, gloamingforest:default_fallen_logs, gloamingforest:tree/mega_canopy_tree | top_layer_modification: freeze_top_layer
- `gloamingforest:oak_savannah`: carvers gloamingforest:gf_caves; lakes: gloamingforest:water_lake | surface_structures: gloamingforest:druid_hut, gloamingforest:well_placer, gloamingforest:foundation | underground_ores: disk_sand, disk_clay, disk_gravel, gloamingforest:wood_roots, gloamingforest:legacy_coal_ore, gloamingforest:legacy_iron_ore, gloamingforest:legacy_gold_ore, gloamingforest:legacy_redstone_ore, gloamingforest:legacy_diamond_ore, gloamingforest:legacy_lapis_ore, gloamingforest:legacy_copper_ore, gloamingforest:small_andesite, gloamingforest:small_diorite, gloamingforest:small_granite | underground_decoration: gloamingforest:plant_roots, gloamingforest:torch_berries, gloamingforest:iron_oreberries, gloamingforest:gold_oreberries, gloamingforest:copper_oreberries | vegetal_decoration: patch_tall_grass, gloamingforest:patch_grass_badlands, gloamingforest:patch_grass_savanna, patch_sugar_cane, gloamingforest:patch_grass_taiga_2, gloamingforest:mayapple, gloamingforest:flower_placer, gloamingforest:grove_ruins, gloamingforest:stone_circle, gloamingforest:outside_stalagmite, gloamingforest:monolith, gloamingforest:hollow_stump, gloamingforest:hollow_log, gloamingforest:tree/savannah_oak_tree, gloamingforest:tree/large_gloaming_oak_tree, gloamingforest:default_fallen_logs, gloamingforest:tree/savannah_mega_oak_tree | top_layer_modification: freeze_top_layer
- `gloamingforest:snowy_forest`: carvers gloamingforest:gf_caves; lakes: gloamingforest:frozen_lake | underground_ores: gloamingforest:legacy_coal_ore, gloamingforest:legacy_iron_ore, gloamingforest:legacy_gold_ore, gloamingforest:legacy_redstone_ore, gloamingforest:legacy_diamond_ore, gloamingforest:legacy_lapis_ore, gloamingforest:legacy_copper_ore | underground_decoration: gloamingforest:plant_roots, gloamingforest:torch_berries | vegetal_decoration: gloamingforest:snowy_blueberry_bushes, gloamingforest:maloberry_bushes, gloamingforest:tree/selector/snowy_forest_trees, gloamingforest:spruce_fallen_log | top_layer_modification: gloamingforest:snow_under_trees, freeze_top_layer
- `gloamingforest:spooky_forest`: carvers gloamingforest:gf_caves; surface_structures: gloamingforest:graveyard | underground_ores: disk_sand, disk_clay, disk_gravel, gloamingforest:wood_roots, gloamingforest:legacy_coal_ore, gloamingforest:legacy_iron_ore, gloamingforest:legacy_gold_ore, gloamingforest:legacy_redstone_ore, gloamingforest:legacy_diamond_ore, gloamingforest:legacy_lapis_ore, gloamingforest:legacy_copper_ore, gloamingforest:small_andesite, gloamingforest:small_diorite, gloamingforest:small_granite | underground_decoration: gloamingforest:plant_roots, gloamingforest:torch_berries | vegetal_decoration: patch_tall_grass, gloamingforest:patch_grass_badlands, gloamingforest:patch_grass_savanna, patch_sugar_cane, gloamingforest:patch_grass_jungle, gloamingforest:mayapple, gloamingforest:tree/gloaming_oak_tree, gloamingforest:tree/large_gloaming_oak_tree, gloamingforest:tree/dead_canopy_tree, gloamingforest:pumpkin_lamppost, gloamingforest:gf_oak_fallen_log, gloamingforest:canopy_fallen_log, gloamingforest:webs, gloamingforest:fallen_leaves, patch_pumpkin, patch_dead_bush | top_layer_modification: freeze_top_layer
- `gloamingforest:stream`: carvers none; underground_ores: gloamingforest:legacy_coal_ore, gloamingforest:legacy_iron_ore, gloamingforest:legacy_gold_ore, gloamingforest:legacy_redstone_ore, gloamingforest:legacy_diamond_ore, gloamingforest:legacy_lapis_ore, gloamingforest:legacy_copper_ore, gloamingforest:small_andesite, gloamingforest:small_diorite, gloamingforest:small_granite | underground_decoration: gloamingforest:iron_oreberries, gloamingforest:gold_oreberries, gloamingforest:copper_oreberries | vegetal_decoration: seagrass_normal, seagrass_simple | top_layer_modification: freeze_top_layer
- `gloamingforest:swamp`: carvers gloamingforest:gf_caves; surface_structures: gloamingforest:druid_hut, gloamingforest:well_placer, gloamingforest:foundation | underground_ores: disk_sand, disk_clay, disk_gravel, gloamingforest:wood_roots, gloamingforest:legacy_coal_ore, gloamingforest:legacy_iron_ore, gloamingforest:legacy_gold_ore, gloamingforest:legacy_redstone_ore, gloamingforest:legacy_diamond_ore, gloamingforest:legacy_lapis_ore, gloamingforest:legacy_copper_ore, gloamingforest:small_andesite, gloamingforest:small_diorite, gloamingforest:small_granite | underground_decoration: gloamingforest:plant_roots, gloamingforest:torch_berries | vegetal_decoration: patch_tall_grass, gloamingforest:patch_grass_badlands, gloamingforest:patch_grass_savanna, patch_sugar_cane, gloamingforest:swamp_raspberry_bushes, gloamingforest:swamp_blackberry_bushes, patch_grass_taiga_2, gloamingforest:grove_ruins, gloamingforest:stone_circle, gloamingforest:outside_stalagmite, gloamingforest:monolith, gloamingforest:hollow_stump, gloamingforest:hollow_log, gloamingforest:tree/mangrove_tree, gloamingforest:tree/swampy_oak_tree, gloamingforest:tree/oak_bush, patch_sugar_cane_swamp, vines, patch_dead_bush, gloamingforest:mangrove_fallen_log, gloamingforest:huge_lily_pad, gloamingforest:huge_water_lily, patch_waterlily | top_layer_modification: freeze_top_layer
- `gloamingforest:thornlands`: carvers none; vegetal_decoration: gloamingforest:stone_circle, gloamingforest:outside_stalagmite, gloamingforest:monolith, gloamingforest:hollow_stump, gloamingforest:hollow_log, gloamingforest:thorns
- `gloamingforest:underground`: carvers gloamingforest:gf_caves; underground_ores: disk_sand, disk_clay, disk_gravel, gloamingforest:wood_roots, gloamingforest:legacy_coal_ore, gloamingforest:legacy_iron_ore, gloamingforest:legacy_gold_ore, gloamingforest:legacy_redstone_ore, gloamingforest:legacy_diamond_ore, gloamingforest:legacy_lapis_ore, gloamingforest:legacy_copper_ore, gloamingforest:small_andesite, gloamingforest:small_diorite, gloamingforest:small_granite | underground_decoration: gloamingforest:plant_roots, gloamingforest:torch_berries | top_layer_modification: freeze_top_layer

## 7. Biome tags (owned; union fragments may add members, R6)

| Tag | Members |
|---|---|
| `worldgen/biome/#gloamingforest:in_gloaming_forest` | gloamingforest:clearing, gloamingforest:dense_forest, gloamingforest:dense_mushroom_forest, gloamingforest:firefly_forest, gloamingforest:forest, gloamingforest:mushroom_forest, gloamingforest:oak_savannah, gloamingforest:spooky_forest, gloamingforest:enchanted_forest, gloamingforest:dense_mushroom_forest, gloamingforest:lake, gloamingforest:stream, gloamingforest:underground, gloamingforest:swamp, gloamingforest:fire_swamp, gloamingforest:dark_forest, gloamingforest:dark_forest_center, gloamingforest:snowy_forest, gloamingforest:glacier, gloamingforest:highlands, gloamingforest:thornlands, gloamingforest:final_plateau |
| `worldgen/biome/#gloamingforest:valid_aurora_palace_biomes` | gloamingforest:glacier |
| `worldgen/biome/#gloamingforest:valid_camp_biomes` | gloamingforest:oak_savannah, gloamingforest:clearing, gloamingforest:mushroom_forest, gloamingforest:forest, gloamingforest:firefly_forest |
| `worldgen/biome/#gloamingforest:valid_dark_tower_biomes` | gloamingforest:dark_forest_center |
| `worldgen/biome/#gloamingforest:valid_final_castle_biomes` | gloamingforest:final_plateau |
| `worldgen/biome/#gloamingforest:valid_giant_house_biomes` | gloamingforest:highlands |
| `worldgen/biome/#gloamingforest:valid_hedge_maze_biomes` | gloamingforest:clearing, gloamingforest:dense_forest, gloamingforest:dense_mushroom_forest, gloamingforest:firefly_forest, gloamingforest:forest, gloamingforest:mushroom_forest, gloamingforest:oak_savannah, gloamingforest:spooky_forest |
| `worldgen/biome/#gloamingforest:valid_hollow_hill_biomes` | gloamingforest:clearing, gloamingforest:dense_forest, gloamingforest:dense_mushroom_forest, gloamingforest:firefly_forest, gloamingforest:forest, gloamingforest:mushroom_forest, gloamingforest:oak_savannah, gloamingforest:spooky_forest |
| `worldgen/biome/#gloamingforest:valid_hollow_tree_biomes` | gloamingforest:dense_forest, gloamingforest:fire_swamp, gloamingforest:dense_mushroom_forest, gloamingforest:firefly_forest, gloamingforest:forest, gloamingforest:mushroom_forest, gloamingforest:oak_savannah, gloamingforest:enchanted_forest |
| `worldgen/biome/#gloamingforest:valid_hydra_lair_biomes` | gloamingforest:fire_swamp |
| `worldgen/biome/#gloamingforest:valid_knight_stronghold_biomes` | gloamingforest:dark_forest |
| `worldgen/biome/#gloamingforest:valid_labyrinth_biomes` | gloamingforest:swamp |
| `worldgen/biome/#gloamingforest:valid_lich_tower_biomes` | gloamingforest:clearing, gloamingforest:dense_forest, gloamingforest:dense_mushroom_forest, gloamingforest:firefly_forest, gloamingforest:forest, gloamingforest:mushroom_forest, gloamingforest:oak_savannah, gloamingforest:spooky_forest |
| `worldgen/biome/#gloamingforest:valid_mushroom_tower_biomes` | gloamingforest:dense_mushroom_forest |
| `worldgen/biome/#gloamingforest:valid_naga_courtyard_biomes` | gloamingforest:clearing, gloamingforest:dense_forest, gloamingforest:dense_mushroom_forest, gloamingforest:firefly_forest, gloamingforest:forest, gloamingforest:mushroom_forest, gloamingforest:oak_savannah, gloamingforest:spooky_forest |
| `worldgen/biome/#gloamingforest:valid_quest_grove_biomes` | gloamingforest:enchanted_forest |
| `worldgen/biome/#gloamingforest:valid_troll_cave_biomes` | gloamingforest:highlands |
| `worldgen/biome/#gloamingforest:valid_yeti_cave_biomes` | gloamingforest:snowy_forest |

Owned tags: `worldgen/biome/#gloamingforest:in_gloaming_forest`, `worldgen/biome/#gloamingforest:valid_aurora_palace_biomes`,
`worldgen/biome/#gloamingforest:valid_camp_biomes`, `worldgen/biome/#gloamingforest:valid_dark_tower_biomes`,
`worldgen/biome/#gloamingforest:valid_giant_house_biomes`, `worldgen/biome/#gloamingforest:valid_hedge_maze_biomes`,
`worldgen/biome/#gloamingforest:valid_hollow_hill_biomes`, `worldgen/biome/#gloamingforest:valid_hollow_tree_biomes`,
`worldgen/biome/#gloamingforest:valid_hydra_lair_biomes`, `worldgen/biome/#gloamingforest:valid_knight_stronghold_biomes`,
`worldgen/biome/#gloamingforest:valid_labyrinth_biomes`, `worldgen/biome/#gloamingforest:valid_lich_tower_biomes`,
`worldgen/biome/#gloamingforest:valid_mushroom_tower_biomes`, `worldgen/biome/#gloamingforest:valid_naga_courtyard_biomes`,
`worldgen/biome/#gloamingforest:valid_quest_grove_biomes`, `worldgen/biome/#gloamingforest:valid_troll_cave_biomes`.
(`valid_final_castle_biomes` and `valid_yeti_cave_biomes` are listed for completeness; their owners are U30 / U27.)

## 8. Registries owned

`registry biome_layer_stack` (`gloaming/biome_layer_stack`) and `registry biome_terrain_data`
(`gloaming/biome_terrain_data`), with the three entries above. Both are data-driven in upstream (datapack overridable);
GloamingForest bakes them into `assets/data/biomes/` (R7 data) and reads them at world load.

## 9. Bedrock adaptation notes

- Bedrock clients need every custom biome registered client-side for fog/colours; ship client biome JSON with the
  section 6 colours (`fog`, `water`, `sky` via U15 for the client mod), and server biome definitions so vanilla
  `/locate biome` and biome-dependent mechanics (rain vs snow by temperature) work.
- Precipitation: `has_precipitation false` for enchanted forest and fire swamp; the glacier and snowy forest are cold
  enough for snow.
- Natural spawns: register `spawn::setBiomeSpawns(biomeId, table)` from section 6; spawn costs where present.

## 10. Selftests (`biomes.*`, dx 100-199)

`biomes.layer_golden` (biome map of a fixed seed and area equals the F13 golden), `biomes.key_biome_quadrants`
(each 8x8 region gets the four key biomes), `biomes.companions`, `biomes.thornlands_border`, `biomes.streams`,
`biomes.vertical_layers` (underground below -3), `biomes.tags` (tag membership), `biomes.grass_tint` (client mod
only: dark forest centre and spooky colours).
