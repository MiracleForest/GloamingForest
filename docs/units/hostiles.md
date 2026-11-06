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
