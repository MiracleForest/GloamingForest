# aurora (U28): implementation notes

Spec: [docs/spec/aurora.md](../spec/aurora.md). Code: `src/feature/aurora/` (server), `src/client/aurora/`
(client mod tints), `src/core/aurora/` (floor plans and aurora colours, gtested in `tests/aurora/`).

## What is where

| Part | Files |
|---|---|
| Aurora palace (9 icetower classes, floor plans) | `IceTower.{h,cpp}`, `core/aurora/FloorPlan.{h,cpp}` |
| Mushroom tower (6 mushroomtower classes) | `MushroomTower.{h,cpp}` |
| Landmark generators, module | `Structures.{h,cpp}` (F6 `registerGenerator`, F7 piece types and decorators) |
| Ice mobs, ice snowball, aurora block mining | `Mobs.{h,cpp}` |
| Snow Queen (phases, hover goals, shields, summons, beam, boss kit, spawner) | `SnowQueen.cpp` |
| Selftests | `StructureTests.cpp`, `MobTests.cpp` |
| Aurora colours (client mod) | `src/client/aurora/AuroraClient.cpp` |
| BP entities / blocks, RP particles, animations, render controllers, geometry, sounds | `assets/pack/{bp,rp}/**/aurora/**`, `assets/pack/rp/{sounds.json.d,terrain_texture.d}/aurora.json` |
| RP client entities | F1's skeletons patched by `tools/import/overrides/aurora.json` (regenerate with `pwsh tools/import/run.ps1 -Kinds skeleton -Unit aurora`): the RP's `entity/` folder is claimed by F8's `**/{entity,boss}/**` path glob, which ties with `**/aurora/**` |

## Deviations from TF (Bedrock limits or decisions)

- Golden hashes are computed on an in-memory region (both structures are 100+ blocks wide and 150 high,
  beyond a test row): every placed block, sorted by y, z, x, hashed with `core::VolumeHasher`. The floor plan
  candidate order is the enum order (spec section 1.2), so the palace hash is GloamingForest's own golden.
- Aurora block texture variants: Bedrock picks one of the 16 textures per position (terrain texture
  variations), not TF's `calcVariant` noise. The colour is TF's exact per-position colour with the client mod
  (F12 tint); the blocks carry the `water` tint method, so vanilla clients see the biome's water colour.
- The aurora block's 10-tick break with `progress_glacier`: the server breaks the block 10 ticks after the
  first hit when the player still reaches it (Bedrock computes mining progress on the client).
- Ice mobs' vanilla goals are BP behaviours; the stable ice core's RangedAttackGoal, the queen's hover goals
  and everything TF-specific are native. The snow guardian's equipment drop chances and enchantments are not
  applied.
- Snow Queen shields: F8 parts; a shield blocks every hit (TF lets a piercing arrow through without effect, so
  the outcome is the same: no damage). They are drawn by a second geometry of the queen (packed ice cubes
  turning with `query.life_time`), so vanilla clients see them too.
- The unstable ice core's map colours are Bedrock's (`BlockType::getMapColor`), not Java's.
- Snow particles: one Bedrock effect per TF particle over a 4-frame atlas of TF's snow sprites (a random frame
  per particle); the motion uses drag / acceleration equivalents of TF's per-tick factors.
- Ice mob particles are sent by the server through F10's fx (TF spawns them on the client).
