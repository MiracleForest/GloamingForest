# U1 `biomes`: implementation notes

Spec: [docs/spec/biomes.md](../spec/biomes.md). What is where:

| Part | Files |
|---|---|
| Layer stack, terrain grid, biome source (pure, gtested) | `src/core/biomes/BiomeLayers.{h,cpp}`, `tests/biomes/BiomeLayersTest.cpp` |
| Biome colours (BiomeColorAlgorithms, FoliageColorHandler, grass modifiers, Java's blend) | `src/core/biomes/Colors.{h,cpp}`, `tests/biomes/ColorsTest.cpp` |
| Installation into F5 (biome source, main biome, possible biomes), config `biomes` | `src/feature/biomes/Module.cpp` |
| Spawn tables (BiomeHelper) | `src/feature/biomes/Spawns.cpp` |
| Ambient particles (server stand-in for ClientLevel#animateTick) | `src/feature/biomes/Ambient.cpp` |
| Client tints on vanilla grass / foliage blocks | `src/client/biomes/Tints.cpp` |
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

- BP: `minecraft:climate` (temperature; downfall 0 where TF has `has_precipitation: false`, since
  Bedrock rains wherever the downfall is above 0; snow accumulation [0, 0.125]) and
  `minecraft:map_tints` (TF's grass / foliage colours where TF sets them).
- RP: `minecraft:sky_color`, `minecraft:grass_appearance` / `minecraft:foliage_appearance` (TF's
  colours; the swamp's and dark forest's are already the modifiers' results), `minecraft:water_appearance`
  where TF's water is not Java's default `#3F76E4` (Bedrock's own default water stands for it), and
  a fog (`gloamingforest:<biome>_fog`) where TF's fog colour is not its usual `#C0FFD8` or its water
  fog not Java's default.
- The enchanted forest's static grass and foliage are `#00FF80`, the middle of TF's bands
  (`0x00FFxx`): vanilla clients cannot draw the bands; the client mod does.

## Client colours

Bedrock blends static biome colours. TF's spooky forest, dark forest centre and enchanted forest
change their colours by position. The client mod registers tints on vanilla grass and foliage
blocks (F12 `tint::registerBlock`): where the game's blended colour is exactly one of those
biomes' static colours (the block's whole blend lies inside it) TF's colours are computed and
blended as Java does (`tintBlendRadius`, default 2 = 5x5); elsewhere the game's colour stays. No
seed is needed, so this works in remote worlds too. TF blocks tinted by the biome colour belong to
their owners; they can call `gf::biomes::ext::grassColor / foliageColor` for TF's colours.
