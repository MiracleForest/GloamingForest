# Java oracle (unit F13)

The oracle runs **The Twilight Forest 4.8** on a local **NeoForge 1.21.1** dedicated server and dumps
*derived facts* about it into `tests/golden/`, so the Bedrock port can be checked against the real mod:
biome and height grids, landmark picks, mob attributes, loot statistics, recipes, uncrafting outputs and
reference values of Minecraft's random and noise generators. It never exports an asset: no texture,
model, sound, music or structure template, and no file is ever extracted from the TF jar (rules R10, R11).

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

Each server is fresh: a new world (flat overworld; the Twilight Forest dimension is TF's own generator), default TF
config, the TF jar and the dumper as the only mods, RCON on a free local port, `max-tick-time=-1`. The seed-dependent
kinds run first, so nothing generates Twilight Forest chunks before the surface dump. The dumper's fake player lives
in the overworld (a player made in a level generates the chunks around its spawn).

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
| `surface/seed-*.json` | Chunks generated to `minecraft:surface` (noise, landmark terraforming, surface rules; no carvers or features): heights and the top block, 128x128 at step 2 around the origin. | U2, U1 |
| `density/seed-*.json` | The seeded noise router of `twilightforest:twilight_noise_gen` at 256 points (`compute` on a single point: no interpolation or blending; constant functions listed as such) and `BiomeDensitySource#sampleTerrain` depth/scale. | U2, F5 |
| `landmarks/seed-*.json` | One center per region over 16x16 regions: `getNearestCenterXZ`, `pickVarietyLandmark`, `pickLandmarkForChunk` (with the biome it read), the main biome, and the structures that actually start in the center chunk; 1024 sampled chunks (half near the origin) with nearest center, `chunkHasLandmarkCenter`, Manhattan distance and variety pick; the variety weights. | F6, landmark units |
| `mobs.json` | Every `twilightforest:` entity type: category, size, fire immunity, tracking; `DefaultAttributes` base values; the attributes (base and value), max health, armor and experience range of a freshly created instance. | U16, U17, boss units, W2 |
| `loot.json` | Every loot table of TF's data pack rolled 2048 times (fixed seed, documented context): per item the rolls containing it, the total count and the per-roll min/max. Statistics, not rules: compare with a tolerance. | loot owners, W2 |
| `recipes.json` | Every recipe of TF's data pack: type, serializer, result and count, shaped size, ingredients (item ids, tags expanded). | F9, recipe owners |
| `uncrafting.json` | TF's `UncraftingMenu` itself, for every item: each recipe it cycles through, the items consumed, the XP cost and the 3x3 grid (`!` = marked, cannot be taken). | U13 |

To change what is dumped, edit `spec.json` or the dumper, rerun `run.ps1`, and review the diff of `tests/golden`
like code: the goldens are only ever written by the oracle.
