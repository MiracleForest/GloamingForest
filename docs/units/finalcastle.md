# finalcastle (U30): Final castle and thornlands

Implementation notes of U30 (spec: [docs/spec/finalcastle.md](../spec/finalcastle.md)). The code documents its
Bedrock adaptations in the file headers of `src/feature/finalcastle/` (force fields, castle doors, thorns, the
cinder furnace, the gazebo's WIP texts).

## Audit round 1

Checked against TF 4.8 (CinderFurnaceBlock / CinderFurnaceBlockEntity, ForceFieldBlock, CastleDoorBlock, TFBlocks
properties, FinalCastleBossGazeboComponent).

Fixed:
- The cinder furnace's animateTick ([VanillaCopy] FurnaceBlock#animateTick: the crackle 1 in 10, smoke and flame
  at the front face) was not drawn. The server now sends it while the furnace burns, at the rate Java's
  ClientLevel#animateTick picks a block near the player (667 / 32^3 + 667 / 64^3 a tick).

Open (round 2):
- Force field corner panels: TF's `cornerConnects` (a sturdy neighbour or a field connected the other way) has no
  block state; the BP draws a corner when both arms connect. Needs a client block renderer (F12
  `registerBlockRenderer`) or extra states within the permutation budget.
- Castle doors: TF's overlay is drawn full-bright (glow) and vanished doors have MapColor.NONE; Bedrock's material
  instances have no emissive option and `minecraft:map_color` is per block (the vanished door keeps its dye
  colour on maps). Face culling between connected doors follows the BP geometry's bone visibility.
- Cinder furnace hopper faces: Java's WorldlyContainer sides (top input, sides fuel, bottom output); F10's
  `inventory::Handler` has no face argument (a W0 contract change for F10 / A8).
- Thorn connection details, the roving cube and the WIP texts as world text (name tags today; the client mod has
  no world-text renderer) are unchanged this round.
- Helper actor ids `gloamingforest:final_castle_text` / `_punch`: namespace decision left to A8.
