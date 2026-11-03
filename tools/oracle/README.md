# Java oracle (unit F13)

The oracle runs **The Twilight Forest 4.8** on a local **NeoForge 1.21.1** dedicated server and dumps
*derived facts* about it into `tests/golden/`, so the Bedrock port can be checked against the real mod:
biome and height grids, landmark picks, the structure decoration steps, mob attributes, loot statistics,
recipes, uncrafting outputs and reference values of Minecraft's random and noise generators. It never exports
an asset: no texture, model, sound, music or structure template, and no file is ever extracted from the TF jar
(rules R10, R11).

| File | What |
|---|---|
| `oracle.json` | Pinned downloads (URL + SHA-256): Temurin JDK 21, NeoForge 21.1.209 installer, `twilightforest-4.8.3802-universal.jar`. |
| `spec.json` | What is dumped: seeds, grid origins and sizes, sample counts, loot rolls. |
| `fetch.ps1` | Downloads and verifies the pins into `%LOCALAPPDATA%\gf-oracle` (`$env:GF_ORACLE_HOME`), installs the server. |
| `run.ps1` | Builds the dumper, takes a `bds` throttle slot, runs one fresh server per seed, writes the goldens and `manifest.json`; `-Check` re-dumps and compares. |
| `dumper/` | The dumper mod (Java, original code, LGPL-3.0-or-later): `/gforacle dump <kind> <file>`, compiled with `javac` against the installed server. |
| `lib/Oracle.psm1` | Paths, downloads, the dumper build, FNV-1a, the RCON client. |

## Running it

```powershell
pwsh tools/oracle/run.ps1 -AcceptEula                       # every kind, every seed -> tests/golden
pwsh tools/oracle/run.ps1 -AcceptEula -Kinds landmarks -Seeds 0
pwsh tools/oracle/run.ps1 -AcceptEula -Check                # reproducible? (exit 1 on a difference)
```

`-AcceptEula` (or `GF_ORACLE_ACCEPT_EULA=1`) accepts the [Minecraft EULA](https://aka.ms/MinecraftEULA) for the
oracle's private server. The JVM's proxy comes from `HTTPS_PROXY` / `HTTP_PROXY` (the NeoForge installer downloads
Minecraft and its libraries). A run takes a few minutes; exit code 2 is NOT RUN (no `bds` slot in time).

Each server is fresh: a new world (flat overworld; the Twilight Forest dimension uses the generator type
`minecraft:noise` with TF's biome source `twilightforest:twilight_biomes` and noise settings
`twilightforest:twilight_noise_gen`), default TF config, the TF jar and the dumper as the only mods, RCON on a free
local port, `max-tick-time=-1`. The seed-dependent kinds run first, so nothing generates Twilight Forest chunks before
the surface dump. The dumper's fake player lives in the overworld (a player made in a level generates the chunks around
its spawn).

What a run checks:

- **The server log.** Every `ERROR` / `FATAL` line (and every stderr line) logged from the first dump command on fails
  the run, unless it is in `$LogAllowList` of `run.ps1` with the reason it is harmless (the empty flat-world generator
  settings, the offline Yggdrasil key fetch, TF's own `Did not find boss room from exit 3 - EPIC FAIL` when a Knight
  Stronghold layout has no boss room). Allowed lines are printed as warnings.
- **`-Check`.** The dumps of the requested kinds and seeds are compared byte for byte with `tests/golden`; a committed
  golden that the requested kinds and seeds should produce but the run did not, a committed golden of a kind or seed
  that is no longer in `spec.json` (stale), and a `manifest.json` that differs from the manifest rebuilt from the
  committed goldens all fail the check.

**Provenance of the TF jar.** TF's CI publishes `<mod_version>.<build>` jars to `maven.tamaized.com` (group
`team-twilight`); 4.8.3802 is the last 1.21.1-branch build. When it was pinned, all 1566 `.java` files of its
`-sources.jar` were byte-identical to `src/main/java` of commit `9e40a757` (the commits after the last code change
touch only build files and one `.nbt` template).

## Goldens (`tests/golden/`)

Every file is JSON with `format` (1), `kind`, `source` (TF version and commit, Minecraft, NeoForge), `seed` for
seed-dependent kinds, and a `note` describing its fields. `manifest.json` lists every file with its size, FNV-1a 64
(`gf::core::fnv1a64`) and SHA-256; `tests/golden/OracleGoldenTest.cpp` checks the manifest, the structure of every
file, and recomputes what can be recomputed independently (random sources, TF's landmark grid, biome column layers).
Numbers are exact: doubles use Java's shortest round-trip form (read them with `strtod` / nlohmann and compare with
`==`), floats are widened to double. Seeds are `0`, `123456789` and `-8346138912345678901` (file names `seed-0`,
`seed-123456789`, `seed-m8346138912345678901`).

| File | Content | For |
|---|---|---|
| `rng.json` | `LegacyRandomSource`, `XoroshiroRandomSource` (all methods, fork, positional `at` / `fromHashOf` / `fromSeed`), `WorldgenRandom` decoration / feature / large-feature / salted seeding over both, slime chunks, `RandomSupport` seed upgrades and MD5 seeds, `String#hashCode`, `Mth#getSeed`. Each list starts from a fresh source. | F5 (`src/core/jrand`) |
| `noise.json` | `ImprovedNoise` (incl. the y-scaled variant), `SimplexNoise` 2D/3D, `PerlinNoise.create` and `createLegacyForLegacyNetherBiome`, `NormalNoise.create` / `createLegacyNetherBiome` with their `maxValue`, for both source kinds and several octave sets, at `points3` / `points2`. | F5 (`src/core/noise`) |
| `biomes/seed-*.json` | `TFBiomeProvider#getMainBiome` on a 256x256 quart grid at step 1 (`main`) and step 8 (`wide`), one palette character per cell; `columns`: each key biome's elevation layers; `noise`: `[qx, qy, qz, getNoiseBiome]`; `zoom`: `[x, y, z, Level#getBiome]` (BiomeManager zoom). | U1, F6 |
| `heights/seed-*.json` | `NoiseBasedChunkGenerator#getBaseHeight` (`WORLD_SURFACE_WG`, `OCEAN_FLOOR_WG`) on a 128x128 grid at step 8: the noise terrain only. | U2 |
| `surface/seed-*.json` | Chunks generated to `minecraft:surface` (noise, landmark terraforming, surface rules; no carvers or features): heights and the top block, 128x128 at step 2 around the origin; `samples.chunks`: whole 16x16 chunks for every main biome of the search grid (step-8 quart grid over +-4096 blocks, at most 2 per biome, the most interior cells nearest the origin first) with heights, the top block, the 8 blocks from the floor down (`profile`) and the quart biome of each column; `samples.unsampled`: key biomes the grid never reaches (none for the 3 seeds). | U2, U1 |
| `density/seed-*.json` | The seeded noise router of `twilightforest:twilight_noise_gen` at 256 points (`compute` on a single point: no interpolation or blending; constant functions listed as such) and `BiomeDensitySource#sampleTerrain` depth/scale. | U2, F5 |
| `landmarks/seed-*.json` | One center per region over 16x16 regions: `getNearestCenterXZ`, `pickVarietyLandmark`, `pickLandmarkForChunk` (with the biome it read), the main biome, and the structures that actually start in the center chunk; 1024 sampled chunks (half near the origin) with nearest center, `chunkHasLandmarkCenter`, Manhattan distance and variety pick; the variety weights. | F6, landmark units |
| `structure_steps.json` | The Structure registry (`registryAccess().registryOrThrow(Registries.STRUCTURE)`, every enabled data pack: Minecraft's 34 structures and TF's 21) the way `ChunkGenerator#applyBiomeDecoration` walks it, see below. | F6 (`src/core/landmark/Decoration`), F5 (`worldgen::ext::structuresByStep`) |
| `mobs.json` | Every `twilightforest:` entity type: category, size, fire immunity, tracking; `DefaultAttributes` base values; the attributes (base and value), max health, armor and experience range of a freshly created instance. | U16, U17, boss units, W2 |
| `loot.json` | Every loot table of TF's data pack rolled 2048 times (fixed seed, documented context): per item the rolls containing it, the total count and the per-roll min/max. `variants`: the same rolls with the player holding shears, a silk touch pickaxe or a fortune III pickaxe (block tables, also the tool) or a looting III sword (entity tables); variants equal to the default are listed in `variantsSameAsDefault`. Statistics, not rules: compare with a tolerance. | loot owners, W2 |
| `recipes.json` | Every recipe of TF's data pack: type, serializer, result and count, shaped size, cooking time and experience, ingredients (item ids, tags expanded) and `data` (the recipe encoded with its serializer's codec: drying time, travellers' gear modifier key, scepter repair durability, ...). `probe`: what each travellers' gear modifier recipe crafts from its ingredients (the modifier components); `probes`: the code-only recipes (casket, essence, moonworm queen and scepter repair, emperor's cloth, map cloning, vest and gloves merge) crafted from the inputs in `spec.json`. | F9, recipe owners |
| `uncrafting.json` | TF's `UncraftingMenu` itself, with every `UNCRAFTING_STUFFS` config value: for every item, each recipe it cycles through, the items consumed, the XP cost and the 3x3 grid (`!` = marked, cannot be taken); `ingredientCycles` (ingredient switching) for TF's items and a few vanilla examples; `damaged` (every damageable item at 1/4, 1/2 and max-1 damage: marked parts and cost); `recraft` (repair and recraft cases of `spec.json` through the assembly matrix: result and `getRecraftingCost`). | U13 |

**Who must read these goldens.** No production code reads them yet: in W0b F5 and F6 are written in parallel, so the
gtest checks `rng.json`, `landmarks/` and the biome columns against reference C++ written inside
`tests/golden/OracleGoldenTest.cpp`, and `noise.json` only for shape and bounds. F5 (`tests/worldgen`, for
`src/core/jrand` and `src/core/noise`) and F6 (`tests/landmark`, for `src/core/landmark`) must add tests that run their
implementations against `rng.json`, `noise.json` and `landmarks/seed-*.json`; A2 / A8 then replace the test-local
reference code with the real implementations. U1 and U2 compare against `biomes/`, `heights/`, `density/` and
`surface/`, U13 against `uncrafting.json`, the recipe and loot owners against `recipes.json` and `loot.json`.
`structure_steps.json` is compared in `tests/golden/StructureStepsTest.cpp` with F6's `src/core/landmark/Decoration`
(Minecraft's structures, the registry order, `slotOf`, plus TF's steps from `assets/data/landmarks/twilightforest`) and
with F5's built-in table (the `kVanilla` / `kTwilight` rows of `src/feature/worldgen/Structures.cpp` and the
`DecorationStep` enumerators of `src/feature/ext/worldgen/Generation.h`, read as text: feature code the gtests cannot
link), which `worldgen::ext::structuresByStep` / `structureSlot` sort into the runtime registry the landmark starts are
seeded with first. F6's selftest `landmark.decoration` then checks in game that `structuresByStep()` is F6's table entry
by entry, so the runtime order is tied to the golden too.

To change what is dumped, edit `spec.json` or the dumper, rerun `run.ps1`, and review the diff of `tests/golden`
like code: the goldens are only ever written by the oracle.

### `structure_steps.json`

`applyBiomeDecoration` groups the registry with `registry.stream().collect(Collectors.groupingBy(s -> s.step().ordinal()))`
and, at decoration step `o`, calls `worldgenrandom.setFeatureSeed(decorationSeed, l, o)` before placing the starts of the
`l`-th structure of `map.getOrDefault(o, emptyList())`, whether or not it has a start in the chunk. The golden records
exactly that table (seed-independent: one file, no `seed`):

| Field | What |
|---|---|
| `count` | `Registry#size` (55). |
| `stepNames` | `GenerationStep.Decoration#getSerializedName` of every ordinal (`raw_generation` .. `top_layer_modification`, 11). |
| `steps` | `steps[o]`: the ids of the structures whose `step().ordinal()` is `o`, in registry stream order; `[]` for a step without structures. Every ordinal is present, so `steps[o][l]` is the structure seeded with `setFeatureSeed(decorationSeed, l, o)`. |
| `order` | Every id in registry stream order (`MappedRegistry#stream`: registration order, which `RegistryDataLoader` takes from the sorted file locations `<ns>:worldgen/structure/<path>.json`, path first, then namespace). |

The dumper checks that the groups partition the registry (every id once; a structure at an ordinal outside
`GenerationStep.Decoration` would be in no group and fail the count).
`tests/golden/StructureStepsTest.cpp` compares the golden with F6's pure table: the steps of `vanillaStructures()` and of
TF's records in `assets/data/landmarks/twilightforest`, `slotOf` for every entry, and `registryBefore` over `order`.
