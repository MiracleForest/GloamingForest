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

- The pillar and the slab are F1's skeletons, patched by `tools/import/overrides/aurora.json` (baked textures on
  every material, a `minecraft:item_visual`); the auroralized glass is F11's `connected_texture` family (upstream's
  ConnectedTextureModel: the overlay `auroralized_glass`, the connected sheet `_ct`, no base, blend, tint index 0),
  its textures and materials patched into F1's family data. Only the aurora block is a hand-made file
  (`assets/pack/bp/blocks/aurora/aurora_block.json`).
- Aurora block textures: upstream's NoiseVaryingModel draws one of 16 animated textures per position
  (`SimplexNoiseHelper#calcVariant`). The block has a GloamingForest state `gloamingforest:variant` (0..15, one
  permutation per texture, each an animated atlas tile of `flipbook_textures.d/aurora.json`). Upstream's 16 PNGs are
  identical; the variants differ in the start phase of their frame lists. Each flipbook's `frames`,
  `ticks_per_frame` and `blend_frames` are copied from that variant's `aurora_block_N.png.mcmeta` (30 frames of
  4 ticks, interpolated; variant 1 starts at frame 2, variant 2 at frame 4, ...), so neighbouring variants
  ripple out of phase as in upstream. The palace's pieces (`applyAuroraVariants`, wing bases included: the columns
  `fillColumnDown` puts under a based wing down to the ground) and every placement F11's `onPlaced` reports
  (players, `/setblock`, `/fill`: `aurora.aurora_block_variant`) set the variant of the position.
- Item colours (ColorHandler#registerItemColors, the colour without a level): `minecraft:item_visual` with
  pre-tinted textures: hsvToRGB(0.45, 1, 1) = #00FFB2 baked into a tf-derived copy of upstream's `aurora_block_0`
  strip (all 16 frames, animated with variant 0's mcmeta frames like upstream's item), and the pale colour
  auroraPaleColor(#00FFB2) = #73E6C3 as `tint_color` of the pillar, slab and glass textures (the gtest
  `AuroraColor.JavaGoldens` pins both against upstream's Java code).

## Deviations from upstream (Bedrock limits or decisions)

- Palace and mushroom tower golden hashes are computed on an in-memory region (both are 100+ blocks wide and
  150 high, beyond a test row); the Snow Queen's tower is also placed in the world and hashed with
  `snapshotVolume` (`aurora.boss_wing_in_world`). The client scenario screenshots a wing's lowest floor and the
  queen's room of a placed palace.
- Block colours: with the client mod, upstream's exact per-position colour (F12 tint); vanilla clients draw baked
  textures (spec section 13). The block definitions (one for every client) name baked textures without a tint
  method: `tf_aurora_baked_aurora_block_N` (tf-derived copies of upstream's strip times the item colour hsvToRGB(0.45)
  = #00FFB2, animated with variant N's frames) and `tf_aurora_baked_aurora_pillar{,_top}` /
  `_slab_side` (upstream's textures with the pale `tint_color` #73E6C3). The client mod swaps each of them for the
  untinted texture with the `water` tint method when it resolves the block's materials (contract extension
  `src/feature/ext/aurora/MaterialSwap.{h,cpp}`, R18, a hook on
  `ClientBlockPipeline::MaterialRepository::initializeMaterials`; item visuals are never swapped), so the chunk
  builder asks F12's tint for the per-position colour. For A8 to fold into F12's client.render (a material
  swap registry next to `tint::registerBlock`). The client scenario waits for `aurora: client materials swapped`
  (local, remote-mod) and screenshots the blocks in every mode.- Auroralized glass beacon colour (`getBeaconColorMultiplier`): Bedrock's beacon takes beam colours only from
  vanilla stained glass. The contract extension `src/feature/ext/aurora/BeaconColor.{h,cpp}` (R18, for A8 to
  fold into F12's client.render as a beacon colour-multiplier registry: it is a W1 engine hook on
  `BeaconBlockActor::generateBeamSections`, so no other unit may hook that function) rebuilds the beam sections
  after the engine's with Java's rule (`core::aurora::beaconSections`, gtest `AuroraBeacon.Sections`) whenever
  a registered block is in the beam: the beacon's white, the first colour replacing it, every further change
  averaged (FastColor's integer average); the auroralized glass registers its aurora colour. Bedrock's sections
  start at the beacon block itself (an unobstructed beam's sections add up to the blocks above the beacon plus
  one): that block goes to the beacon's white section, so the beam is white from the beacon up to the glass, as
  Java's. Client build only: `generateBeamSections` is declared for `LL_PLAT_C` only in LeviLamina's server
  headers, because BDS never computes beam sections (the beam is drawn by each client). Vanilla clients draw the
  engine's own beam (white through the auroralized glass): Bedrock has no block component for a beacon colour
  (the 1.26.51 client binary names no `minecraft:*beacon*` component, only the beacon block), and the beam is
  each client's own computation, so nothing the server sends can colour it; there is no closer workaround than
  vanilla stained glass, which would change the block. Checked by the client scenario (the `aurora: beacon beam
  at ... coloured` line with the engine's sections, the vanilla red-glass beacon's sections next to it, and the
  `beacon-beam` screenshot).- The aurora block's 10-tick break with `progress_glacier` (AuroraBrickBlock#getDestroyProgress, both sides in
  Java): the server turns its own destroy progress into 0.1 a tick (server-authoritative breaking), and the client
  mod does the same to the local player's game mode (`src/client/aurora`), so the crack animation and the client's
  break prediction run at upstream's speed. Without server-side progress within 3 ticks (client-authoritative breaking)
  a companion's client breaks the block itself; for a vanilla client nothing tells a held button from a tap then,
  so its mining is not sped up (the engine's own break time). The client scenario mines a block with a held
  button and waits for the server's `aurora: aurora block broken after N ticks of mining` line (local and
  remote-mod: N <= 14), then screenshots the spot (no rollback).
- Ice mobs' vanilla goals are BP behaviours; the stable ice core's RangedAttackGoal, the queen's hover goals and
  MeleeAttackGoal, LookAtPlayerGoal and RandomLookAroundGoal and everything upstream-specific are native (the
  queen's look goals are F8 goals so that the hover goals' LOOK flag keeps them off her head while she beams).
  HoverBeamGoal turns her as Mob#lookAt does (yaw and pitch by rotlerp, written to the rotation component:
  `Actor::setRotationWrapped` is client-only) and runs Java's LookControl step itself (`lookControlTick`:
  xRot reset to 0 and turned by at most the tracking, yHeadRot turned by at most the tracking): Bedrock's own
  look control never moves the head at the in-position tracking of 1 degree a tick (the head froze at a stale
  yaw, so the beam missed a target that had moved, and `aurora.snow_queen_beam` failed whenever the random
  hover spot needed a turn). Her ray uses that yaw and pitch, so it follows the target at upstream's rate. As in
  Java, the in-position pitch is at most 1 degree: the beam runs level from 0.25 above her feet and hits once
  she sinks to the target's height. The snow guardian's gear is
  enchanted by DifficultyInstance#getSpecialMultiplier (day time and moon phase, as Java) without the chunk's
  inhabited time (Bedrock has none). Its chestplate goes into the armour container directly (the engine's
  setItemSlot refuses an item without `minecraft:wearable`, which U7's chestplates do not have yet).
- The unstable ice core's death: the lethal hit is remembered and, after the 60 ticks of upstream's tickDeath, the core
  is finished with the same damage type and entities and a hit of its remaining health (so `combat::onDealt`
  listeners see no inflated amount), so the kill (progression's onKilled, gloaming_hunter), looting and XP stay
  the killer's. Its death sound plays once, at the lethal hit (the RP gives the core no
  engine death sound).
- The ice crystal's age (crystalAge / maxCrystalAge, not saved in upstream) lives in memory only: a reloaded summoned
  crystal stays, as in upstream.
- Snow Queen shields: F8 parts; a hit is blocked with upstream's break sound; a piercing arrow is let through (the
  damage is cancelled before the shield forwards it and the arrow keeps its flight past the shield). The arrow
  that hit is the shooter's arrow (by unique id) whose flight, now or as recorded at the queen's last tick
  before the engine stopped it, crosses the shield first, else the nearest one; a plain arrow next to a
  piercing one is blocked (`aurora.snow_queen_shields`). The
  shields are drawn by a second geometry of the queen turned from a synced anchor
  (`gloamingforest:shield_tick`, every second) and the client's clock, so the drawn shields follow the hit
  boxes on every client.
- The unstable ice core's map colours: Java's MapColor#col from a table of the vanilla blocks it meets most
  (terrain, the biome-tinted grass block and leaves, planks, wool, concrete, terracotta: `javaMapColor` in
  `Mobs.cpp`); any other block falls back to Bedrock's `BlockType::getMapColor` (Java's palette for most plain
  vanilla blocks; `aurora.map_colors` reports how many of a sample agree).
- Snow particles: one Bedrock effect per upstream particle over a 4-frame atlas of upstream's snow sprites (a random frame
  per particle). The motion: upstream's random start (Particle's normalised random direction x 0.06..0.18 x 0.1 a
  tick), drag ln(0.7) x 20 = 7.13 / s and an acceleration of 9.51 / s^2, the continuous form of
  `v = 0.7 v - 0.02` a tick (same terminal speed 1.33 / s; time constant 2.8 against 3.3 ticks).
- The snow around ice mobs and the queen is a looping emitter of their client entities (upstream spawns it on the
  client). The beam (10 particles a tick, `gloamingforest:aurora_ice_beam`) and the drop warnings at the seven
  shields (`gloamingforest:aurora_shield_warning`) are emitted by the queen's client entity too, from one-tick
  looping animations started by the synced `gloamingforest:beam` / `gloamingforest:phase` properties, with
  `pre_effect_script` variables for her look (body yaw, pitch towards her target: getLookAngle) and each
  shield's angle (60 i + tick x 5 from the synced shield anchor); the server sends no particle packets for them.
  The melting particles are still sent through F10's fx: upstream's 3 spots a tick around the eyes, each a cloud
  (1 in 4) and a falling water drop (1 in 2), never while dying (the snow aura stops then too: the client
  controllers leave their emitter state when the mob dies or the unstable core starts dying).
- The dying unstable ice core (UnstableIceCoreRenderer#scale / getWhiteOverlayProgress): the server syncs
  deathTime (`gloamingforest:dying`) every tick; the client entity scales a root bone (a copy of F2's geometry
  with one root at the feet, `assets/pack/rp/models/entity/aurora/unstable_ice_core_swell.geo.json`) by
  1.4 x f2 / 1.1 / f2 with f2 = 1 + sin(t x 100) x t x 0.01 (the bob is the root's translation, before the
  scale, as Java's), and the render controller's overlay is 75 % white when (t / 2) % 2 == 1.
- AI state and F8's sweep: F8 drops an actor's state (goals, move control) 200 ticks after it stops resolving
  (an unloaded chunk). The queen's and the stable core's goals are added whenever F8's state of the actor has
  none (`nativeGoalsPresent`), and a queen that gets them again restarts in SUMMON with 6 summons, as a new Java
  entity (`aurora.native_state_reload`).
- The aurora block's pointers (its 16 variants and F7's block of it) are the current world's: reset when a world
  closes (the client opens many worlds in one process); the local client scenario reopens the world and runs
  `aurora.aurora_block_variant` again.
- Eye height: Bedrock has no eye-height component and puts a mob's eyes at 0.9 x its collision height (1.62,
  2.25); `getEyePos` reads the offsets component's eye offset, which `applyJavaEyeHeight` sets every tick to
  Java's 1.53 (snow guardian, ice crystal), 1.35 (ice cores) and 2.125 (queen); `aurora.mob_attributes` checks
  the live eye heights. The other golden attributes (health, speeds, attack, follow range, knockback
  resistance, box, XP only for a player kill) are gtested against `tests/golden/mobs.json`
  (`AuroraMobs.MatchJavaGoldens`).
## Audit round 1

- Checked: `aurora.snow_queen_beam` failing in `full` and `aurora` runs on several branches. Diagnosed with
  per-10-tick traces of the queen: in position (HoverBeamGoal's tracking 1) Bedrock's look control froze her
  head yaw (unchanged for 300 ticks while the target was 40 degrees off after a shield knock-back), so the ray
  missed. Not caused by the F5 / F6 / F7 / F8 / F10 / F11 PRs: the result only depended on the random hover
  spot (U28's own random source), which explains the pass on one branch and fail on another.
- Fixed: HoverBeamGoal runs Java's LookControl step itself and aims the ray with it (see the deviation above);
  the test waits up to two goal cycles (600 ticks, stopping at the first hit) and also reads her beam flag
  after each 10-tick wait (a beam that started and hit inside one wait ended the loop before the flag was
  read: "she beams once in place" failed although the beam had hit).
- Open: `aurora.unstable_ice_core` failed twice in all recorded runs ("the ice became stained glass:
  minecraft:ice"): transformBlocks did not reach the ice. A likely cause is the core's block at its death
  (knock-back into a neighbour, or feet just below the block), which can put the ice beyond the smallest
  random range (2); placing the blocks around the dying core's own block instead lets the explosion break the
  ice (adjacent, at its height), so that was not the fix. The failure message now reports where the dying
  core lay, for round 2.
- Open: none for the beam. HoverSummonGoal / HoverThenDropGoal (tracking 30) still rely on Bedrock's look
  control, which turns the head at that rate.

## Audit round 2

Sounds (W2 round 2, audio pass). Checked with `tools/sounds/lint.ps1` on the assembled pack (`tools/assemble.ps1 -Check`): before this round 9 of this unit's sound events still played F3's name-derived baseline placeholder. Ice core, Snow Guardian and Snow Queen (shared ice files, as in upstream). `tools/sounds/recipes/aurora.json` now designs every one of them (original synthesized sounds, unit-local patches layered from the shared library; upstream's audio never read; ids, subtitles, categories, volumes, pitches and variant counts unchanged), rendered with `pwsh tools/sounds/synth.ps1 -Unit aurora` into `assets/gen/audio/aurora/`. `synth.ps1 -Check` and `lint.ps1` pass; with the other round-2 audio PRs (one per owning unit) the lint reports 0 baseline-only events for the whole pack.

Also: the unstable ice core's death sound is played once from `Mobs.cpp` at the lethal hit; its `sounds.json.d/aurora.json` entry now says `"death": ""` so Bedrock's `entity_sounds.defaults` (`game.player.die`) cannot add a second, generic death sound (upstream's UnstableIceCore plays ICE_CORE_DEATH only).

Flake check: `aurora.unstable_ice_core` passed 45 times and failed once in the recorded runs of the machine's
worktrees; that failure came from a build older than round 1's report (its kill was `minecraft:generic`, its message
had no core position), so there is no new evidence to work from. Unchanged; the message reports where the dying core
lay if it fails again. A5's sweep row for the core's death sound is settled by the `"death": ""` entry above (the
sound itself was already right).
