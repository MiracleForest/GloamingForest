# U1 `biomes`: implementation notes

Spec: [docs/spec/biomes.md](../spec/biomes.md). What is where:

| Part | Files |
|---|---|
| Layer stack, terrain grid, biome source (pure, gtested) | `src/core/biomes/BiomeLayers.{h,cpp}`, `tests/biomes/BiomeLayersTest.cpp` |
| Biome colours (BiomeColorAlgorithms, FoliageColorHandler, grass modifiers, Java's blend) | `src/core/biomes/Colors.{h,cpp}`, `tests/biomes/ColorsTest.cpp` |
| Installation into F5 (biome source, main biome, possible biomes), config `biomes` | `src/feature/biomes/Module.cpp` |
| Spawn tables (BiomeHelper) | `src/feature/biomes/Spawns.cpp` |
| Spawn costs (MobSpawnCost, NaturalSpawner's spawn potential) | `src/core/biomes/SpawnCost.{h,cpp}`, `tests/biomes/SpawnCostTest.cpp`, `src/feature/biomes/SpawnCosts.cpp` |
| Ambient particles (server stand-in for ClientLevel#animateTick), mood sound | `src/feature/biomes/Ambient.cpp`, `src/core/biomes/Mood.{h,cpp}`, `tests/biomes/MoodTest.cpp` |
| Client tints on vanilla grass / foliage blocks | `src/client/biomes/Tints.{h,cpp}`, `src/client/biomes/SelfTests.cpp` (`biomes.grass_tint`) |
| Client colour hooks (grass / foliage policies, leaves without seasons, water colour) | `src/feature/ext/biomes/VanillaTint.cpp` |
| Extension for other units (terrain blend, key biome, client colours) | `src/feature/ext/biomes/Biomes.h`, `src/feature/biomes/Ext.cpp` |
| Behavior pack biomes (climate, map tints) | `assets/pack/bp/biomes/biomes/*.biome.json` |
| Client biomes (sky, grass, foliage, water, fog ids) and fogs | `assets/pack/rp/biomes/biomes/*.client_biome.json`, `assets/pack/rp/fogs/biomes/*_fog.json` |
| Selftests, e2e | `src/feature/biomes/SelfTests.cpp` (+ `Golden.inc`, oracle samples), `tools/e2e/{server,client}/scenarios/biomes.ps1` |

## Data

The layer stacks (`biome_layer_stack`) and the terrain grid (`biome_terrain_data`) are read at
install from the mod's data folder, `data/twilightforest/twilight/...`, which F1 imports from TF's
data pack (`assets/gen/import/biomes/data/...`). The stack is built from that JSON as TF's codecs
read it, so a data change needs no code change.

## Pack files

The BP and RP biome files are generated once from TF's imported biome JSON and committed as the
manual layer:

- BP: `minecraft:climate` (temperature; downfall: TF's, 0 where TF has `has_precipitation: false`
  (enchanted forest, fire swamp) since Bedrock rains wherever the downfall is above 0, and 0.0001
  where TF rains with a downfall of 0 (oak savannah, underground, highlands underground: the
  colour map does not change visibly); snow accumulation [0, 0.125]) and `minecraft:map_tints`
  (TF's grass / foliage colours where TF sets them).
- RP (format 1.21.120): `minecraft:sky_color`, `minecraft:grass_appearance` /
  `minecraft:foliage_appearance` (TF's colours; the swamp's and dark forest's are already the
  modifiers' results), `minecraft:water_appearance` with TF's water colour in every biome (Java's
  default `#3F76E4` where TF sets none: Bedrock's own default is `#44AFF5`), a fog
  (`gloamingforest:<biome>_fog`) in every biome with TF's water fog (`#050533` by default; it ends
  at 96 blocks, 81.6 in TF's `has_closer_water_fog` biomes: Java's
  `96 * max(0.25, waterVision)` once the eyes have adapted, without Java's 5-30 s ramp from 24
  blocks) and TF's air fog where it is not TF's usual `#C0FFD8` (the sky and air fog of the
  dimension are U15's), and `minecraft:ambient_sounds`: the underwater loop and additions (chance
  0.01) vanilla Bedrock biomes carry. No mood: the server plays TF's (see Ambient).
- The server's biome registry gets TF's water colours (`Biome::mMapWaterColor`, which the biome
  definitions sent to clients carry) at install and at every world load.
- The enchanted forest's static grass and foliage are `#00FF80`, the middle of TF's bands
  (`0x00FFxx`): vanilla clients cannot draw the bands; the client mod does.
- The dark forest centre's static colours are what TF draws most: foliage `#E94E14` (TF's
  BiomeColorAlgorithms tests `(noise + 1) / 2 < -0.1`, never true, so its leaves are always
  `#E94E14`; the biome JSON's `#F9821E` is never drawn) and grass `#554114` (the branch of
  darkForestCenterGrass taken where the temperature noise is at least -0.2, most of the map:
  `tests/biomes/ColorsTest.cpp`).
- The snowy forest's white grass and foliage are written `#FEFEFE` (invisible difference).

## Client colours

Bedrock blends static biome colours. TF's spooky forest, dark forest centre and enchanted forest
change their colours by position. The client mod registers tints for vanilla grass and foliage
blocks with F12's registry (`tint::registerBlock`); F12's hook only reaches blocks of the client
block pipeline, so `src/feature/ext/biomes/VanillaTint.cpp` hooks the game's grass and foliage
tessellation policies (`BiomeColorSampling::GrassTessellationPolicy` /
`FoliageTessellationPolicy::get`) and applies the registry there; every other block the game draws
with the biome grass or (default) foliage colour in a TF biome gets TF's colour too (Java's
`BiomeColors.getAverage*Color`: TF's twilight oak / dark / giant / fallen leaves and the fiddlehead,
and the probe block F12's resolver asks for the biome colour F11's canopy and mangrove leaves start
from). The tint reads the biome of every column of the blend (`tintBlendRadius`, default 2 = 5x5)
from the tessellated block source (the client's synced chunks, so remote worlds need no seed) and
averages each column's own TF colour as Java does: the modifier or foliage handler, else TF's
JSON colour (fire swamp, snowy forest), else the colour map at the biome's climate
(`core/biomes/Colors.cpp`: Bedrock's colour maps, within one step of Java's in one channel). Outside
TF's biomes the game's colour stays after one biome lookup per block. Caches keyed by block or
biome addresses are checked against the object's name / serialization hash on every use, so a
remote join that reuses addresses never draws a stale biome.

Leaves: in a cold biome Bedrock draws leaves through its seasons map (the tint is a place in that
map, not a colour; a white tint drew the map's red corner). Java has no seasons:
`LeavesBlock::isSeasonTinted` is false in TF's biomes with the mod, so the snowy forest's leaves
are TF's white `#FFFFFF` and the glacier's its colour-map colour. Vanilla clients keep Bedrock's
seasons look there (they cannot be told otherwise: the foliage colour is the only data and it is
read as a map place).

Water: TF's water colours (`water_color` of TF's biome JSON) are given to the game in every place
U1 can reach: the client biome files (`minecraft:water_appearance`, now at the pack's `biomes/` root:
the files under `biomes/biomes/` were not read by the client), the server's biomes
(`Biome::mMapWaterColor`) and with them the biome definitions sent to clients (the cached
`BiomeDefinitionListPacket`s are patched too when they exist), and with the mod
`BiomeColorSampling::getWaterColor`. Bedrock 1.26.51 still draws the Twilight Forest's water in one
colour. Evidence (client e2e, local and remote-vanilla): the client level's own biomes carry TF's
colours (`client biome twilightforest:fire_swamp: map water #A66C2C2C`, the vanilla swampland
`#A5617B64` the same way), the hooked lookup answers them, and still the five test pools (white
concrete basins, 5x5x3 water, photographed from straight above) measure the same `#436168` in the
forest, swamp, fire swamp, spooky forest and dense forest. The water mesh's colour comes from a
place none of these reach (no MCAPI water policy or water render attribute is exposed in the
26.51.7 headers: `WaterTessellationPolicy`, `WaterRenderAttributes` and `BiomeTintCache` are
empty declarations). Closest workaround in place: every data path carries TF's colour, so the water
follows as soon as the engine reads one of them; the scenario measures the pools and logs a warning.

## Spawn costs

`SpawnCosts.cpp` applies BiomeHelper's spawn costs to F5's natural spawner through
`spawn::onSpecialSpawn`: in a biome with costs the list asked for a position leaves out the types
whose charge times the spawn potential there exceeds their energy budget, which is where Java's
SpawnState#canSpawn refuses a pack member. The potential is built from the non-persistent mobs
near the dimension's players (the boxes F5's spawner counts) and rebuilt whenever the level's
entity count changed, so a mob spawned a moment ago counts for the next member (SpawnState#afterSpawn).
The handler registers on the first server tick, after every other unit's: F5 takes the first answer,
so another unit's structure list is used as it is (Java would also apply the biome's costs to it).
One deviation remains: F5 asks the same question to pick a pack's type and to check each member, so
when the pack's first position refuses the type Java would have picked, another type of the list is
picked instead (Java picks it and refuses every member). Fixing it needs F5 to separate the two
questions or carry the costs in `spawn::Table`.

Vanilla entries of TF's tables (chicken, wolf, enderman, zombie, skeleton, spider, creeper, slime,
witch, bat) are refused by F5's `spawn::spawnMob` (`Spawner::spawnMob(..., natural = true, ...)`,
`src/feature/spawn/Spawn.cpp:394-395`): Bedrock then applies the mobs' own spawn rules, whose biome
filters need vanilla biome tags. U1 does not give TF's biomes those tags (the same tags would let
Bedrock's own spawn rules place vanilla mobs there, which TF does not) and cannot hook `Spawner::spawnMob` (rule R8).
F5's fix: pass `natural = false` for natural and chunk-generation spawns, whose Java rules F5 has
already checked.

## Ambient particles

The server stands in for ClientLevel#animateTick per player; a sample only spawns when its player
is the nearest Twilight Forest player to it, so players close together do not see their densities
added. A sample is skipped when its block's collision is the full unit cube.

## Mood sound

TF's mood (`ambient.cave`, tick delay 6000, search extent 8, offset 2.0 in every biome) is played
by the server per player in a TF biome as BiomeAmbientSoundsHandler does (`core/biomes/Mood`,
gtested): one random block within 8 of the eyes per tick, darkness adds `(1 - blockLight) / 6000`,
sky light takes `sky / 15 * 0.001`; at 1 the sound plays to that player only (PlaySoundPacket,
volume 0.5) from that block's direction, 2 blocks beyond it. The client biome files carry no mood,
so Bedrock's own timing does not add to it.
