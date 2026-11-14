# U20 `naga`: Naga courtyard and Naga

Behaviour reference: [docs/spec/naga.md](../spec/naga.md) and upstream 4.8 (`9e40a757`).

## Audit round 1

Checked (A3, landmarks and bosses I):

- The fight end to end (`naga.death`): the spawner's long range, the segments popping one by one, the 124-tick trail
  to the gloaming oak / canopy death chest with the scales and the trophy, `player_killed_entity` / `hurt_boss` /
  `inventory_changed` and `progress_naga`'s criteria. Added: the conquest of the home landmark (`BaseTFBoss#postmortem`
  marks `gloamingforest:naga_courtyard` conquered at the home point).
- The courtyard against upstream's `NagaCourtyardStructure` record (`naga.landmark_walk`): no progression lock, no hint book,
  conquerable, no protected piece (upstream's `ConquerableStructure` has no `AdvancementLockConfig`).

Fixed: nothing in the unit's code needed a change.

Open: none found in this round.

## Audit round 2

Sounds (W2 round 2, audio pass). Checked with `tools/sounds/lint.ps1` on the assembled pack (`tools/assemble.ps1 -Check`): before this round 3 of this unit's sound events still played F3's name-derived baseline placeholder. Hiss, hurt and rattle. `tools/sounds/recipes/naga.json` now designs every one of them (original synthesized sounds, unit-local patches layered from the shared library; upstream's audio never read; ids, subtitles, categories, volumes, pitches and variant counts unchanged), rendered with `pwsh tools/sounds/synth.ps1 -Unit naga` into `assets/gen/audio/naga/`. `synth.ps1 -Check` and `lint.ps1` pass; with the other round-2 audio PRs (one per owning unit) the lint reports 0 baseline-only events for the whole pack.

Open: nothing for audio.
