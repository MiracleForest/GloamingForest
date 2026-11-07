# U25 `stronghold`: Knight stronghold and Knight Phantoms

Behaviour reference: [docs/spec/stronghold.md](../spec/stronghold.md) and TF 4.8 (`9e40a757`).

## Audit round 1

Checked (A3, landmarks and bosses I):

- The fight (`stronghold.phantom_fight`): six knights from the spawner, their numbers, weapons, home and formations, no
  chest while a knight lives, `IT_IS_OVER` on the last one and the dark chest with the trophy. The conquest of
  `twilightforest:knight_stronghold` at the home point is the unit's own `setConquered` call (`Phantom.cpp`, `KnightPhantom#postmortem`); it does not go through the boss kit's default postmortem, so the test cannot see it without a start at the test site (checked by reading the code against TF).
- The stronghold against TF's `KnightStrongholdStructure` record (`stronghold.landmark_walk`): locked by
  `progress_trophy_pedestal`, the `tfstronghold` hint book, conquerable; its protected pieces refuse a survival player
  without the advancement and let him in with it.

Fixed:

- `twilightforest:stronghold_shield` could never be broken: the imported BP keeps TF's `strength(-1)`, but
  `StrongholdShieldBlock#getDestroyProgress` gives the inside face `digSpeed(stone) / 1.5 / 100` a tick. A manual BP
  (`assets/pack/bp/blocks/stronghold/stronghold_shield.json`) now gives the block `seconds_to_destroy` 5 (7.5 s by
  hand, Bedrock's pickaxe speeds through `minecraft:is_pickaxe_item_destructible`: 150 / digSpeed ticks as in TF) and
  keeps it indestructible by explosions and without drops (TF's `noLootTable()`: `loot_tables/empty.json`); the unit's interaction handlers still refuse survival mining on any other
  face (`stronghold.shield_faces`).

Open:

- F1's `import.behaviour` selftest (`src/feature/ext/import/ImportTests.cpp`, `stronghold_shield` case, and
  `tools/import/expected-behaviour.json`) expects the stored destroy speed -1 (TF's registered `strength(-1.0F,
  6000000.0F)`); with the manual BP it reads 5. That case needs an exception (the block's effective break time is the
  overridden `getDestroyProgress`, not its registered strength), owned by F1.
- `KnightStructureStart`'s bounding box max y clamped to the start y (docs/spec/stronghold.md, Start): needs F6's
  start-box hook, which another worker is adding in this round; not done here.
- The client sees the crack animation on the outside faces before the server refuses the break (Bedrock's mining
  progress is the client's); TF's client shows no progress there.
