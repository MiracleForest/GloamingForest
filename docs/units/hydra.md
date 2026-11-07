# U24 `hydra`: Hydra lair, Hydra and fire swamp

Behaviour reference: [docs/spec/hydra.md](../spec/hydra.md) and TF 4.8 (`9e40a757`).

## Audit round 1

Checked (A3, landmarks and bosses I):

- The fight and the lair (`hydra.conquest`): the lair forced in a flat area, its hydra boss spawner, the Hydra woken
  with a home at the spawner, its death conquering the lair, the mangrove death chest at the home point. Added: the
  chest's contents (the hydra trophy, hydra chops and fiery blood of `entities/hydra`).
- The lair against TF's `HydraLairStructure` record (`hydra.landmark_walk`): locked by `progress_labyrinth`, the
  `hydralair` hint book, conquerable; its protected pieces refuse a survival player without the advancement, let him in
  with it, and never refuse a creative player.

Fixed: nothing in the unit's code needed a change.

Open: none found in this round.

## Audit round 2

Sounds (W2 round 2, audio pass). Checked with `tools/sounds/lint.ps1` on the assembled pack (`tools/assemble.ps1 -Check`): before this round 1 of this unit's sound events still played F3's name-derived baseline placeholder. The bite warning (`hydra.warn`). `tools/sounds/recipes/hydra.json` now designs every one of them (original synthesized sounds, unit-local patches layered from the shared library; TF's audio never read; ids, subtitles, categories, volumes, pitches and variant counts unchanged), rendered with `pwsh tools/sounds/synth.ps1 -Unit hydra` into `assets/gen/audio/hydra/`. `synth.ps1 -Check` and `lint.ps1` pass; with the other round-2 audio PRs (one per owning unit) the lint reports 0 baseline-only events for the whole pack.

Open: nothing for audio.
