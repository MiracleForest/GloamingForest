# darktower (U26): Dark tower, Ur-Ghast and tower devices

Behaviour spec of unit **U26** (`darktower`), written by F15 from the upstream 4.8 (`9e40a757`). Java paths are
relative to `src/main/java/gloamingforest/`; data paths to `src/generated/resources/`.

| Unit fact | Value |
|---|---|
| Selftests | `darktower.*`, testPos dx `2600-2699` (R12) |
| World data | `gf:darktower:*` (device counters live in BlockData; nothing global needed) |
| Net channels | `darktower.*` (optional: ur-ghast tantrum rain flag for companions) |
| Contracts | landmark, structure, boss, entity, projectile, families+blockevents, blockdata+redstone, loot, progression |
| Config | none |

**Tag namespaces.** Tags are written `#<ns>:<path>`. `#gloamingforest:` tags and the `#c:` / `#neoforge:` tags that
owners.tsv lists (`tag` rows) are upstream data (`data/<ns>/tags/**`) imported by F1; `#minecraft:` tags are vanilla Java
1.21.1 tags (upstream adds members to some). Bedrock has no Java tags: every tag here resolves through `gf::tags` (F9,
`src/feature/tags/Tags.h`), and units add members only through union fragments (R6). Upstream's damage-type and tool-tier
tags are in the `neoforge` namespace (`#neoforge:is_physical`, `#neoforge:is_magic`, `#neoforge:is_environment`,
`#neoforge:needs_wood_tool`), never `#c:`.

NeoForge-provided common tags used in this spec (defined by NeoForge 21.1's own data pack, not by upstream, so owners.tsv has
no row for them; F1 imports their members with the vanilla tags upstream references):

| Tag | NeoForge 21.1 members | Bedrock equivalent | Used by |
|---|---|---|---|
| `item/#c:ores/redstone` | vanilla `#minecraft:redstone_ores` = `minecraft:redstone_ore`, `minecraft:deepslate_redstone_ore` | items `minecraft:redstone_ore`, `minecraft:deepslate_redstone_ore`; Bedrock recipe ingredients cannot name a Java tag, so the recipe import expands it to these ids | carminite reactor recipe |
| `item/#c:dusts/redstone` | `minecraft:redstone` | `minecraft:redstone`; expanded to the item in the Bedrock recipe | reappearing block and carminite recipes |

## 1. Structure `gloamingforest:dark_tower`

Sources: `world/components/structures/type/DarkTowerStructure.java`, `world/components/structures/darktower/*.java`
(15 piece classes + `EnumDarkTowerDoor`, `StructureDecoratorDarkTower`), F7 tower bases (`bases/Tower.h`:
`TowerWingComponent`, `TowerRoof*`), `world/components/structures/GloamingMaze.java` (F7 `bases/Maze.h`).

| Field | Value |
|---|---|
| Structure type | `gloamingforest:dark_tower` (`ControlledSpawningStructure`) |
| Structure set | `gloamingforest:dark_tower`, placement `gloamingforest:landmark_grid`, weight 1 |
| Biomes | `#gloamingforest:valid_dark_tower_biomes` (dark forest centre ring) |
| Step | `surface_structures`, terrain adaptation `beard_thin` |
| Progression lock | `gloamingforest:progress_knights` |
| Hint | kobold + book `gloamingforest.book.darktower` ("Notes on a Wooden Tower", 3 pages) |
| Decoration clearance | radius 1, surface decorations false, underground true, vegetation true, adjust elevation true (y clamped to sea level + 1 .. + 7 of the surface) |
| Controlled spawns label 0 | `gloamingforest:carminite_golem` 10 1-2, `minecraft:skeleton` 10 1-2, `minecraft:creeper` 5 1-1, `minecraft:enderman` 2 1-2, `minecraft:witch` 1 1-1, `gloamingforest:carminite_ghastling` 10 1-2, `gloamingforest:carminite_broodling` 10 4-4, `gloamingforest:pinch_beetle` 10 1-1 |
| Controlled spawns label 1 (roofs) | `gloamingforest:carminite_ghastguard` 10 1-2 |
| Water spawns | `minecraft:squid` 10 4-4 (aquariums) |
| Spawn overrides | all categories empty over the structure box |
| Map icon | `gloamingforest:dark_tower` |

Decorator (`StructureDecoratorDarkTower`): block `gloamingforest:towerwood`, accent / pillar / platform
`gloamingforest:encased_towerwood`, fence `minecraft:oak_fence`, stairs `minecraft:spruce_stairs`, random blocks =
towerwood selector (towerwood, mossy / cracked towerwood, `gloamingforest:infested_towerwood` mix; F7 selector).
Structure strings: `structure.gloamingforest.dark_tower` "Dark Tower".

### 1.1 Piece types

| Id | Class | Summary |
|---|---|---|
| `gloamingforest:GloamingDTMai` | DarkTowerMainComponent | Main tower, size 19, height `56 + (rand(32) / 5) x 5` (56..86), placed at (x + 10, y, z + 10) facing NORTH; lowered in steps of 5 until max y <= 245 (logs "Lowering Dark Tower max height by N"). Depth 0: two entrance towers (NONE and CW180 sides, size 9, height `validate(10 + rand(5))`) via entrance bridges, base filled with accent blocks below y 0 of the piece. Depth < 2: one random side gets the next main tower through a main bridge with a **LOCKED** door; the other three sides get wings (size 11, height `validate(21 + rand(10))`). Depth 2: four boss-trap wings instead. Depth > 0: three more base wings (all but CW180) and a beard. Roof only below depth 2. Key towers: after building children at depth < 2, up to 4 random size-9 wings of the same depth are flagged `keyTower` (warns "Dark forest tower could not find four small towers to place keys in." if fewer). `validateChildHeight(h) = (h / 4) x 4 + 1`. postProcess: encased walls, hollow inside, `decoRNG = seed + minX x 321534781 ^ minZ x 756839`; floors of 5: beam maze (50 %, 4 centre floors) or builder platforms (centre = half the floors); three-quarter floors on the bottom and top portions decorated per floor; depth 2: boss floors, five `destroyTower` netherrack blobs at the top, `gloamingforest:ur_ghast_boss_spawner` at (9, height - 2, 9); openings last |
| `gloamingforest:GloamingDTWin` | DarkTowerWingComponent | Wing tower (sizes 9 / 11 / 15). Children: roof (rand(5): 2 cactus, 3 rings, 4 four-post, else antenna), beard; size > 10: per side a wing of `size - 2` with height `validate(height - 4 + rand(10) - rand(10))` (needs height >= 8, top <= 250, no collision), else a balcony (always on CW180, else 50 %); small wings: 1/4 chance one balcony (not entrance towers). Floors every 4 blocks (rotation alternating 180 degrees, starting rotation `(minY + bottom) % 3`): half floors decorated by rand(8): 0 reappearing floor (size < 11, else falls through to 1), 1 spawner (`minecraft:spawner` at (x + 1, y + 2, z + 1) with x, z = 4, 5 for size > 9 else 3, 4: carminite golem or carminite broodling 50/50 when size > 9, else broodling), 2 lounge, 3 library (bookshelves), 4 experiment pulser, 5 experiment lamp (redstone lamps, repeaters, sticky pistons), 6 puzzle chest (`darktower_cache` behind levers / pressure plates), 7 nothing; the top floor is a full floor and, when the wing is a dead end (at most one non-balcony opening), a treasure room chest at the centre (`gloamingforest:darktower_key` in key towers, else `gloamingforest:darktower_cache`); stairs down per floor (double width when size > 9) and stairs to the roof. Doors: `VANISHING` (default) = 3 x 3 `unbreakable_vanishing_block` in a 5 x 5 encased frame; `REAPPEARING` (balconies) = 3 x 3 `reappearing_block`; `LOCKED` = unbreakable vanishing 3 x 3 with four `locked_vanishing_block` corners. Destroyed tops use `netherTransformBlob` (netherrack, fire). Saved: `keyTower`, door coordinates and types |
| `gloamingforest:GloamingDTBri` | DarkTowerBridgeComponent | 5-long bridge leading to a wing; checks the bridge and the wing box for collisions |
| `gloamingforest:GloamingDTEB` | DarkTowerEntranceBridgeComponent | bridge to an entrance tower |
| `gloamingforest:GloamingDTEnt` | DarkTowerEntranceComponent | small entrance tower at ground level (no balconies) |
| `gloamingforest:GloamingDTMB` | DarkTowerMainBridgeComponent | bridge to the next main tower (size 15, height 56) |
| `gloamingforest:GloamingDTBB` | DarkTowerBossBridgeComponent | bridge to a boss trap tower (size 11, height 9) |
| `gloamingforest:GloamingDTBT` | DarkTowerBossTrapComponent | open-topped trap tower: destroyed rim, half floor, **`gloamingforest:ghast_trap` at (5,1,5)** wired by redstone wire (5,1,6)-(5,1,8)-(3,1,8) to an oak pressure plate at (2,1,8); stairs down |
| `gloamingforest:GloamingDTBal` | DarkTowerBalconyComponent | balcony on a wing, reached through a reappearing door |
| `gloamingforest:GloamingDTBea` | DarkTowerBeardComponent | stepped encased-towerwood underside of a wing |
| `gloamingforest:GloamingDTRooS` | DarkTowerRoofComponent | base roof slab |
| `gloamingforest:GloamingDTRA` | DarkTowerRoofAntennaComponent | antenna roof |
| `gloamingforest:GloamingDTRC` | DarkTowerRoofCactusComponent | "cactus" roof |
| `gloamingforest:GloamingDTRFP` | DarkTowerRoofFourPostComponent | four-post roof |
| `gloamingforest:GloamingDTRR` | DarkTowerRoofRingsComponent | rings roof |

Main-tower floor decorations (`decorateFloor`): top floors rand(3): botanical / netherwart / aquarium; bottom floors
rand(4): aquarium, botanical, netherwart (only when the floor's world y > 64, else falls through to forge), forge;
middle floors rand(8): 0-1 reappearing-block maze (GloamingMaze with reappearing walls and dead-end decorations), 2 unbuilder
(antibuilder) maze, 3 aquarium (water, squid spawns), 4 botanical (tree planters, berry bushes: blightberry, duskberry,
skyberry, stingberry; potted plants from `#gloamingforest:dark_tower_allowed_pots`; the planter at (6, y + 1, 12) is a
dirt block ringed by pillars and stairs with a tree on top: configured feature by `decoRNG.nextInt(5)` -> 1
`gloamingforest:tree/smaller_jungle_tree` (upstream's custom jungle tree that does not cut through the floor), 2
`minecraft:birch`, 3 `gloamingforest:tree/gloaming_oak_tree`, 4 `gloamingforest:tree/rainbow_oak`, else `minecraft:oak`;
only when the tree position is inside the chunk box, retried up to 100 times with the level random
(`world.getRandom()`), stopping at the first success), 5 netherwart (y > 64, else lounge), 6 lounge, 7 forge (furnaces,
anvils, chipped/damaged anvils, crafting tables, fire pit, dispenser pillar).
Experiment room: crafting corner with item frames showing the carminite recipe (borer essence, redstone, ghast tear,
towerwood, encased towerwood, carminite) and, below height - 13, a carminite reactor device (obsidian, netherrack,
redstone blocks, inactive `gloamingforest:carminite_reactor`). Builder platforms: `gloamingforest:carminite_builder`
with levers, lamp clusters, `minecraft:spawner` of carminite ghastling (`makeMiniGhastSpawner`: spawn range 16, max
nearby 2, spawn count 1). Timber maze: dark log beams. Chests use `gloamingforest:darktower_cache` (5 placements).
Exact translation of every piece (R19), golden hash per piece; the decorations use `decoRNG`, so hashes are stable,
except the planter trees, which grow with the level random: the golden hash seeds that random for the test (Java derives
the WorldGenRegion random from the world seed and the centre chunk) or masks the tree volume above the planter.

## 2. Tower device blocks

Sources: `block/{VanishingBlock,ReappearingBlock,LockedVanishingBlock,BuilderBlock,TranslucentBuiltBlock,AntibuilderBlock,CarminiteReactorBlock,ReactorDebrisBlock,SliderBlock,GhastTrapBlock,InfestedTowerwoodBlock}.java`,
`block/entity/{CarminiteBuilderBlockEntity,AntibuilderBlockEntity,CarminiteReactorBlockEntity,ReactorDebrisBlockEntity,GhastTrapBlockEntity}.java`,
`enums/TowerDeviceVariant.java`, `init/GloamingBlocks.java`.

| Block | Properties | States | Light | Tool / tags | Loot |
|---|---|---|---|---|---|
| `gloamingforest:reappearing_block` | map SAND, sound WOOD, strength 10 / 35, requires correct tool, push BLOCK, force solid on | `active`, `vanished` (booleans) | 4 | axe; `#gloamingforest:antibuilder_ignores` | `gloamingforest:blocks/reappearing_block` |
| `gloamingforest:vanishing_block` | map SAND, sound WOOD, strength 10 / 35, push BLOCK | `active` | 4 while active, else 0 | axe | `gloamingforest:blocks/vanishing_block` |
| `gloamingforest:unbreakable_vanishing_block` | copy of vanishing, strength -1 / 6000000, no loot | `active` | as vanishing | `#gloamingforest:common_protections`; no block item (structure only, shares the "Vanishing Block" name) | none |
| `gloamingforest:locked_vanishing_block` | map SAND, sound WOOD, strength -1 / 2000, push BLOCK | `active`, `locked` (default true) | 0 | `#gloamingforest:common_protections` | `gloamingforest:blocks/locked_vanishing_block` |
| `gloamingforest:carminite_builder` | map SAND, sound WOOD, strength 10 / 6, requires correct tool, push BLOCK | `state` = builder_inactive / builder_active / builder_timeout | 4 while active | axe; antibuilder_ignores | `gloamingforest:blocks/carminite_builder` |
| `gloamingforest:built_block` | sound WOOD, strength 50 / 2000, no loot, no occlusion (translucent), push BLOCK | `active` | 0 | antibuilder_ignores; no block item | none |
| `gloamingforest:antibuilder` | map SAND, sound WOOD, strength 10 / 6, requires correct tool, no loot, push BLOCK | none | 10 | axe; antibuilder_ignores; `#gloamingforest:structure_banned_interactions` | none (no loot table in upstream) |
| `gloamingforest:antibuilt_block` | sound WOOD, strength 0.3 / 2000, no loot, no occlusion, push BLOCK | none | 0 | no block item | none |
| `gloamingforest:carminite_reactor` | map SAND, sound WOOD, strength 10 / 6, requires correct tool, push BLOCK | `active` | 15 while active | axe; antibuilder_ignores | `gloamingforest:blocks/carminite_reactor` |
| `gloamingforest:reactor_debris` | sound ANCIENT_DEBRIS, strength 0.3 / 2000, no loot, no occlusion, push BLOCK | none | 0 | antibuilder_ignores; no block item | none |
| `gloamingforest:fake_gold` | sound METAL, strength 50 / 2000, no loot, push BLOCK; name "Gold Block" | none | 0 | `#minecraft:dragon_immune`, `#minecraft:wither_immune`; block item `gloamingforest:fake_gold` | none |
| `gloamingforest:fake_diamond` | same; name "Diamond Block" | none | 0 | same; block item `gloamingforest:fake_diamond` | none |
| `gloamingforest:slider` | map DIRT, strength 2 / 10, no loot, no occlusion, random ticks | `axis` (x/y/z, default y), `delay` 0..3, `waterlogged` | 0 | - | none |
| `gloamingforest:ghast_trap` | map SAND, sound WOOD, strength 10 / 6, requires correct tool, push BLOCK | `active` | 15 while active | axe; antibuilder_ignores | `gloamingforest:blocks/ghast_trap` |
| `gloamingforest:infested_towerwood` | full copy of `gloamingforest:towerwood` (ignited by lava, orange map colour) with instrument FLUTE, strength 2 / 6, no loot; name "Infested Towerwood Planks" | none | 0 | member of `#gloamingforest:towerwood` | none |

Block items: `gloamingforest:reappearing_block`, `gloamingforest:vanishing_block`, `gloamingforest:locked_vanishing_block`,
`gloamingforest:carminite_builder`, `gloamingforest:antibuilder`, `gloamingforest:carminite_reactor`,
`gloamingforest:ghast_trap`, `gloamingforest:slider`, `gloamingforest:fake_gold`, `gloamingforest:fake_diamond`,
`gloamingforest:infested_towerwood`. Most device blocks are also in `#c:relocation_not_supported` (F1 tag).

Block entities (upstream types): `gloamingforest:tower_builder` (carminite builder), `gloamingforest:antibuilder`,
`gloamingforest:carminite_reactor`, `gloamingforest:reactor_debris`, `gloamingforest:ghast_trap`. Bedrock: custom
blocks cannot carry block entities (S3): keep each one's state in `gf::blockdata` and drive the tickers from a U26
registry of active positions; redstone via `gf::redstone::watch` / `power` (no neighbour updates for custom blocks).

### 2.1 Vanishing family (`VanishingBlock`, `ReappearingBlock`, `LockedVanishingBlock`)

- **Locked check** (`areBlocksLocked`): flood fill over connected vanishing-family blocks (6 directions, max 512
  visited); locked if any reached `locked_vanishing_block` has `locked=true`.
- **Activation**: right-click with an empty-item interaction (`useWithoutItem`) on a non-vanished, inactive block: if
  locked play `block.gloamingforest.vanish.locked` ("Towerwood door clicks", vol 1.0, pitch 0.3), else activate;
  redstone (`hasNeighborSignal`) on a non-vanished inactive unlocked block activates. `activate(pos)`: set
  `active=true` and schedule a tick in `2 + rand(5)` ticks.
- **Tick** (server): not vanished and active: reappearing blocks go `active=false, vanished=true` and reschedule 80
  ticks; plain vanishing blocks are removed. Sound `block.gloamingforest.reappearing_block.vanish` (reappearing) or
  `block.gloamingforest.vanish.vanish` (vanishing) at vol 0.3, pitch 0.5; then activate all 6 neighbours (chain
  reaction). Vanished and not active: play `block.gloamingforest.reappearing_block.vanish` (0.3, 0.5), set
  `active=true`, reschedule 15. Vanished and active: restore (`vanished=false, active=false`), sound
  `block.gloamingforest.reappearing_block.reappear` (0.3, 0.3).
- Vanished shape: outline (6,6,6)-(10,10,10) pixels, no collision.
- Inactive blocks: explosion resistance 6000; entities may destroy them only when not locked.
- Active blocks: client redstone-ore sparkle (redstone dust on each exposed face).
- **Locked vanishing block**: right-click holding `gloamingforest:tower_key` while locked (server): consume one key,
  set `locked=false`, sound `block.gloamingforest.vanish.unlock` ("Towerwood door unlocks", 0.3, 0.6). Locked:
  explosion resistance 6000000 and no entity destruction.

### 2.2 Carminite builder and built blocks

- `BuilderBlock`: `onPlace` while powered -> active + `block.gloamingforest.builder.on` (0.3, 0.6). Neighbour change:
  inactive + powered -> active, sound on, schedule 4; active + unpowered -> inactive, `block.gloamingforest.builder.off`
  (0.3, 0.6), schedule 4; timeout + unpowered -> inactive. Scheduled tick: active and powered -> start building (if not
  already); inactive or timeout -> reset the block entity and `activateBuiltBlocks` on the 6 neighbours. Removal also
  activates neighbouring built blocks. Active: redstone sparkle client-side. Ticker only while active.
- `CarminiteBuilderBlockEntity` (server tick while making blocks): track the nearest non-spectator player within 16;
  next direction from the player's pitch `floor(pitch x 4 / 360 + 1.5) & 3`: 0 = UP, 2 = DOWN, else the player's
  horizontal facing; every 10 ticks (`ticksRunning % 10 == 0`, with a tracked player and a cursor) place
  `gloamingforest:built_block[active=false]` at the cursor + direction while `blocksMade <= 16` (upstream `<= RANGE` with
  `RANGE = 16`, so one run builds up to **17** blocks) and that position is air (`isEmptyBlock`), with
  `block.gloamingforest.builder.create` at the builder (vol 0.75, pitch 1.2), then advance the cursor and count the
  block; otherwise (17 blocks made or the position not air) the run is blocked -> stop making blocks (forget the
  player, reset the stop timer). Not making blocks: after 60 ticks set `builder_timeout` and schedule 4.
- `activateBuiltBlocks(pos)`: an inactive built block becomes active, plays `block.gloamingforest.builder.replace`
  ("Built Block expires", 0.3, 0.6) and schedules 10 ticks; its tick removes it (sound 0.3, 0.5) and activates its 6
  neighbours (chain).

### 2.3 Antibuilder (`AntibuilderBlockEntity`)

Ticks only while a living non-spectator player is within 16 blocks of its centre (else drops its snapshot): client
draws redstone dust in the block and, 1/10, three outline lines of the 9 x 9 x 9 region (one of 12 edges; 16
particles per line). Server (`tickCount` + 1 per tick while a player is in range, reset to 0 with the snapshot):

- The first tick with the area loaded (radius 4) snapshots the 9 x 9 x 9 block states around it (x, then y, then z
  from -4 to 4) and enters slow scan. A scan runs every tick in fast scan, or when `tickCount % 20 == 0` in slow scan;
  the capture tick itself also scans when its `tickCount % 20 == 0` (capture and scan run in the same tick).
- A scan compares each position's current block with the snapshot (by block, not state). For a changed position
  (`revertBlock(current, snapshot)`):
  - not revertible -> the snapshot takes the current state and the position does not count: the current state is air
    and the snapshot does not block motion (`!blocksMotion()`), or the current state is unbreakable (destroy speed
    < 0), or either state is in `#gloamingforest:antibuilder_ignores` (U2's tag: redstone lamp, TNT, water, the tower
    devices, common protections, ...);
  - revertible -> the position counts as a **detected change** whether or not it is reverted this scan, and the
    snapshot keeps its old state. It is reverted only with chance 1/10 (`REVERT_CHANCE`, the block entity's own
    random): the replacement is `gloamingforest:antibuilt_block` when the snapshot was not air, else air; when the
    current state is air, level event 2001 (`PARTICLES_DESTROY_BLOCK`: break particles and break sound) of the
    replacement plays first; then `Block.updateOrDestroy(current, replacement, flags 2)`: a replacement of air
    **destroys the placed block with drops** (`destroyBlock(pos, true)`, with its break effects), an antibuilt block
    is set with flag 2 (clients updated, no neighbour updates).
- A scan with at least one detected change switches to fast scan and resets `ticksSinceChange` to 0; a scan without
  one increments it, and once it is **more than 20** (`ticksSinceChange > 20`, the 21st consecutive change-free scan)
  slow scan resumes. A block placed during slow scan is found by the next 20-tick scan; from then on it is rolled
  every tick (about 10 scans on average until its revert), not every 20 ticks.
- Consequence of these rules (upstream behaviour, translate as is; do not "fix" it): a revert to an antibuilt block never
  makes the position match its snapshot again. The snapshot keeps the original block (towerwood, say), and
  `gloamingforest:antibuilt_block` is not in `#gloamingforest:antibuilder_ignores` (BlockTagGenerator.java:517-530),
  is breakable (destroy speed 0.3) and is not air, so `revertBlock(antibuilt, original)` returns true on **every**
  scan: the position counts as a detected change each time (its 1/10 roll still consumes a `nextInt(10)`; a won
  roll calls `updateOrDestroy(antibuilt, antibuilt)`, a no-op because the states are equal). So once any antibuilt
  block exists in the region, the antibuilder scans every tick and never returns to slow scan while a player stays
  within 16 blocks. Breaking an antibuilt block whose snapshot blocks motion changes nothing (air over a
  motion-blocking snapshot is revertible, and the next won roll refills it); breaking one whose snapshot is a non-air
  block that does not block motion (a torch, flower or carpet that was replaced by a solid block, then reverted)
  settles the position (`stateThere.isAir() && !replaceWith.blocksMotion()`: not revertible, the snapshot becomes
  air). Slow scan resumes only when every changed position is settled: reverted to air (a block placed into snapshot
  air and destroyed now matches its snapshot again) or absorbed by a non-revertible change (the snapshot takes the
  current state, e.g. an `antibuilder_ignores` block put where the antibuilt block was, or the air left by breaking an
  antibuilt block over a non-motion-blocking snapshot as above), after which 21 change-free scans switch it back.
- The snapshot is transient: upstream never saves `blockData` (no `saveAdditional`), so it is lost when every player leaves
  the 16-block range (`blockData = null`, `tickCount = 0`) and also whenever the block entity is recreated (chunk
  unload / reload, server restart). The next capture includes the antibuilt blocks, which then match, and the capture
  itself sets `slowScan = true` (as the first bullet says): slow scan is immediate after a recapture, not 21 scans
  later (`ticksSinceChange` is neither saved nor reset by the capture; it only matters again after the next detected
  change resets it). U26 keeps the snapshot in memory only (not BlockData, not the world DB), so a reload behaves the
  same way.

### 2.4 Carminite reactor (`CarminiteReactorBlock`, `CarminiteReactorBlockEntity`)

- Activates (server neighbour change) when all 6 neighbours are `minecraft:redstone_block`.
- Random secondary and tertiary burst centres at (+-3, +-3, +-3) each (tertiary mirrored if equal).
- Server, every 5 ticks while active (counter + 1 per tick): at counter 5 transform the 3 x 3 x 3 around it into
  `fake_diamond` / `fake_gold` (corners and centre column diamond on top/bottom layers, edges gold; exact table in upstream)
  unless the block is in `#gloamingforest:carminite_reactor_immune` (U26 tag = `#gloamingforest:common_protections`)
  or unbreakable. Primary burst `p = counter - 80`: for 10 <= p <= 249 draw air shells of radius `(p - 10) / 40`; for
  p <= 200 draw `reactor_debris` shells of radius `p / 40`. Secondary `s = counter - 120`: air shells for 10..129,
  nether-transform shells for 0..160; tertiary `t = counter - 160` likewise. Shell drawing (`drawBlob`): octant
  traversal with fuzz `(fuzz + d) % 8` choosing the mirror, distance `max + 0.5 mid + 0.25 min`; transform skips immune
  and unbreakable blocks, spawns break particles when fuzz == 0, nether transform: 1/8 a random block of
  `#gloamingforest:carminite_reactor_ores` (U2's tag: nether quartz ore, nether gold ore) else netherrack, and fire
  above when air and fuzz % 3 == 0.
- At counter >= 350: destroy itself, explosion power 4.0 with fire, damage type `gloamingforest:reactor`, interaction
  BLOCK; spawn 3 + 3 `gloamingforest:carminite_ghastling` near the secondary and tertiary centres (+-1.5).
- Client: every 5 ticks up to 250, `block.gloamingforest.reactor.ambient` with volume and pitch = counter / 100.
- Removal destroys any fake gold/diamond in the surrounding 3 x 3 x 3 (no drops).

### 2.5 Reactor debris (`ReactorDebrisBlock`, `ReactorDebrisBlockEntity`)

Random sub-box shape (each axis min rand(16)/16, length 1..remaining, volume >= 1/8, inset 0.008) and random textures
per face from netherrack, bedrock, nether portal, obsidian (`minecraft:block/...`); collision is a full block; no
landing particles; block entity tick: re-randomise at timeAlive 5 (or 1/5 per tick when `rerolls`), destroy at
timeAlive 60 (when `will_disappear`); saved `textures` (west/east/bottom/top/north/south), `pos`, `sizes`, `rerolls`,
`will_disappear`, `timeAlive`. A scheduled tick also destroys it. Client renderer `ReactorDebrisRenderer` draws the
box with those textures (client mod; vanilla clients see a fixed debris texture).

### 2.6 Slider (`SliderBlock`, entity `gloamingforest:slider` = `SlideBlock`)

- Rotated pillar block with `delay` 0..3; shapes 6 x 6 px columns along the axis
  ((0.3125..0.6875) on the other two axes).
- Scheduled tick every 80 ticks offset by `delay x 20` (`80 - (gameTime - delay x 20) % 80`); on tick, if a player
  is within 32 blocks of it or of any identical slider block in a straight line along its axis (recursive), spawn a
  `SlideBlock` entity at the block (x + 0.5, y, z + 0.5) with its state; always reschedule.
- `entityInside`: 5 `gloamingforest:slider` damage and knockback 2 toward the block centre for living entities.
- Entity `gloamingforest:slider` (MISC, 0.98 x 0.98, tracking 80, interval 1, no egg): move direction = first of
  (axis Y: west, east, north, south; axis X: down, up, north, south; axis Z: down, up, west, east) with air ahead and
  a non-air block behind, else the first with air ahead. Tick: after 20 warm-up ticks accelerate 0.04 per tick in
  that direction; every 5 ticks `block.gloamingforest.slider.move` ("Sliding Trap creaks", vol 1, pitch 0.9 + r x 0.4);
  at tick 1 remove the source block (discard if it changed); at tick 60 stop and reverse; on any collision: motion x
  0.7, discard and re-place the block if unobstructed, else drop it as an item; after 100 ticks outside the build
  height or after 600 ticks: drop as an item. Damages living entities in its box 5 (`slider`) with knockback 2.
  Saved `Time`, `Direction` (3D data value), `BlockState`. Renderer spins the block 60 degrees per tick around its
  axis (FallingBlockRenderer copy). Name "Moving Slide Trap".

### 2.7 Ghast trap (`GhastTrapBlock`, `GhastTrapBlockEntity`, POI `gloamingforest:ghast_trap`)

- POI `gloamingforest:ghast_trap`: all states of the block, max tickets 0, valid range 1 (used by the Ur-Ghast to
  find traps; Bedrock: U26 keeps its own index of ghast trap positions per dimension in BlockData / world data).
- Inactive tick: every dying (deathTime > 0) `carminite_ghastling` within the trap's block box inflated (10, 16, 10)
  (`new AABB(pos).inflate(10, 16, 10)`: x - 10 .. x + 11, y - 16 .. y + 17, z - 10 .. z + 11) is added to
  the charge list (and gets 5 `gloamingforest:ghast_trap` particles from the trap top to its eye); charge level =
  min(3, count). Client effects: every 20 ticks a particle line to a random nearby ghastling; level >= 1 every 10
  ticks sparkle + `block.gloamingforest.ghast_trap.ambient` (vol 1, pitch 1); level >= 2 smoke and the ambient sound
  at (1.2, 0.8); level 3 large smoke, sparkle, ambient every 5 ticks at (1.5, 2.0).
- Activation (server neighbour change): inactive, charged (3 dying ghastlings recorded) and powered: every server
  player within 6 gets trigger `gloamingforest:activate_ghast_trap`; set `active`, sound `block.gloamingforest.jet.start`
  (U24's event, 0.3, 0.6), block event 0 (clears counter and charge list).
- Active tick: counter + 1. Server: every `Ghast` (vanilla ghasts, ghastguards, ghastlings) or Ur-Ghast in the box
  `new AABB(pos.above(16).getCenter(), pos.above(16).offset(1, 1, 1).getCenter()).inflate(6, 16, 6)`, i.e. the unit
  box min (x + 0.5, y + 16.5, z + 0.5), max (x + 1.5, y + 17.5, z + 1.5) inflated (6, 16, 6): x in [x - 5.5, x + 7.5],
  y in [y + 0.5, y + 33.5], z in [z - 5.5, z + 7.5] (offset half a block towards +x / +z, not centred on the trap
  column), is pulled with motion `(trap + (0.5, 1.5 or 2.5, 0.5) -
  pos) x 0.1`; the Ur-Ghast additionally loses tantrum, gets 20 trapped ticks, noPhysics, and with chance 1/10 takes
  7 generic damage and resets its phase damage; other ghasts take 10 generic damage with chance 1/10. At counter 120:
  deactivate and block event 1. Client: before 100 ticks six ghast-trap particles spiralling upward (dx = cos(c/10) x
  2.5, dy 20 / 10 / 5); after 100, every 4 ticks a huge smoke; sounds: warm-up (`block.gloamingforest.ghast_trap.warmup`)
  before 30, on (`block.gloamingforest.ghast_trap.on`) before 80, spin-down (`block.gloamingforest.ghast_trap.spindown`)
  after, all vol 1, pitch 4.

### 2.8 Infested towerwood

`spawnAfterBreak`: if block drops are on and the tool lacks an enchantment in `#minecraft:prevents_infested_spawns`
(silk touch), spawn a `gloamingforest:towerwood_borer` at the block centre with the spawn animation.

## 3. Items

| Id | Properties | Notes |
|---|---|---|
| `gloamingforest:tower_key` | fire resistant, rarity UNCOMMON | unlocks a locked vanishing block (consumed) |
| `gloamingforest:borer_essence` | plain | dropped by towerwood borers; carminite ingredient |
| `gloamingforest:carminite` | plain; member of `#c:gems/carminite` (F1 tag) | crafting material |

## 4. Recipes and recipe advancements

| Recipe | Definition |
|---|---|
| `gloamingforest:carminite_builder` | shaped (redstone): `#6#` / `6o6` / `#6#`, `#` encased towerwood, `6` `#c:gems/carminite`, `o` `minecraft:dispenser` -> 1 |
| `gloamingforest:carminite_reactor` | shaped (redstone): `#6#` / `6%6` / `#6#`, `%` `#c:ores/redstone` -> 1 |
| `gloamingforest:reappearing_block` | shaped (redstone): `#∴#` / `∴6∴` / `#∴#`, `∴` `#c:dusts/redstone` -> 2 |
| `gloamingforest:vanishing_block` | shaped (redstone): `#w#` / `w6w` / `#w#`, `w` `#gloamingforest:towerwood` -> 8 |
| `gloamingforest:material/carminite` | shapeless (misc): 4 `gloamingforest:borer_essence`, 4 `#c:dusts/redstone`, 1 `minecraft:ghast_tear` -> 1 carminite |
| `gloamingforest:compressed_blocks/reversed/carminite_block` | shapeless: `gloamingforest:carminite_block` -> 9 carminite |

Unlock advancements (F1-generated): `gloamingforest:recipes/redstone/carminite_builder` and
`gloamingforest:recipes/redstone/carminite_reactor` (has `#c:gems/carminite`), `gloamingforest:recipes/redstone/reappearing_block`
and `gloamingforest:recipes/redstone/vanishing_block` (has `gloamingforest:reappearing_block`),
`gloamingforest:recipes/misc/material/carminite` (has `gloamingforest:borer_essence`),
`gloamingforest:recipes/misc/compressed_blocks/reversed/carminite_block` (has `gloamingforest:carminite_block`).

## 5. Boss `gloamingforest:ur_ghast` (UrGhast extends BaseTFBoss)

Sources: `entity/boss/UrGhast.java`, `entity/ai/goal/UrGhast{Attack,Flight,Look}Goal.java`, `entity/projectile/UrGhastFireball.java`,
`block/entity/spawner/UrGhastSpawnerBlockEntity.java`, `client/renderer/entity/UrGhastRenderer.java`, `client/model/entity/UrGhastModel.java`,
`client/particle/GhastTearParticle.java`, `client/renderer/GloamingWeatherRenderer.java`.

| Field | Value |
|---|---|
| Size | 14.0 x 18.0, fire immune |
| Attributes | `CarminiteGhastguard.registerAttributes()` (+ vanilla ghast base) then max_health 250, follow_range 128, knockback_resistance 1.0 |
| XP | 317 |
| Physics | noPhysics true at spawn (cleared once the flight goal reaches its first point), NoClipMoveControl, FlyingMob travel copy, no fall damage, never climbs, not pushable, no knockback |
| Invulnerable to | in-wall and `#minecraft:is_fire` damage, unless the damage is a **player-reflected large fireball** |
| Synced | `ATTACK_STATUS` (0 idle, 1 tracking), `ATTACK_TIMER`, `ATTACK_PREVTIMER`, `DATA_IS_CHARGING`, `DATA_TANTRUM` |
| Saved | `inTantrum` + BaseTFBoss data |
| Boss bar | colour 0xFF0000 |
| Home | spawner position, radius 64, structure `gloamingforest:dark_tower` |
| Death chest | `gloamingforest:dark_chest`; spawner block `gloamingforest:ur_ghast_boss_spawner` |
| Sound | volume 16.0, voice pitch `(r1 - r2) x 0.2 + 0.5`; ambient `entity.gloamingforest.ur_ghast.ambient`, hurt `entity.gloamingforest.ur_ghast.hurt`, death `entity.gloamingforest.ur_ghast.death` |
| Spawn egg | `gloamingforest:ur_ghast_spawn_egg` 0xbcbcbc / 0xb77878; placement ON_GROUND `Monster::checkMonsterSpawnRules` |

Goals: 5 UrGhastFlight (MOVE); 7 UrGhastLook (ghast look copy, every tick); 7 UrGhastAttack. Target: 1 NearestAttackable
Player (must see).

- **Traps**: on the first tick and every 100 ticks scan POIs `gloamingforest:ghast_trap` within 64 of the home (or its
  position) whose block above sees the sky, sorted by distance, add new ones; every 60 ticks drop entries that are no
  longer ghast traps or lost sky access.
- **Flight**: path = shuffled trap list, or (no traps) four points +-20 on x/z around the scan point shuffled plus the
  centre last; usable when the wanted position is reached (< 1) or far (> 60) or unset; moves to the next point + 20 y
  (when it has a home) at speed 1, noPhysics off; after the last point, restart and, if no trap has at least 4
  ghastlings within (8, 16, 8), spawn ghasts at traps.
- **Attack** (with a target, not in tantrum): within 64 and visible: timer + 1, look at the target; at 10
  `entity.gloamingforest.ur_ghast.warn` (vol 10); at 20 `entity.gloamingforest.ur_ghast.shoot` (vol 10) and three
  `UrGhastFireball`s (power 1): one aimed at the target's centre spawned 8.5 blocks along the view vector at mid
  height + 2, two with x/z offsets `(r1 - r2) x 8` at mid height; timer -40. Out of sight the timer decays. Charging
  while timer > 10.
- **Phases**: `damageUntilNextPhase` starts 10, resets to 18. After `super.hurt` (server), a hit subtracts the health
  actually lost (`oldHealth - health`) only when it started a new hurt animation (`hurtTime == hurtDuration`, i.e. not
  an invulnerability-window top-up: a stronger hit during the i-frames still lowers health but does not count toward
  the switch; a second hit in the same tick as a fresh one does count), the ghast is not dying and not trapped
  (`inTrapCounter <= 0`); at <= 0 the phase toggles. In tantrum all damage is divided by 10 before `super.hurt`.
  - **Start tantrum**: visual-only lightning near a point 18 blocks away at a random angle; `spawnGhastsAtTraps`: at 2
  random traps, a visual lightning at trap + 4 y and up to 6 attempts to spawn a `carminite_ghastling` minion
  (`makeBossMinion`: wander 0.005, max health 6) at trap + ((r1 - r2) x 4, r x 8, (r1 - r2) x 4) when its spawn rules
  pass (MOB_SUMMONED).
  - **In tantrum** (server): no target; cry `entity.gloamingforest.ur_ghast.tantrum` every `20 + rand(30)` ticks
  (resets the ambient timer); every 10 ticks players under it (its box moved down 16 and stretched 16) who can see the
  sky take 3 `gloamingforest:ghast_tear` damage and ghastlings there are pushed up 1.0. Client: `gloamingforest:boss_tear`
  particles under it, and the weather renderer fades rain in (+0.1 per tick to 1.0) while any tantruming Ur-Ghast is
  alive (fades out -0.02).
- Absorbs any ghastling touching its box (+1): the ghastling poofs and is discarded, Ur-Ghast heals 2.
- Trapped (`setInTrap`: 20 ticks): target cleared every tick.
- Death: visual lightning at mid height; deathTime counts to 90 (`isDeathAnimationFinished`), descends 0.03 per tick;
  client: first 30 ticks 12 particles (poof / explosion / redstone dust), then a 40-particle redstone trail to the
  chest location; the renderer hides it after 30 death ticks; no death flip; no poof.

Boss spawner (R20, `UrGhastSpawnerBlockEntity`): `boss::registerSpawner({blockId: gloamingforest:ur_ghast_boss_spawner,
entityId: gloamingforest:ur_ghast, range: 9, particle: minecraft:dust (redstone)})` with the in-range check "nearest
player within 9 (spectators excluded) whose y > spawner y - 4".

Render: `UrGhastModel` (ghast body with 9 tentacles), shadow 8.0, scale 24 with the attack squash
`v = 1 / ((timer / 20)^5 x 2 + 1)`, `y = (24 + v) / 2`, `xz = (24 + 1 / v) / 2`; textures `towerboss.png`,
`towerboss_openeyes.png` (status 1), `towerboss_fire.png` (charging, dying or status 2).

Loot `gloamingforest:entities/ur_ghast`: pool 1 (4 rolls) `gloamingforest:carminite` 1-3 + looting 0..1 +
multiplayer 1..3 per extra player; pool 2 (2 rolls) `gloamingforest:fiery_tears` 1-5 + looting 0..1 + multiplayer
1..2; pool 3 `gloamingforest:ur_ghast_trophy`.

### 5.1 Ur-Ghast fireball (`UrGhastFireball`, vanilla `minecraft:fireball` entity type)

LargeFireball subclass (no new entity id): hitting an entity (server, not another hurting projectile) deals 16
`fireball` damage (vanilla + 10), post-attack enchantment effects, then an explosion of power 1 (fire if mob griefing,
interaction NONE: no block damage) and discard; hitting a block also explodes the same way. `shoot` normalises,
adds gaussian `0.0075 x dist`, scales. Reflectable like vanilla fireballs (a player hit re-aims it); a player-reflected
fireball can damage the Ur-Ghast. Bedrock: spawn a vanilla `minecraft:fireball` actor and handle its impact in the F8
projectile hook (`gf::projectile`), keeping the 16 damage.

## 6. Ghasts and tower mobs

### 6.1 `gloamingforest:carminite_ghastguard` (CarminiteGhastguard extends vanilla Ghast)

| Field | Value |
|---|---|
| Size | 4.0 x 6.0, fire immune |
| Attributes | `Ghast.createAttributes()` + max_health 30.0, follow_range 64.0 |
| Synced | `ATTACK_STATUS`, `ATTACK_TIMER`, `ATTACK_PREVTIMER`, optional home GlobalPos |
| Home | radius 64 and y between min build height + 64 and max - 64 (cylinder); saved `HomePos` |
| Spawn cluster | 8 |
| Sounds | volume 0.5, ambient interval 160; ambient `entity.gloamingforest.carminite_ghastguard.ambient`, hurt `.hurt`, death `.death`, warn `entity.gloamingforest.carminite_ghastguard.warn`, shoot `entity.gloamingforest.carminite_ghastguard.shoot` (full ids `entity.gloamingforest.carminite_ghastguard.hurt`, `entity.gloamingforest.carminite_ghastguard.death`) |
| Spawn | `ghastSpawnHandler`: not Peaceful and `checkMobSpawnRules`; spawn obstruction: unobstructed and no liquid |
| Spawn egg | `gloamingforest:carminite_ghastguard_spawn_egg` 0xbcbcbc / 0xb77878 |

Goals (replaces vanilla): 5 GhastguardHomedFlight (outside home: fly toward home: random point +-wander (16) or, when
farther than 16 blocks from home, a point along the home direction scaled by wander plus jitter, else to the home
block); 5 GhastguardRandomFly (no target: random point +-16 when the wanted point is < 1 or > 60 away); 7
`Ghast.GhastLookGoal`; 7 GhastguardAttack. Target: 1 NearestAttackable Player (must see).

GhastguardAttackGoal (vanilla shoot copy): with target and `shouldAttack`; within 64 and visible: timer + 1, look at
target (10, 500); at 10 warn sound (vol 10, voice pitch); at 20 shoot sound (vol 10) and a vanilla `LargeFireball`
(explosion power = the ghast's ExplosionPower, default 1) from 4 blocks along the view at mid height + 0.5 toward the
target centre, then 1/6 chance to drop the target; timer -40. Charging (vanilla flag) while timer > 10. Client:
redstone dust particle 1/2 per tick. Vanilla ghast traits kept: reflected player fireballs kill it (vanilla
`Ghast.hurt` deals 1000), flying mob.

Render: `CarminiteGhastRenderer` (GloamingGhastModel, 9 tentacles), scale 8 with the attack squash (as the Ur-Ghast with 8),
shadow 3.0; textures `towerghast.png`, `towerghast_openeyes.png`, `towerghast_fire.png`.

Loot `gloamingforest:entities/carminite_ghastguard`: references `minecraft:entities/ghast`.

### 6.2 `gloamingforest:carminite_ghastling` (CarminiteGhastling extends CarminiteGhastguard)

| Field | Value |
|---|---|
| Size | 1.1 x 1.5, eye height 0.5, fire immune |
| Attributes | ghastguard + max_health 10, follow_range 16 |
| Wander | 4 (minion: 0.005 and max health 6) |
| Saved | `isMinion` |
| Spawn cluster | 16 |
| Attack condition | like an enderman stare: never with a `minecraft:pumpkin` helmet; within 3.5 blocks if the target can see it; otherwise only if the target looks at it (`dot > 1 - 0.025 / distance`) and can see it |
| Ignores block triggers | true (cannot trip pressure plates, so it never fires a ghast trap itself) |
| Sounds | `entity.gloamingforest.carminite_ghastling.ambient`, `entity.gloamingforest.carminite_ghastling.hurt`, `entity.gloamingforest.carminite_ghastling.death`, `entity.gloamingforest.carminite_ghastling.warn`, `entity.gloamingforest.carminite_ghastling.shoot` |
| Spawn | `canSpawnHere`: not Peaceful, (MOB_SUMMONED or dark enough), `checkMobSpawnRules` |
| Spawn egg | `gloamingforest:carminite_ghastling_spawn_egg` 0xbcbcbc / 0xa74343 |

Render `GloamingGhastRenderer` (no scale), shadow 0.625. Loot `gloamingforest:entities/carminite_ghastling`: 1 pool with
condition `gloamingforest:is_minion` inverted (F10 loot condition) referencing `minecraft:entities/ghast` (boss
minions drop nothing).

### 6.3 `gloamingforest:carminite_golem` (CarminiteGolem)

| Field | Value |
|---|---|
| Size | 1.4 x 2.9 |
| Attributes | max_health 40.0, movement_speed 0.25, attack_damage 9.0, armor 2.0 |
| Pathfinding | water malus -1 (avoids water) |
| Spawn cluster | 2 |
| Spawn egg | `gloamingforest:carminite_golem_spawn_egg` 0x6b3d20 / 0xe2ddda |

Goals: 1 MeleeAttack 1.0; 2 WaterAvoidingRandomStroll (1.0, probability 0.0); 3 LookAtPlayer 6; 3 RandomLookAround.
Targets: 1 HurtBy; 2 Player. On a successful hit: attack timer 10, entity event 4 (client: timer 10 and
`entity.gloamingforest.carminite_golem.attack` vol 1), target pushed up 0.4. Red dust particles 1/2 per tick; sprint
particles 1/5 while moving. Sounds: hurt `entity.gloamingforest.carminite_golem.hurt` ("Carminite Golem hurts"), death
`entity.gloamingforest.carminite_golem.death`, step `entity.gloamingforest.carminite_golem.step` (vol 1.0); no ambient.
Render: iron-golem-style sway (`6.5 x ((|f % 13 - 6.5| - 3.25) / 3.25)` degrees on Z), `carminitegolem.png`, shadow
0.75; arms swing by the attack timer.

Loot `gloamingforest:entities/carminite_golem`: pool 1 `minecraft:iron_ingot` 0..2 + looting 0..1 (entry functions);
pool 2 `gloamingforest:towerwood` 0..2 + looting 0..1 (pool-level `set_count` uniform 0..2 and
`enchanted_count_increase` looting 0..1, which apply to the towerwood entry: not a fixed 1).

### 6.4 `gloamingforest:carminite_broodling` (TowerBroodling extends SwarmSpider)

Size 0.7 x 0.5; attributes: swarm spider + max_health 7, attack_damage 4 (speed 0.3); XP 3; reinforcement type itself;
**no skeleton-druid jockey** (`summonJockey` empty); inherits the swarm spider's 1/4 hit chance, light-independent
melee/targeting, cluster size 6. Sounds: `entity.gloamingforest.carminite_broodling.ambient`,
`entity.gloamingforest.carminite_broodling.hurt`, `entity.gloamingforest.carminite_broodling.death`,
`entity.gloamingforest.carminite_broodling.step` (vol 0.15). Render `GloamingSpiderRenderer` `towerbroodling.png`, shadow 0.6,
scale 0.7. Spawn: monster rules. Egg `gloamingforest:carminite_broodling_spawn_egg` 0x343c14 / 0xbaee02. Loot
`gloamingforest:entities/carminite_broodling`: references `minecraft:entities/spider`.

### 6.5 `gloamingforest:towerwood_borer` (TowerwoodBorer, silverfish analogue)

| Field | Value |
|---|---|
| Size | 0.4 x 0.3 |
| Attributes | max_health 15.0, movement_speed 0.27, attack_damage 5.0, follow_range 8.0 |
| Movement emission | EVENTS only (no step sounds from vanilla; plays its own step at 0.15) |
| Spawn egg | `gloamingforest:towerwood_borer_spawn_egg` 0x5d2b21 / 0xaca03a |

Goals: 1 Float; 1 ClimbOnTopOfPowderSnow; 3 SummonBorers; 4 MeleeAttack 1.0; 5 HideInTowerwood. Targets: 1 HurtBy
(alert others); 2 NearestAttackable Player (must see).

- Hurt by an entity or `#minecraft:always_triggers_silverfish`: SummonBorers starts a 20-tick countdown; then scan the
  box (y +-5, x/z +-10, alternating outward order) for `infested_towerwood`: with mob griefing destroy it with drops
  (a borer pops out) else revert it to towerwood; 50 % chance to stop after each.
- HideInTowerwood (silverfish merge copy): no target and idle, 1/10 and mob griefing: pick a random direction; if the
  block there (from y + 0.5) is `gloamingforest:towerwood`, with chance 1/5 turn it into infested towerwood and vanish
  (spawn animation); else random stroll (speed 1.0, interval 10).
- Body yaw follows head yaw every tick. Render: vanilla silverfish model, `towertermite.png`, shadow 0.3.

Sounds: `entity.gloamingforest.towerwood_borer.ambient`, `entity.gloamingforest.towerwood_borer.hurt`,
`entity.gloamingforest.towerwood_borer.death`, `entity.gloamingforest.towerwood_borer.step`. Loot
`gloamingforest:entities/towerwood_borer`: `gloamingforest:borer_essence` 0..2 + looting 0..1.

## 7. Chest loot tables

- `gloamingforest:darktower_cache`: pool 1 (4 rolls): `gloamingforest:useless` w25; w75 each: `minecraft:stick` 1-12,
  `minecraft:charcoal` 1-12, `minecraft:arrow` 1-12, `gloamingforest:experiment_115` 1-9, `minecraft:red_wool`,
  `minecraft:redstone` 1-6; pool 2 (2 rolls): `minecraft:redstone_lamp` 1-4, `minecraft:iron_ingot` 1-6,
  `gloamingforest:ironwood_ingot` 1-8, `gloamingforest:firefly` 1-5, `gloamingforest:charm_of_keeping_1`; pool 3 (1):
  `gloamingforest:steeleaf_ingot` 1-8, `minecraft:diamond` 1-2.
- `gloamingforest:darktower_key` (key towers): pool 1 (4): useless w25; w75: iron ingot 1-4, gunpowder 1-4, ironwood
  ingot 1-4, firefly 1-5, experiment_115 1-12, glowstone dust 1-12, redstone 1-6; pool 2 (2): steeleaf ingot 1-6,
  charm of keeping 1, steeleaf helmet / chestplate / leggings / boots / pickaxe, ironwood chestplate, ironwood sword;
  pool 3 (1): `gloamingforest:charm_of_life_1`, enchanted books feather falling 3, knockback 2, efficiency 3; pool 4
  (1): **`gloamingforest:tower_key`**.
- `gloamingforest:darktower_boss`: pool 1 (4): carminite 1-3; pool 2 (2): fiery tears 1-5; pool 3: ur-ghast trophy.
  Registered in upstream's chest loot data but not placed by any 4.8 code (the boss drops through its entity table); keep it
  as data.

## 8. Damage types (owned by U26)

| Id | message_id (en_us) | Exhaustion | Tags |
|---|---|---|---|
| `gloamingforest:ghast_tear` | `gloamingforest.ghastTear` ("%1$s was scalded by fiery tears") | 0.0 | `#minecraft:bypasses_armor`, `#minecraft:damages_helmet`, `#neoforge:is_physical`, `#minecraft:avoids_guardian_thorns`, `#minecraft:no_knockback` |
| `gloamingforest:reactor` | `gloamingforest.reactor` ("%1$s stood too close to a Carminite Reactor") | 0.1 | `#neoforge:is_environment`, `#neoforge:is_magic`, `#minecraft:no_knockback` |
| `gloamingforest:slider` | `gloamingforest.slider` ("%1$s was sliced up by a Sliding Trap") | 0.1 | `#neoforge:is_environment`, `#neoforge:is_physical` |

All effects hurt, scaling `when_caused_by_living_non_player`.

## 9. Particles (owned by U26)

| Id | Behaviour |
|---|---|
| `gloamingforest:boss_tear` | special factory (no sprite JSON): sprite = the `minecraft:ghast_tear` item icon, size 2.0, gravity 0.6, lifetime 60 + rand(40), physics; on ground: 50 % play `entity.gloamingforest.tear.break` ("Tear shatters", vol 0.5, pitch 1.65) and burst 50 ghast-tear item particles, then removed |
| `gloamingforest:ghast_trap` | `GhastTrapParticle`: sprites `minecraft:generic_7..0` by age; red (r 1, g/b 0.8-1.0 x brightness 0..0.4), size x 0.75 x 3, lifetime `10 / (rand x 0.8 + 0.2)`; spawned at origin + velocity and moves linearly back toward the origin |

## 10. Tags (owned by U26)

| Tag | Values | Consumer |
|---|---|---|
| `block/#gloamingforest:carminite_reactor_immune` | `#gloamingforest:common_protections` | reactor transforms |
| `block/#gloamingforest:dark_tower_allowed_pots` | upstream potted saplings/plants (gloaming oak, canopy, mangrove, darkwood, rainbow oak saplings, mayapple, fiddlehead, mushgloom) and vanilla flower pots and potted plants (full list in upstream data) | botanical room random plants |
| `painting_variant/#gloamingforest:tower_paintings` | vanilla paintings (kebab ... tides, incl. earth/wind/water/fire, unpacked) | data only in 4.8 (no code reads it) |
| `painting_variant/#gloamingforest:tower_boss_paintings` | vanilla paintings without the four elements and unpacked | data only in 4.8 |

## 11. Trigger

`gloamingforest:activate_ghast_trap` (SimpleAdvancementTrigger): fired for every server player within 6 blocks when a
charged ghast trap is powered; consumed by U14's advancement (`gf::progression::trigger`, R21).

## 12. Sound events (owned by U26)

| Event | Subtitle | Event | Subtitle |
|---|---|---|---|
| `block.gloamingforest.builder.create` | Carminite Builder creates block | `entity.gloamingforest.carminite_ghastguard.ambient` | Carminite Ghastguard cries |
| `block.gloamingforest.builder.off` | Carminite Builder deactivates | `entity.gloamingforest.carminite_ghastguard.hurt` | Carminite Ghastguard screams |
| `block.gloamingforest.builder.on` | Carminite Builder activates | `entity.gloamingforest.carminite_ghastguard.death` | Carminite Ghastguard dies |
| `block.gloamingforest.builder.replace` | Built Block expires | `entity.gloamingforest.carminite_ghastguard.shoot` | Carminite Ghastguard shoots |
| `block.gloamingforest.ghast_trap.ambient` | Ghast Trap dings | `entity.gloamingforest.carminite_ghastguard.warn` | Carminite Ghastguard shoots |
| `block.gloamingforest.ghast_trap.on` | Ghast Trap buzzes | `entity.gloamingforest.carminite_ghastling.ambient` | Carminite Ghastling cries |
| `block.gloamingforest.ghast_trap.spindown` | Ghast Trap shuts off | `entity.gloamingforest.carminite_ghastling.hurt` | Carminite Ghastling screams |
| `block.gloamingforest.ghast_trap.warmup` | Ghast Trap warms up | `entity.gloamingforest.carminite_ghastling.death` | Carminite Ghastling dies |
| `block.gloamingforest.reactor.ambient` | Carminite Reactor whooshes | `entity.gloamingforest.carminite_ghastling.shoot` | Carminite Ghastling shoots |
| `block.gloamingforest.reappearing_block.reappear` | Reappearing Block reappears | `entity.gloamingforest.carminite_ghastling.warn` | Carminite Ghastling shoots |
| `block.gloamingforest.reappearing_block.vanish` | Reappearing Block disappears | `entity.gloamingforest.carminite_golem.attack` | Carminite Golem swings |
| `block.gloamingforest.slider.move` | Sliding Trap creaks | `entity.gloamingforest.carminite_golem.hurt` | Carminite Golem hurts |
| `block.gloamingforest.vanish.locked` | Towerwood door clicks | `entity.gloamingforest.carminite_golem.death` | Carminite Golem dies |
| `block.gloamingforest.vanish.unlock` | Towerwood door unlocks | `entity.gloamingforest.carminite_golem.step` | footsteps |
| `block.gloamingforest.vanish.vanish` | Vanishing Block vanishes | `entity.gloamingforest.tear.break` | Tear shatters |
| `entity.gloamingforest.carminite_broodling.ambient` | Carminite Broodling hisses | `entity.gloamingforest.towerwood_borer.ambient` | Towerwood Borer hisses |
| `entity.gloamingforest.carminite_broodling.hurt` | Carminite Broodling hurts | `entity.gloamingforest.towerwood_borer.hurt` | Towerwood Borer hurts |
| `entity.gloamingforest.carminite_broodling.death` | Carminite Broodling dies | `entity.gloamingforest.towerwood_borer.death` | Towerwood Borer dies |
| `entity.gloamingforest.carminite_broodling.step` | footsteps | `entity.gloamingforest.towerwood_borer.step` | footsteps |
| `entity.gloamingforest.ur_ghast.ambient` | Ur-Ghast cries | `entity.gloamingforest.ur_ghast.shoot` | Ur-Ghast shoots |
| `entity.gloamingforest.ur_ghast.hurt` | Ur-Ghast screams | `entity.gloamingforest.ur_ghast.tantrum` | Ur-Ghast wails |
| `entity.gloamingforest.ur_ghast.death` | Ur-Ghast dies | `entity.gloamingforest.ur_ghast.warn` | Ur-Ghast shoots |

(`block.gloamingforest.ghast_trap.active` is commented out in upstream and not registered.)

## 13. Spawn eggs and placements

Eggs: `gloamingforest:ur_ghast_spawn_egg`, `gloamingforest:carminite_ghastguard_spawn_egg`,
`gloamingforest:carminite_ghastling_spawn_egg`, `gloamingforest:carminite_golem_spawn_egg`,
`gloamingforest:carminite_broodling_spawn_egg`, `gloamingforest:towerwood_borer_spawn_egg`. Placements ON_GROUND:
ghastguard `ghastSpawnHandler`, ghastling `canSpawnHere`, golem / broodling / borer / ur-ghast monster rules.

## 14. Bedrock adaptation notes

- Ghasts: vanilla Bedrock ghast behaviour cannot be inherited; build custom flying actors with the F8 move control,
  vanilla `minecraft:fireball` projectiles (`gf::projectile::shoot` or the vanilla shooter component), and the
  reflected-fireball instant kill in the F10 incoming-damage hook.
- Ur-Ghast tantrum rain is client-mod only (F12 weather provider); vanilla clients get no rain (best effort: a
  temporary `minecraft:rain` weather particle emitter is acceptable only if A6 approves).
- Reactor shells and slider movement edit blocks from server ticks; batch updates per tick to stay in budget.
- POI: Bedrock has no POI manager for custom blocks; keep a per-dimension index of ghast trap positions (U26 BlockData
  or `gf:darktower:traps:<dim>`), updated on place / break / structure generation.

## 15. Acceptance (selftests `darktower.*`, dx 2600-2699)

Per the unit record: golden hash of the dark tower (seeded); devices: vanishing chain reaction, reappearing cycle (2-6
ticks, 80 vanished, 15 warning), locked door refuses (click sound) until a tower key unlocks it, carminite builder
builds up to 17 blocks (`blocksMade <= 16`) along the player's look, stops at the 18th, and they expire in a chain after
deactivation, antibuilder (within its 9 x 9 x 9 region) refills a broken motion-blocking block with an antibuilt block
and destroys a block placed into snapshot air with its drop (fast scan after the first detected change; slow scan again
after 21 change-free scans only when every change was reverted to air or was not revertible: once an antibuilt block
exists in the region the antibuilder scans every tick while a player is within 16 blocks, and a selftest must expect
that; a fresh capture after all players left range or a reload absorbs the antibuilt blocks and starts in slow scan at
once), reactor turns its shell into fake gold/diamond and explodes at tick 350 with 6 ghastlings, slider spawns a moving
slide block that damages 5 and returns; ghast trap charges from 3 dying ghastlings and, powered, pulls a scripted
Ur-Ghast down (tantrum off, 7 damage chance) and fires `activate_ghast_trap`; Ur-Ghast phases toggle after 10/18 damage,
tantrum divides damage by 10 and spawns minions at traps; death places the dark chest.
