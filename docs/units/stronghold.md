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

## Audit round 2

Sounds (W2 round 2, audio pass). Checked with `tools/sounds/lint.ps1` on the assembled pack (`tools/assemble.ps1 -Check`): before this round 17 of this unit's sound events still played F3's name-derived baseline placeholder. Goblin knights and the block-and-chain goblin (shared goblin files, as in TF; the muffled set is the same voice inside a helmet), helmet crab, Knight Phantom (wraith files, as in TF) and the trophy pedestal's stand-in for Java's zombie infection sound. `tools/sounds/recipes/stronghold.json` now designs every one of them (original synthesized sounds, unit-local patches layered from the shared library; TF's audio never read; ids, subtitles, categories, volumes, pitches and variant counts unchanged), rendered with `pwsh tools/sounds/synth.ps1 -Unit stronghold` into `assets/gen/audio/stronghold/`. `synth.ps1 -Check` and `lint.ps1` pass; with the other round-2 audio PRs (one per owning unit) the lint reports 0 baseline-only events for the whole pack.

Shared files: the goblin (redcap) and Knight Phantom (wraith) files are TF's shared files, so `recipes/stronghold.json` keeps U17 hostiles' designs of them (same patch, parameters, seeds) and both owners' trees sound the same, as in TF. Also: the lower goblin knight's ambient/hurt/death are played from `Goblins.cpp` (muffled by `hasArmor`); `sounds.json.d/stronghold.json` now gives it an entry with empty `ambient` / `hurt` / `death` so Bedrock's `entity_sounds.defaults` cannot add generic hurt/death sounds on top.

Checked:

- `twilightforest:lower_goblin_knight` hurt / death / ambient sounds (A5's sweep): played by the code
  (`Goblins.cpp`: `GOBLIN_KNIGHT_(MUFFLED_)AMBIENT / HURT / DEATH` by `hasArmor`), which sounds.json cannot choose
  by armour; the sweep only reads sounds.json and keeps reporting them. Correct as they are.
- `KnightStructureStart`'s box (max y one below the start point): F6 builds it in (`Starts.cpp`, trait
  `topBelowStart`, PR "F6: Clamp the knight stronghold start box"); nothing left for U25.

Fixed:

- Real conquest: `stronghold.conquest` forces a knight stronghold start far from the test rows (nothing placed),
  gives a lone knight phantom (the last of its group) its home inside the first piece and kills it:
  `KnightPhantom#postmortem` conquers the start. The dark chest at the unloaded home is dropped
  (`stronghold.phantom_fight` checks the chest).

Open:

- The crack animation on the shield's outside faces: Bedrock's mining progress is the client's own, from the block's
  single destroy time. Hiding it needs a client-side destroy-progress hook (R8: no unit may hook mining; an F0 / F12
  contract such as a per-face destroy speed filter on `interactions`), so it stays as in round 1.
- F1's `import.behaviour` case for `stronghold_shield` (see round 1).
