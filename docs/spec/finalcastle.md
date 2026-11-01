# finalcastle (U30): Final castle and thornlands

Behaviour spec of unit **U30** (`finalcastle`), written by F15 from The Twilight Forest 4.8 (`9e40a757`). Java paths are
relative to `src/main/java/twilightforest/`; data paths to `src/generated/resources/`.

| Unit fact | Value |
|---|---|
| Selftests | `finalcastle.*`, testPos dx `3000-3099` (R12) |
| World data | `gf:finalcastle:*` (cinder furnace inventories and timers live in BlockData / F10 inventory; reserved for a per-world "WIP message sent" set if needed) |
| Net channels | `finalcastle.*` (optional: force-field corner connections for companions; computed client side otherwise) |
| Contracts | landmark, structure, template, features, families+blockevents, entity, boss (+ loot, inventory, combat, progression, fx by string) |
| Config | none |

Unit record note: the content line lists "+ 4 spawn eggs", but TF 4.8 registers **no** eggs for these mobs
(`TFEntities.build` skips the egg when both colours are 0; adherent, harbinger cube, roving cube and plateau boss all use
0 / 0). owners.tsv agrees (no `spawn_egg` rows for U30); the lang keys `item.twilightforest.adherent_spawn_egg` /
`harbinger_cube_spawn_egg` are leftovers and stay unused.

## 1. Structure `twilightforest:final_castle`

Sources: `world/components/structures/type/FinalCastleStructure.java`, `world/components/structures/finalcastle/*.java`
(30 classes + `StructureTFDecoratorCastle`), F7 tower base (`bases/Tower.h`: `TowerWingComponent`, glyph branches),
`world/components/structures/util/StructureTemplateDefinitions.java` (template pools), `events/EntityEvents.java`.

| Field | Value |
|---|---|
| Structure type | `twilightforest:final_castle` (`FinalCastleStructure extends ControlledSpawningStructure`) |
| Structure set | `twilightforest:final_castle`, placement `twilightforest:landmark_grid` (`forceStructureForCenters`), weight 1 |
| Biomes | `#twilightforest:valid_final_castle_biomes` = `twilightforest:final_plateau` (U1 tag); legacy table maps the final plateau centre to the castle (F6) |
| Step | `surface_structures`, terrain adaptation **`beard_box`** (dungeon rooms override to `bury`) |
| Progression lock | `twilightforest:progress_troll` |
| Restrictions (U14 data) | `restrictions/twilightforest:final_plateau` (acid rain, multiplier 1.5) and `restrictions/twilightforest:thornlands` (acid rain, 1.0), both: structure final_castle, hint item `twilightforest:lamp_of_cinders`, advancement `progress_troll` |
| Hint | `twilightforest:kobold` with the default book `twilightforest.book.unknown` ("Notes on the Unexplained", 2 pages; F6 keys) |
| Decoration clearance | chunk clearance radius 4, surface decorations false, underground true, vegetation true, adjust elevation false |
| Centred in chunk | true |
| Controlled spawns | labels 0 (plain parts), 1 (keep), 2 (dungeons), 3 (forge) are all **empty** in 4.8 (TODO in TF: kobold / adherent / harbinger cube / enderman / armored giant / blaze lists commented out); ambient and water lists empty |
| Spawn overrides | every category empty over the structure box |
| Map icon | `twilightforest:final_castle` (section 8) |
| Structure tag | `#twilightforest:landmark` |
| Name | `structure.twilightforest.final_castle` "Final Plateau Castle" |

Start position: centred (`x = chunkX x 16 + 7`, `z = chunkZ x 16 + 7`), y = sea level (0); piece random
`RandomSource.create(seed + chunkX x 25117 + chunkZ x 151121)`. First piece `FinalCastleMainComponent(0, x, y, z)`.

Decorator `StructureTFDecoratorCastle`: block `twilightforest:castle_brick`, accent `minecraft:chiseled_quartz_block`, roof
`twilightforest:castle_roof_tile`, pillar `twilightforest:bold_castle_brick_pillar`, fence `minecraft:oak_fence`, stairs
`minecraft:quartz_stairs`, random blocks = castle selector (worn castle brick 0.1, cracked castle brick 0.1, castle
brick 0.8). Castle blocks, rune bricks and deadrock are U5's; doors and force fields are U30's (section 2).

Rune colours (`color` field of maze towers, saved as a block state): yellow = entrance maze, blue = bell maze, pink =
dungeons, violet = keep roof / gazebo. Each colour picks its door: `doorColor()` maps yellow / blue / pink / violet rune
brick to the castle door of the same colour (warns "Couldn't add door to tower, rune color couldn't be read" and uses
air otherwise).

### 1.1 Piece types

| Id | Class | Summary |
|---|---|---|
| `twilightforest:TFFCMain` | FinalCastleMainComponent | The keep: orientation SOUTH, controlled-spawn label 1, box offset (-24, 120, -24) size 49 x 41 x 49 (y 120..160 above the start). Children in order: `TFFCToF48` foundation, `TFFCRo48Cr` roof, `TFFCBoGaz` gazebo; corner towers at the keep box corners (y = minY + 3): stair tower NORTH at (minX, minZ), **large tower EAST at (maxX, minZ)**, stair tower WEST at (minX, maxZ), stair tower SOUTH at (maxX, maxZ); **yellow tower maze**: from local (48, 0, 24) 60 blocks SOUTH towards `(minX - 4, maxY, minZ - 24)`; **blue tower maze**: from (0, 30, 24) 60 blocks NORTH towards `(maxX + 4, minY, maxZ + 24)` (`buildTowerMaze`: a 13 maze tower plus a bridge of `howFar - 7`, `buildTowards(dest)`; the attempt is kept only if the piece list now contains an entrance tower (yellow) / bell tower (blue), else the list is restored; up to 15 attempts); **dungeon steps**: `TFFCDunSt` at (minX + 18, minY + 1, minZ + 18) SOUTH, three more turning CCW90 each, then `buildLevelUnder(level 1)`; **murals**: 35 wide x 30 high facing SOUTH at `offsetTowerCCoords(48, 23, 25, 1, SOUTH)` and 19 x 12 facing NORTH at `offsetTowerCCoords(48, 33, 24, -1, SOUTH)` but with the first mural's z (Java bug kept). postProcess: random-block shell 0..48 x 0..40; mezzanines at y 30 (13..47 x 1..12, 13..47 x 36..47, 1..12 x 1..47) with oak fence rails, a landing at y 25 (38..47 x 13..35), quartz-stair flights (5 steps each) between them; 3 x 3 bold pillars every 12 blocks (x, z in 11, 23, 35) from y 1 to 40 with quartz-stair collars at y 1, 19 (upside down), 21, 39 (upside down); wall half-pillars (skipping the door columns z 23 on the NONE / CW180 sides); floor at y 20 (1..47); a **pink force field cage** (12..24, 1..10, 12..24, roofed at y 10) with pink castle doors (17..19, 1..4) at z 12 and z 24; small-tower stair stubs (three corners) and large-tower stairs; **yellow castle door** (48, 1..4, 23..25) and **blue castle door** (0, 31..34, 23..25) |
| `twilightforest:TFFCToF48` | FinalCastleFoundation48Component | Keep foundation (keep box minus the top layer): every column 4..44 filled down with castle brick from y -1; per side stepped skirts (rows y -2..-1 at 1, -4..-1 at 2, -6..-1 at 3) and pilings every 6 blocks (9..39 plus 4 and 44) reaching down to the ground (`replaceAirAndLiquidDownwardsRotated`); extra buttresses at x 21 and 27 on the CW90 side |
| `twilightforest:TFFCRo48Cr` | FinalCastleRoof48CrenellatedComponent | Keep roof: box (keep -2, maxY - 1 .. maxY + 4, keep + 2) (height 5: `maxY + height - 1`); violet rune brick layer (2..50, 2, 2..50); per side a wall (3..45, 1..3, 1) and 3 x 5 x 3 merlons every 5 blocks from 10 to 40 |
| `twilightforest:TFFCBoGaz` | FinalCastleBossGazeboComponent | Boss gazebo on the keep roof: box (keep minX + 14, maxY + 2, minZ + 14)..(maxX - 14, maxY + 13, maxZ - 14) (21 x 12 x 21), no spawns (label -1), own decorator (block violet rune brick, fence violet force field); children: one template piece from pool `twilightforest:final_castle/temp/gazebo` placed at local (10, -1, 10) with orientation `UP_SOUTH` rotated by the piece, joined at the jigsaw named `twilightforest:final_castle/final_boss`. postProcess places nothing (the old procedural cage is commented out) |
| `twilightforest:TFFCStTo` | FinalCastleStairTowerComponent | Corner stair tower: size 9, height 51, box (-4, 0, -4) 9 x 51 x 9; child `TFFCRo9Cr` roof. postProcess: random shell 0..8 x 0..49, `6 + rand(4)` yellow glyph branches (decoRNG), stepped base under it (3 rings down to -7), yellow castle door (0, 1..3, 1..2), spiral stairs (5 flights of 3, quartz stairs on brick) up to a landing at y 17 with a yellow door (1..2, 18..20, 0), 7 more flights to a landing at y 38 with oak fence and a **violet castle door** (1..2, 39..41, 0) |
| `twilightforest:TFFCLaTo` | FinalCastleLargeTowerComponent | Corner large tower: size 13, height 61, box (-6, 0, -6) 13 x 61 x 13; child `TFFCRo9Cr` roof (box from the tower) and a template piece from pool `twilightforest:final_castle/temp/large_tower` at local (0, 1, 2), orientation `WEST_UP`, jigsaw `twilightforest:final_castle/large_tower`. postProcess: random shell, `6 + rand(4)` **blue** glyph branches, stepped base, yellow castle door (0, 1..4, 1..3) |
| `twilightforest:TFFCSiTo` | FinalCastleMazeTower13Component | Maze tower: size 13, floors `2 + rand(3)`, height `floors x 8 + 1`, entrance floor `rand(floors)` clamped so the door stays within y `LOWEST_DOOR` 108 .. `HIGHEST_DOOR` 186; box (-6, -entranceFloor x 8, -6), door opening (0, entranceFloor x 8 + 1, 6) CW180. Children: `TFFCToF13` foundation and a roof (`rand.nextBoolean()`: `TFFCRo13Con` else `TFFCRo13Cr`). `buildTowards(dest)` while depth < 20: within 30 blocks (x and z) of dest -> end tower 20 blocks away (yellow: `TFFCEnTo`, else `TFFCBelTo`) trying best, second, third direction; else a new maze tower `14 + rand(24)` away in the best direction (skipped when it equals the tower's own facing), then second, third, finally the own facing; the opening floor is moved to the top floor / floor 0 when within 60 blocks and less than 12 above / below dest; the new tower must lie within 128 blocks of the castle's 256-grid centre `((start.minX + 128) >> 8) << 8` and its box grown by 6 (y 0..255) must not collide. Then `buildNonCriticalTowers`: one random direction without an opening: a damaged tower (`TFFCDamT`), retried once. Every connection is a `TFFCBri` bridge of `howFar - 7`. postProcess: random shell, columns filled down with castle brick, `2 + rand(4) + rand(3)` glyph branches of the rune colour, floors every 8 (1..11 square) with alternating double quartz-stair flights, stairs to the roof when `height - highestOpening < 9`; door openings: chiseled quartz frame 5 wide x 6 high, castle door 3 x 4 of the rune colour. Saves `color` |
| `twilightforest:TFFCBri` | FinalCastleBridgeComponent | Bridge of length L: random castle deck (0..L, 0..1, 0..6), bold pillar rails (x axis) at y 2 on both edges, arch undersides `sl = l3 - (int)(cos((l3 - i) / l3 x 1.6) x l3)` with `l3 = L / 3` from both ends, gate posts (pillars at z 1 and 5, y 2..7, chiseled quartz lintel at y 6, pillar cap at (0/L, 7, 3)) |
| `twilightforest:TFFCDamT` | FinalCastleDamagedTowerComponent | Damaged maze tower (yellow runes): foundation + `TFFCFTh21` thorn foundation, then its own non-critical towers (wrecked towers). postProcess: maze tower, then `destroyTower` with decoRNG: three 5 x 11 x 5 `DestroyArea`s (random x / z within the box - 2, non intersecting, 100 tries) carve air from the top down to the first `twilightforest:deadrock` (or y 64); an area entirely above the current y is replaced by a new one at that y |
| `twilightforest:TFFCWrT` | FinalCastleWreckedTowerComponent | Wrecked tower: only the thorn foundation; four destroy areas and the inverse rule: every block **outside** the areas is removed (only stumps remain) |
| `twilightforest:TFFCToF13` | FinalCastleFoundation13Component | Tower foundation: box (tower minX - 2, minY - 1, minZ - 2)..(minX + 2, minY, maxZ + 2) (as TF: x span is only 5); finds the ground level from y 150 (first deadrock, F7 `findGroundLevel`), aborts when none; corner buttresses down to the ground (`(1|2, -1, 1|2)` to the ground, `(2, -mid, 0)` / `(0, -mid, 2)` with `mid = height / 2`). The side-buttress loop `for (x = 6; x < size - 3; x += 4)` never runs because `size = maxX - minX = 4` (TF quirk: keep it, no side buttresses) |
| `twilightforest:TFFCFTh21` | FinalCastleFoundation13ComponentThorns | Thorn vines around a damaged / wrecked tower: box (tower - 5, maxY - 1 .. maxY, tower + 5); with decoRNG, for each of the 4 rotations a 2 x 2 brown-thorn vine from `(3 + rand(13), ySpan + 5, 3 + rand(13))` down until deadrock or world y 90, twisting (twist `rand(4)`, period `3 + rand(3)`), with green-thorn branches (`seed + x x 321534781 ^ y x 756839 + z` random: direction, length 2..4, a vertical end) when `y % twistMod == 1` |
| `twilightforest:TFFCEnTo` | FinalCastleEntranceTowerComponent | Entrance tower at the end of the yellow maze: maze tower with 3 floors, entrance floor 2, yellow; peaked roof `TFFCRo13Pk`; `missingFloors = (minY - 127) / 8` split into bottom / middle: a side tower `TFFCEnSiTo` (middle floors + 1) 20 blocks away on CW90, else CCW90, else straight ahead (with bridge); a bottom tower `TFFCEnBoTo` under it reaching the ground; openings always at floor 0 |
| `twilightforest:TFFCEnSiTo` | FinalCastleEntranceSideTowerComponent | Side tower of the entrance (yellow), extra door at (0, 1, 6), foundation + peaked roof |
| `twilightforest:TFFCEnBoTo` | FinalCastleEntranceBottomTowerComponent | Ground tower of the entrance (yellow): four `TFFCEnSt` stair pieces on its four sides (doors at floor 1), no accessible roof |
| `twilightforest:TFFCEnSt` | FinalCastleEntranceStairsComponent | Stepped 13-wide quartz-stair pyramid leading down from a door to the ground (stairs only into replaceable blocks, castle brick filled below each step) |
| `twilightforest:TFFCBelTo` | FinalCastleBellTower21Component | Bell tower at the end of the blue maze: size 21, 8 floors, height 65, entrance at y 9 (opening (0, 9, 10)); `TFFCBeF21` foundation, `TFFCRo13Cr` roof; template piece from pool `twilightforest:final_castle/temp/bell_tower` at local (0, 9, 10), orientation `WEST_UP`, jigsaw `twilightforest:final_castle/room`; no procedural floors (`addFloors` no-op); blue force-field windows: y 48..62 every 2 blocks (x 5..16) on all sides and y 24..38 pairs at x 1/3, 9/11, 17/19 |
| `twilightforest:TFFCBeF21` | FinalCastleBellFoundation21Component | Bell tower foundation: corner buttresses down 1, 16 and 32 blocks; the side loop never runs (box only 5 wide, `size` 4), as in TFFCToF13 |
| `twilightforest:TFFCRo13Con` | FinalCastleRoof13ConicalComponent | Cone roof: `slope = 2 + rand(3) + rand(3)`, height `slope x 4`; corner turrets, crenels every 2, stepped cone of castle brick edges and roof tiles, spire at (8, slope x 6 + 2 .. slope x 7 + 2, 8). Saves `slope` |
| `twilightforest:TFFCRo13Cr` | FinalCastleRoof13CrenellatedComponent | Crenellated roof: height 5, corner blocks, wall, 3 x 4 x 3 merlons every 4 blocks |
| `twilightforest:TFFCRo13Pk` | FinalCastleRoof13PeakedComponent | Peaked (gabled) roof: height 18, roof-tile slopes in three steps, ridge at y 18 with end finials, crenels on the CW90 / CCW90 sides |
| `twilightforest:TFFCRo9Cr` | FinalCastleRoof9CrenellatedComponent | Crenellated roof for 9 / 13 towers: height 5, corner blocks and two merlons per side |
| `twilightforest:TFFCDunSt` | FinalCastleDungeonStepsComponent | Dungeon stairway: label 2, box `getComponentToAddBoundingBox2(x, y, z, -2, -15, -3, 5, 15, 20)` = 6 x 16 x 21 (local y 0..15); 15 quartz-stair steps down (y 14 to 0) with 6 air above, landing (0..4, 0..5, 15..19). `buildMoreStepsTowards(rotation)`: next steps at local (2, 0, 17) shifted by 5 / 6 per rotation; `buildLevelUnder(level)`: `TFFCDunEn` at local (2, -7, 19); `buildBossRoomUnder`: `TFFCDunBoR` at (2, -31, 19) |
| `twilightforest:TFFCDunEn` | FinalCastleDungeonEntranceComponent | First room of a dungeon level (pink): decorator block pink rune brick, fence pink force field; room 31 logic (below); stairs of deadrock and quartz stairs from the ceiling down to the floor at the centre; pink castle door (23, 0..3, 12..14) under a rune lintel. Tracks `hasExit` for its level |
| `twilightforest:TFFCDunR31` | FinalCastleDungeonRoom31Component | Dungeon room: size 31, height 7, label 2, terrain adaptation `bury`; spreads up to 12 neighbour rooms (random start rotation stepping through 4) 9 blocks beyond each side at offset `rand(15) - 9` (+31 half the time) while `depth - entranceDepth < maxSpread` (2 on level 1, 3 on level 2); a room at exactly maxSpread builds the level's single exit (8 tries). postProcess (skipped when the box leaves the `thornlands` / `final_plateau` biomes): carves air through air / deadrock variants, castle-brick floor and ceiling with thick-castle-brick border inside a 7-block margin, force-field walls (decoRNG: green with yellow runes or blue with blue runes, 50 %) with rune-brick columns every 4 and a rune bar at a random height |
| `twilightforest:TFFCDunEx` | FinalCastleDungeonExitComponent | Level exit room (pink): steps towards the parent entrance (`findStairDirectionTowards`), then level 1 -> `buildLevelUnder(2)`, level 2 -> `buildBossRoomUnder`; pink castle door (7, 0..3, 16..18) |
| `twilightforest:TFFCDunBoR` | FinalCastleDungeonForgeRoomComponent | Forge room (label 3): box (-15, 0, -15) 51 x 31 x 51 of air with a sign at (25, 0, 25): "Mini-boss 2" / "Gives talisman" (placeholder, as TF) |
| `twilightforest:TFFCMur` | FinalCastleMuralComponent | Rune mural on a keep wall: box 2 x height x width; `FinalCastleMural` bitmap seeded with **`WorldUtil.getOverworldSeed()`** `+ minX x 321534781 ^ minZ x 756839`: a 3 x 3 dot at (width / 2 - 1, 2), horizontal branch trees both ways (length `rand(width / 6) + width / 6`), a vertical tree (`rand(height / 6) + height / 6`) and stripes from the top down to height / 3; set pixels become yellow rune brick at x 0 (others untouched). Saves `muralWidth`, `muralHeight`, `muralBytes` |

`decoRNG` everywhere is `RandomSource.create(seed + minX x 321534781 ^ minZ x 756839)` with Java precedence
`(seed + minX x 321534781) ^ (minZ x 756839)`; the template pieces themselves are F7 `TwilightJigsawPiece`s.

### 1.2 Templates (original `.gft`, R11 / R19)

TF's templates `twilightforest:final_castle/temp/{gazebo,large_tower,bell_tower}` are `.nbt` files (All Rights
Reserved, never opened). U30 owns the template-pool entries (registry `twilightforest:template_definition`):

| Registry entry | Content (TF data) | Original template to author (`assets/structures/finalcastle/final_castle/temp/<name>.gft.json`) |
|---|---|---|
| `template_definition/twilightforest:final_castle/temp/gazebo` | `{"twilightforest:final_castle/temp/gazebo": 100}` | 21 x 12 x 21 gazebo on the keep roof: violet force-field walls and roof (as the commented procedural code: four walls 0..10 high, roof layer at y 11), a jigsaw connector named `twilightforest:final_castle/final_boss` at the floor centre (pairs with the host at local (10, -1, 10), `UP_SOUTH`); may hold the U5 `twilightforest:final_boss_boss_spawner` (it never spawns, section 4.4) and the "Final Castle WIP." text displays with an interaction box tagged `final_castle_wip` (attacking it removes the texts, `EntityEvents.removeCastleTextIfAttacked`) |
| `template_definition/twilightforest:final_castle/temp/large_tower` | `{"twilightforest:final_castle/temp/large_tower": 100}` | interior of the 13 x 61 x 13 large tower (floors and stairs between the yellow door at y 1 and the roof), connector `twilightforest:final_castle/large_tower` at local (0, 1, 2), `WEST_UP` |
| `template_definition/twilightforest:final_castle/temp/bell_tower` | `{"twilightforest:final_castle/temp/bell_tower": 100}` | interior of the 21-wide bell tower from the entrance at y 9 upwards (floors, stairs, bell room under the force-field windows), connector `twilightforest:final_castle/room` at local (0, 9, 10), `WEST_UP` |

`tools/structures/build.ps1 -Unit finalcastle -Check` validates the three files against F7's imported definitions
(size limits, connector names); `/tf structure_util place twilightforest:final_castle/temp/<name>` shows them.

### 1.3 Golden hash

`finalcastle.castle_hash` (R19): force-start the castle at a fixed seed and hash the keep plus the first maze towers
(`snapshotVolume`), reviewed once with screenshots (rune glows, force fields, doors).

## 2. Blocks

Sources: `init/TFBlocks.java`, `block/CastleDoorBlock.java`, `block/ForceFieldBlock.java`,
`client/model/block/forcefield/*.java`, `block/CinderFurnaceBlock.java`, `block/entity/CinderFurnaceBlockEntity.java`,
`block/ThornsBlock.java`, `block/BurntThornsBlock.java`, `block/ThornRoseBlock.java`, `block/SpecialStemLeavesBlock.java`,
`client/event/ColorHandler.java`, `data/tags/BlockTagGenerator.java`.

### 2.1 Castle doors

`twilightforest:yellow_castle_door`, `twilightforest:violet_castle_door`, `twilightforest:pink_castle_door`,
`twilightforest:blue_castle_door` (`CastleDoorBlock`, states `active` false, `vanish` false): strength 100 / 100,
requires tool, deepslate-tiles sound, push BLOCK, force solid on, in `#twilightforest:castle_blocks` (U5 tag: needs
diamond tool). Map colour: none when vanished, else yellow / **light blue (violet door)** / magenta / **purple (blue
door)** (TF swap kept). Tint (client): yellow 0xFFFF00, violet 0x4B0082, pink 0xFF00FF, blue 0x00FFFF (shared with
the rune bricks); faces between two doors with the same `vanish` value are culled. Names: pink = "Magenta Castle Door".

- Shapes: vanished -> no collision, outline 0.375..0.625 cube; occlusion empty when vanished **or not active** (as TF).
- Activation (`useWithoutItem`, or `neighborChanged` from a non-door block while the door has a neighbour signal): if
  vanished or active -> FAIL; `isBlockLocked` is always false in 4.8 (the locked branch would play
  `block.twilightforest.door.activate` 1.0 / 0.3 and PASS); else set `active` and schedule a tick in `2 + rand(5)`.
- Scheduled tick: solid + active -> `vanish = true, active = false`, schedule 80 ticks, play
  `block.twilightforest.door.vanish` (BLOCKS, 0.125, pitch `r x 0.25 + 1.75`), send 64 `twilightforest:annihilate`
  particles (4 x 4 x 4 grid, gaussian x 0.2 speed) to players within 32, and activate every adjacent solid inactive door
  (6 directions): the whole door vanishes as a chain. Vanished + inactive (after 80 ticks) -> becomes active again
  (schedule 2..6) and plays `block.twilightforest.door.reappear` (0.125, `r x 0.25 + 1.25`); vanished + active -> solid
  again (`vanish = false, active = false`) with the reappear sound.
- Loot: `twilightforest:blocks/yellow_castle_door`, `twilightforest:blocks/violet_castle_door`,
  `twilightforest:blocks/pink_castle_door`, `twilightforest:blocks/blue_castle_door` drop the door (survives explosion).
- Bedrock: states map to `twilightforest:active` / `twilightforest:vanish` (F1 names); use through
  `interactions::onUseOn`, redstone through `redstone::watch`, delays through F11 scheduled ticks; vanished doors use a
  small selection box and no collision via a state-driven `minecraft:collision_box` permutation.

### 2.2 Force fields

`twilightforest:violet_force_field`, `twilightforest:pink_force_field`, `twilightforest:orange_force_field`,
`twilightforest:green_force_field`, `twilightforest:blue_force_field` (`ForceFieldBlock`, states `down/up/north/south/west/east`
+ `waterlogged`): light 2, no loot, no occlusion, push BLOCK, strength -1 / 3600000.8 (unbreakable), entities cannot
destroy, shade brightness 1, propagates skylight; tags `#twilightforest:common_protections` (dragon / wither / reactor
immune), `#twilightforest:annihilation_inclusions`, relocation not supported. Map colours purple / magenta / orange / green / light blue;
tints violet 0x5C1074, pink 0xFA057E, orange 0xFF5B02, green 0x89E701, blue 0x0DDEFF. Names: pink = "Magenta Force Field".

- Placement: the face opposite the clicked face always connects; the clicked direction connects unless sneaking; other
  directions connect when `canConnectTo` (same block always; else only while fewer than 3 connections exist and the
  neighbour face is sturdy and not a connection exception).
- `updateShape` only ever **adds** connections (never removes); water is ticked when waterlogged.
- Shape (= collision): 2 x 2 x 2 core, a 2-thick arm per connection, and corner panels (e.g. DOWN_NORTH (7..9, 0..7,
  0..7)) when both directions are set and `cornerConnects` (either neighbour along one direction has a sturdy face or is a
  force field connected in the other direction). The model (`ForceFieldModel`) draws the same parts.
- Rotate / mirror swap the horizontal connections.
- Bedrock: 128 permutations per colour; corner panels depend on neighbours, so the client mod draws them with F12
  `registerBlockRenderer`, the collision box uses the union of arms (vanilla clients see arms + panels approximated
  from the states).

### 2.3 Thorns and thorn plants

| Id | Class / properties | Behaviour |
|---|---|---|
| `twilightforest:brown_thorns` | `ThornsBlock` (`ConnectableRotatedPillarBlock` with `axis`, 6 connection booleans, `waterlogged`; F11 connected family), PODZOL, wood sound, strength 50 / 2000, no loot, push BLOCK | see below |
| `twilightforest:green_thorns` | same, PLANT map colour | see below |
| `twilightforest:burnt_thorns` | `BurntThornsBlock`, instant break, STONE, sand sound, push DESTROY, no loot | a living entity or projectile inside destroys it (no drops) ("Burnt Thorns will disintegrate when stepped on"); broken by a player it becomes the fluid state (no drop); no path penalty |
| `twilightforest:thorn_rose` | `ThornRoseBlock` (BushBlock, `facing` 6 directions, default up), PLANT, no collision, grass sound, strength 10 / 0, push DESTROY; shape centred 0.8 cube | survives when the block behind it (opposite `facing`) is brown / green thorns, or when that behind block's face toward the rose (`facing`) is sturdy (TF evaluates the behind block's state with the rose's position; shapes rarely depend on it); item in `minecraft:small_flowers`, `#twilightforest:immune_to_thorns`; mineable/hoe; drops itself |
| `twilightforest:thorn_leaves` | `SpecialStemLeavesBlock` (stem = brown or green thorns), PLANT, azalea leaves sound, strength 0.2, random ticks, no occlusion, push DESTROY, ignited by lava | distance from thorns, decays at 7; evergreen foliage tint (0x619961); mineable/hoe; `minecraft:leaves`; item in `#twilightforest:immune_to_thorns` |
| `twilightforest:potted_thorn`, `twilightforest:potted_green_thorn`, `twilightforest:potted_dead_thorn` | `SpecialFlowerPotBlock` (F11 `potted` family) holding brown / green / burnt thorns ("Potted Thorn", "Potted Green Thorn", "Potted Burnt Thorn") | `#minecraft:flower_pots`; loot `twilightforest:blocks/potted_thorn`, `twilightforest:blocks/potted_green_thorn`, `twilightforest:blocks/potted_dead_thorn` = flower pot (the thorn item is returned by the pot interaction, vanilla) |

Brown / green thorns:

- Connections (`canConnectTo`): any thorns block, thorn rose, thorn leaves, `twilightforest:weathered_deadrock` (U5);
  air neighbours never remove an existing connection (`updateShape` returns the state unchanged for air).
- Shape: 10 x 10 core (3..13) with 3-deep arms per connection; path type DAMAGE_OTHER.
- `entityInside` (and `stepOn` for Y-axis thorns): 4 damage of `twilightforest:thorns` (no attacker) to any entity except
  item entities whose item is in `#twilightforest:immune_to_thorns`.
- Broken by a non-creative player: the block **stays** and grows a "thorn burst": green thorns (axis of the direction)
  along both ends of its axis and in 3 random directions, each `1 + rand(3)` long minus one (0..2 blocks), only into empty
  blocks (`UPDATE_CLIENTS`); creative players break it normally.
- The lamp of cinders (U8) turns brown / green thorns into burnt thorns with the same properties (by id); thorns are
  in `#twilightforest:annihilation_inclusions` (cube of annihilation) and relocation-not-supported.
- Block items: `twilightforest:brown_thorns` ("Thorns"), `twilightforest:green_thorns`, `twilightforest:burnt_thorns`,
  `twilightforest:thorn_rose`, `twilightforest:thorn_leaves`.
- Loot: `twilightforest:blocks/thorn_rose` (itself, survives explosion); `twilightforest:blocks/thorn_leaves`: shears or
  Silk Touch -> itself, else sticks with Fortune chances 2.5 / 2.78 / 3.125 / 4.17 / 10 %, plus (not shears / silk) 1-2
  sticks at 2 / 2.22 / 2.5 / 3.33 / 10 % (explosion decay).

### 2.4 Cinder furnace, cinder log, cinder wood

| Id | Properties |
|---|---|
| `twilightforest:cinder_furnace` | `CinderFurnaceBlock extends AbstractFurnaceBlock` (`facing`, `lit`), WOOD map colour, strength 7, requires tool (mineable/pickaxe), **light 15 at all times**; block entity `twilightforest:cinder_furnace` |
| `twilightforest:cinder_log` | RotatedPillarBlock (`axis`), COLOR_GRAY, BASS, strength 1, ignited by lava, mineable/axe |
| `twilightforest:cinder_wood` | plain block (no axis), same properties |

All three items are in `#twilightforest:wip` (hidden from the creative tab). Loot: `twilightforest:blocks/cinder_furnace`,
`twilightforest:blocks/cinder_log`, `twilightforest:blocks/cinder_wood` drop themselves.

Cinder furnace behaviour (`CinderFurnaceBlockEntity extends FurnaceBlockEntity`, slots 0 input, 1 fuel, 2 output):

- `openContainer` is empty: **right-clicking opens nothing** in 4.8; only hoppers / automation fill and empty it.
- Vanilla furnace tick with `RecipeType.SMELTING` only, except: cooking progress advances by
  `getCurrentMultiplier(2)` per tick and completes at `>=` total; the output count is multiplied by
  `getCurrentMultiplier(10)` for every item (TF mutates the recipe's shared result stack here; Bedrock copies instead,
  noted deviation); the output-space check uses `ceil(logs / 10)` for `#minecraft:logs` / `c:ores` inputs; wet sponge
  + empty bucket in fuel gives a water bucket (vanilla).
- `getCurrentMultiplier(f)`: `logs` = cinder logs in the 3 x 3 x 3 around the furnace; `logs < f` -> 1, else `logs / f +
  (rand(f) >= logs % f ? 0 : 1)`.
- While burning, when `litTime % 5 == 0`: pick an offset `(rand(2) - rand(2))` per axis; a `#minecraft:logs` block there
  (not cinder log) becomes a cinder log with the same properties (`UPDATE_CLIENTS`), two mob-spawner particle events and
  `block.fire.ambient` (1, 1).
- Client `animateTick` (vanilla furnace copy): 10 % crackle sound, smoke + flame at the front when lit.
- Bedrock: the block entity is emulated with BlockData + an F10 `inventory::registerBlock` handler (3 slots) so hoppers
  work; no screen (as TF). The smelting recipe lookup goes through F9 `recipes::ofType("minecraft:smelting")`.

## 3. Features (thornlands)

| Id | Data |
|---|---|
| `twilightforest:thorns` (feature type, `world/components/feature/ThornFeature.java`) | config `ThornsConfig{max_spread, chance_of_branch, chance_of_leaf, chance_leaf_is_rose}` |
| `twilightforest:thorns` (configured feature) | `max_spread` 7, `chance_of_branch` 3, `chance_of_leaf` 3, `chance_leaf_is_rose` 50 |
| `twilightforest:thorns` (placed feature) | placement `twilightforest:chunk_blanketing` (U2 modifier): integrity 0.7 per column of the chunk, heightmap `OCEAN_FLOOR_WG` + 1, biome lock `twilightforest:thornlands` |

`ThornFeature.place`: `nextLength = 2 + rand(4)`, `maxLength = 2 + rand(4) + rand(4) + rand(4)`; `placeThorns(origin,
nextLength, UP, maxLength, origin, avoidCloud = true)`: for i in 0..length-1 at `pos + dir x i`: (first stalk only)
stop when the column is above `seaLevel + 150` (`max(y, MOTION_BLOCKING_NO_LEAVES height)`, keeps away from the giants'
cloud layer); stop when `|dx|` or `|dz|` from the origin reaches `max_spread` or the block is not air / leaves; place
`brown_thorns` (axis of dir, `UPDATE_CLIENTS`, post-processed for connections); on the last block, 1 in
`chance_of_leaf`, if the next block is empty: thorn leaves (distance 1) or, 1 in `chance_leaf_is_rose`, a thorn rose
facing dir. When complete: if `maxLength > 1` continue from the end with a random direction, length `1 + rand(maxLength)`,
`maxLength - 1`; if length > 3, 1 in `chance_of_branch` a branch from a random middle block; if length > 3, 1 in
`chance_of_leaf` a thorn leaf beside a random middle block. Registered with F5 `features::registerType`; the U1
thornlands biome lists the placed feature.

## 4. Mobs

Sources: `entity/monster/Adherent.java`, `entity/monster/HarbingerCube.java`, `entity/RovingCube.java`,
`entity/ai/goal/CubeMoveToRedstoneSymbolsGoal.java`, `entity/ai/goal/CubeCenterOnSymbolGoal.java`,
`entity/boss/PlateauBoss.java`, `block/entity/spawner/FinalBossSpawnerBlockEntity.java`, client renderers. None of them
spawn naturally in 4.8 (castle spawn lists empty, no eggs); they exist for commands and future content. Placements
(F5): harbinger cube, adherent, roving cube = ON_GROUND, `MOTION_BLOCKING_NO_LEAVES`, monster spawn rules.

### 4.1 `twilightforest:adherent` (Adherent, RangedAttackMob, ITFCharger)

Size 0.8 x 2.2. Attributes: max health 20, speed 0.25. Goals: 1 Float, 2 RestrictSun, 3 FleeSun 1.0, 4 RangedAttack (1.0,
interval 60, range 10), 5 WaterAvoidingRandomStroll 1.0, 6 LookAtPlayer 8, 6 RandomLookAround; targets HurtBy, Player.
Attack: a `twilightforest:nature_bolt` (U17 projectile) aimed at `target eye y - 1.1` plus `dist x 0.2`, velocity 0.6,
inaccuracy `10 - difficultyId x 4`, sound `entity.ghast.shoot` (1, `1 / (r x 0.4 + 0.8)`). Synced `CHARGE_FLAG` (unused).
Cannot ride. Render: `AdherentModel` (robed, arms swaying `zRot = +-(cos((age + 10) x 0.133) x 0.3 + 0.3)`), texture
`adherent.png`, shadow 0.625, hover offset `-0.125 - sin(age x 0.133) x 0.1`. Loot `twilightforest:entities/adherent`: empty.

### 4.2 `twilightforest:harbinger_cube` (HarbingerCube)

Size 1.9 x 2.4, fire immune. Attributes: max health 40, speed 0.23. Goals: 1 Float, 1 WaterAvoidingRandomStroll, 2
LookAtPlayer 8, 2 RandomLookAround; targets HurtBy, Player (no attack goal: it never attacks). Render:
`HarbingerCubeModel` (32 px cube body on four 8 px legs, quadruped walk), `apocalypse2.png`, shadow 1.0. Loot
`twilightforest:entities/harbinger_cube`: empty.

### 4.3 `twilightforest:roving_cube` (RovingCube)

Size 1.2 x 2.1. Attributes: max health 10, speed 0.23, attack 5. Goals: 0 `CubeMoveToRedstoneSymbolsGoal` (1 in 20 per
tick: scan x, z in -16..15 and y in -5..4 for an air block whose **six** neighbours are all redstone wire (TF checks
all directions, so it practically never matches); remember it in `symbolX/Y/Z` and path there), 1
`CubeCenterOnSymbolGoal` (when navigation is done and 0.25 < distance < 10, move to the symbol centre until within 0.1).
No target goals. Client: 3 `twilightforest:annihilate` particles per tick within +-2 around the eye. Render
`RovingCubeRenderer`: cube-of-annihilation model scaled x 2, spinning `wrapDegrees(age) x 11` degrees, raised 0.75,
texture `cubeofannihilation.png`. Loot `twilightforest:entities/roving_cube`: empty.

### 4.4 `twilightforest:plateau_boss` (PlateauBoss extends BaseTFBoss)

Placeholder final boss: size 1 x 1, fire immune, default monster attributes (health 20), xp 647, no goals of its own,
invisible (`NoopRenderer`). Boss kit: `boss::registerBoss{entityId plateau_boss, homeStructure final_castle (radius 30),
lootTable twilightforest:entities/plateau_boss (empty), deathChestBlock twilightforest:canopy_chest (U4), bar rgb
0xFFFFFF}`; `boss::registerSpawner{blockId twilightforest:final_boss_boss_spawner (U5), entityId plateau_boss, particle
twilightforest:annihilate, spawnFn = never spawn}` (`FinalBossSpawnerBlockEntity.spawnMyBoss` returns false: the spawner
only shows particles).

## 5. Damage type `twilightforest:thorns` (owned by U30)

message_id `twilightforest.thorns` ("%1$s walked into some thorns", `.player` "... while escaping %2$s"; F10 keys),
exhaustion 0.1, scaling `when_caused_by_living_non_player`, tags `minecraft:no_knockback`, `neoforge:is_environment`,
`neoforge:is_physical`. Used by brown / green thorns (4 damage).

## 6. Item tag `item/#twilightforest:immune_to_thorns`

Members `twilightforest:thorn_leaves`, `twilightforest:thorn_rose`: item entities of these are not hurt by thorns.

## 7. Recipe

`twilightforest:thorn_rose_to_red` (shapeless, misc): thorn rose -> 1 `minecraft:red_dye`; unlock advancement
`twilightforest:recipes/misc/thorn_rose_to_red` (has thorn rose; F1, R21). R4: the output is vanilla but the recipe is
U30's in owners.tsv.

## 8. Map decoration `twilightforest:final_castle`

`MapDecorationType(twilightforest:final_castle, showOnItemFrame true, colour -1, exploration false, trackCount true)`,
drawn by `ConqueredMapIconRenderer` (crossed out when conquered); U13 draws it via F12 `maps::setIcon` with U30's icon.

## 9. Sound events (owned by U30)

| Event | Subtitle | TF source | Use |
|---|---|---|---|
| `block.twilightforest.door.activate` | "Castle Door clicks" | event `block.comparator.click` | locked castle door (never reached in 4.8) |
| `block.twilightforest.door.vanish` | "Castle Door vanishes" | event `block.fire.extinguish` | castle door vanish (0.125, 1.75..2.0) |
| `block.twilightforest.door.reappear` | "Castle Door reappears" | event `block.fire.extinguish` | castle door reappear (0.125, 1.25..1.5) |

Synthesized by F3 from `tools/sounds/recipes/finalcastle.json` (never TF audio).

## 10. Progression end message

`EntityEvents.alertPlayerCastleIsWIP`: when a player earns `twilightforest:progression_end` (U14: previous `progress_troll`
and standing in `twilightforest:final_plateau`), send the system message `gui.twilightforest.progression_end.message`
with `gui.twilightforest.progression_end.discord` as a blue underlined link to `https://discord.experiment115.com/`
(F12 keys, R9: U30 passes them with `gf::trFor` and English fallbacks). Bedrock: `progression::onGranted`; vanilla
clients get the text without the click event (deviation).

## 11. Bedrock adaptation notes

- Block entities: castle-door timers use F11 scheduled ticks; the cinder furnace uses BlockData + F10 inventory; the
  thorn burst and burnt-thorn dissolving use `interactions::onDestroy` (Deny + nextTick growth, R16) and
  `blockevents::onEntityInside`.
- Thorn damage for vanilla clients is server-side (`blockevents::onEntityInside`), item entities filtered by the tag.
- Templates: original `.gft` designs only (section 1.2); the jigsaw connector names must match TF's so F7's pool logic
  places them exactly as TF does.
- Dungeon rooms use `bury` terrain adaptation inside a `beard_box` structure: F7 must honour the per-piece override.
- The castle is the end of progression in 4.8: nothing inside drops loot chests; doors, force fields and runes are the
  interactive parts.

## 12. Acceptance (selftests `finalcastle.*`, dx 3000-3099)

Per the unit record: golden hash of the castle (`finalcastle.castle_hash`) and `-Check` of the three templates; a
castle door activated by use or redstone vanishes after 2-6 ticks, chains to adjacent doors (rune-colour flow through a
door frame), reappears after 80 ticks with the sounds; force fields connect on placement and are unbreakable; thorns deal
4 `thorns` damage, spare thorn-rose items, burst instead of breaking for survival players, burn into burnt thorns with
the lamp of cinders (U8, by id) and burnt thorns dissolve when walked into; the cinder furnace smelts faster and
multiplies output next to cinder logs and turns adjacent logs into cinder logs; the thorns feature only grows in the
thornlands below sea level + 150; earning `progression_end` sends the WIP message once; client screenshots of the rune
doors, force-field corners and thorns.
