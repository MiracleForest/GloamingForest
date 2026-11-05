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

Previously listed and now implemented: Ur-Ghast fireballs in flight across a save, a world reload or a chunk unload
keep `UrGhastFireball#onHit` (the tracked fireballs are saved in `gf:darktower:fireballs` and wait for their chunk).
