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

## Blocks

- The pillar and the slab are F1's skeletons, patched by `tools/import/overrides/aurora.json` (a tint method on
  every material, a `minecraft:item_visual`); the auroralized glass is F11's `connected_texture` family (TF's
  ConnectedTextureModel: the overlay `auroralized_glass`, the connected sheet `_ct`, no base, blend, tint index 0),
  its textures and materials patched into F1's family data. Only the aurora block is a hand-made file
  (`assets/pack/bp/blocks/aurora/aurora_block.json`).
- Aurora block textures: TF's NoiseVaryingModel draws one of 16 animated textures per position
  (`SimplexNoiseHelper#calcVariant`). The block has a GloamingForest state `gloamingforest:variant` (0..15, one
  permutation per texture, each an animated atlas tile of `flipbook_textures.d/aurora.json`, 30 frames of 4 ticks
  with blending as TF's mcmeta). The palace's pieces (`applyAuroraVariants`) and player placement (F11's
  `onPlaced`) set the variant of the position; a block set by command keeps variant 0.
- Item colours (ColorHandler#registerItemColors, the colour without a level): `minecraft:item_visual` with
  pre-tinted textures (hsvToRGB(0.45, 1, 1) = #00FFB2 baked into a tf-derived copy of the first frame of
  `aurora_block_0`, the pale colour #72E5C2 as `tint_color` of the pillar, slab and glass textures).

## Deviations from TF (Bedrock limits or decisions)

- Palace and mushroom tower golden hashes are computed on an in-memory region (both are 100+ blocks wide and
  150 high, beyond a test row); the Snow Queen's tower is also placed in the world and hashed with
  `snapshotVolume` (`aurora.boss_wing_in_world`). The client scenario screenshots a wing's lowest floor and the
  queen's room of a placed palace. Aurora blocks of F7's shared utility pieces (tower bases) keep variant 0.
- Block colours: with the client mod, TF's exact per-position colour (F12 tint). The colour needs a tint method
  on the material (F12's tint hook only reaches tinted materials) and the tint method is part of the block
  definition the server sends to every client, so vanilla clients multiply the texture with the biome's
  `water` colour. A baked texture for vanilla clients would be multiplied again on top of that; there is no
  constant tint method. Closest workaround: the pre-tinted item visuals show TF's colours in the inventory
  for every client.
- Auroralized glass beacon colour (`getBeaconColorMultiplier`): Bedrock's beacon takes beam colours only from
  vanilla stained glass; a custom block has no colour hook, and an engine hook is W0's (R8). Not implemented.
- The aurora block's 10-tick break with `progress_glacier`: the server breaks the block on the tenth tick of
  continuous mining (survival only; it stops when the player's game mode reports the mining stopped or moved).
  The client's crack animation runs at the block's normal speed (Bedrock computes it on the client).
- Ice mobs' vanilla goals are BP behaviours; the stable ice core's RangedAttackGoal, the queen's hover goals and
  MeleeAttackGoal and everything TF-specific are native. The snow guardian's gear is enchanted by
  DifficultyInstance#getSpecialMultiplier without the chunk's inhabited time (Bedrock has none).
- Snow Queen shields: F8 parts; a hit is blocked with TF's break sound; a piercing arrow is let through (the
  damage is cancelled before the shield forwards it and the arrow keeps its flight past the shield). The
  shields are drawn by a second geometry of the queen turned from a synced anchor
  (`twilightforest:shield_tick`, every second) and the client's clock, so the drawn shields follow the hit
  boxes on every client.
- The unstable ice core's map colours are Bedrock's (`BlockType::getMapColor`), which are Java's MapColor
  values for vanilla blocks (`aurora.map_colors`).
- Snow particles: one Bedrock effect per TF particle over a 4-frame atlas of TF's snow sprites (a random frame
  per particle). The motion: TF's random start (Particle's normalised random direction x 0.06..0.18 x 0.1 a
  tick), drag ln(0.7) x 20 = 7.13 / s and an acceleration of 9.51 / s^2, the continuous form of
  `v = 0.7 v - 0.02` a tick (same terminal speed 1.33 / s; time constant 2.8 against 3.3 ticks).
- The snow around ice mobs and the queen is a looping emitter of their client entities (TF spawns it on the
  client); the melting, beam and drop warning particles are sent by the server through F10's fx.