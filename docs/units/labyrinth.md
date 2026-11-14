# U23 `labyrinth`: Labyrinth and Minoshroom

Behaviour reference: [docs/spec/labyrinth.md](../spec/labyrinth.md) and upstream 4.8 (`9e40a757`). This page records what
the unit reproduces in a different way than upstream, with the reason and the workaround (docs/spec/not-ported.md, section
"How to add an entry"; A7/A8 fold the rows into that list in W2).

## Layout

| File | What |
|---|---|
| `src/core/labyrinth/Terraformer.{h,cpp}` | `LabyrinthStructure#getStructureTerraformer` (HollowHill, Focused, AbsoluteDifference, yClampedGradient in upstream's float maths), the maze helpers of `MinotaurMazeComponent` and the mob rules; gtested in `tests/labyrinth` |
| `src/feature/labyrinth/Pieces.{h,cpp}` | the 27 `minotaurmaze` piece types on F7's `GloamingStructureComponentOld` / `GloamingMaze` (exact translations; the room and cell subclasses are one class each, the kind is the piece type id) |
| `src/feature/labyrinth/Structure.cpp` | the F6 landmark generator (`MazeRuinsComponent` at `(x + 5, y, z + 5)`, children from `Structure.GenerationContext#random()`) and the terraformer |
| `src/feature/labyrinth/Mobs.cpp` | Minotaur, Minoshroom (boss kit, spawner), Maze Slime |
| `src/feature/labyrinth/Blocks.cpp` | the mazebreaker rule (`interactions::onDestroy` on `#gloamingforest:mazebreaker_accelerated_mining`) and the maze slime block |
| `src/client/labyrinth/MazeSlime.cpp` | client mod: the maze slime block's bounce and slowdown of the local player |
| `src/feature/ext/labyrinth/W0Internals.h` | the W0 internal helpers U23 uses (F8 `EntityInternal.h`, F7 `Internal.h` / `Region.h`), the same set as U28's |

## Not ported

| Upstream behaviour (class) | Status | Reason | Workaround |
|---|---|---|---|
| `MazeSlimeBlock#isStickyBlock` / `canStickTo` (sticky for pistons, sticks to maze slime blocks and `#gloamingforest:mazestone`) | not reproduced | Bedrock custom blocks cannot be sticky for pistons (no component; the piston's sticky logic is the engine's) | the block keeps the slime block's other behaviour (no fall damage, bounce, slowdown, translucent inner + outer model) |
| `SlimeBlock#stepOn` (slowdown) for vanilla clients | degraded: a vanilla player is not slowed | a player's movement is the client's: a server slowdown would fight it | with the client mod the local player is bounced and slowed by its own client (`src/client/labyrinth/MazeSlime.cpp`, the server leaves companions alone); a vanilla player still bounces by a motion packet (with its horizontal speed); every other actor bounces (its last airborne speed, kept from a per-tick sweep of the actors in the air; non-living ones keep 80 %) and is slowed on the server |
| `Minotaur#aiStep` walk animation speed-up (`walkAnimation.setSpeed(speed + 0.6)` every client tick while charging) | Molang translation | Bedrock's walk animation state is the client's | while `gloamingforest:charging` is set the limb phase grows by 0.9 a tick on top of the distance moved (`WalkAnimationState#update`'s steady state: position += target + 0.9) at full amplitude, continuous before and after the charge. `ChargeAttackGoal`'s +0.8 is server-only in upstream (the server's walk animation is never drawn) |
| Slime squish (`Slime#tick`'s squish / targetSquish) | client Molang approximation | the squish fields are client-only in upstream too | the maze slime's client entity keeps its own squish variables (land -0.5, jump 1.0, decays 0.6 a tick) and scales the model like `MazeSlimeRenderer#scale` |

## Spec corrections (upstream's source wins)

| Spec | upstream 4.8 | GloamingForest |
|---|---|---|
| Section 1.1: the slam happens 30..59 ticks after the start | `GroundAttackGoal` does not override `requiresUpdateEveryTick`: `Mob#serverAiStep` ticks it only every other tick | the countdown runs on F8's goal cadence (every other tick), 60..118 ticks, while the client's charge animation fills in `maxCharge` ticks as in upstream |
| Section 7.1: `MazeEntranceShaftComponent` grows its box down to sea level - 9 in `postProcess` | upstream changes the shared piece's box in its first `postProcess` | the box is grown once in the constructor (no generation thread writes a piece's box); the start's box and the chunks the shaft touches are the same, only the saved `BB` of an ungenerated shaft differs |
| Section 1: the Minoshroom's armour | `Minoshroom#populateDefaultEquipmentSlots` calls `super` (`Mob`'s random armour, 15 % x the special multiplier) before giving the axe; the Minotaur's does not | the Minoshroom may spawn with armour on harder difficulties; the minotaurs never do |

## Audit round 1

Checked (A3, landmarks and bosses I):

- A seeded fight from the spawner (`labyrinth.gauntlet`): a player wakes the Minoshroom at its spawner (home at the
  spawner), hits and kills it; the mangrove death chest holds the trophy and the meef stroganoff, the boss kit's
  postmortem conquers `gloamingforest:labyrinth` at the home point, `player_killed_entity` / `hurt_boss` fired.
- The labyrinth against upstream's `LabyrinthStructure` record (`labyrinth.landmark_walk`): locked by `progress_lich`, the
  `labyrinth` hint book, conquerable; its protected pieces refuse a survival player without `progress_lich`.

Fixed:

- `MinoshroomRenderer.MinoshroomMushroomLayer`: the three red mushrooms are drawn. A second render controller
  (`controller.render.gloamingforest.minoshroom_mushrooms`) draws `geometry.gloamingforest.minoshroom.mushrooms`
  with the vanilla `textures/blocks/mushroom_red`: two on the cow torso and one on the head, each the cross model of
  `block/cross` as a poly mesh whose vertices are upstream's pose stack (`translateAndRotate`, the -90 / -48 / 42 / -78 degree
  turns, the -1 / -1 / 1 scale) mapped like the converted model (x, 24 - y, z), in bones named `cow_torso` / `head`
  so the slam and head animations move them. Not yet seen in a client screenshot (the client scenario shows the
  Minoshroom in `labyrinth-mobs` and `labyrinth-slam`; A6/A7 runs it).
- The charge walk animation follows `Minotaur#aiStep` instead of a fixed speed: the limb phase grows by 0.9 a tick on
  top of the distance moved while charging (Minotaur and Minoshroom).
- The maze slime block's bounce / slowdown split: every non-player actor that lands on it after a fall bounces
  (`updateEntityAfterFallOn`, its airborne speed from a per-tick record of falling actors; 80 % for non-living ones;
  `labyrinth.slime_block` drops a pig from 6 blocks and sees it rise about 5); a companion's own client bounces and
  slows its player (`src/client/labyrinth/MazeSlime.cpp`) and the server leaves companions alone.

Open:

- A vanilla player on the block is bounced but not slowed (client-side movement).
- The mushrooms' look on a real client (no client run in this round).

## Audit round 2

Checked (W2 round 2, A5's sweep findings and round 1's open items):

- `gloamingforest:maze_slime` attributes: A5's sweep compares a freshly created entity with F13's golden (20 / 0.7 /
  2: `Monster.createMonsterAttributes` before any `Slime#setSize`). Every maze slime in upstream goes through
  `Slime#finalizeSpawn` -> `MazeSlime#setSize` (MAX_HEALTH size^2 + the double_health modifier = 3 size^2, speed
  0.2 + 0.1 size, attack = size), which is what the BP's size groups give (3 / 0.3 / 1 at size 1). Unchanged: the
  sweep's row stays a known deviation of an unsized Java entity, not a bug.
- Maze slime hurt / death / squish sounds: played by the code (`Mobs.cpp`: `MAZE_SLIME_*(_SMALL)` by size at
  `0.1 x size` volume), which sounds.json cannot express (one sound per event, no size). The sweep only reads
  sounds.json, so it keeps reporting them; correct as they are.

Fixed:

- A vanilla player on a maze slime block is slowed (`SlimeBlock#stepOn`). The server cannot scale a vanilla
  client's speed each tick, so while one stands on the block (on the ground, not sneaking) its movement speed gets a
  multiply-total modifier `gloamingforest:maze_slime_step` of `core::labyrinth::slimeStepSpeedFactor(0.8)` (about
  0.39: the steady walking speed with stepOn's x(0.4 + |dy| x 0.2) on the block's friction 0.8 over the speed
  without; gtest `Labyrinth.SlimeStepSpeedFactor`); the BP's `minecraft:friction` 0.2 already is Java's 0.8.
  Companions keep slowing themselves on their client. `labyrinth.slime_block` checks the SimulatedPlayer's speed on
  and off the block.
- A vanilla player teleported onto the block was bounced by the whole teleport height (the server's per-tick
  position change counted as a fall): a drop faster than Java's terminal speed (4 blocks a tick) is no fall now.
- Real conquest: `labyrinth.conquest` forces a labyrinth start far from the test rows (nothing placed), gives a
  Minoshroom its home at the start's center and kills it: the start is conquered (the boss kit's postmortem,
  `lastConquestFound`). The death chest at the unloaded home is dropped (`labyrinth.gauntlet` checks the chest).

Open:

- The mushrooms' look on a real client (no client run this round).

Sounds (W2 round 2, audio pass): the maze slime's hurt, death and squish sounds are played from `Mobs.cpp` (MAZE_SLIME_*(_SMALL) by size, at upstream's volume); `assets/pack/rp/sounds.json.d/labyrinth.json` now gives `gloamingforest:maze_slime` an entry with empty `hurt` / `death` so Bedrock's `entity_sounds.defaults` (`game.player.hurt` / `game.player.die`) cannot add generic sounds on top. Open: nothing for audio.
