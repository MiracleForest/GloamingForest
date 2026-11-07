# U17 `hostiles`: forest and hollow-hill hostiles

Behaviour reference: [docs/spec/hostiles.md](../spec/hostiles.md) and TF 4.8 (`9e40a757`). This page records what
the unit reproduces in a different way than TF, with the reason and the workaround (docs/spec/not-ported.md, section
"How to add an entry"; A7/A8 fold the rows into that list in W2).

## How the mobs are built

- The vanilla parts of the goal stacks (float, leap, melee, stroll, look, avoid, sun, and every target goal) are the
  behavior pack's behaviours (`assets/pack/bp/entities/hostiles`); TF's own goals are native F8 goals
  (`src/feature/hostiles`): the breath (winter wolf, fire beetle), the charge (pinch beetle), the redcap goals, the
  druid's and slime beetle's `RangedAttackGoal`, the slime beetle's `AvoidEntityGoal` and every wraith goal.
- Java's goal flags: while a native goal with `MOVE` runs, the BP's melee and stroll (component group
  `gf:vanilla_move`) are switched off, so a lower-priority vanilla goal cannot move the mob (Java's flag lock).
- Light-dependent behaviour uses Java's values: the king spider's `SpiderTargetGoal` (group `gf:hostile`, switched by
  the Twilight Forest `getLightLevelDependentMagicValue` >= 0.5), the mist wolf's bite (raw brightness 0) and its
  render colour (property `twilightforest:brightness`, the raw brightness at its block).
- The animations are TF's `setupAnim` (F2's translated base poses) plus the conditional parts driven by entity
  properties and queries: the hostile wolves' angry face and tail (`twilightforest:aggressive`,
  `twilightforest:has_target`), the pinch beetle's open pincers (`query.has_rider`), the slime beetle's translucent
  tail (a second render controller over TF's tail model), the wraith's alpha 0.6, the mist wolf's light tint.

## Not ported

| TF behaviour (class) | Status | Reason | Workaround |
|---|---|---|---|
| `jeed:effect_provider` recipes `twilightforest:jeed/blindness`, `twilightforest:jeed/poison` | not ported | they load only with the JEED mod (spec section 6) | none |
| Breath particles spawned on the client (`WinterWolf` / `FireBeetle#aiStep`, `isClientSide`) | different path, same result | the breathing flag is not a vanilla-visible state | the server sends the same particles per tick through `gf::fx` (companions and vanilla clients alike) |
| `HostileWolf` variants (`WolfVariant` holder) | reduced to TF's effective behaviour | TF never rolls a variant at spawn (always `pale`); only an NBT edit changes it | the pale wild / angry textures; no variant property |
| `MistWolfRenderer` colour from the client's light | server computed | Molang has no light query | the server writes the raw brightness at the wolf's block every 5 ticks into `twilightforest:brightness`; the render controller applies TF's formula |
| Skeleton druid loot pickup (`AbstractSkeleton#finalizeSpawn` `setCanPickUpLoot`) | not ported | Bedrock's equip-item behaviour does not follow Java's chance | druids never pick up items |
| `Mob#doHurtTarget` replacements (`Wraith` haunt, `PinchBeetle` clamped, `FireBeetle` breathing scorched) | one tick later | the replacement damage cannot run inside the engine's melee hurt (the combat pipeline runs one hurt per victim) | the vanilla hit is cancelled and the TF damage (with its type, death message and effects) applied on the next tick |
| Spider jockeys of a druid on top of a vanilla skeleton (king spider with a 1-in-100 skeleton) | degraded | Bedrock skeletons cannot carry riders | the druid rides the spider when the skeleton refuses it |
| Wraith home for `MobSpawnType.STRUCTURE` | by the placer | Bedrock keeps no spawn reason | a wraith from a spawner (`MobSpawnMethod::Spawner`) keeps its spawn position as home; a structure that places a wraith directly sets its home with `gf::entity::setHome(mob, pos, 20)`, which the wraith keeps |

## Audit round 1

A5 (mobs audit, W2 round 1). Checked against F13's goldens (tests/golden/mobs.json, loot.json) and TF 4.8.

- **Spawn sweep** (new): `src/core/hostiles/MobSweep.h` lists all 80 TF entity types (attributes, box, fire
  immunity, summonability, loot table, Peaceful despawn, null sounds, bosses), kept equal to the goldens by
  `tests/hostiles/MobSweepTest.cpp`. Selftests in `src/feature/hostiles/SpawnSweep.cpp`:
  `hostiles.spawn_sweep` (every type spawns; living: max health, movement speed, attack damage, follow
  range, knockback resistance, box, fire immunity; others: box, fire immunity), `hostiles.mob_assets`
  (loot table file, hurt / death entity sounds, far-away despawn component, `/summon` refusal),
  `hostiles.peaceful_despawn` (Peaceful removes every non-boss monster, keeps the rest) and
  `hostiles.multiplayer_target` (two SimulatedPlayers: a hostile wolf takes the nearer one and switches
  to the other when the first goes creative). Other units' entities are audited too; their known
  deviations are info lines (`AUDIT <owner> <id>: ...`), anything new fails.
- **setupAnim todos**: the redcap's `FixedHumanoidModel#setupAttackAnimation` arm offsets
  (`rightArm.x/z`, `leftArm.x/z` from `armWidth` 3 and the body turn) are
  `animation.twilightforest.redcap.attack_arms` (redcap and sapper). The other todos of U17's models were
  already covered: the wraith's vanilla `HumanoidModel.setupAnim` (look-at plus `wraith.legs`), the pinch
  beetle's `isVehicle` pincers (`pincers_open`), the redcap's `isUsingItem` (redcaps never use items) and
  `swimAmount` (mobs never take the swimming pose: always 0).
- **Fixed: Peaceful** (`Monster#shouldDespawnInPeaceful`, `HostileWolf` / `Wraith` too): Bedrock kept every
  custom monster on Peaceful; each U17 mob's tick now removes it first thing (`despawnInPeaceful`,
  Common.cpp). The sweep showed the same for every other unit's monsters (reported, not fixed here).
- **Sweep notes**: Bedrock reports projectile-like actors fire immune whatever the BP says, so only the
  Java fire-immune non-living types are checked. The other units' deviations the sweep reports are
  listed in `known()` of SpawnSweep.cpp with their owners.
- **Still open**: a left-handed redcap (Mob#finalizeSpawn, 5 %) swings and holds its item in the right
  hand (Bedrock renders a mob's main hand on the right bone only). The breath particles stay server
  sent (same particles, every client); an entity-side emitter would need head rotation in
  `pre_effect_script` that cannot be verified without a client run. The haunt / clamped / scorched hits
  stay one tick late (the combat contract cannot change a hurt's damage type: F10). The wraith's
  STRUCTURE home still needs the spawn reason on the actor (F5 `spawn::spawnMob` / F7 `runEntity`).
- **Client scenario**: `tools/e2e/client/scenarios/hostiles.ps1` adds the redcap swing frames and an
  animation review of every living non-boss TF mob (`anim_<name>`); written, not run (A5).

## Audit round 2

Sounds (W2 round 2, audio pass). Checked with `tools/sounds/lint.ps1` on the assembled pack (`tools/assemble.ps1 -Check`): before this round 25 of this unit's sound events still played F3's name-derived baseline placeholder. Fire, pinch and slime beetles, redcap, the three wolves (shared files, as in TF) and the wraith (U25 stronghold mirrors the redcap and wraith designs for its goblins and Knight Phantom, TF's shared files). `tools/sounds/recipes/hostiles.json` now designs every one of them (original synthesized sounds, unit-local patches layered from the shared library; TF's audio never read; ids, subtitles, categories, volumes, pitches and variant counts unchanged), rendered with `pwsh tools/sounds/synth.ps1 -Unit hostiles` into `assets/gen/audio/hostiles/`. `synth.ps1 -Check` and `lint.ps1` pass; with the other round-2 audio PRs (one per owning unit) the lint reports 0 baseline-only events for the whole pack.

Open (C++, not edited in this audio-only round): `src/feature/hostiles/SpawnSweep.cpp` `known()` still lists `sound.hurt` / `sound.death` deviations for adherent, harbinger_cube, roving_cube, plateau_boss (U30) and giant_miner, armored_giant (U29); their owners' `sounds.json.d` now map Monster's default hurt/death, so those rows are stale (harmless: the finding no longer fires).

- **Fixed: `[Animation][error] can't find animation riding.body`** (x3 in the remote-vanilla client run of
  2026-11-07). The skeleton druid's client entity (`assets/pack/rp/entity/hostiles/skeleton_druid.entity.json`)
  animates with Bedrock's `controller.animation.humanoid.riding`, whose `riding` state plays `riding.body`,
  `riding.arms` and `riding.legs` since the 1.21.90 vanilla pack (`vanilla_1.21.90` .. `vanilla_1.21.130`
  `animation_controllers`; the base `vanilla` pack's older copy plays only arms and legs). The entity named only
  `riding.arms` / `riding.legs`; it now also maps `riding.body` to `animation.humanoid.riding.body` (as Bedrock's own
  skeleton does). No other client entity of the packs uses that controller.
- **Breath particles** (winter wolf snow, fire beetle flames): still server sent through `gf::fx::particle`, so every
  client (companion or vanilla) gets the same particles from the server's view vector, as TF's client-side
  `aiStep` emits them. Moving them into the client mod needs the server to stop sending them to companions only,
  which `gf::fx` cannot do today (no per-player or vanilla-only emission; F10's contract): open, a candidate for
  an fx extension. The velocity of `twilightforest:snow` / `minecraft:flame` on vanilla clients is F10's fx
  molang (see the `variable.velocity` content-log errors of other units' particles).
- **Spawn sweep sound rows**: the `known()` rows of the lower goblin knight's `sound.hurt` / `sound.death` are gone (U25's
  audio PR #149 maps them). The rows of U23's maze slime, U29's giants and U30's final castle mobs stay until their
  audio PRs (#161, #158, #160) are on main; the unstable ice core's `sound.death` row stays (U28 keeps its death
  single, played by code). A stale row is harmless (it only turns a mismatch into an info line); drop each with its PR.
