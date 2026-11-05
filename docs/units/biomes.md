# U1 `biomes`: implementation notes

Spec: [docs/spec/biomes.md](../spec/biomes.md). What is where:

| Part | Files |
|---|---|
| Layer stack, terrain grid, biome source (pure, gtested) | `src/core/biomes/BiomeLayers.{h,cpp}`, `tests/biomes/BiomeLayersTest.cpp` |
| Biome colours (BiomeColorAlgorithms, FoliageColorHandler, grass modifiers, Java's blend) | `src/core/biomes/Colors.{h,cpp}`, `tests/biomes/ColorsTest.cpp` |
| Installation into F5 (biome source, main biome, possible biomes), config `biomes` | `src/feature/biomes/Module.cpp` |
| Spawn tables (BiomeHelper) | `src/feature/biomes/Spawns.cpp` |
| Spawn costs (MobSpawnCost, NaturalSpawner's spawn potential) | `src/core/biomes/SpawnCost.{h,cpp}`, `tests/biomes/SpawnCostTest.cpp`, `src/feature/biomes/SpawnCosts.cpp` |
| Ambient particles (server stand-in for ClientLevel#animateTick) | `src/feature/biomes/Ambient.cpp` |
| Client tints on vanilla grass / foliage blocks | `src/client/biomes/Tints.{h,cpp}`, `src/client/biomes/SelfTests.cpp` (`biomes.grass_tint`) |
| Vanilla tint hook (F12's tint registry applied to vanilla blocks) | `src/feature/ext/biomes/VanillaTint.cpp` |
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
  (`gloamingforest:<biome>_fog`) in every biome with TF's water fog (`#050533` by default) and TF's
  air fog where it is not TF's usual `#C0FFD8` (the sky and air fog of the dimension are U15's),
  and `minecraft:ambient_sounds`: TF's mood sound (`ambient.cave`; Bedrock's mood has no tick
  delay / search extent / offset fields, its own timing applies) and the underwater loop and
  additions (chance 0.01) vanilla Bedrock biomes carry.
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
`FoliageTessellationPolicy::get`) and applies the registry there. The tint reads the biome of every
column of the blend (`tintBlendRadius`, default 2 = 5x5) from the tessellated block source (the
client's synced chunks, so remote worlds need no seed): TF's colour where TF's biome has one
(position-dependent, modifier or flat: the fire swamp, the snowy forest's grass), the game's colour
elsewhere, averaged as Java does. In a cold biome (the snowy forest) Bedrock's leaf tint is not a
colour but the coordinates of its seasons map (`textures/environment/seasons`, green leaves with
snow; a white tint drew the map's red corner in the e2e screenshots), so oak-kin leaves stay
green there instead of TF's white, on every client (a Bedrock limit). TF blocks tinted by the biome colour belong to
their owners; they can call `gf::biomes::ext::grassColor / foliageColor`.

## Spawn costs

`SpawnCosts.cpp` applies BiomeHelper's spawn costs to F5's natural spawner through
`spawn::onSpecialSpawn`: in a biome with costs the list asked for a position leaves out the types
whose charge times the spawn potential there exceeds their energy budget, which is where Java's
SpawnState#canSpawn refuses a pack member. The potential is rebuilt each tick from the
dimension's non-persistent mobs. Two deviations remain (F5 has no hook for them): a mob spawned
during a tick adds its charge from the next tick, and when the pack's first position refuses the
type Java would have picked, another type of the list is picked instead.

## Ambient particles

The server stands in for ClientLevel#animateTick per player; a sample only spawns when its player
is the nearest Twilight Forest player to it, so players close together do not see their densities
added. A sample is skipped when its block's collision is the full unit cube.
