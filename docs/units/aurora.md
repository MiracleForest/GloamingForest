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
  permutation per texture, each an animated atlas tile of `flipbook_textures.d/aurora.json`). TF's 16 PNGs are
  identical; the variants differ in the start phase of their frame lists. Each flipbook's `frames`,
  `ticks_per_frame` and `blend_frames` are copied from that variant's `aurora_block_N.png.mcmeta` (30 frames of
  4 ticks, interpolated; variant 1 starts at frame 2, variant 2 at frame 4, ...), so neighbouring variants
  ripple out of phase as in TF. The palace's pieces (`applyAuroraVariants`, wing bases included: the columns
  `fillColumnDown` puts under a based wing down to the ground) and every placement F11's `onPlaced` reports
  (players, `/setblock`, `/fill`: `aurora.aurora_block_variant`) set the variant of the position.
- Item colours (ColorHandler#registerItemColors, the colour without a level): `minecraft:item_visual` with
  pre-tinted textures: hsvToRGB(0.45, 1, 1) = #00FFB2 baked into a tf-derived copy of TF's `aurora_block_0`
  strip (all 16 frames, animated with variant 0's mcmeta frames like TF's item), and the pale colour
  auroraPaleColor(#00FFB2) = #73E6C3 as `tint_color` of the pillar, slab and glass textures (the gtest
  `AuroraColor.JavaGoldens` pins both against TF's Java code).

## Deviations from TF (Bedrock limits or decisions)

- Palace and mushroom tower golden hashes are computed on an in-memory region (both are 100+ blocks wide and
  150 high, beyond a test row); the Snow Queen's tower is also placed in the world and hashed with
  `snapshotVolume` (`aurora.boss_wing_in_world`). The client scenario screenshots a wing's lowest floor and the
  queen's room of a placed palace.
- Block colours: with the client mod, TF's exact per-position colour (F12 tint). The colour needs a tint method
  on the material (F12's tint hook only reaches tinted materials) and the tint method is part of the block
  definition the server sends to every client, so vanilla clients multiply the texture with the biome's
  `water` colour. A baked texture for vanilla clients would be multiplied again on top of that; there is no
  constant tint method. Closest workaround: the pre-tinted item visuals show TF's colours in the inventory
  for every client.
- Auroralized glass beacon colour (`getBeaconColorMultiplier`): Bedrock's beacon takes beam colours only from
  vanilla stained glass. The contract extension `src/feature/ext/aurora/BeaconColor.{h,cpp}` (R18, for A8 to
  fold) rebuilds the beam sections after `BeaconBlockActor::generateBeamSections` (server and client builds)
  with Java's rule whenever a registered block is in the beam: the beacon's white, the first colour replacing
  it, every further change averaged; the auroralized glass registers its aurora colour
  (`aurora.auroralized_glass_beacon`). Vanilla clients draw the engine's own beam.
- The aurora block's 10-tick break with `progress_glacier` (AuroraBrickBlock#getDestroyProgress, both sides in
  Java): the server turns its own destroy progress into 0.1 a tick (server-authoritative breaking), and the client
  mod does the same to the local player's game mode (`src/client/aurora`), so the crack animation and the client's
  break prediction run at TF's speed. Without server-side progress (client-authoritative breaking) a companion's
  client breaks the block itself; a vanilla client's mining counts only while the player keeps looking at the
  block, then the server breaks it on the tenth tick.
- Ice mobs' vanilla goals are BP behaviours; the stable ice core's RangedAttackGoal, the queen's hover goals and
  MeleeAttackGoal, LookAtPlayerGoal and RandomLookAroundGoal and everything TF-specific are native (the
  queen's look goals are F8 goals so that the hover goals' LOOK flag keeps them off her head while she beams).
  HoverBeamGoal turns her as Mob#lookAt does (yaw and pitch by rotlerp, the head with them) besides the
  LookControl, so her view vector, and the ray along it, follow the target. The snow guardian's gear is
  enchanted by DifficultyInstance#getSpecialMultiplier (day time and moon phase, as Java) without the chunk's
  inhabited time (Bedrock has none). Its chestplate goes into the armour container directly (the engine's
  setItemSlot refuses an item without `minecraft:wearable`, which U7's chestplates do not have yet).
- The unstable ice core's death: the lethal hit is remembered and, after the 60 ticks of TF's tickDeath, the core
  is finished with the same damage type and entities, so the kill (progression's onKilled, twilight_hunter),
  looting and XP stay the killer's. Its death sound plays once, at the lethal hit (the RP gives the core no
  engine death sound).
- The ice crystal's age (crystalAge / maxCrystalAge, not saved in TF) lives in memory only: a reloaded summoned
  crystal stays, as in TF.
- Snow Queen shields: F8 parts; a hit is blocked with TF's break sound; a piercing arrow is let through (the
  damage is cancelled before the shield forwards it and the arrow keeps its flight past the shield). The
  shields are drawn by a second geometry of the queen turned from a synced anchor
  (`twilightforest:shield_tick`, every second) and the client's clock, so the drawn shields follow the hit
  boxes on every client.
- The unstable ice core's map colours: Java's MapColor#col from a table of the vanilla blocks it meets most
  (terrain, the biome-tinted grass block and leaves, planks, wool, concrete, terracotta: `javaMapColor` in
  `Mobs.cpp`); any other block falls back to Bedrock's `BlockType::getMapColor` (Java's palette for most plain
  vanilla blocks; `aurora.map_colors` reports how many of a sample agree).
- Snow particles: one Bedrock effect per TF particle over a 4-frame atlas of TF's snow sprites (a random frame
  per particle). The motion: TF's random start (Particle's normalised random direction x 0.06..0.18 x 0.1 a
  tick), drag ln(0.7) x 20 = 7.13 / s and an acceleration of 9.51 / s^2, the continuous form of
  `v = 0.7 v - 0.02` a tick (same terminal speed 1.33 / s; time constant 2.8 against 3.3 ticks).
- The snow around ice mobs and the queen is a looping emitter of their client entities (TF spawns it on the
  client); the melting, beam and drop warning particles are sent by the server through F10's fx (TF spawns the
  beam's 10 particles a tick and the drop warnings on the client; on Bedrock they would need entity-attached
  emitters following the head's pitch and the orbiting shields, which the client entity cannot express without
  locators in F2's generated geometry, so the server sends them: about 200 packets a second per nearby player
  while she beams).