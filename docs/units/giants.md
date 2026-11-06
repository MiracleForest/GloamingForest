# U29 giants: Beanstalk, cloud castle and giants

Spec: [docs/spec/giants.md](../spec/giants.md). Code: `src/core/giants` (gtested in `tests/giants`),
`src/feature/giants`, `src/client/giants`, `src/feature/ext/giants` (contract extensions).

## How it maps to Bedrock

| TF | GloamingForest |
|---|---|
| `GrowingBeanstalkBlockEntity` | BlockData key `grower` (`core::giants::BeanstalkState`, TF's saved fields) + `gf:giants:growers`; the module ticks every loaded grower each server tick (`tickBeanstalk`, exact translation with Mth.sin's table) |
| Beanstalk rumble | dust: block particles (5 a tick instead of 20); camera shake: `giants.rumble` to companions (F12 `camera::shake`) |
| `SpecialStemLeavesBlock` | F11 leaves family; `huge_stalk` carries the families `log` flag so the leaf distance counts from it |
| `UberousSoilBlock` | F11 `onNeighborChanged` / `onPlaced`, `interactions::onPlace` (getStateForPlacement), bone meal through `BlockType::onFertilized` (15 times on the next tick) |
| `GiantBlockModel` | block state `twilightforest:giant_cell` (0 = untiled item / unfixed block, 1..64 = cell position) whose permutations pick one of 64 geometries with the face's 4 x 4 sub-square and only the cell's outer faces: every client, vanilla included, draws the tiling |
| `GiantBlock` placement / removal | `interactions::onUseOn` refuses an occupied cell, `onPlace` fills it next tick, F11 `onRemoved` removes an incomplete cell without drops |
| Giant pickaxe mining + loot modifier | `interactions::onDestroy` / `onDestroyed`, F10 `registerCondition("twilightforest:giant_pick_used")` and `addGlobalModifier("twilightforest:giant_block_grouping")` |
| Cloud weather | F11 random ticks (cauldrons, snow layers; farmland moisture and fire out under rainy clouds); `rainingFromCloudAt` = BlockHooks#isRainingAt's scan |
| CloudEvents rendering | client mod: drops (`gf:giants_cloud_rain` / `_snow`) and rain splashes from the clouds near the camera; vanilla clients: the same drops sent by the server |
| Giants | BP goals / attributes; native gear, `twilightforest:ant` melee, spawn rule |
| `giant_house` | F6 generator creating U27's `twilightforest:TFClCa` from its saved form (R2), registered once the piece type exists |

## Deviations

- Giants render with the default skin (Bedrock cannot bind another player's skin to a mob);
  `client.giantSkinUUIDs` is kept in the config section but unused.
- The rain / snow under clouds are falling particles, not TF's sheets; vanilla clients get them from
  the server (4 a tick per player).
- Entity wetness under rainy clouds (enderman / blaze damage) is not reproduced (no isRainingAt hook).
- The giant pickaxe / sword's +2.5 interaction range has no Bedrock attribute (not reproduced).
- The giant block outline is drawn besides Bedrock's own highlight (it cannot be replaced).
- Up / down faces of the tiled geometry follow Blockbench's Bedrock UV convention (both axes flipped):
  to be checked on the post-merge client screenshots.
