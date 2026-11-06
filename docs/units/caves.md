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
- The alpha yeti's ground pound runs on every landing while rampaging (Java's causeFallDamage on every landing;
  Bedrock has no fall event below 3 blocks).

## Goldens

`caves.troll_cave_layout`, `caves.troll_cave_hash`, `caves.cloud_castle_hash`, `caves.yeti_cave_hash` and
`caves.features_hash` are GloamingForest's own goldens recorded from the translation (in memory, seed 20260521).
The garden caves' features owned by other units (uberous soil patches, troll cave dirt / mycelium blobs, U18's
types) are left out of the goldens, so they stay stable when those units register their types.
