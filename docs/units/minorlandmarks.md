# U18 `minorlandmarks`: implementation notes

Spec: [docs/spec/minorlandmarks.md](../spec/minorlandmarks.md). Code: `src/feature/minorlandmarks/`, pure maths in
`src/core/minorlandmarks/` (gtests in `tests/minorlandmarks/`).

| File | What |
|---|---|
| `Landmarks.cpp` | the hollow hills (F7's `bases::HollowHill` as `gloamingforest:TFHill`, upstream's terraformer) and the hedge maze (`HedgeMazeComponent` on F7's `bases::Maze`, the inverted pyramid terraformer); F6 generators |
| `HollowTrees.cpp` | `HollowTreeStructure` and the six hollow tree pieces (`TFHTTr`, `TFHTLB`, `TFHTMB`, `TFHTSB`, `TFHTRo`, `TFHTLD`) |
| `FallenTrunks.cpp` | `FallenTrunkStructure`, `FallenTrunkPiece` (+ `Hole`) and `TrunkUnderDensityFunction` as the start's terraformer |
| `Features.cpp` | the 12 feature types (block_spike, dark_forest_placer, fallen_hollow_log, fallen_small_log, foundation, hollow_stump, lampposts, monolith, mycelium_blob, small_lake, webs, wood_roots) |
| `Hedge.cpp` | `HedgeBlock`: contact damage (F11 block events), punch and break damage (interactions), the 10-tick re-check |
| `SelfTests.cpp`, `Goldens.inc` | `minorlandmarks.*` (dx 1800..1899) and the golden volume hashes |

## Notes

- **Hollow hill spawns**: `HollowHillStructure#canSpawnMob` is applied by F6 for every structure of type
  `gloamingforest:hollow_hill` (`src/feature/landmark/README.md`); U18 only gives the hill piece upstream's box and leaves
  `Generator::spawnIndex` unset.
- **Hollow trees and fallen trunks** find their own generation point. A start whose center is its chunk's corner (a new
  natural start) or the point the chunk's random gives (a rebuilt one) runs `findGenerationPoint`; any other center is
  a placed start (the robust gloaming oak sapling through `landmark::generateAt`, `/gloamingforest place`, selftests)
  and grows as `generateFromSapling` does (the fallen trunk keeps the center's height). A placement at exactly those
  coordinates is taken for a natural one.
- **Biomes**: the fallen trunk's `findBiomeHorizontal` and F6's noise biome checks use `gf::worldgen::biomeAt` (the
  zoomed biome) at quart positions.
- **Fallen trunk hole**: upstream saves the hole seed with `putLong` and reads it back with `getInt`; a reloaded piece's hole
  uses the low 32 bits, as in upstream.
- **Hedge punching**: Java re-checks `player.swinging` and the player's ray trace every 10 ticks; Bedrock uses the
  server's swing flag or the player still mining that hedge, and a stepped eye ray (5 blocks).
- **W0 access**: `src/feature/ext/minorlandmarks/W0Internals.h` (F7's block helpers and block entity queue), the same
  door U28 opened.

## Audit round 1

Checked against upstream 4.8 (A1, W2 round 1):

- **Hedge punching**: `minorlandmarks.hedge_damage` called the interactions dispatch directly; it now
  mines the hedge with the simulated player (`simulateDestroyBlock`), so the real path is covered:
  the first hit hurts (HedgeBlock#attack through `onStartDestroy`), the 10-tick re-checks keep
  hurting while the player keeps mining and looking at it (HedgeBlock#tick), and nothing more once
  it stops.
- **Hedge pathing** (`HedgeBlock#getBlockPathType` DANGER_OTHER for every mob it would hurt): not
  ported, for the reason U5 measured on knightmetal (`building.knightmetal_path`): Bedrock's walk
  evaluator avoids magma and cactus by their vanilla types, the node classification is MCNAPI and
  `BlockType::$dealsContactDamage` is never asked during pathfinding, so a custom block cannot be
  made a danger node. Mobs walk into hedges and take the contact damage. Candidate for
  docs/spec/not-ported.md (A7).
- **Placed vs natural hollow trees / fallen trunks**: the layouts decided by the center alone, so a
  robust gloaming oak sapling or `/gloamingforest place` at exactly a chunk corner (or corner + 7)
  ran findGenerationPoint and grew the tree elsewhere in the chunk. The check is now
  `naturalStart` (Helpers.cpp): the start data's `forced` flag decides when F6 records it (F6's
  `createStart` knows it, `Record::forced`, but does not put it in `Start::data` yet: reported to
  F6), the center otherwise (unchanged behaviour until then).
- **Noise biome checks**: upstream's `findBiomeHorizontal` (fallen trunk) and `Structure#isValidBiome`
  read the raw quart biome (`BiomeSource#getNoiseBiome`); the worldgen contract only offers
  `worldgen::biomeAt`, the zoomed (BiomeManager) biome, so both read it at quart corners. Near a
  biome border the zoom can answer the neighbouring quart: a fallen trunk next to an invalid biome
  may start or not where upstream's would not or would, and `findBiomeHorizontal`'s reservoir draws can
  differ. Needs a raw `noiseBiomeAt(qx, qy, qz)` in F5's contract (reported); open.
- **Vanilla configured feature**: upstream's placed feature `patch_grass_jungle` (U18's partition) names
  `minecraft:patch_grass_jungle`, which F5's vanilla set lacks (`unknown configured feature` in every
  full run): `tools/import/overrides/minorlandmarks.json` inlines Minecraft 1.21.1's
  VegetationFeatures.PATCH_GRASS_JUNGLE (32 tries, spread 7 / 3, short grass 3 : fern 1, in air, not
  on podzol).

Still open: hedge pathing (not portable), the `forced` flag (F6), raw noise biomes (F5); the features'
plants are to be re-checked once F5's `canSurvive` fix lands.

## Audit round 2

Checked (W2 round 2): the vanilla feature override against F5's vanilla set and the plants the
minor features place.

Dependency: F5's vanilla set never reached the feature data on main. `vanillaFeatures()`
(src/feature/features/Features.cpp:82) loops over `Json::parse(kVanillaFeaturesExtra).items()`, whose
temporary dies before the C++20 range-for body runs, so the five extra features (spruce, pine, the huge
mushrooms, jungle grass) were never merged ("worldgen data: 150 configured features" instead of 155).
The inline copies hid this. Without them, `patch_grass_jungle` warn `unknown configured feature` until F5's fix (a
named variable) is merged. With that fix applied locally: 155 configured features and no warnings.
This change merges after it.

Fixed:

- **Vanilla feature**: F5's vanilla set now has `minecraft:patch_grass_jungle` (the same Java 1.21.1
  values), so `tools/import/overrides/minorlandmarks.json` (its only patch inlined it) is removed and
  the placed feature names the vanilla id again, as upstream's data does.
- **Floating plants**: `minorlandmarks.features` now also checks every moss patch, grass, fern and
  seagrass a feature placed against F5's survival rule (`kit::canSurvive`): upstream's features (fallen
  logs, hollow stumps, ...) put them on what they built without a survival check of their own. None
  floats. The features place no small mushrooms or oreberries, so F5's brightness limit for their
  survival in generation (round 2) does not change them.

Still open: hedge pathing (not portable), the `forced` flag (F6, being added this round; `naturalStart`
already reads it), raw noise biomes (F5's `noiseBiomeAt`, being added this round; the fallen trunk and
biome checks switch to it once merged).
