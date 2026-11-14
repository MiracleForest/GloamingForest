# U1 `biomes`: implementation notes

Spec: [docs/spec/biomes.md](../spec/biomes.md). What is where:

| Part | Files |
|---|---|
| Layer stack, terrain grid, biome source (pure, gtested) | `src/core/biomes/BiomeLayers.{h,cpp}`, `tests/biomes/BiomeLayersTest.cpp` |
| Biome colours (BiomeColorAlgorithms, FoliageColorHandler, grass modifiers, Java's blend) | `src/core/biomes/Colors.{h,cpp}`, `tests/biomes/ColorsTest.cpp` |
| Installation into F5 (biome source, main biome, possible biomes), config `biomes` | `src/feature/biomes/Module.cpp` |
| Spawn tables (BiomeHelper), vanilla mobs of the tables | `src/feature/biomes/Spawns.cpp` |
| Spawn costs (MobSpawnCost, NaturalSpawner's spawn potential) | `src/core/biomes/SpawnCost.{h,cpp}`, `tests/biomes/SpawnCostTest.cpp`, `src/feature/biomes/SpawnCosts.cpp` |
| Ambient particles (ClientLevel#animateTick: client mod, server for vanilla players), mood sound | `src/feature/biomes/Ambient.{h,cpp}`, `src/client/biomes/Ambient.cpp`, `src/core/biomes/Mood.{h,cpp}`, `tests/biomes/MoodTest.cpp` |
| Client tints on vanilla grass / foliage blocks | `src/client/biomes/Tints.{h,cpp}`, `src/client/biomes/SelfTests.cpp` (`biomes.grass_tint`) |
| Client colour hooks (grass / foliage / water policies, leaves without seasons, the fiddlehead's grass tint method) | `src/feature/ext/biomes/VanillaTint.cpp` |
| Extension for other units (terrain blend, key biome, client colours) | `src/feature/ext/biomes/Biomes.h`, `src/feature/biomes/Ext.cpp` |
| Behavior pack biomes (climate, map tints) | `assets/pack/bp/biomes/biomes/*.biome.json` |
| Client biomes (sky, grass, foliage, water, white ash, fog and water ids), fogs, Vibrant Visuals water | `assets/pack/rp/biomes/*.client_biome.json`, `assets/pack/rp/fogs/biomes/*_fog.json`, `assets/pack/rp/water/biomes/*_water.json` |
| Selftests, e2e | `src/feature/biomes/SelfTests.cpp` (+ `Golden.inc`, oracle samples), `tools/e2e/{server,client}/scenarios/biomes{,.classic}*.ps1` |

## Data

The layer stacks (`biome_layer_stack`) and the terrain grid (`biome_terrain_data`) are read at
install from the mod's data folder, `data/gloamingforest/gloaming/...`, which F1 imports from upstream's
data pack (`assets/gen/import/biomes/data/...`). The stack is built from that JSON as upstream's codecs
read it, so a data change needs no code change.

## Pack files

The BP and RP biome files are generated once from upstream's imported biome JSON and committed as the
manual layer:

- BP: `minecraft:climate` (temperature; downfall: upstream's, 0 where upstream has `has_precipitation: false`
  (enchanted forest, fire swamp) since Bedrock rains wherever the downfall is above 0, and 0.0001
  where upstream rains with a downfall of 0 (oak savannah, underground, highlands underground: the
  colour map does not change visibly); snow accumulation [0, 0.125]) and `minecraft:map_tints`
  (upstream's grass / foliage colours where upstream sets them).
- RP (format 1.21.120): `minecraft:sky_color`, `minecraft:grass_appearance` /
  `minecraft:foliage_appearance` (upstream's colours; the swamp's and dark forest's are already the
  modifiers' results), `minecraft:water_appearance` with upstream's water colour in every biome (Java's
  default `#3F76E4` where upstream sets none: Bedrock's own default is `#44AFF5`), a fog
  (`gloamingforest:<biome>_fog`) in every biome with upstream's water fog (`#050533` by default; it ends
  at 96 blocks, 81.6 in upstream's `has_closer_water_fog` biomes: Java's
  `96 * max(0.25, waterVision)` once the eyes have adapted, without Java's 5-30 s ramp from 24
  blocks) and upstream's air fog where it is not upstream's usual `#C0FFD8` (the sky and air fog of the
  dimension are U15's), and `minecraft:ambient_sounds`: the underwater loop and additions (chance
  0.01) vanilla Bedrock biomes carry. No mood: the server plays upstream's (see Ambient).
- The server's biome registry gets upstream's water colours (`Biome::mMapWaterColor`, which the biome
  definitions sent to clients carry) at install and at every world load.
- The enchanted forest's static grass and foliage are `#00FF80`, the middle of upstream's bands
  (`0x00FFxx`): vanilla clients cannot draw the bands; the client mod does.
- The dark forest centre's static colours are what upstream draws most: foliage `#E94E14` (upstream's
  BiomeColorAlgorithms tests `(noise + 1) / 2 < -0.1`, never true, so its leaves are always
  `#E94E14`; the biome JSON's `#F9821E` is never drawn) and grass `#554114` (the branch of
  darkForestCenterGrass taken where the temperature noise is at least -0.2, most of the map:
  `tests/biomes/ColorsTest.cpp`).
- The snowy forest's white grass and foliage are written `#FEFEFE` (invisible difference).

## Client colours

Bedrock blends static biome colours. Upstream's spooky forest, dark forest centre and enchanted forest
change their colours by position. The client mod registers tints for vanilla grass and foliage
blocks with F12's registry (`tint::registerBlock`); F12's hook only reaches blocks of the client
block pipeline, so `src/feature/ext/biomes/VanillaTint.cpp` hooks the game's grass and foliage
tessellation policies (`BiomeColorSampling::GrassTessellationPolicy` /
`FoliageTessellationPolicy::get`) and applies the registry there; every other block the game draws
with the biome grass or (default) foliage colour in an upstream biome gets upstream's colour too (Java's
`BiomeColors.getAverage*Color`: upstream's gloaming oak / dark / giant / fallen leaves and the fiddlehead,
and the probe block F12's resolver asks for the biome colour F11's canopy and mangrove leaves start
from). The tint reads the biome of every column of the blend (`tintBlendRadius`, default 2 = 5x5)
from the tessellated block source (the client's synced chunks, so remote worlds need no seed) and
averages each column's own upstream colour as Java does: the modifier or foliage handler, else upstream's
JSON colour (fire swamp, snowy forest), else the colour map at the biome's climate
(`core/biomes/Colors.cpp`: Bedrock's colour maps, within one step of Java's in one channel). Outside
Upstream's biomes the game's colour stays after one biome lookup per block. Caches keyed by block or
biome addresses are checked against the object's name / serialization hash on every use, so a
remote join that reuses addresses never draws a stale biome.

Leaves: in a cold biome Bedrock draws leaves through its seasons map (the tint is a place in that
map, not a colour; a white tint drew the map's red corner). Java has no seasons:
`LeavesBlock::isSeasonTinted` is false in upstream's biomes with the mod, so the snowy forest's leaves
are upstream's white `#FFFFFF` and the glacier's its colour-map colour. Vanilla clients keep Bedrock's
seasons look there (they cannot be told otherwise: the foliage colour is the only data and it is
read as a map place).

Water: upstream's water colours (`water_color` of upstream's biome JSON) are given to the game in every place
U1 can reach: the client biome files (`minecraft:water_appearance`, now at the pack's `biomes/` root:
the files under `biomes/biomes/` were not read by the client), the server's biomes
(`Biome::mMapWaterColor`) and with them the biome definitions sent to clients (the cached
`BiomeDefinitionListPacket`s are patched too when they exist), and with the mod
`BiomeColorSampling::getWaterColor`. Bedrock 1.26.51 still draws the Gloaming Forest's water in one
colour. Evidence (client e2e, local and remote-vanilla): the client level's own biomes carry upstream's
colours (`client biome gloamingforest:fire_swamp: map water #A66C2C2C`, the vanilla swampland
`#A5617B64` the same way), the hooked lookup answers them, and still the five test pools (white
concrete basins, 5x5x3 water, photographed from straight above) measure the same `#436168` in the
forest, swamp, fire swamp, spooky forest and dense forest. The water mesh's colour comes from a
place none of these reach (no MCAPI water policy or water render attribute is exposed in the
26.51.7 headers: `WaterTessellationPolicy`, `WaterRenderAttributes` and `BiomeTintCache` are
empty declarations). Closest workaround in place: every data path carries upstream's colour, so the water
follows as soon as the engine reads one of them; the scenario measures the pools and logs a warning.

## Spawn costs

`SpawnCosts.cpp` applies BiomeHelper's spawn costs to F5's natural spawner through
`spawn::onSpecialSpawn`: in a biome with costs the list asked for a position leaves out the types
whose charge times the spawn potential there exceeds their energy budget, which is where Java's
SpawnState#canSpawn refuses a pack member. The potential is built from the non-persistent mobs
near the dimension's players (the boxes F5's spawner counts), once per tick and again after every
mob spawn (LeviLamina's `SpawnedMobEvent`), so a mob spawned a moment ago counts for the next member
(SpawnState#afterSpawn); a despawn cannot fall inside a spawner pass (both run on the server thread,
the pass in one go). The handler answers on the server thread only: chunk-generation spawns
(NaturalSpawner#spawnMobsForChunkGeneration) have no SpawnState in Java, so no costs either.
The handler registers on the first server tick, after every other unit's: F5 takes the first answer,
so another unit's structure list is used as it is (Java would also apply the biome's costs to it).

One deviation remains, and it cannot be removed inside U1: F5 asks the same question
(`detail::spawnListAt(region, pos, category, biome)`, `src/feature/spawn/Spawn.cpp:765` for the pick,
`:775` for each member's check, identical arguments) to pick a pack's type and to check each member,
so the handler cannot tell them apart; when the pack's first position refuses the type Java would
have picked, another type of the list is picked instead (Java picks it, refuses that member and may
spawn it at the next positions). The only per-member hook of the contract,
`spawn::ext::registerSpawnBehaviour(entityId, {.checkSpawnRules})`, is keyed by entity id and
replaces the behaviour the mob's owner registers (U16/U17's kobolds, wolves, ...), so U1 cannot use
it. F5's fix: pass the question's purpose to `SpecialSpawnFn`, or carry the costs in `spawn::Table`
and apply them where `filter.test` sits.

## Vanilla mobs of upstream's tables

F5's `spawn::spawnMob` passes `Spawner::spawnMob(..., naturalSpawn = true, ...)` for natural and
chunk-generation spawns (`src/feature/spawn/Spawn.cpp:394-395`), and Bedrock then refuses a vanilla
mob (chicken, wolf, enderman, zombie, skeleton, spider, creeper, slime, witch, bat) whose own spawn
rules do not allow it there (their biome filters need vanilla biome tags; giving upstream's biomes those
tags would let Bedrock's own spawner place vanilla mobs in the Gloaming Forest, which upstream does not).
`Spawns.cpp` listens to LeviLamina's `SpawningMobEvent` (no hook of U1's own) and turns such a spawn
of a vanilla mob of upstream's tables in the Gloaming Forest into a non-natural one: F5 has already checked
Java's placement, rules and obstruction for the position. `biomes.vanilla_spawns` spawns each kind
through `spawn::spawnMob(..., SpawnReason::Natural)` with and without it and logs both outcomes.

## Ambient particles

- White ash (the fire swamp, probability 0.05): Bedrock clients draw white ash themselves for a
  biome whose client biome file asks for it (`minecraft:precipitation` `white_ash`, what the basalt
  deltas use: Java's 0.118093334 is Bedrock's 2.0 there), so the fire swamp's file carries
  0.05 / 0.118093334 * 2.0 = 0.8468. Every client, vanilla included, draws Java's small specks; the
  server no longer sends a falling-dust stand-in.
- Wandering fireflies (the forests): the client mod runs ClientLevel#animateTick's ambient part
  itself (`src/client/biomes/Ambient.cpp`, the sampler of `feature/biomes/Ambient.cpp`: only its
  own samples, drawn by F12's native factory when one is registered, else as U6's resource pack
  effect added to the client level). For players without the mod the server runs the same sampler
  per player and sends each its own samples only (a `SpawnParticleEffectPacket` to that player), so
  players close together never see each other's samples, as on Java. A sample is skipped when its
  block's collision is the full unit cube.
## Mood sound

Upstream's mood (`ambient.cave`, tick delay 6000, search extent 8, offset 2.0 in every biome) is played
by the server per player in an upstream biome as BiomeAmbientSoundsHandler does (`core/biomes/Mood`,
gtested): one random block within 8 of the eyes per tick, darkness adds `(1 - blockLight) / 6000`,
sky light takes `sky / 15 * 0.001`; at 1 the sound plays to that player only (PlaySoundPacket,
volume 0.5) from that block's direction, 2 blocks beyond it. The client biome files carry no mood,
so Bedrock's own timing does not add to it.

## Audit round 2

- **Biome music**: every client biome file now carries `minecraft:biome_music` with `music_definition`
  `gloamingforest:<biome>`, the entry U10 defines for it in `sounds/music_definitions.json`
  (`assets/pack/rp/music_definitions.d/provisions.json`: upstream's biome `music`, `music.gloamingforest.gloaming_forest` in
  every biome). Bedrock's 1.21.120 client biomes pick their music through this component (its own biomes name theirs,
  e.g. the End's `"end"`); before, no upstream biome named a definition.
- **Vibrant Visuals atmosphere**: every client biome file references U15's `gloamingforest:<biome>_atmospherics`
  (`minecraft:atmosphere_identifier`; the files are U15's, `assets/pack/rp/atmospherics/atmosphere/`, same round): upstream's
  sky and horizon colours for VV's physical sky without the sun's dusk band. Bedrock's other per-biome VV
  identifiers (lighting, colour grading) stay the game's defaults.
- Not verified in a client run this round (the e2e client instance was missing from the machine): the music and the
  VV sky need the client scenario `biomes` / `atmosphere` once it is back.
