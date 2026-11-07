# U26 `darktower`: dark tower, Ur-Ghast and tower devices

Behaviour reference: [docs/spec/darktower.md](../spec/darktower.md) and TF 4.8 (`9e40a757`). This page records what
the unit reproduces in a different way than TF, with the reason and the workaround (docs/spec/not-ported.md, section
"How to add an entry"; A7/A8 fold the rows into that list in W2).

## Not ported

| TF behaviour (class) | Status | Reason | Workaround |
|---|---|---|---|
| `InfestedBlock#spawnAfterBreak` runs only for a block broken in the world (player, explosion, `destroyBlock(pos, true)`) | degraded: infested towerwood that another unit's code or a `/setblock` / `/fill` command turns into air also spawns a towerwood borer | Bedrock tells a removal (`blockevents::onRemoved`, F11) without its cause; a block replaced by a command and one broken by the world look the same | the unit's own edits (setBlock, removeBlock, destroyBlock without drops) are marked quiet and spawn nothing; a player's break is told apart by an `interactions::onDestroy` note (creative and silk touch spawn nothing, as in TF) |
| `ReactorDebrisRenderer#renderSide` crops each face's texture to the random sub-box (`Mth.lerp(min, u0, u1)`) | degraded on the client mod: each face's texture is scaled over the sub-box face instead of cropped; the six inner duplicate faces are not drawn | F12's `render::drawGeo` draws a fixed geometry whose UVs cannot change per frame; exact crops would need one geometry per sub-rectangle (136 x 136 per face orientation) | one one-face geometry per side, scaled and placed to the random box (`calculateVoxelShape`, re-rolled at tick 5), with a random texture per face (netherrack, bedrock, nether portal, obsidian) |
| Reactor debris on a vanilla client | degraded: the debris block shows its blank model for its 3 seconds | vanilla clients have no block renderer | the block disappears on time (60 ticks) as in TF |
| `TFWeatherRenderer` halves the Ur-Ghast tantrum rain in fast graphics | not reproduced | the companion raises the client dimension's own rain level; Bedrock's rain has no fast-graphics density switch reachable from a mod | the rain level fades in and out as TF's `urGhastAlive` does (+0.1 a tick up to 1, -0.02 down) |
| `TFWeatherRenderer.urGhastAlive` through the weather renderer (spec section 14: F12's `sky::setProvider` weather) | different path, same result | F12's weather provider only draws (it has no rain-level input) and U15 owns the Twilight Forest provider | the companion writes the client dimension's rain level (at least the Ur-Ghast rain); the server also sends U15's `atmosphere.ur_ghast` `{"active"}` on change, and U15 only reads the level, so the two never fight. A8 may fold a rain-level input into F12's provider |

## Spec corrections (TF's source wins)

| Spec | TF 4.8 | GloamingForest |
|---|---|---|
| Section 5: `damageUntilNextPhase` starts at 10 | the field initializer says 10, but the `UrGhast` constructor calls `setInTantrum(false)`, which calls `resetDamageUntilNextPhase()` (18); `readAdditionalSaveData` does the same on every load | the first tantrum comes after 18 damage; a loaded Ur-Ghast resets to 18 |
| Section 6.1: the Ur-Ghast relies on `Ghast#hurt` (a reflected fireball deals 1000) | `UrGhast extends BaseTFBoss extends Monster`: `Ghast#hurt` is not in its chain; a reflected `UrGhastFireball` deals its own 16 (`onHitEntity`), a reflected ghastguard `LargeFireball` 6, divided by 10 in a tantrum | 16 / 6 (1.6 / 0.6 in a tantrum); the 1000 rule stays for the carminite ghastguard and ghastling, which extend `Ghast` |
| Goal timings (attack every 20 / 60 ticks) | `UrGhastAttackGoal`, `GhastguardAttackGoal`, the flight goals and the borer goals do not override `requiresUpdateEveryTick`, and `Mob#serverAiStep` ticks such goals only every other tick | warn at about 20 ticks, fire at about 40, a 120-tick cycle; the borer's summon countdown lasts 40 ticks and its merge chance is 1 in 20 a tick; target goals drop a target unseen for 60 ticks |

Previously listed and now implemented: Ur-Ghast fireballs in flight across a save, a world reload or a chunk unload
keep `UrGhastFireball#onHit` (the tracked fireballs are saved in `gf:darktower:fireballs` and wait for their chunk).

## Audit round 2

Sounds (W2 round 2, audio pass). Checked with `tools/sounds/lint.ps1` on the assembled pack (`tools/assemble.ps1 -Check`): before this round 4 of this unit's sound events still played F3's name-derived baseline placeholder. Ghast trap (on, warm-up, spin-down; played at Java's clamped pitch 2) and the sliding trap. `tools/sounds/recipes/darktower.json` now designs every one of them (original synthesized sounds, unit-local patches layered from the shared library; TF's audio never read; ids, subtitles, categories, volumes, pitches and variant counts unchanged), rendered with `pwsh tools/sounds/synth.ps1 -Unit darktower` into `assets/gen/audio/darktower/`. `synth.ps1 -Check` and `lint.ps1` pass; with the other round-2 audio PRs (one per owning unit) the lint reports 0 baseline-only events for the whole pack.

Open: nothing for audio.

Audit (W2 round 2, A5 / audit items):

Checked: the round-1 F10 damage pair (S3 "Open") in `UrGhast.cpp`.

Fixed: `reflectedFireball` reads `Ghast#isReflectedFireball` as TF writes it (direct entity a large fireball,
causing entity a player) and the fire-typed reflected hit goes in again with the same pair; the both-orders
workaround is gone. The unit had no round-1 audit list.

Open: the "Not ported" rows above (reactor debris crops, fast-graphics tantrum rain).

Regression fix (after the round-2 merges): `darktower.fireball_flight` spawned its carminite ghastling owner in
the e2e world's Peaceful difficulty; since F8 removes TF's monsters on Peaceful at their first tick
(`Monster#shouldDespawnInPeaceful`, src/feature/entity/Peaceful.cpp), the owner was gone before the second
fireball. The test now runs at Easy (`EasyDifficulty`, like the other mob tests of the unit).
