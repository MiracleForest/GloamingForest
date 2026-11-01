# stronghold (U25): Knight stronghold and Knight Phantoms

Behaviour spec of unit **U25** (`stronghold`), written by F15 from The Twilight Forest 4.8 (`9e40a757`). Java paths are
relative to `src/main/java/twilightforest/`; data paths to `src/generated/resources/`.

| Unit fact | Value |
|---|---|
| Selftests | `stronghold.*`, testPos dx `2500-2599` (R12) |
| World data | `gf:stronghold:*` (not needed by TF; pedestal and phantoms keep their state in blocks and entities) |
| Net channels | `stronghold.*` (none required) |
| Contracts | landmark, structure, boss, entity, projectile, loot, progression |
| Config | none (boss chest / multiplayer settings are F8's) |

**Tag namespaces.** Tags are written `#<ns>:<path>`. `#twilightforest:` tags and the `#c:` / `#neoforge:` tags that
owners.tsv lists (`tag` rows) are TF data (`data/<ns>/tags/**`) imported by F1; `#minecraft:` tags are vanilla Java
1.21.1 tags (TF adds members to some). Bedrock has no Java tags: every tag here resolves through `gf::tags` (F9,
`src/feature/tags/Tags.h`), and units add members only through union fragments (R6). TF's damage-type and tool-tier
tags are in the `neoforge` namespace (`#neoforge:is_physical`, `#neoforge:is_magic`, `#neoforge:is_environment`,
`#neoforge:needs_wood_tool`), never `#c:`.

## 1. Structure `twilightforest:knight_stronghold`

Sources: `world/components/structures/type/KnightStrongholdStructure.java`,
`world/components/structures/stronghold/*.java` (25 classes): the shared piece base `KnightStrongholdComponent.java`
(`addNewComponent`, `addNewUpperComponent`, the `isOutOfRange` checks, the doors and the break-in logic of section 1.1),
the weight record `StrongholdPieceWeight.java` (factory, weight, instance limit with 0 = unlimited, minimum depth,
instances spawned), the selector `StrongholdPieces.java`, the decorator `StrongholdDecorator.java`, the 20 piece classes
of the table below and the legacy `StrongholdShieldStructure.java`; plus
`world/components/structures/util/LandmarkStructure.java` (F6).

| Field | Value |
|---|---|
| Structure type | `twilightforest:knight_stronghold` (`ControlledSpawningStructure`) |
| Structure set | `twilightforest:knight_stronghold`, placement `twilightforest:landmark_grid`, weight 1 |
| Biomes | `#twilightforest:valid_knight_stronghold_biomes` (dark forest) |
| Step | `underground_structures`, terrain adaptation `bury` (per piece: protected pieces bury, upper pieces none) |
| Progression lock | `twilightforest:progress_trophy_pedestal` |
| Hint | kobold + book `twilightforest.book.tfstronghold` ("Notes on a Stronghold", 4 pages) |
| Decoration clearance | radius 3, surface decorations true, underground false, vegetation true, adjust elevation false |
| Controlled spawns (label 0, monsters) | `twilightforest:blockchain_goblin` 10 1-2, `twilightforest:lower_goblin_knight` 5 1-2, `twilightforest:helmet_crab` 10 2-4, `twilightforest:slime_beetle` 10 2-3, `twilightforest:redcap_sapper` 2 1-2, `twilightforest:kobold` 10 2-4, `minecraft:creeper` 5 1-2, `minecraft:slime` 5 4-4 |
| Spawn overrides | all categories empty over the structure box |
| Map icon | `twilightforest:knight_stronghold` (MapDecorationType, item-frame visible, track count) |
| Start | `KnightStructureStart`: bounding box max y clamped to the start y (generation y - 1), saved as `knight_y` |

Start position: `x = chunkX x 16 + 7`, `z = chunkZ x 16 + 7`, y = sea level (0) because the decoration config does not
adjust elevation; piece random `RandomSource.create(seed + chunkX x 25117 + chunkZ x 151121)`. First piece
`StrongholdEntranceComponent(0, x, (y + rand(3)) == 0 ? 5 : 1, z)` (Java precedence: the y argument is 5 when
`y + rand(3) == 0`, else 1; keep it).

### 1.1 Piece types (all `twilightforest:<id>`, F7 `registerPieceType`)

| Id | Class | Box (orientBox x, y, z offsets; sizes) | Contents / children |
|---|---|---|---|
| `twilightforest:TFSEnter` | StrongholdEntranceComponent | (-1, -1, 0; 18 x 7 x 18) | decorator StrongholdDecorator; four exits (NONE (4,1,18), CW90 (-1,1,13), CW180 (13,1,-1), CCW90 (18,1,4)) each from a fresh piece list, marking the boss room used once present; logs fatal "Did not find boss room from exit 3 - EPIC FAIL" if none; then adds the access chamber at (minX + 8, minY + 7, minZ + 4) depth 2; 4 corner + 4 wall statues |
| `twilightforest:TFSAC` | StrongholdAccessChamberComponent | (-4, 1, 0; 9 x 5 x 9) | not protected (no controlled spawns, no bury); 4 small doorways; mossy stone brick shaft (2,-2,2)-(6,0,6) with air core (3,-2,3)-(5,2,5); stairs around the shaft; pillar at (2,0,2); **`twilightforest:trophy_pedestal` at (2,1,2)**; **`twilightforest:stronghold_shield` layer (2,-1,2)-(6,-1,6)** (default facing DOWN); upper children in 4 directions (NONE (4,1,9), CW90 (-1,1,4), CW180 (4,1,-1), CCW90 (9,1,4)) |
| `twilightforest:TFSSH` | StrongholdSmallHallwayComponent | (-4, -1, 0; 9 x 7 x 18) | child NONE (4,1,18) |
| `twilightforest:TFSLT` | StrongholdLeftTurnComponent | (-4, -1, 0; 9 x 7 x 9) | child CCW90 (9,1,4) |
| `twilightforest:TFSRT` | StrongholdRightTurnComponent | (-4, -1, 0; 9 x 7 x 9) | child CW90 (-1,1,4) |
| `twilightforest:TFSCr` | StrongholdCrossingComponent | (-13, -1, 0; 18 x 7 x 18) | children NONE (4,1,18), CW90 (-1,1,13), CCW90 (18,1,4); 2 corner + 4 wall statues, oak/spruce stair furniture |
| `twilightforest:TFSDE` | StrongholdDeadEndComponent | (-4, -1, 0; 9 x 7 x 9) | chest at (4,1,3) facing SOUTH with `twilightforest:stronghold_cache`; trapped (1/3, saved `chestTrapped`): trapped chest + `minecraft:tnt` under it at (4,0,3); 3 wall statues |
| `twilightforest:TFSBalR` | StrongholdBalconyRoomComponent | (-4, -1, 0; 18 x 14 x 27) or (-13, -8, 0; ...) by entry side | 6-8 exits on two levels |
| `twilightforest:TFSTR` | StrongholdTrainingRoomComponent | (-13, -1, 0; 18 x 7 x 18) | sand pits, anvil, carved pumpkin + oak fence dummies, birch planks; 2 corner statues; child NONE (4,1,18) |
| `twilightforest:TFSSS` | StrongholdSmallStairsComponent | (-4, -1, 0; 9 x 14 x 9) entering bottom, or (-4, -8, 0; ...) entering top | enter bottom when y < -21, top when y > -15, else `(z & 1) == 0`; treasure 1/2 (`hasTreasure`): chest at (4,1,6) with `twilightforest:stronghold_cache`, trapped 1/3 with TNT under it; wall statue |
| `twilightforest:TFSTC` | StrongholdTreasureCorridorComponent | (-4, -1, 0; 9 x 7 x 27) | chest at (8,2,13) in the side wall with `twilightforest:stronghold_cache`; 4 wall statues; child NONE (4,1,27) |
| `twilightforest:TFSAt` | StrongholdAtriumComponent | (-4, -1, 0; 18 x 14 x 18) or (-13, -8, 0; ...) | grass centre and a tree at (8,1,8): configured feature by piece-random rand(5) -> 1 `minecraft:jungle_tree`, 2 `minecraft:birch`, 3 `twilightforest:tree/twilight_oak_tree`, 4 `twilightforest:tree/rainbow_oak`, else `minecraft:oak` (`spawnATree`); only when (8,1,8) is inside the chunk box `sbb`; placement retried up to 100 times with the level random (`world.getRandom()`, the WorldGenRegion random, not the piece random), stopping at the first success; 4 corner statues; 4 exits |
| `twilightforest:TFSFo` | StrongholdFoundryComponent | (-4, -20 / -6 / -13, 0; 18 x 26 x 18) by entry level | lava, ore column (coal/iron/gold/redstone/copper/diamond/emerald ores, glowstone) with rand(9)/rand(13) choices; 3 exits |
| `twilightforest:TFTreaR` | StrongholdTreasureRoomComponent | (-4, -1, 0; 9 x 7 x 18) | `minecraft:spawner` of `twilightforest:helmet_crab` at (4,1,4) and (4,4,15); chests at (2,4,13) WEST and (6,4,13) EAST with `twilightforest:stronghold_room`; iron bars; 5 wall statues |
| `twilightforest:TFSBR` | StrongholdBossRoomComponent | (-13, -1, 0; 27 x 7 x 27) | `spawnListIndex = Integer.MAX_VALUE` (no controlled spawns); corner pillars with stair decorations, 6 sarcophagi (torches or fence posts, rand(7)), iron-bar doorway (12,1,3)-(14,4,3); **`twilightforest:knight_phantom_boss_spawner` at (13,2,13)** |
| `twilightforest:TFSUA` | StrongholdUpperAscenderComponent | (-2, -1, 0; 5 x 10 x 10) exiting top when y < sea level + 5, else (-2, -6, 0; ...) | stone brick stairs; upper child at (2, exitTop ? 6 : 1, 10) |
| `twilightforest:TFSUCo` | StrongholdUpperCorridorComponent | (-2, -1, 0; 5 x 5 x 9) | upper child NONE (2,1,9) |
| `twilightforest:TFSULT` | StrongholdUpperLeftTurnComponent | (-2, -1, 0; 5 x 5 x 5) | upper child CCW90 (5,1,2) |
| `twilightforest:TFSURT` | StrongholdUpperRightTurnComponent | (-2, -1, 0; 5 x 5 x 5) | upper child CW90 (-1,1,2) |
| `twilightforest:TFSUTI` | StrongholdUpperTIntersectionComponent | (-2, -1, 0; 5 x 5 x 5) | upper children CCW90 (5,1,2) and CW90 (-1,1,2) |
| `twilightforest:TFSShield` | StrongholdShieldStructure | none (`generateBoundingBox` returns null) | legacy piece type only deserialised from old saves (never generated in 4.8): shells its box with `stronghold_shield` facing inward; register it so old chunks load |

Lower piece selection (`StrongholdPieces`, weights / instance limit / minimum depth): small hallway 40/0, left turn 20/0,
crossing 10/4, right turn 20/0, dead end 5/0, balcony room 10/3 (depth 2), training room 10/2, small stairs 10/0,
treasure corridor 5/0, atrium 5/2 (depth 3), foundry 5/1 (depth 4), treasure room 5/1 (depth 4), boss room 15/1
(depth 4). Up to 5 rolls: a roll that hits a piece not deep enough, exhausted or equal to the last piece made
(static `lastPieceMade`, shared across starts as in TF) aborts the roll; the piece must not collide; fallback is a
dead end if it fits. Limit: depth <= 30 and within 75 blocks (x and z) of the entrance's min corner; a new piece
whose entry point lies on another piece's wall (not on its corners) breaks into it instead (door added on both).
Upper pieces (`addNewUpperComponent`): rand(5) -> 1 left turn, 2 right turn, 3 corridor, 4 ascender, else T
intersection; depth <= 100, within 48 blocks; skipped when colliding.

Walls: `placeStrongholdWalls` fills the piece shell (cobblestone where air on the boundary, stronghold stones on
floor/ceiling, decorator random blocks on walls, dirt walls kept); upper pieces use `placeUpperStrongholdWalls`
(only on base stone / dirt, or air with 1/3 chance above knight/stronghold stones). Decorator: block
`twilightforest:underbrick`, accent `twilightforest:cracked_underbrick`, fence `minecraft:cobblestone_wall`, stairs
`minecraft:stone_brick_stairs`, pillar `minecraft:mossy_stone_bricks`, platform `minecraft:smooth_stone_slab[type=top]`,
random blocks = knight stones selector. Statues (`placeCornerStatue`, `placeWallStatue`): 5-high pillar, oak fence
antlers, stair arms and feet, cobblestone wall sword. Doors (`placeDoorwayAt`): cobblestone-wall frame 5 wide x 4
high with a 3 x 4 opening. These are exact translations (R19), golden hash per piece (the atrium tree grows with the
level random, not the piece random: the golden hash seeds that random for the test, as Java derives the WorldGenRegion random
from the world seed and the centre chunk, or masks the tree volume above (8,1,8)).

Protected pieces (`isComponentProtected` true for everything except the access chamber and the upper pieces) get
controlled-spawn index 0 and F6 protection; the upper ruins are not protected.

Strings: `structure.twilightforest.knight_stronghold` "Knight Stronghold".

## 2. Blocks

### 2.1 `twilightforest:trophy_pedestal` (TrophyPedestalBlock)

| Property | Value |
|---|---|
| Material | map colour STONE, sound STONE, strength 2.0 / 2000.0, requires correct tool (pickaxe), push reaction BLOCK (NORMAL once active) |
| States | `active` (boolean, default false), `waterlogged` (boolean) |
| Shape | union of boxes (pixels): (1,0,1)-(15,3,15), (2,3,2)-(14,13,14), (1,13,1)-(15,16,15), corners (1,12,1)-(4,13,4), (12,12,1)-(15,13,4), (1,12,12)-(4,13,15), (12,12,12)-(15,13,15) |
| Breaking | unbreakable (destroy progress -1) until `active` |
| Comparator | analog output = comparator value of the trophy block above (U5's trophies via `gf::boss::trophyVariant(blockId).comparator`), else 0 |
| Tags | `#minecraft:mineable/pickaxe`, `#twilightforest:common_protections` (F1 tags) |
| Loot | `twilightforest:blocks/trophy_pedestal` (survives explosion, drops itself) |
| Block item | `twilightforest:trophy_pedestal` |

Activation (`neighborChanged`, server): first update comparator neighbours; if not active and the block above is in
`#twilightforest:trophy_pedestal_activation_blocks` (U5's tag: naga, lich, minoshroom, hydra, knight phantom, ur-ghast,
alpha yeti, snow queen trophies): when progression is enforced (F10 gamerule `tfEnforcedProgression`), activate only if
some player within 16 blocks has `twilightforest:progress_lich` or is in creative (instabuild), and show every
ineligible player within 16 the action-bar message `misc.twilightforest.pedestal_ineligible` ("You are unworthy.");
when not enforced, always activate. Activation: set `active=true`, destroy (no drops) every
`twilightforest:stronghold_shield` within +-5 on each axis, play `block.twilightforest.trophy_pedestal.activate`
("Trophy Pedestal accepts trophy") at volume 4.0, pitch 0.1. Then, in every case where a trophy is on top and the
pedestal was not yet active, every server player within 16 gets the trigger `twilightforest:placed_on_trophy_pedestal`
(`gf::progression::trigger`, R21) and the stat `twilightforest:trophy_pedestals_activated` + 1 (`gf::stats::award`).

Bedrock: custom blocks get no neighbour updates; detect the trophy placement through
`gf::interactions::onPlace(Match::tag("twilightforest:trophy_pedestal_activation_blocks"))` on the block above (and
F11 `onNeighborChanged` if available), and expose the comparator value through `gf::redstone::registerAnalog`.
Waterlogging needs the Bedrock liquid-container component or the F11 waterloggable family.

### 2.2 `twilightforest:stronghold_shield` (StrongholdShieldBlock)

| Property | Value |
|---|---|
| Material | map colour STONE, sound METAL, strength -1.0 / 6000000.0, requires correct tool, no loot table, push reaction BLOCK |
| States | `facing` (6 directions, default down); placement: nearest looking direction, vertical ones reversed |
| Breaking | only from the "inside" face: for facing up/down the hit face must equal `facing`, otherwise the hit face must equal `facing.opposite`; then progress = `digSpeed(stone) / 1.5 / 100` per tick (about 3.3 x slower than stone); from any other face unbreakable. Ray from the player with reach + 1 |
| Entities | `canEntityDestroy` false (withers, dragons, explosions by entities cannot break it) |
| Tags | `#minecraft:mineable/pickaxe`, `#twilightforest:common_protections`, `#c:relocation_not_supported` |
| Block item | `twilightforest:stronghold_shield` (creative only; no loot) |

Bedrock: per-face hardness needs the F6/F11 interaction hook (`onStartDestroy` returns Deny unless the face matches,
and a custom break-time); vanilla Bedrock has no per-face destroy speed, so the server computes the time.

## 3. Boss: `twilightforest:knight_phantom` (KnightPhantom extends BaseTFBoss), six at once

Sources: `entity/boss/KnightPhantom.java`, `entity/ai/goal/Phantom{WatchAndAttack,UpdateFormationAndMove,AttackStart,ThrowWeapon}Goal.java`,
`entity/ai/control/NoClipMoveControl.java`, `block/entity/spawner/KnightPhantomSpawnerBlockEntity.java`,
`client/renderer/entity/KnightPhantomRenderer.java`, `client/model/entity/KnightPhantomModel.java`.

| Field | Value |
|---|---|
| Category / size | `MONSTER`, registered 1.25 x 2.5; dynamic: 1.25 x 2.5 (fixed) while not charging, **1.75 x 4.0** while charging; after the dying ascent 0 x 0 (`UNTOUCHABLE`) |
| Attributes | max_health 35.0, attack_damage 1.0 |
| Modifiers | not charging: armor x(1 + 4.0) multiply-total (`twilightforest:inactive_armor_boost`); charging: attack + 7 (`twilightforest:charging_attack_boost`) |
| Equipment | `twilightforest:knightmetal_sword` (main hand, replaced by number), `twilightforest:phantom_chestplate`, `twilightforest:phantom_helmet`; plus difficulty enchantments. Hard: knight number 5 gets `twilightforest:knightmetal_shield` in the off hand |
| Physics | `noPhysics` true (toggled by the formation goal), NoClipMoveControl, FlyingMob-style travel copy, no fall damage, never on ladders, in-wall damage ignored |
| XP | 93 each |
| Boss bar | one shared bar shown by knight 0, title `entity.twilightforest.knight_phantom.plural` ("Knight Phantoms"), colour 0x86BF37; progress = sum of alive knights' health / (their max + max x missing knights) |
| Home | restriction point = spawner position, radius 30, home structure `twilightforest:knight_stronghold` |
| Death chest | `twilightforest:dark_chest` |
| Spawner block | `twilightforest:knight_phantom_boss_spawner` |
| Saved | `TotalKnownKnights`, `MyNumber`, `Formation` (ordinal), `TicksProgress`, `IsItOver` |
| Spawn egg | `twilightforest:knight_phantom_spawn_egg` 0xa6673b / 0xd3e7bc; placement ON_GROUND `Mob::checkMobSpawnRules` |

Goals: 0 PhantomWatchAndAttack; 1 PhantomUpdateFormationAndMove; 2 PhantomAttackStart (LOOK); 3 PhantomThrowWeapon
(LOOK). Target: 0 NearestAttackable Player (must see = false).

- **Numbering** (`updateMyNumber`, on first server tick and on formation changes): numbers of other alive knights
  within 64; take 0 if free (and copy home from knight 0), else the smallest unused in 1..count; `totalKnownKnights`
  = max seen. Setting number 0 adds every player within 64 to the boss bar. Weapon by `number % 3`: 0 knightmetal
  sword, 1 knightmetal axe, 2 knightmetal pickaxe.
- **Formations** (duration ticks): HOVER 90, LARGE_CLOCKWISE 180, SMALL_CLOCKWISE 90, LARGE_ANTICLOCKWISE 180,
  SMALL_ANTICLOCKWISE 90, CHARGE_PLUSX 180, CHARGE_MINUSX 180, CHARGE_PLUSZ 180, CHARGE_MINUSZ 180,
  WAITING_FOR_LEADER 10, ATTACK_PLAYER_START 50, ATTACK_PLAYER_ATTACK 50. `switchToFormation` resets progress,
  renumbers, and sets charging = formation is ATTACK_PLAYER_START or ATTACK_PLAYER_ATTACK.
- **UpdateFormationAndMove** (always): `noPhysics = progress % 20 != 0` (physics one tick in 20), progress + 1, at the
  formation's duration switch: ATTACK_PLAYER_START -> ATTACK_PLAYER_ATTACK; ATTACK_PLAYER_ATTACK -> WAITING_FOR_LEADER
  if more than one knight nearby, else random weapon (rand(3) sword/axe/pick) and ATTACK_PLAYER_START;
  WAITING_FOR_LEADER -> copy the formation and progress of `nearbyKnights.get(1)` if more than one, else
  ATTACK_PLAYER_START; other formations: only the lowest-numbered knight (leader) picks rand(8): 0, 7 SMALL_CLOCKWISE;
  1, 2 SMALL_ANTICLOCKWISE; 3 CHARGE_PLUSX; 4 CHARGE_MINUSX; 5 CHARGE_PLUSZ; 6 CHARGE_MINUSZ, broadcasts it to every
  non-charging knight, and if nobody charges, a random knight goes ATTACK_PLAYER_START. Destination (move speed 2
  when charging else 1), home H: circles: angle = progress x 2 (negated anticlockwise) + 60 x number, radius 8.5
  (large) / 2.5 (small), y = H.y + cos(progress / 7 + number); charges: offset0 = number x 3 - 7.5, offset1 = -7 for
  the first 60 ticks then -7 + (progress - 60) / 120 x 14 (negated for MINUS), along the axis; HOVER and
  ATTACK_PLAYER_START: stay at the last position clamped to 8.5 blocks (XZ) from H, y as above;
  ATTACK_PLAYER_ATTACK: sword knights fly to the charge position, others hover; default (WAITING): H.
- **AttackStart** (ATTACK_PLAYER_START with a target): charge position = the target's previous-tick block position
  while the knight is inside its home area, else the home point.
- **ThrowWeapon** (ATTACK_PLAYER_ATTACK with a target, every 4 progress ticks): axe knights throw one
  `twilightforest:thrown_wep` holding `twilightforest:knightmetal_axe` from 1 block along the body yaw at 0.82 x height
  toward the target's centre (speed 0.75, inaccuracy 1.0), sound `entity.twilightforest.knight_phantom.axe` (vol 1,
  pitch 0.4 +- 0.2), game event PROJECTILE_SHOOT; pick knights throw 8 `knightmetal_pickaxe` weapons radially (45
  degrees apart, speed 0.5, inaccuracy 1.0, damage 3, gravity 0.015), sound `entity.twilightforest.knight_phantom.pickaxe`.
- **WatchAndAttack** (with a target): look at it (10, 500); melee every 20 ticks when visible, within 2 blocks and
  vertically overlapping (`doHurtTarget`: `twilightforest:haunt` damage = attack damage via the custom-source helper);
  shield knights raise the shield (off hand) while guarding outside ATTACK_PLAYER_ATTACK; guard toggles every 180
  ticks of cooldown counting.
- `hurt`: a blocked hit (shield) plays `minecraft:item.shield.block` (vol 1, pitch 0.8 + r x 0.4) and is cancelled.
- `knockback`: halve motion, push 0.2 away and 0.2 up, cap y at 0.4.
- Client: while charging and still visible, 4 item particles (phantom helmet or knightmetal sword, falling 0.1) and 4
  smoke particles per tick.

Death and conquest (`postmortem`, `tickDeath`, `tickDeathAnimation`):

- While other knights live: the dead knight's loot (table `twilightforest:entities/knight_phantom`) plus its buffered
  items are shuffled into the **first remaining knight's** 27-slot death buffer (overflow dropped as items with
  extended lifetime and no pickup delay); survivors renumber.
- The last knight: boss bar 0; loot = its table + `twilightforest:entities/knight_phantom_defeated` (the
  `twilightforest:knight_phantom_trophy`), into its own buffer; the killing player and every server player within 32
  of the treasure position get the trigger `twilightforest:kill_all_phantoms`; the stronghold is marked conquered
  (F6 `setConquered`); all dying knights within 64 restart their death animation (deathTime 1 synced); `IsItOver`
  true. Only then is the `dark_chest` placed at the boss chest location on removal.
- Dying: ascend at (0, 0.015, 0) per tick while visible (`hasYetToDisappear`: alive, or deathTime <= 18 and (it is
  over or other knights live)); at deathTime 18 the hitbox becomes 0 x 0; removal when deathTime >= 18 + 70 and no
  knights remain nearby. Client effects: 20-particle poof at 18; floating `poof`/`smoke` and item particles before;
  smoke swirl while others live; when every knight is past 18, a smoke trail from each knight to the chest location
  over 70 ticks. Renderer hides the knight once it has disappeared; no death flip.
- Boss bar: hidden for everyone when a knight dies while others live (knight 0 re-adds players).

Render: humanoid `phantomskeleton.png`, item in hand + armour layers, scale 1.8 while charging else 1.2, shadow 0.625;
model shows the charging pose. Sounds: ambient `entity.twilightforest.knight_phantom.ambient` ("Knight Phantom gasps"),
hurt `entity.twilightforest.knight_phantom.hurt`, death `entity.twilightforest.knight_phantom.death`.

Spawner logic (R20; `KnightPhantomSpawnerBlockEntity`): `boss::registerSpawner({blockId:
twilightforest:knight_phantom_boss_spawner, entityId: twilightforest:knight_phantom, range: 9, particle:
twilightforest:ominous_flame, spawnFn})` where the player-in-range check is "nearest player within 9 (spectators
excluded) whose y > spawner y - 2", and spawnFn places knights `spawned..5` at angle `60 x i` degrees, 4 blocks from
the block centre at the spawner's y, random yaw, `finalizeSpawn(SPAWNER)`, Hard: knight 5 gets the knightmetal shield,
home = spawner, `setNumber(i)`; succeeds (spawner destroyed) only when all 6 spawned.

Loot `twilightforest:entities/knight_phantom` (3 pools, binomial rolls with p 0.17 and n from
`twilightforest:looting_rolls` over `twilightforest:multiplayer_rolls`): pool 1 n = 4 + 1.5 per extra player:
knightmetal sword / pickaxe / axe enchanted with levels 20 (`#minecraft:on_random_loot`); pool 2 n = 2 + 1.0: phantom
helmet / chestplate enchanted 20; pool 3 n = 1 + 0.5: phantom helmet / chestplate enchanted 30.
`twilightforest:entities/knight_phantom_defeated`: 1 x `twilightforest:knight_phantom_trophy`.

## 4. Thrown weapon `twilightforest:thrown_wep` (ThrownWep)

Source: `entity/projectile/ThrownWep.java`, `client/renderer/entity/ThrownWepRenderer.java`.

- Registered with `make(...)`: category MISC, 0.5 x 0.5, tracking 80, interval 3, no spawn egg (colours 0).
- Synced: `DATA_ITEMSTACK` (rendered item), `DATA_VELOCITY` (gravity, default 0.001). Damage default 6.
- Hit entity: ignores knight phantoms and its owner; otherwise (server) deals the damage as
  `twilightforest:thrown_pickaxe` when holding a knightmetal pickaxe else `twilightforest:thrown_axe` (no attacker
  entity). Any hit: entity event 3 (8 `large_smoke`) and discard. Pickable, pick radius 1.0 (can be hit).
- Render: the held item at 1.25 x scale, spinning 10 degrees per tick around Z, ground display context.

## 5. Goblin knights

### 5.1 `twilightforest:lower_goblin_knight` (LowerGoblinKnight)

Source: `entity/monster/LowerGoblinKnight.java`, `entity/ai/goal/RiderSpearAttackGoal.java`, model `LowerGoblinKnightModel`, renderer `TFBipedRenderer` (`doublegoblin.png`, shadow 0.625).

| Field | Value |
|---|---|
| Size | 0.7 x 1.1 |
| Attributes | max_health 20.0, movement_speed 0.28, attack_damage 4.0 |
| Armour | synced `ARMOR` (starts true): armor + 17 (`twilightforest:armor_boost`); saved `hasArmor` |
| Rider | `finalizeSpawn` spawns a `twilightforest:upper_goblin_knight` at y + 1 (finalized NATURAL with the same group data) riding it; passenger attachment (0, height x 0.91, 0); the rider's body/head/yaw follow the mount |
| Spawn egg | `twilightforest:lower_goblin_knight_spawn_egg` ("Goblin Knight Spawn Egg") 0x566055 / 0xd3e7bc |

Goals: 0 RiderSpearAttack (MOVE, LOOK: holds still while its upper knight's spear timer is in 1..59 and the target is
valid); 1 Float; 3 MeleeAttack 1.0 (not while the rider's spear timer > 0); 6 WaterAvoidingRandomStroll; 7 LookAtPlayer
8; 7 RandomLookAround. Targets: 1 HurtBy; 2 NearestAttackable Player (must see false).

- Melee while ridden is delegated to the rider (`rider.doHurtTarget`).
- Hurt: angle `difference = |(yBodyRot - angle) % 360|` with `angle = atan2(dz, dx) deg - 90` from attacker to it;
  hits from the front (150..230) go to the rider's shield first (`takeHitOnShield`, cancels when it returns true);
  hits from behind (> 300 or < 60) break its armour (entity event 5: three iron-chestplate break effects, armour
  modifier removed).
- Sounds: muffled variants while armoured: `entity.twilightforest.goblin_knight.muffled.ambient`,
  `entity.twilightforest.goblin_knight.muffled.hurt`, `entity.twilightforest.goblin_knight.muffled.death`; without
  armour `entity.twilightforest.goblin_knight.ambient`, `.hurt`, `.death`.
- Model: the tunic shows while armoured; unarmoured and unridden the arms swing freely.

Loot `twilightforest:entities/lower_goblin_knight`: `twilightforest:armor_shard` 0..2 + looting 0..1.

### 5.2 `twilightforest:upper_goblin_knight` (UpperGoblinKnight)

Source: `entity/monster/UpperGoblinKnight.java`, `entity/ai/goal/HeavySpearAttackGoal.java`, `client/renderer/entity/UpperGoblinKnightRenderer.java`, `client/model/entity/UpperGoblinKnightModel.java`.

| Field | Value |
|---|---|
| Size | 1.1 x 1.3; **no spawn egg** (colours 0) |
| Attributes | max_health 30.0, movement_speed 0.28, attack_damage 8.0 |
| Synced | `DATA_EQUIP` byte (bit 1 armour, bit 2 shield; both start set), `SHIELD_DISABLED` boolean |
| Armour | armor + 20 (`twilightforest:armor_boost`) while armoured; saved `hasArmor`, `hasShield` |
| Spear | `heavySpearTimer` (60 at start); damage modifier `twilightforest:spear_attack_boost` +12 x base (multiplied-base) while the timer > 0 |

Goals: 0 HeavySpearAttack (MOVE, LOOK; active while the timer is 1..59 with a valid target; at timer 25 calls
`landHeavySpearAttack`); 1 Float; 3 MeleeAttack 1.0 (not during a spear attack); 6 WaterAvoidingRandomStroll; 7
LookAtPlayer 8; 7 RandomLookAround. Targets: 1 HurtBy; 2 NearestAttackable Player (must see false).

- `doHurtTarget`: no attack while the timer runs; 50 %: start the spear (timer 60, entity event 4 so clients animate)
  and do not hit; else swing and vanilla melee.
- Spear landing: point 1.25 ahead along the look at the feet (- 0.75 when riding); 50 `large_smoke` particles sent to
  trackers; vanilla melee (`Mob.doHurtTarget`) on every entity in the 3 x 3 x 3 box except its mount; if any,
  `minecraft:entity.player.attack.crit`; game event HIT_GROUND.
- Server tick: re-enable the shield after 100 ticks disabled; copy the mount's target when it has none; dismounted
  with a shield -> the shield breaks; maintain the spear damage modifier.
- Hurt: in-wall damage ignored while riding; front hits (150..230) with a shield go to `takeHitOnShield`; other hits
  with a shield damage it with chance 1/2; rear hits (> 300 or < 60) break the armour.
- `takeHitOnShield`: disabled shield -> not blocked; attacker holding an axe -> shield disabled (100 ticks), sound
  `minecraft:item.shield.break`, blocked; damage > 10 -> `damageShield`, else sound `minecraft:item.shield.block`;
  knock back the mount (or itself) by (dx / 4, dz / 4) away from the attacker and remember it as attacker; blocked.
  `damageShield`: sound `minecraft:entity.zombie.attack_iron_door` (vol 0.25, pitch 0.25), 3 hits break the shield
  (entity event 5).
- While the shield is disabled: `splash` particles at eye height each tick.
- Sounds: `entity.twilightforest.goblin_knight.ambient` ("Goblin Knight chuckles"), `entity.twilightforest.goblin_knight.hurt`
  ("Goblin Knight screeches in pain"), `entity.twilightforest.goblin_knight.death` ("Goblin Knight groans in agony").
- Render (`doublegoblin.png`): pitch during the spear by elapsed `t = 60 - timer`: 0..10 rock back `t x 3`; 10..30 hold
  30; 30..33 slam `(t - 30) x -25 + 30`; 33..50 hold -45; 50..60 return `(10 - (t - 50)) x -4.5`; right arm follows;
  breastplate and shield parts visible by the flags; disabled shield lowers the arm.

Loot `twilightforest:entities/upper_goblin_knight`: `twilightforest:armor_shard` 0..2 + looting 0..1.

### 5.3 `twilightforest:blockchain_goblin` (BlockChainGoblin, multipart)

Source: `entity/monster/BlockChainGoblin.java`, `entity/SpikeBlock.java`, `entity/ai/goal/ThrowSpikeBlockGoal.java`, `client/renderer/entity/BlockChainGoblinRenderer.java`.

| Field | Value |
|---|---|
| Size | 0.9 x 1.4 |
| Attributes | max_health 20.0, movement_speed 0.28, attack_damage 8.0, armor 11.0 |
| Synced | `DATA_CHAINLENGTH` byte (length x 127), `DATA_CHAINPOS` byte (angle / 360 x 255), `IS_THROWING` boolean |
| Part | `SpikeBlock` 0.75 x 0.75: not pickable, not pushable, cannot be hurt, no add-entity packet; part id = goblin id + 1 |
| Spawn egg | `twilightforest:blockchain_goblin_spawn_egg` 0xd3e7bc / 0x1f3fff |

Goals: 0 Float; 1 AvoidAnyEntity PrimedTnt 2.0 (1.0 / 2.0); 4 ThrowSpikeBlock; 5 MeleeAttack 1.0; 6 WaterAvoidingRandomStroll;
7 LookAtPlayer 8; 7 RandomLookAround. Targets: 1 HurtBy; 2 NearestAttackable Player.

- Chain swing: angle + 16 degrees per tick (mod 360); length 0.9 while swinging (swinging arm or has a target and no
  recoil) else 0.3; ball at `pos + (cos a, 1.5 - length / 4, sin a) x length`, yaw = angle.
- Throw (ThrowSpikeBlockGoal, MOVE + LOOK): target within distance^2 42, cooldown done, visible, chance 1/56; start:
  throwing, cooldown `100 + rand(100)`, game event PROJECTILE_SHOOT; continue while the throw length > 0. Throw length
  +0.5 per tick while throwing (max 6, then stops throwing), -1.5 per tick otherwise; while > 0 the ball sits along the
  view vector at that length at eye height.
- Collisions (server, alive, throwing or swinging): pushable entities within the ball box inflated (0.2, 0, 0.2) are
  pushed and, if living (not the goblin), hit with the vanilla melee (`Mob.doHurtTarget`, attack 8); on a hit: push up
  0.4, sound `item.twilightforest.block_and_chain.hit` (U8's event, vol 1, pitch 1), game event PROJECTILE_LAND, recoil
  40 ticks, stop throwing. A thrown ball in a wall stops the throw, plays `item.twilightforest.block_and_chain.collide`
  (vol 0.65, pitch 0.75) and HIT_GROUND.
- Direct melee (`doHurtTarget`): `twilightforest:spiked` damage (indirect: direct = the spike block, attacker = goblin)
  through the custom-source helper.
- Goblin dead: the ball falls (0.04 per tick) and plays the collide sound once on landing.
- Render: goblin `blockgoblin.png` (humanoid, shadow 0.4), spike block model `block_and_chain.png` at the ball, four
  chain links at 0, 0.25, 0.5, 0.75 of the eye-to-ball vector (y halved).
- Sounds: `entity.twilightforest.blockchain_goblin.ambient` ("Block and Chain Goblin chuckles"),
  `entity.twilightforest.blockchain_goblin.hurt` ("... screams"), `entity.twilightforest.blockchain_goblin.death`.

Loot `twilightforest:entities/blockchain_goblin`: `twilightforest:armor_shard` 0..2 + looting 0..1.

## 6. `twilightforest:helmet_crab` (HelmetCrab)

Source: `entity/monster/HelmetCrab.java`, `client/renderer/entity/HelmetCrabRenderer.java`, `client/model/entity/HelmetCrabModel.java`.

| Field | Value |
|---|---|
| Size | 0.8 x 1.1, eye height 0.45 |
| Attributes | max_health 13.0, movement_speed 0.28, attack_damage 3.0, armor 6.0 |
| Blue | synced `BLUE`, set at spawn with chance 1/10000, saved `blue`; texture `helmetcrabblue.png` instead of `helmetcrab.png` |
| Helmet rotation | client-side `helmetRot` follows the previous head yaw (wrapped) |
| Spawn egg | `twilightforest:helmet_crab_spawn_egg` 0xfb904b / 0xd3e7bc |

Goals: 0 Float; 1 AlwaysWatchTarget (F8 `goals::alwaysWatchTarget`); 2 LeapAtTarget 0.28; 3 MeleeAttack 1.0; 6
RandomStroll 1.0; 7 LookAtPlayer 8; 7 RandomLookAround. Targets: 1 HurtBy; 2 Player. Render shadow 0.625, no death flip.

Sounds: `entity.twilightforest.helmet_crab.ambient` ("Helmet Crab snips"), `entity.twilightforest.helmet_crab.hurt`,
`entity.twilightforest.helmet_crab.death`, `entity.twilightforest.helmet_crab.step` (vol 0.15).

Loot `twilightforest:entities/helmet_crab`: `twilightforest:armor_shard` 0..2 + looting 0..1; pool 2 with chance 0.5
unenchanted (0.6 + 0.1 per looting level above the first when enchanted): 1 `minecraft:cod`, smelted when on fire or
killed with a smelting weapon, + looting 0..1.

Treasure-room spawners (`minecraft:spawner` with `twilightforest:helmet_crab`) are placed by U25's own piece.

## 7. Loot tables (chests)

`twilightforest:stronghold_cache` (dead ends, small stairs, treasure corridor):
- pool 1, 4 rolls: `minecraft:stick` 1-12, `minecraft:coal` 1-12, `minecraft:arrow` 1-12, `twilightforest:maze_wafer`
  1-9, `minecraft:blue_wool`, `minecraft:iron_ingot` 1-2 (weight 1 each);
- pool 2, 2 rolls: `minecraft:bucket`, `minecraft:iron_ingot` 1-6, `twilightforest:ironwood_ingot` 1-6,
  `twilightforest:firefly` 1-5, `twilightforest:charm_of_keeping_1`, `twilightforest:armor_shard` 1-3;
- pool 3, 1 roll: weight 75 each: `twilightforest:knightmetal_ingot` 1-8, `minecraft:bow` enchanted 20,
  `minecraft:iron_sword` enchanted 20, `twilightforest:ironwood_sword` enchanted 15, `twilightforest:steeleaf_sword`
  enchanted 10; weight 25 each: enchanted books bane of arthropods 4, sharpness 4, smite 4, unbreaking 2, protection 3,
  projectile protection 3, feather falling 3, and `minecraft:book` with `enchant_randomly` from
  `twilightforest:destruction` (U10's enchantment set).

`twilightforest:stronghold_room` (treasure room):
- pool 1, 4 rolls: `twilightforest:useless` (F10 shared table) weight 25; weight 75: `minecraft:iron_ingot` 1-4,
  `minecraft:gunpowder` 1-4, `minecraft:milk_bucket`, `twilightforest:maze_wafer` 1-12,
  `twilightforest:ironwood_ingot` 1-4, `twilightforest:firefly` 1-5;
- pool 2, 2 rolls: `twilightforest:steeleaf_ingot` 1-6, `twilightforest:charm_of_life_1`, steeleaf helmet,
  chestplate, leggings, boots, pickaxe, `twilightforest:ironwood_chestplate`, `twilightforest:ironwood_sword`;
- pool 3, 1 roll: `twilightforest:ironwood_sword` enchanted 25, `twilightforest:steeleaf_sword` 20,
  `minecraft:iron_sword` 30, `minecraft:bow` 30, `minecraft:diamond_sword` 15, `minecraft:book` enchant_randomly
  destruction, `twilightforest:maze_map_focus`.

Chests are placed with `gf::loot::placeContainer` (F10); trapped variants use `minecraft:trapped_chest`.

## 8. Damage types (owned by U25)

| Id | message_id (en_us) | Exhaustion | Tags | Used by |
|---|---|---|---|---|
| `twilightforest:axing` | `twilightforest.axing` ("%1$s was chopped up by %2$s") | 0.1 | `#neoforge:is_physical` | minotaur and minoshroom melee (U23, by string) |
| `twilightforest:spiked` | `twilightforest.spiked` ("%1$s was skewered by %2$s") | 0.1 | `#neoforge:is_physical` | blockchain goblin melee |
| `twilightforest:thrown_axe` | `twilightforest.thrownAxe` ("%1$s was decapitated by a thrown axe") | 0.0 | `#minecraft:is_projectile`, `#neoforge:is_physical` | thrown weapon (axe) |
| `twilightforest:thrown_pickaxe` | `twilightforest.thrownPickaxe` ("%1$s was decapitated by a thrown pickaxe") | 0.0 | `#minecraft:is_projectile`, `#neoforge:is_physical` | thrown weapon (pickaxe) |

All: effects hurt, scaling `when_caused_by_living_non_player`. Death messages are F10's lang (R9).

## 9. Progression, stat and triggers

- Trigger `twilightforest:placed_on_trophy_pedestal` (SimpleAdvancementTrigger, TF `TFAdvancements`): fired by the
  pedestal; consumed by U14's `twilightforest:progress_trophy_pedestal` (with `progress_lich` already done).
- Trigger `twilightforest:kill_all_phantoms`: fired when the last phantom dies; consumed by
  `twilightforest:progress_knights` (with the knight phantom trophy and `progress_trophy_pedestal`).
- Stat `twilightforest:trophy_pedestals_activated` (custom stat, format default; lang
  `stat.twilightforest.trophy_pedestals_activated`): +1 per player per pedestal activation event.
- Triggers are registered through `gf::progression::onTrigger` by U14; U25 only fires them by string (R21).

## 10. Sound events (owned by U25)

| Event | Subtitle |
|---|---|
| `block.twilightforest.trophy_pedestal.activate` | Trophy Pedestal accepts trophy |
| `entity.twilightforest.blockchain_goblin.ambient` | Block and Chain Goblin chuckles |
| `entity.twilightforest.blockchain_goblin.hurt` | Block and Chain Goblin screams |
| `entity.twilightforest.blockchain_goblin.death` | Block and Chain Goblin dies |
| `entity.twilightforest.goblin_knight.ambient` | Goblin Knight chuckles |
| `entity.twilightforest.goblin_knight.hurt` | Goblin Knight screeches in pain |
| `entity.twilightforest.goblin_knight.death` | Goblin Knight groans in agony |
| `entity.twilightforest.goblin_knight.muffled.ambient` | Muffled Goblin Knight chuckles |
| `entity.twilightforest.goblin_knight.muffled.hurt` | Muffled Goblin Knight screeches in pain |
| `entity.twilightforest.goblin_knight.muffled.death` | Muffled Goblin Knight groans in agony |
| `entity.twilightforest.helmet_crab.ambient` | Helmet Crab snips |
| `entity.twilightforest.helmet_crab.hurt` | Helmet Crab hurts |
| `entity.twilightforest.helmet_crab.death` | Helmet Crab dies |
| `entity.twilightforest.helmet_crab.step` | footsteps |
| `entity.twilightforest.knight_phantom.ambient` | Knight Phantom gasps |
| `entity.twilightforest.knight_phantom.hurt` | Knight Phantom hurts |
| `entity.twilightforest.knight_phantom.death` | Knight Phantom dies |
| `entity.twilightforest.knight_phantom.axe` | Knight Phantom throws axe |
| `entity.twilightforest.knight_phantom.pickaxe` | Knight Phantom throws pickaxe |

## 11. Spawn eggs and placements

Eggs: `twilightforest:knight_phantom_spawn_egg`, `twilightforest:lower_goblin_knight_spawn_egg`,
`twilightforest:blockchain_goblin_spawn_egg`, `twilightforest:helmet_crab_spawn_egg`. Placements: blockchain goblin,
upper and lower goblin knights, helmet crab ON_GROUND `Monster::checkMonsterSpawnRules`; knight phantom ON_GROUND
`Mob::checkMobSpawnRules`. None spawn naturally outside the stronghold's controlled spawns and spawners.

## 12. Bedrock adaptation notes

- Upper/lower goblin knights are a rider/mount pair (`SetActorLink`); the shield-angle logic runs in F10
  `combat::onIncoming` for the lower knight.
- The block-and-chain ball is an F8 part (`addPart`) with no add packet for vanilla Java; on Bedrock the part is a
  separate lightweight actor or a bone animated from the synced angle/length properties (client mod); collisions
  stay server-side.
- Knight phantoms' dynamic size changes need `minecraft:collision_box` swaps through properties (or the F8 size API).
- Per-face shield breaking and unbreakable pedestals: F6/F11 interaction hooks with the computed break time.

## 13. Acceptance (selftests `stronghold.*`, dx 2500-2599)

Per the unit record: golden volume hash of the stronghold (seeded start); placing a trophy on the pedestal activates
it, removes every shield within 5, fires `placed_on_trophy_pedestal` and the stat (and refuses with "You are
unworthy." when progression is enforced and nobody has `progress_lich`); breaking a shield only from its inner face;
phantom fight: 6 knights spawn on a circle with numbers 0-5 and weapons by number, formations cycle, axe/pick
throws, loot passes to the survivors, the last death fires `kill_all_phantoms`, conquers the structure and places the
dark chest with the trophy; goblin knight shield/armour breaking by attack angle; blockchain goblin throw range;
helmet crab blue variant persists.
