# U4 woodwork: wood sets and wooden furniture

Behaviour spec: [docs/spec/woodwork.md](../spec/woodwork.md). Code: `src/feature/woodwork/`.

## What comes from where

| Part | Route |
|---|---|
| The eight wood sets (logs, wood, stripped, planks, stairs, slabs, fences, gates, doors, trapdoors, buttons, plates, signs, hanging signs, chests, trapped chests) | F11 family data imported by F1 (`assets/gen/import/woodwork/data/gf/families/woodwork.json`): placement, stripping (`AxeItem.STRIPPABLES`), door halves, slab doubling, sign text and editor, chest pairing, containers, lids and the trapped chest's signal are the families kit's. |
| Banisters | `Banister.cpp`: the shape on placement (connected under a banister), the axe cycle short -> tall -> connected with the extension toggled on a full cycle, the un-extension over a non-short banister (`blockevents::onNeighborChanged`), the gray tooltip. Facing and water: the families kit (`horizontal`, BanisterBlock: look direction; waterloggable). |
| Drying racks | `DryingRack.cpp`: DryingRackBlockEntity in block data (`drying_rack`: item, dry_time, total_dry_time, drying, the water seen last tick), an index of racks holding an item (`gf:woodwork:racks`) ticked every server tick while loaded, the `twilightforest:drying` recipes F9 loads, the inventory contract's 1-slot handler (insert one item into an empty rack, no extraction while drying; the structure kit's loot fill), the add / remove sounds, the drying particle. `DryingRackDisplay.cpp`: the item shown by a display entity (`gloamingforest:drying_rack_item`) holding it, placed as DryingRackRenderer draws it. |
| Hollow logs | `HollowLogs.cpp`: the item is a BP block placer of the vertical log, turned into the horizontal log on a side face; moss / grass / snow / shovel / shears inside the tunnel, vine / ladder in the shaft, shears on the climbable log; bucket water follows the variants. Shapes: the import overrides. Climbing: the families kit's climbable modifier (BlockProperty::Climbable). |
| Boats | `Boats.cpp`: `twilightforest:boat` / `twilightforest:chest_boat` (BP runtime identifiers `minecraft:boat` / `minecraft:chest_boat`: the game's boat physics, seats, paddling and chest), the wood as the entity property `twilightforest:wood` (0..7, TFWoodTypes order) choosing TF's texture on every client. The items place them (BoatItem#use: the view's block or fluid hit within reach). Geometry: Java's BoatModel / ChestBoatModel (`assets/pack/rp/models/entity/woodwork/boat.geo.json`, the boat rows along its local x as Bedrock's boats do). |
| Canopy bookshelf | `Bookshelf.cpp`: the enchanting table counts TF members of `#minecraft:enchantment_power_provider` on its ring (EnchantUtils::getBookCasePositions). Loot: imported (2 books, Silk Touch itself). |
| Shapes | `tools/import/overrides/woodwork.json` (generated from the Java VoxelShapes): per-permutation collision and selection boxes of the banisters, drying racks and the three hollow log blocks; the horizontal hollow logs' water layer; the hollow log items' block placer and 15 s fuel. Boxes are written unturned: Bedrock turns them with the permutation's transformation. |

## Engine hooks of the unit

R8 keeps the listed engine events with W0; these two are specific to this unit's own content:

- `Boat::$destroy` / `ChestBoat::$destroy`: after the game's own drop, a TF boat's vanilla boat item
  (the game drops the item of the entity's variant) is replaced by the TF item. Other boats pass.
- `EnchantUtils::getBookCasePositions`: the TF bookshelves are added to the game's list.

## Deviations

- Drying rack item: drawn as an item held by a display entity (both clients), not by a block entity
  renderer; Bedrock's held-item pose approximates Java's FIXED context.
- Banister extension and the climbable hollow logs' shapes keep Bedrock's limits: a block's boxes
  stay inside its own space (the extension's posts 8 px below the block have no box), and a
  selection box is one box (the hollow logs select as a full block; collisions are Java's).
- Horizontal hollow log crawling: Java sets the swimming pose on a sneak-click inside the log; the
  server cannot force a pose on a Bedrock player.
- Boats: no paddle animation (the game's own boat renderer animates vanilla boats only).
