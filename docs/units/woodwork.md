# U4 woodwork: wood sets and wooden furniture

Behaviour spec: [docs/spec/woodwork.md](../spec/woodwork.md). Code: `src/feature/woodwork/` (server),
`src/client/woodwork/` (client mod), `src/feature/ext/woodwork/` (contract extensions).

## What comes from where

| Part | Route |
|---|---|
| The eight wood sets (logs, wood, stripped, planks, stairs, slabs, fences, gates, doors, trapdoors, buttons, plates, signs, hanging signs, chests, trapped chests) | F11 family data imported by F1 (`assets/gen/import/woodwork/data/gf/families/woodwork.json`): placement, stripping (`AxeItem.STRIPPABLES`), door halves, slab doubling, sign text and editor, chest pairing, containers, lids and the trapped chest's signal are the families kit's. |
| Banisters | `Banister.cpp`: the shape on placement (connected under a banister), the axe cycle short -> tall -> connected with the extension toggled on a full cycle, the un-extension over a non-short banister (`blockevents::onNeighborChanged`), the gray tooltip. Facing and water: the families kit (`horizontal`, BanisterBlock: look direction; waterloggable). Block edits inside a `Consume` interaction are not rolled back (Interactions.h: unlike a cancelled PlayerInteractBlockEvent), so they stay in the click. |
| Drying racks | `DryingRack.cpp`: DryingRackBlockEntity in block data (`drying_rack`: item, dry_time, total_dry_time, drying, the water seen last tick), an index of racks holding an item (`gf:woodwork:racks`) ticked every server tick while loaded, the `gloamingforest:drying` recipes F9 loads, the inventory contract's 1-slot handler (insert one item into an empty rack, no extraction while drying), the loot fill (DryingRackBlockEntity#fillFromLootTable through `loot::placeContainer`: the table rolled at once, the last rolled stack kept whole, the timers untouched, so a dryable item turns on the next tick as in upstream), the add / remove sounds, the drying particle (ParticleUtils.spawnParticleOnFace: 0.3 out on the facing axis, no speed) at the rate of ClientLevel#animateTick's sampling, rolled per player and shown to that player only (`ext/woodwork/ParticleTo`). |
| Rack item (DryingRackRenderer) | Client mod: `src/client/woodwork/WoodworkClient.cpp` draws the item with the game's item renderer in Java's pose (0.4 towards the facing, 0.5 / 0.325 / 0.45 high, scale 0.99, -yRot, FIXED context) from what the server sends (`woodwork.rack`), and hides the display entity. Vanilla clients: `DryingRackDisplay.cpp`, a display entity (`gloamingforest:drying_rack_item`, a GloamingForest-only id, not summonable) holding the item, placed where the renderer draws it; one per rack (twins that load late are adopted or removed), it never drops its item. |
| Hollow logs | `HollowLogs.cpp`: the item places by the clicked face (side: horizontal along the face's axis, top / bottom: vertical); moss / grass / snow / shovel / shears inside the tunnel, vine / ladder in the shaft (a click from above faces the inner wall the look reaches; a vine leaves no water), shears on the climbable log; bucket water follows the variants; a sneak-click with an empty hand at the tunnel's end moves the player into it. Shapes: the import overrides. Climbing: the families kit's climbable modifier (BlockProperty::Climbable). |
| Boats | `Boats.cpp`: `gloamingforest:boat` / `gloamingforest:chest_boat` (BP runtime identifiers `minecraft:boat` / `minecraft:chest_boat`: the game's boat physics, seats, paddling and chest), the wood as the entity property `gloamingforest:wood` (0..7, GloamingWoodTypes order) and mark variant. The items place them (BoatItem#use). A destroyed upstream boat drops its upstream item (`ext/woodwork/BoatDrops`); an upstream boat landing after a fall of more than 3 blocks breaks into 3 planks and 2 sticks (Boat#checkFallDamage, the entity contract's tick). Client mod: the boats are drawn with their client entities (`ext/woodwork/EntityRenderers`): Java's BoatModel / ChestBoatModel geometry, upstream's textures, the chest part with the game's own chest boat texture, paddles animated from the rider's movement. |
| Canopy bookshelf | `Bookshelf.cpp`: the enchanting table counts upstream members of `#minecraft:enchantment_power_provider` on its ring (`ext/woodwork/EnchantPower`). Loot: imported (2 books, Silk Touch itself). |
| Materials | `Materials.cpp`: the dried slime drops are food (`minecraft:food` from the import overrides: nutrition 1 / 2, saturation 0.2 / 0.3, 32 ticks; Speed / Resistance for 600 ticks applied when the eating finishes, `item::registerItem`); tannin's craft remainder (crafting `treated_leather` gives the glass bottle back into the tannin's grid slot, `recipes::registerSpecialCrafting` on the BP recipe). |
| Fuel | `Fuel.cpp` + `ext/woodwork/Fuel`: Java's vanilla fuel tags for the wood set items (logs, planks 300, slabs 150, stairs, fences, gates, trapdoors, plates 300, doors and signs 200, buttons 100, hanging signs 800, boats 1200), HollowLogItem 300; banisters keep upstream's data map row. The BP items with a file (doors, signs, hanging signs, boats, hollow logs) also carry `minecraft:fuel` for vanilla clients. |
| Creative inventory | The vanilla-wood banisters and drying racks go into the vanilla fence gate group, the vanilla-wood hollow logs into the vanilla wood group (GloamingCreativeTabs insertAfter); the upstream woods are in upstream's own tab (F1's catalog). |
| Shapes | `tools/import/overrides/woodwork.json` (generated from the Java VoxelShapes): per-permutation collision and selection boxes of the banisters, drying racks and the three hollow log blocks; the horizontal hollow logs' water layer; the hollow log items' block placer and 15 s fuel. Boxes are written unturned: Bedrock turns them with the permutation's transformation. |

## Contract extensions (rule R18, `src/feature/ext/woodwork/`)

- `BoatDrops.{h,cpp}`: `Boat::$destroy` / `ChestBoat::$destroy`; a replacement item for the boats a
  unit names; only the boat's own drop is replaced (a new item actor of exactly the variant's boat
  item, one plain item, at most one per boat, no second replacement from a nested destroy): a chest
  boat's contents keep their items.
- `ParticleTo.{h,cpp}`: a particle shown to one player, and ClientLevel#animateTick's sampling rate
  (fold into `fx`).
- `EnchantPower.{h,cpp}`: `EnchantUtils::getBookCasePositions`; extra enchantment power providers.
- `Fuel.{h,cpp}`: `FurnaceBlockActor::getBurnDuration` for items without a `neoforge:furnace_fuels` row.
- `EntityRenderers.{h,cpp}` (client): `ActorRenderDispatcher::_getRenderer` draws a data-driven entity
  with its client entity instead of the renderer of its runtime identifier (never null);
  `ActorRenderDispatcher::render` skips the entities the client mod draws itself.

## Deviations

- Banister extension and the climbable hollow logs' shapes keep Bedrock's limits: a block's boxes
  stay inside its own space (the extension's posts 8 px below the block have no box), and a
  selection box is one box (the hollow logs select as a full block; collisions are Java's).
- Horizontal hollow log crawling: Java sets the swimming pose on a sneak-click; Bedrock's pose is the
  client's own (PlayerAuthInput), so the server moves the player into the tunnel; the client mod's local
  player takes the swimming pose there (`woodwork.crawl`) and leaves it outside the log; a vanilla
  client crawls only where the game itself does.
- Climbing the climbable hollow logs needs the client mod (Bedrock has no data-driven climbable block).
- Vanilla clients: upstream boats look like the game's boats (the game's boat renderer, by runtime
  identifier); the rack item is a held item of the display entity (the game's third-person hand pose).

## Audit round 2

- Fixed: the horizontal hollow logs declared `on_liquid_touches: "no_reaction"`, so flowing water
  spread into dry logs. They now use `"blocking"` (`tools/import/overrides/woodwork.json`), as F11's
  waterloggable families do: Java's `SimpleWaterloggedBlock#canPlaceLiquid` takes only a source, and a
  bucket still waterlogs the log.
- Checked, not changed: the 18 hollow log items have no `minecraft:icon` on purpose (the override
  removes it, commit d40cc841). Without an icon the game draws the placer block, the horizontal hollow
  log, which matches upstream's item model (parent `block/hollow_<wood>_log_horizontal`). F1's asset lint
  lists them as a note only; the woodwork client scenario's hotbar screenshot covers them.
