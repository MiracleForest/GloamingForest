# U23 `labyrinth`: Labyrinth and Minoshroom

Behaviour reference: [docs/spec/labyrinth.md](../spec/labyrinth.md) and TF 4.8 (`9e40a757`). This page records what
the unit reproduces in a different way than TF, with the reason and the workaround (docs/spec/not-ported.md, section
"How to add an entry"; A7/A8 fold the rows into that list in W2).

## Layout

| File | What |
|---|---|
| `src/core/labyrinth/Terraformer.{h,cpp}` | `LabyrinthStructure#getStructureTerraformer` (HollowHill, Focused, AbsoluteDifference, yClampedGradient in TF's float maths), the maze helpers of `MinotaurMazeComponent` and the mob rules; gtested in `tests/labyrinth` |
| `src/feature/labyrinth/Pieces.{h,cpp}` | the 27 `minotaurmaze` piece types on F7's `TFStructureComponentOld` / `TFMaze` (exact translations; the room and cell subclasses are one class each, the kind is the piece type id) |
| `src/feature/labyrinth/Structure.cpp` | the F6 landmark generator (`MazeRuinsComponent` at `(x + 5, y, z + 5)`, children from `Structure.GenerationContext#random()`) and the terraformer |
| `src/feature/labyrinth/Mobs.cpp` | Minotaur, Minoshroom (boss kit, spawner), Maze Slime |
| `src/feature/labyrinth/Blocks.cpp` | the mazebreaker rule (`interactions::onDestroy` on `#twilightforest:mazebreaker_accelerated_mining`) and the maze slime block |
| `src/feature/ext/labyrinth/W0Internals.h` | the W0 internal helpers U23 uses (F8 `EntityInternal.h`, F7 `Internal.h` / `Region.h`), the same set as U28's |

## Not ported

| TF behaviour (class) | Status | Reason | Workaround |
|---|---|---|---|
| `MazeSlimeBlock#isStickyBlock` / `canStickTo` (sticky for pistons, sticks to maze slime blocks and `#twilightforest:mazestone`) | not reproduced | Bedrock custom blocks cannot be sticky for pistons (no component; the piston's sticky logic is the engine's) | the block keeps the slime block's other behaviour (no fall damage, bounce, slowdown, translucent inner + outer model) |
| `SlimeBlock#updateEntityAfterFallOn` (bounce) for mobs | degraded: players bounce, mobs and items do not | the bounce needs the speed an entity landed with; players are tracked every tick (their movement is client-side), every other actor would cost a per-tick sweep of all actors | `SlimeBlock#stepOn`'s slowdown runs for mobs; the fall damage is cancelled for every entity that does not sneak |
| `MinoshroomRenderer.MinoshroomMushroomLayer` (three red mushrooms on the back and the head) | not drawn | an entity render controller draws one geometry with one texture; the mushrooms are the red mushroom block model | none yet (A8 / the client mod: a block render on the cow torso and the head bones) |
| `Minotaur#aiStep` / `ChargeAttackGoal` walk animation speed-up (`walkAnimation.setSpeed(+0.6 / +0.8)`) | approximated | Bedrock's walk animation state is the client's | while `twilightforest:charging` is set the animations swing the limbs at a fixed speed of 1.2 blocks a tick and full amplitude, the steady state of TF's per-tick additions |
| `EntityUtil#properlyApplyCustomDamageSource`'s knockback for a knockback-enchanted axe | the strength ignores the victim's knockback resistance | `LivingEntity#knockback` is applied as a motion change | the vanilla hit knockback still applies through the damage itself |
| Slime squish (`Slime#tick`'s squish / targetSquish) | client Molang approximation | the squish fields are client-only in TF too | the maze slime's client entity keeps its own squish variables (land -0.5, jump 1.0, decays 0.6 a tick) and scales the model like `MazeSlimeRenderer#scale` |

## Spec corrections (TF's source wins)

| Spec | TF 4.8 | GloamingForest |
|---|---|---|
| Section 1.1: the slam happens 30..59 ticks after the start | `GroundAttackGoal` does not override `requiresUpdateEveryTick`: `Mob#serverAiStep` ticks it only every other tick | the countdown runs on F8's goal cadence (every other tick), 60..118 ticks, while the client's charge animation fills in `maxCharge` ticks as in TF |
| Section 1: the Minoshroom's armour | `Minoshroom#populateDefaultEquipmentSlots` calls `super` (`Mob`'s random armour, 15 % x the special multiplier) before giving the axe; the Minotaur's does not | the Minoshroom may spawn with armour on harder difficulties; the minotaurs never do |
