# caves (U27): Troll caves and yeti cave

Implementation notes of U27 (spec: [docs/spec/caves.md](../spec/caves.md)).

## Code

| Path | What |
|---|---|
| `src/core/caves/` | Pure maths (gtested in `tests/caves/`): the yeti cave terraformer, the troll cave hollowing distance, the cloud castle's procedural cloud, the falling ice damage, the raw brightness. |
| `src/feature/caves/Structures.*` | The pieces `TFTCMai`, `TFTCCon`, `TFTCGard`, `TFTCVa`, `TFClCa`, `TFCloud`, `TFClTr`, `TFTCloud`, `TFYeti` (exact translations, no templates) and the two landmark generators (the yeti cave with its terraformer). |
| `src/feature/caves/Features.cpp` | `big_mushgloom`, `troll_cave_big_mushgloom`, `troll_cave_huge_brown_mushroom`, `troll_cave_huge_red_mushroom`, `troll_vines`. |
| `src/feature/caves/Blocks.cpp` | Trollsteinn (faces by light on placement and random ticks, comparator output), troll roots (harvest, ripening, support). |
| `src/feature/caves/Troll.cpp`, `Yeti.cpp`, `AlphaYeti.cpp`, `FallingIce.cpp`, `Goals.h` | The mobs (vanilla goals as BP behaviours, TF's own goals native). |
| `src/client/caves/Sparkle.cpp` | The trollsteinn's purple sparkle (client mod, ClientLevel#animateTick sampling). |
| `src/feature/ext/caves/W0Internals.h` | The one door into F7 / F8 internals (see the header; for A8 to fold). |

## Bedrock adaptations

- The troll's held rock is drawn on the troll (RP render controller `controller.render.twilightforest.troll_rock`,
  property `twilightforest:rock`) instead of a ThrownBlock entity riding it: no actor the projectile kit would fly.
- Thrown blocks and falling ice draw a cube with the block's vanilla texture chosen by the property
  `twilightforest:block` (base stone overworld blocks / the ice blocks).
- The yeti throw state (YetiThrowAttachment) is F8's (`gf::entity::mount`, channel `projectile.thrown`), not a
  separate `caves.thrown` channel.
- The troll roots' support is the BP placement filter (pops when the block above changes) plus a neighbour check.
- The rampage snow ring, the tired splash and the falling ice's warning trail are RP particle emitters driven by
  the entity properties (every client, vanilla included).
- The alpha yeti's ground pound: Java's causeFallDamage runs on a landing with fallDistance > 0 (after the
  rampage's jumps or any drop, never on a step up). Bedrock has no fall event below 3 blocks, so the yeti's tick
  remembers whether it moved down while airborne and pounds on the landing that ends such a fall.

## Goldens

`caves.troll_cave_layout`, `caves.troll_cave_hash`, `caves.cloud_castle_hash`, `caves.yeti_cave_hash` and
`caves.features_hash` are GloamingForest's own goldens recorded from the translation (in memory, seed 20260521).
The garden caves' features owned by other units (uberous soil patches, troll cave dirt / mycelium blobs, U18's
types) are left out of the goldens, so they stay stable when those units register their types.

## Audit round 1

Checked against TF 4.8 (AlphaYeti, YetiRampageGoal, YetiTiredGoal, EntityUtil#canDestroyBlock, YetiCaveComponent,
Troll / ThrownBlock renderers).

Fixed:
- Rampage block breaking (`destroyBlocksInAABB`, also after 15 collisions) spared every block entity; it now spares
  only Java's `Container` block entities (furnaces, chests, barrels, hoppers, dispensers, droppers, shulker boxes,
  brewing stands, crafters, jukeboxes, chiseled bookshelves, decorated pots, plus Bedrock's item frames) and
  gf::inventory containers (TF's chests); signs, banners, beds, skulls and spawners break as in TF. Selftest
  `caves.alpha_yeti` places a chest, a sign and stone in the rampage box.
- The ground pound fired on every landing, including a landing that followed no fall; it now needs a downward
  move while airborne (Java's fallDistance > 0), see above.
- Yeti cave stalactites were untested (the in-memory region's heightmap stopped at y 0, so BlockSpikeFeature's
  `y < surface - 2` rule rejected every block: 0 ice). The test region's heightmap now sees the hill and
  `caves.yeti_cave_hash` checks the ice / packed ice / blue ice stalactites and their tips under the ceiling; the
  golden was re-recorded.

Checked, unchanged: the troll's held rock (render controller `troll_rock`, the rock cube at 48..64 px = Java's
passenger attachment at 1.25 x 2.4 blocks, turned with the body as Troll#positionRider) and the thrown block /
falling ice cubes (index mapping `rockIndex` shared by the troll and the thrown block). They are only verifiable
on screenshots: `troll-rock`, `thrown-block`, `falling-ice` of `tools/e2e/client/scenarios/caves.ps1` (not run
this round).

Open: those client screenshots; the yeti's own fall damage is the engine's (Bedrock's 3-block threshold).

## Audit round 2

Sounds (W2 round 2, audio pass). Checked with `tools/sounds/lint.ps1` on the assembled pack (`tools/assemble.ps1 -Check`): before this round 18 of this unit's sound events still played F3's name-derived baseline placeholder. Alpha Yeti and Yeti (shared files, as in TF) and Troll. `tools/sounds/recipes/caves.json` now designs every one of them (original synthesized sounds, unit-local patches layered from the shared library; TF's audio never read; ids, subtitles, categories, volumes, pitches and variant counts unchanged), rendered with `pwsh tools/sounds/synth.ps1 -Unit caves` into `assets/gen/audio/caves/`. `synth.ps1 -Check` and `lint.ps1` pass; with the other round-2 audio PRs (one per owning unit) the lint reports 0 baseline-only events for the whole pack.

Open: nothing for audio.
