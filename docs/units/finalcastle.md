# finalcastle (U30): Final castle and thornlands

Implementation notes of U30 (spec: [docs/spec/finalcastle.md](../spec/finalcastle.md)). The code documents its
Bedrock adaptations in the file headers of `src/feature/finalcastle/` (force fields, castle doors, thorns, the
cinder furnace, the gazebo's WIP texts).

## Audit round 1

Checked against upstream 4.8 (CinderFurnaceBlock / CinderFurnaceBlockEntity, ForceFieldBlock, CastleDoorBlock, TFBlocks
properties, FinalCastleBossGazeboComponent).

Fixed:
- The cinder furnace's animateTick ([VanillaCopy] FurnaceBlock#animateTick: the crackle 1 in 10, smoke and flame
  at the front face) was not drawn. The server now sends it while the furnace burns, at the rate Java's
  ClientLevel#animateTick picks a block near the player (667 / 32^3 + 667 / 64^3 a tick).

Open (round 2):
- Force field corner panels: upstream's `cornerConnects` (a sturdy neighbour or a field connected the other way) has no
  block state; the BP draws a corner when both arms connect. Needs a client block renderer (F12
  `registerBlockRenderer`) or extra states within the permutation budget.
- Castle doors: upstream's overlay is drawn full-bright (glow) and vanished doors have MapColor.NONE; Bedrock's material
  instances have no emissive option and `minecraft:map_color` is per block (the vanished door keeps its dye
  colour on maps). Face culling between connected doors follows the BP geometry's bone visibility.
- Cinder furnace hopper faces: Java's WorldlyContainer sides (top input, sides fuel, bottom output); F10's
  `inventory::Handler` has no face argument (a W0 contract change for F10 / A8).
- Thorn connection details, the roving cube and the WIP texts as world text (name tags today; the client mod has
  no world-text renderer) are unchanged this round.
- Helper actor ids `gloamingforest:final_castle_text` / `_punch`: namespace decision left to A8.

## Audit round 2

Checked:
- Hurt / death sounds of the adherent, the harbinger cube, the roving cube and the plateau boss (A5's sweep): none
  overrides `getHurtSound` / `getDeathSound` in upstream (Monster's `entity.hostile.hurt` / `.death`). Their
  `sounds.json.d/finalcastle.json` entries come with the round-2 audio pass (branch `w2r2-audio-finalcastle`), not
  this PR.

Fixed:
- Force fields and thorns (upstream: SimpleWaterloggedBlock) declared `on_liquid_touches: "no_reaction"`: now
  `"blocking"` as F11's waterloggable families (flowing water stops at a dry block; a bucket still waterlogs it).

Still open (unchanged, with what they need):
- Force field corner panels (`cornerConnects` and ForceFieldModel's neighbour culling): a client block renderer
  (F12 `registerBlockRenderer`) or states over the permutation budget.
- Castle door glow (full-bright overlay) and the vanished door's MapColor.NONE: no emissive material or transparent
  map colour on Bedrock blocks; not-ported candidates unless F12's block renderer draws the overlay.
- Cinder furnace hopper faces: F10's `inventory::Handler` still has no face argument on main.
- Thorn connection details, the roving cube's look and the WIP texts as world text (name tags today; F12 has no
  world-text renderer).

Sounds (W2 round 2, audio pass): the adherent, harbinger cube, roving cube and plateau boss had no entity-sound entry (hostiles.mob_assets audit; Bedrock's own `entity_sounds.defaults` may already supply hurt/death, the explicit entry states upstream's choice either way). In upstream they keep Monster's defaults (`entity.hostile.hurt` / `entity.hostile.death`; the Adherent's own `GHAST_SHOOT` stays in code); `assets/pack/rp/sounds.json.d/finalcastle.json` now maps them to Bedrock's `game.player.hurt` / `game.player.die` (volume 1, pitch 0.8-1.2 as Java's voice pitch). None of them has an ambient sound in upstream. Open: nothing for audio.
