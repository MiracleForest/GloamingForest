# U6 `flora`: Flora, critters and berries

Behaviour reference: [docs/spec/flora.md](../spec/flora.md) and TF 4.8 (`9e40a757`).

## Audit round 2

Sounds (W2 round 2, audio pass). Checked with `tools/sounds/lint.ps1` on the assembled pack (`tools/assemble.ps1 -Check`): before this round 10 of this unit's sound events still played F3's name-derived baseline placeholder. Cicada and the jar block's sounds. `tools/sounds/recipes/flora.json` now designs every one of them (original synthesized sounds, unit-local patches layered from the shared library; TF's audio never read; ids, subtitles, categories, volumes, pitches and variant counts unchanged), rendered with `pwsh tools/sounds/synth.ps1 -Unit flora` into `assets/gen/audio/flora/`. `synth.ps1 -Check` and `lint.ps1` pass; with the other round-2 audio PRs (one per owning unit) the lint reports 0 baseline-only events for the whole pack.

Open: nothing for audio.
