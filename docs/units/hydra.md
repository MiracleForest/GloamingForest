# U24 `hydra`: Hydra lair, Hydra and fire swamp

Behaviour reference: [docs/spec/hydra.md](../spec/hydra.md) and upstream 4.8 (`9e40a757`).

## Audit round 1

Checked (A3, landmarks and bosses I):

- The fight and the lair (`hydra.conquest`): the lair forced in a flat area, its hydra boss spawner, the Hydra woken
  with a home at the spawner, its death conquering the lair, the mangrove death chest at the home point. Added: the
  chest's contents (the hydra trophy, hydra chops and fiery blood of `entities/hydra`).
- The lair against upstream's `HydraLairStructure` record (`hydra.landmark_walk`): locked by `progress_labyrinth`, the
  `hydralair` hint book, conquerable; its protected pieces refuse a survival player without the advancement, let him in
  with it, and never refuse a creative player.

Fixed: nothing in the unit's code needed a change.

Open: none found in this round.

## Audit round 2

Checked against upstream 4.8 (`Hydra#knockback`, `Hydra#checkDespawn`, `GloamingEntities.HYDRA_MORTAR`) and the round-1 F10
damage pair (S3 / S4 "Open").

Fixed:

- `World.cpp causingAndDirect` is removed (F10 reports attacker = the owner, direct = the projectile now):
  the parts' hurt reads the attacker and the noted direct entity as is, the mortar's reflection reads the pair
  as is.
- The hydra mortar is fire immune (`buildNoEgg(..., true)`: `fireImmune()`): `minecraft:fire_immune` in its BP.

Verified, no change: the hydra's knockback resistance 1 is upstream's behaviour (`Hydra#knockback` is empty and
`isPushable` false: Java's attribute is 0 but the hydra never moves from a hit). Peaceful: `Hydra#checkDespawn`
discards the heads before `BaseTFBoss#checkDespawn`; the heads here are F8 parts of the hydra, removed with it
by the boss kit's Peaceful rule.

Sounds (W2 round 2, audio pass). Checked with `tools/sounds/lint.ps1` on the assembled pack (`tools/assemble.ps1 -Check`): before this round 1 of this unit's sound events still played F3's name-derived baseline placeholder. The bite warning (`hydra.warn`). `tools/sounds/recipes/hydra.json` now designs every one of them (original synthesized sounds, unit-local patches layered from the shared library; upstream's audio never read; ids, subtitles, categories, volumes, pitches and variant counts unchanged), rendered with `pwsh tools/sounds/synth.ps1 -Unit hydra` into `assets/gen/audio/hydra/`. `synth.ps1 -Check` and `lint.ps1` pass; with the other round-2 audio PRs (one per owning unit) the lint reports 0 baseline-only events for the whole pack.

Open: nothing for audio.
