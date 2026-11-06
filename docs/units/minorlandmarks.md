# U18 `minorlandmarks`: implementation notes

Spec: [docs/spec/minorlandmarks.md](../spec/minorlandmarks.md). Code: `src/feature/minorlandmarks/`, pure maths in
`src/core/minorlandmarks/` (gtests in `tests/minorlandmarks/`).

| File | What |
|---|---|
| `Landmarks.cpp` | the hollow hills (F7's `bases::HollowHill` as `twilightforest:TFHill`, TF's terraformer) and the hedge maze (`HedgeMazeComponent` on F7's `bases::Maze`, the inverted pyramid terraformer); F6 generators |
| `HollowTrees.cpp` | `HollowTreeStructure` and the six hollow tree pieces (`TFHTTr`, `TFHTLB`, `TFHTMB`, `TFHTSB`, `TFHTRo`, `TFHTLD`) |
| `FallenTrunks.cpp` | `FallenTrunkStructure`, `FallenTrunkPiece` (+ `Hole`) and `TrunkUnderDensityFunction` as the start's terraformer |
| `Features.cpp` | the 12 feature types (block_spike, dark_forest_placer, fallen_hollow_log, fallen_small_log, foundation, hollow_stump, lampposts, monolith, mycelium_blob, small_lake, webs, wood_roots) |
| `Hedge.cpp` | `HedgeBlock`: contact damage (F11 block events), punch and break damage (interactions), the 10-tick re-check |
| `SelfTests.cpp`, `Goldens.inc` | `minorlandmarks.*` (dx 1800..1899) and the golden volume hashes |

## Notes

- **Hollow hill spawns**: `HollowHillStructure#canSpawnMob` is applied by F6 for every structure of type
  `twilightforest:hollow_hill` (`src/feature/landmark/README.md`); U18 only gives the hill piece TF's box and leaves
  `Generator::spawnIndex` unset.
- **Hollow trees and fallen trunks** find their own generation point. A start whose center is its chunk's corner (a new
  natural start) or the point the chunk's random gives (a rebuilt one) runs `findGenerationPoint`; any other center is
  a placed start (the robust twilight oak sapling through `landmark::generateAt`, `/twilightforest place`, selftests)
  and grows as `generateFromSapling` does (the fallen trunk keeps the center's height). A placement at exactly those
  coordinates is taken for a natural one.
- **Biomes**: the fallen trunk's `findBiomeHorizontal` and F6's noise biome checks use `gf::worldgen::biomeAt` (the
  zoomed biome) at quart positions.
- **Fallen trunk hole**: TF saves the hole seed with `putLong` and reads it back with `getInt`; a reloaded piece's hole
  uses the low 32 bits, as in TF.
- **Hedge punching**: Java re-checks `player.swinging` and the player's ray trace every 10 ticks; Bedrock uses the
  server's swing flag or the player still mining that hedge, and a stepped eye ray (5 blocks).
- **W0 access**: `src/feature/ext/minorlandmarks/W0Internals.h` (F7's block helpers and block entity queue), the same
  door U28 opened.
