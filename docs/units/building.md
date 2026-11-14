# U5 building: unit notes

Behaviour spec: [docs/spec/building.md](../spec/building.md).

## Audit round 2

- Fixed: `rope` and `iron_ladder` declared `on_liquid_touches: "no_reaction"`, so flowing water spread
  into them. They now use `"blocking"`, as F11's waterloggable families do (Java's
  `SimpleWaterloggedBlock#canPlaceLiquid` takes only a source; a bucket still waterlogs them).
- Fixed: the nine boss spawners dropped their own item when broken. Upstream registers them with
  `noLootTable()`; they now name `loot_tables/empty.json` (`tools/import/overrides/building.json`).
  Found by F9's `item.sweep_block_drops`.
- Fixed with F1: the `nagastone_spiral` recipes asked for Java's `minecraft:stone_slab`; the importer
  now writes Bedrock's `minecraft:normal_stone_slab`.
- Not done this round: a full audit of the unit against upstream. It was skipped to finish the round
  quickly and is still open.
