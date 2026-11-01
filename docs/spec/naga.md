# U20 `naga`: Naga courtyard and Naga

Behaviour spec translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ `9e40a757`). It is the
checklist the U20 worker implements and the W2 audit (A3) compares against. Numbers are TF's; where a value is
derived, the formula is given. "Bedrock:" paragraphs mark adaptation notes.

| | |
|---|---|
| Unit | U20 `naga` (W1 batch 1) |
| Selftests | `naga.*`, testPos dx `2000-2099` (owners rows `selftest naga.*`, `selftest_dx 2000-2099`) |
| World DB | `gf:naga:*` |
| Net channels | `naga.*` |
| Config | none (TF has no Naga config key; boss drop chests and the multiplayer adjuster are F8's `bosses` section) |
| Contracts | landmark (F6), structure + template (F7), boss + entity (F8), loot (F10), fx (F10) |
| Java sources | `entity/boss/Naga.java`, `entity/boss/NagaSegment.java`, `entity/ai/goal/NagaMovementPattern.java`, `entity/ai/goal/NagaSmashGoal.java`, `entity/ai/control/NagaMoveControl.java`, `block/entity/spawner/NagaSpawnerBlockEntity.java`, `world/components/structures/type/NagaCourtyardStructure.java`, `world/components/structures/courtyard/*.java` (18 files), `client/renderer/entity/Naga{,Segment}Renderer.java`, `client/model/entity/NagaModel.java`, `init/TFEntities.java`, `events/RegistrationEvents.java` |
| Data | `data/twilightforest/loot_table/entities/naga.json`, `worldgen/structure/naga_courtyard.json`, `worldgen/structure_set/naga_courtyard.json`, `twilight/template_definition/courtyard/spawner.json`, `tags/banner_pattern/pattern_item/naga.json` |

Not owned here (referenced by string only, R2): `twilightforest:naga_boss_spawner`, `twilightforest:naga_trophy`,
`twilightforest:naga_scale`, the naga stone / courtyard building blocks and the `naga` banner pattern (U5 building),
`twilightforest:progress_naga` and the other advancements (U14), the hedge block `twilightforest:hedge` (U18),
the structure processors `NagastoneVariants`, `SmoothStoneVariants`, `StoneBricksVariants`, `CobbleVariants` (F7),
the landmark grid, conquest, protection and hints (F6).

## 1. Entity `twilightforest:naga`

Registered in `TFEntities.NAGA`: category MONSTER, size 2.0 x 3.0, eye height 2.0, **fire immune**, tracking range
80, update interval 3, receives velocity updates. Spawn egg `twilightforest:naga_spawn_egg` (primary `0xa4d316`,
secondary `0x1b380b`, name "Naga Spawn Egg"). Spawn placement ON_GROUND / MOTION_BLOCKING_NO_LEAVES /
`Monster::checkMonsterSpawnRules` (only matters for the egg and commands; the boss comes from its spawner).
Display name `entity.twilightforest.naga` = "Naga". Tags: `#twilightforest:bosses` (itself a member of `#c:bosses`),
`minecraft:fall_damage_immune`, `minecraft:freeze_immune_entity_types` (through bosses).

### 1.1 Attributes and constants

| Attribute / constant | Value |
|---|---|
| max health | 120; `finalizeSpawn`: NORMAL adds +80, HARD adds +130 (permanent modifier `twilightforest:difficulty_health_boost`, ADD_VALUE), then heals to full. EASY stays 120. |
| movement speed | 0.5 (DEFAULT_SPEED) + transient modifier `twilightforest:segment_speed_boost` = `MAX_SEGMENTS / segments * 0.02` (ADD_VALUE), recomputed whenever the segment count changes |
| attack damage | 5.0 |
| follow range | 80.0 |
| knockback resistance | 0.25 |
| step height | 2.0 |
| xp reward | 217 |
| `noCulling` | true |
| MAX_SEGMENTS | 12 (body segments, head not included) |
| healthPerSegment | `maxHealth / 10` (computed in the constructor, i.e. from the base 120 -> 12.0) |
| TICKS_BEFORE_HEALING | 600 |
| home bounds | XZ 46, Y 7 (box around the restriction point); `getHomeRadius()` 40 |
| death animation | 24 ticks + 100 ticks of particles (124 total; `BossSpec.deathTicks = 124`) |
| boss bar | colour `0x5E9916`, overlay NOTCHED_10 (the example colour in `Boss.h` is illustrative only) |
| death container | 50/50 `twilightforest:twilight_oak_chest` or `twilightforest:canopy_chest` |
| boss spawner | `twilightforest:naga_boss_spawner` |
| home structure | `twilightforest:naga_courtyard` |

### 1.2 Synced data

`DATA_DAZE` (bool), `DATA_CHARGE` (bool; setting it false also clears STUNLESS), `DATA_STUNLESS` (bool). Client-side
`stunlessRedOverlayProgress` rises by 0.01 per tick while stunless-charging, capped at 0.65, and falls by 0.1 per tick
otherwise; the head model subtracts it from the green and blue tint channels (red tint).

### 1.3 Goals

| Prio | Goal | Notes |
|---|---|---|
| 1 | FloatGoal | |
| 2 | SimplifiedAttackGoal | F8 `goals::simplifiedAttack` |
| 3 | NagaSmashGoal | `canUse`: `horizontalCollision` and mob griefing allowed. `start` (server): for every block in the box `floor(bb.min - 0.75) .. floor(bb.max + 0.75)` with y from `floor(bb.minY + 1.01)` to `floor(bb.maxY)`: leaves (`#minecraft:leaves`) are destroyed without drops; other blocks are destroyed with drops only when `shouldDestroyAllBlocks()` and `EntityUtil.canDestroyBlock` (not unbreakable, not in the boss-immune tag, mob griefing). |
| 4 | NagaMovementPattern | flags MOVE, LOOK; see 1.4 |
| 5 | AttemptToGoHomeGoal(speed 1.0) | on `start` clears the target |
| 8 | RandomStrollGoal(1, interval 1) | only while inside the home area and without a target; destination `DefaultRandomPos.getPos(mob, 30, 7)` |
| target 1 | HurtByTargetGoal | |
| target 2 | NearestAttackableTargetGoal(Player, mustSee false) | `canUse` also requires `areSelfAndTargetInHome(target)` |

`getWalkTargetValue(pos)` returns `Float.MIN_VALUE` outside the home area, 0 inside. `isSteppingCarefully()` false,
`isInLava()` always false, not pushable.

### 1.4 Movement pattern state machine (`NagaMovementPattern`)

`canUse`: has a target and `areSelfAndTargetInHome(target)`. `stop()`: state CIRCLE, counter 15, clockwise false.
Each tick: if the navigation still has an unfinished path, do nothing except `navigation.stop()` when stuck. Otherwise
decrement the counter; at `counter-- <= 0` transition, else run the state:

| State | Per tick | Transition when the counter runs out |
|---|---|---|
| INTIMIDATE | stop navigation; look at target (30/30); `zza = 0.1`. Once per state: `healthRatio = 1 - health/maxHealth - 0.25`; `chance = clamp(healthRatio + difficultyId * 0.05, 0, 0.5)` (difficulty of the local DifficultyInstance: PEACEFUL 0 .. HARD 3); `stunless = random.nextFloat() * 0.75 < chance`; `setStunlessCharging(stunless)`. | flip `clockwise`; if the target's `bb.minY > naga bb.maxY` -> CRUMBLE, else CHARGE (or STUNLESS_CHARGE when stunless) |
| CRUMBLE | stop navigation; `crumbleBelowTarget(2)`, `crumbleBelowTarget(3)` | CHARGE / STUNLESS_CHARGE |
| CHARGE, STUNLESS_CHARGE | move to `findCirclePoint(clockwise, 5, PI)` at speed 1.5; `setCharging(true)` | CIRCLE (`doCircle`) |
| CIRCLE | `setCharging(false)`, `setDazed(false)`; radius 12 when the counter is even, 14 when odd; rotation 1.0 rad; counter 2 -> radius 16; counter 1 -> rotation 0.1; move to `findCirclePoint(clockwise, radius, rotation)` at speed 1.0 | INTIMIDATE (`doIntimidate`) |
| DAZE | stop navigation; `setDazed(true)`, `setCharging(false)` | CIRCLE |

Transitions: `doDaze()` = DAZE, stop navigation, counter `60 + nextInt(40)`. `doCircle()` = CIRCLE, counter
**+=** `10 + nextInt(10)`, reset stun flag. `forceCircle()` = CIRCLE, counter **=** `10 + nextInt(10)`.
`doCrumblePlayer()` = CRUMBLE, stop navigation, counter `20 + nextInt(20)`. `doCharge(stunless)` = counter 2.
`doIntimidate()` = INTIMIDATE, play `entity.twilightforest.naga.rattle` (volume 4.0, voice pitch), game event
ENTITY_ACTION, counter **+=** `15 + nextInt(10)`.

`findCirclePoint(clockwise, radius, rotation)`: angle = `atan2(naga.z - target.z, naga.x - target.x) +/- rotation`
(+ when clockwise); destination = `target + (cos(angle) * radius, -, sin(angle) * radius)`, y =
`min(naga bb.minY, target.y)`.

`crumbleBelowTarget(range)` (needs mob griefing and a target, and `targetY > floor` where floor = `(int) naga bb.minY`,
targetY = `(int) target bb.minY`): `dx = (int) target.x + nextInt(range) - nextInt(range)`, same for z,
`dy = targetY - nextInt(range) + nextInt(range > 1 ? range - 1 : range)`, `dy <= floor -> dy = targetY`; when
`canDestroyBlock`, destroy it with drops and spawn 20 CRIT particles around the naga (gaussian velocity 0.02).

### 1.5 Move control (`NagaMoveControl`)

Before the vanilla move control tick: DAZE -> `xxa = 0`; not CHARGE and not INTIMIDATE -> slither
`xxa = cos(tickCount * 0.3) * 0.6`; otherwise `xxa *= 0.8`.

### 1.6 Server tick (`customServerAiStep`, `tick`)

- Every tick (both sides) `setSegmentsPerHealth()` before `super.tick()`, `moveSegments()` after.
- Target is dropped when farther than 80 blocks or not both in home.
- With mob griefing: every tick the same block-clearing box as NagaSmashGoal but with min y `floor(bb.minY + (destroyAll ? 1.01 : 0.5))`
  and max y `floor(bb.maxY + 1.0)`: leaves always (no drops), everything destroyable when `shouldDestroyAllBlocks()`
  = charging or outside the home area.
- Every 20 ticks, when the restriction point is valid and the naga is more than 5 blocks below it: teleport to the
  restriction point and stop navigation ("stuck in a hole").
- Path-skipping: while the next path node is closer than `bbWidth * 4` horizontally, advance the path.
- Regeneration: `ticksSinceDamaged++`; above 600 ticks, heal 1 HP every 20 ticks.
- When `damageDuringCurrentStun > 15`: `forceCircle()` and reset it (damage taken while dazed is summed in `hurt`).

### 1.7 Damage

- `isInvulnerableTo`: the attacker or the direct entity is outside the home area, or any explosion
  (`#minecraft:is_explosion`), or vanilla invulnerability.
- `hurt`: on success reset `ticksSinceDamaged`; while dazed add `(int) amount` to `damageDuringCurrentStun`.
- `doHurtTarget(target)`:
  - target is a LivingEntity **blocking with a shield** and state CHARGE: push the target `(motion.x * 1.5, 0.5, motion.z * 1.5)`,
    push the naga `(motion.x * -1.25, 0.5, motion.z * -1.25)`; a ServerPlayer's used item takes 5 durability and
    receives a `MovePlayerPacket(motion.x * 3, motion.y + 0.75, motion.z * 3)`; the naga hurts itself 2.0 (generic);
    play `minecraft:item.shield.block` at the target (PLAYERS, 1.0, pitch 0.8 + rand * 0.4); `doDaze()`; no damage dealt.
  - shield blocking and STUNLESS_CHARGE: a ServerPlayer's used item takes 10 durability, gets a 200-tick cooldown, the
    player stops using it and entity event 30 (shield break) is broadcast; the target takes 4.0 mob-attack damage;
    play `minecraft:entity.fox.bite` (2.0, 0.5); `doCircle()`.
  - otherwise, when not dazed: vanilla melee; on success push the target
    `(-sin(yRot) * 2.0, 0.4, cos(yRot) * 2.0)`.
- Segments (`NagaSegment.hurt`) forward `damage * 2/3` to the head unless invisible.
- Not pushable; `remove()` kills every segment.

### 1.8 Body segments (`NagaSegment`, multipart)

- 12 `TFPart` segments, renderer id `twilightforest:naga_segment`, inactive = size 0 x 0 and invisible, active =
  2.0 x 2.0 visible.
- Segment count: `clamp((int)(health / healthPerSegment) + (health > 0 ? 2 : 0), 0, 12)`. When it decreases,
  segment `i` (new..old-1) self-destructs after `(old - i) * 12` ticks: on expiry the naga makes 20 EXPLOSION
  particles at the segment, plays `entity.twilightforest.naga.hurt` (0.25, `voicePitch * 0.75 + 0.5 * rand`), sets
  the naga's `deathTime` to 0 and deactivates the segment. When it increases, all segments up to the count are
  activated at `(x + 0.1 i, y + 0.5, z + 0.1 i)` with a random yaw, each with 20 EXPLOSION particles.
- `moveSegments()`: for segment `i`, leader = head (i = 0) or segment `i-1`; `straighten = 0.05 + 0.5 / (i + 1)` (0 while
  dying); ideal offset `(-sin(a) * s, (groundY - followY) * s, cos(a) * s)` with `a = (leader.yRot + 180) deg` and
  `groundY = followY + 2` when the segment is inside a wall; new direction = normalize(normalize(segment - leader) + ideal);
  segment position = leader + 2.0 * direction; yaw `atan2(dz, dx) + 90 deg`, pitch `-atan2(dy, horizontal)`.
- Segment contact (each tick, visible only): push colliding pushable entities; when the naga is not dazed and not dying,
  living non-Naga entities take 2 mob-attack damage, 6 against animals ("get rid of nearby deer").
- Segments never use portals and make no step sound.

Bedrock: build the segments with `entity::addPart` (12 linked actors, per-part size switching 0 <-> 2.0); spike S4
covers linked actors. Part damage must route through `combat` to the head with the 2/3 factor.

### 1.9 Sounds and particles

| Event | Owner | Use |
|---|---|---|
| `entity.twilightforest.naga.hiss` | U20 | ambient sound; subtitle `subtitles.twilightforest.entity.naga.hiss` "Naga hisses" (3 variants in TF) |
| `entity.twilightforest.naga.hurt` | U20 | hurt **and** death sound; segment pop (0.25 volume); poof on death (`volume * 1.25`, `pitch * 0.25`); subtitle "Naga hurts" (3 variants) |
| `entity.twilightforest.naga.rattle` | U20 | entering INTIMIDATE (volume 4.0); subtitle "Naga rattles" (2 variants) |
| `minecraft:item.shield.block`, `minecraft:entity.fox.bite` | vanilla | shield interactions (1.7) |

Particles (client): dazed and `deathTime < 10`: 5 CRIT per tick on a 1.5-radius ring at y + 2.15. Stunless charging and
alive: 1 ANGRY_VILLAGER per tick at random x/z within 0.85, y = blockY + 2.25. Death trail: see 1.10. All three sounds
are re-synthesised (R10) through `tools/sounds/recipes/naga.json`.

### 1.10 Death

`BaseTFBoss` lifecycle (F8): loot into a death chest at `EntityUtil.bossChestLocation`, conquest of the courtyard,
spawner not recreated after a kill. Death animation 124 ticks: from tick 24 the client draws a slithering trail of
COMPOSTER particles from the body centre to the chest: for p = 1..4 and d = 0, 0.25, 0.5, 0.75 with
`t = deathTime - 24 - p - d >= 0`, `factor = t / 100`, position = start + diff * factor +
`(sin(t * PI * 0.075) * xMul, sin(t * PI * 0.025) * 0.1, cos(t * PI * 0.0625) * zMul)` where
`xMul = (min(a % 180, 180 - a % 180) / 90)^1.5 * 2`, `zMul` the same for `a + 90`, `a = atan2(end - start) deg + 180`;
each point is ray-snapped to the ground (clip from +2 to -3, colliders and water) and raised 0.15.
`makePoofParticles`: death sound then 20 EXPLOSION particles. The body does not tip over while dying
(`getFlipDegrees` 0) and segments stop straightening.

### 1.11 Rendering (client mod)

Textures `textures/entity/nagahead.png`, `nagahead_charging.png` (charging or dying), `nagahead_dazed.png` (dazed),
`nagasegment.png`. Head scaled 2.01 and translated y +0.75 (+1.075 and z +0.175 when dazed); segments scaled 2.0,
translated y -1.25, lit with the head's light. Geometry from F2 (`NagaModel`). Vanilla clients: the same geometry
via the RP; the red stunless tint is a client-mod effect (vanilla: the angry particles only).

### 1.12 Loot `twilightforest:entities/naga`

- pool 1: rolls 1: `twilightforest:naga_scale` x6-11, +0-1 per looting level, `twilightforest:multiplayer_addition`
  (extra 2-4 per additional player, F10 loot function)
- pool 2: rolls 1: `twilightforest:naga_trophy`

## 2. Boss spawner wiring

`NagaSpawnerBlockEntity`: `boss::registerSpawner({.blockId = "twilightforest:naga_boss_spawner", .entityId =
"twilightforest:naga", .range = 50, .particle = "minecraft:crit"})` (LONG_RANGE 50; the default range is 9). The
generic spawner (F8) spawns the boss at the block, sets its restriction point to the block and destroys the block.
In peaceful the boss turns back into its spawner at the restriction point (`checkDespawn`).

## 3. Structure `twilightforest:naga_courtyard`

| Field | Value |
|---|---|
| structure type | `twilightforest:naga_courtyard` (`NagaCourtyardStructure`, a `ConquerableStructure`) |
| structure set | `twilightforest:naga_courtyard`: placement `twilightforest:landmark_grid` with `structure_grid_lock = twilightforest:naga_courtyard`, one structure weight 1 (F6 places it on the landmark grid) |
| biomes | `#twilightforest:valid_naga_courtyard_biomes` (U1 tag) |
| step | `surface_structures`; terrain adaptation `beard_thin` |
| decoration clearance | `chunk_clearance_radius` 3, `allow_biome_surface_decorations` false, `allow_biome_underground_decorations` true, `allow_biome_vegetation` true, `adjust_structure_elevation` true |
| spawn overrides | every category: bounding box `full`, no spawns (landmark-controlled spawning) |
| progression lock | none (first landmark); no hint creature |
| map icon | map decoration `twilightforest:naga_courtyard` (vanilla fallback: nearest of the vanilla decorations, F6/F12 mapping) |
| centre in chunk | true |
| start | `CourtyardMain(random, 0, x + 1, y, z + 1)` |
| height | `WorldUtil.adjustForTerrain(context, x, z, 40, 4) + 2` |
| terraformer | `CustomDensitySource.getInvertedPyramidTerraformer(start, 3, 4)` (F6 `addTerraformer`) |

### 3.1 Layout (`CourtyardMain` + `StructureMazeGenerator`, translate exactly)

- `ROW_OF_CELLS` 8 (8 x 8 cell grid; the wall connectome is 7 x 7), `RADIUS = ((8 - 2) / 2 * 12) + 8 = 44`.
- Piece box: `getComponentToAddBoundingBox(x, y, z, -22, -1, -22, 44, 10, 44, NORTH)`; size constraints box
  `(-44, -1, -44, 88, 10, 88)`. Orientation NORTH.
- `HEDGE_FLOOF` 0.5 (BlockRotProcessor integrity for the "big" hedge overlays), `WALL_INTEGRITY` 0.95 (intact wall
  templates), `WALL_DECAY` 0.1 (decayed overlay templates).
- Maze generation (`generateMaze`, max corner clipping 2): every connectome cell gets a random WallFacing
  (EAST 0b0001, SOUTH 0b0010, WEST 0b0100, NORTH 0b1000) OR-ed in; a second pass (skipping the centre cell
  `(3, 3)`) links or cuts the chosen direction with the neighbour; the centre's neighbours lose their connection
  toward it and the centre becomes `0b10000`; cells that were zero join zero neighbours west/north; then four corner
  clippings (each `nextInt(2) + 1` cells in x and z) mark corner cells `0b10000` (excluded).
- Cell placement (`processInnerWallsAndFloor`, cell origin `sizeConstraints.min + (x * 12 + 6, 1, y * 12 + 6)`),
  skipping excluded cells except on the centre row/column, and placing nothing at the exact centre:

| Connection bits | Piece (piece type) | Templates (+ "big" overlay at integrity 0.5, air ignored) |
|---|---|---|
| one direction | 50/50 `NagaCourtyardHedgeCapComponent` (`twilightforest:TFNCCp`) or `...CapPillarComponent` (`twilightforest:TFNCCpP`), rotated per direction | `courtyard/hedge_end` + `hedge_end_big`; `courtyard/hedge_end_pillar` + `hedge_end_pillar_big` |
| two adjacent (corner) | `NagaCourtyardHedgeCornerComponent` (`twilightforest:TFNCCr`) | `courtyard/hedge_corner` + `_big` |
| three (T) | `NagaCourtyardHedgeTJunctionComponent` (`twilightforest:TFNCT`) | `courtyard/hedge_t` + `_big` |
| two opposite (line) | `NagaCourtyardHedgeLineComponent` (`twilightforest:TFNCLn`) | `courtyard/hedge_line` + `_big` |
| four | `NagaCourtyardHedgeIntersectionComponent` (`twilightforest:TFNCIs`) | `courtyard/hedge_intersection` + `_big` |
| none | terrace at `(-6, -3, -6)`: 1/150 `terrace_statue`; else `nextInt(5)`: 1 `terrace_duct`, 2 `terrace_channel`, 3 `terrace_reservoir`, 4 `CourtyardTerraceHedge` (`twilightforest:TFNCHe`: `terrace_hedge` + `terrace_hedge_big` at 0.5), 0 `terrace_fire`; all `CourtyardTerrace` (`twilightforest:TFNCTe`) | as named |

  Rotation order for caps: case WEST 0, NORTH 1, EAST 2, SOUTH 3 (fall-through `rotation++`); corners: SOUTH_EAST 0,
  SOUTH_WEST 1, NORTH_WEST 2, NORTH_EAST 3; T: NOT_WEST 0, NOT_NORTH 1, NOT_EAST 2, NOT_SOUTH 3; lines: EAST_WEST 0,
  NORTH_SOUTH 1 (indices into `Rotation.values()`).
- Hedge connectors: for each connection west/north (and east/south only at the grid edge or next to an excluded
  cell) a `NagaCourtyardHedgePadderComponent` (`twilightforest:TFNCPd`, `courtyard/hedge_between` + `_big`) at
  -1 / +4 / +5 offsets (a second padder 6 further when the neighbour is not excluded) and a line piece 6 blocks away
  (coordinates exactly as `processInnerWallsAndFloor`).
- Paths: `CourtyardPathPiece` (`twilightforest:TFNCPa`, template `courtyard/pathway`, NagastoneVariants, placed one
  block lower via `placePieceAdjusted(..., -1)`) at the centre and on the four cardinal and four diagonal sides of
  every hedge cell when the neighbouring cells have no terrace (the eight `...HasNoTerraceOrIsSafe` predicates).
- Outer walls (`processOuterWalls`): for each of the four diagonals, padders and walls along the clipped notches,
  two `CourtyardWallCornerOuter` (`twilightforest:TFNCWC`, `courtyard/courtyard_wall_corner` +
  `_decayed`) and one `CourtyardWallCornerInner` (`twilightforest:TFNCWA`, `courtyard/courtyard_wall_corner_inner` +
  `_decayed`); then the straight north/south/west/east runs of `CourtyardWall` (`twilightforest:TFNCWl`,
  `courtyard/courtyard_wall` + `_decayed`) every 12 blocks with a `CourtyardWallPadder` (`twilightforest:TFNCWP`,
  `courtyard/courtyard_wall_padding` + `_decayed`) between them, coordinates as in the Java.
- Wall pieces are `TwilightDoubleTemplateStructurePiece`s: the intact template with BlockRot 0.95 + SmoothStone +
  Nagastone + StoneBricks + Cobble variant processors, then the decayed template with BlockRot 0.1 + Cobble variants.
- Terraces: processors `CourtyardTerraceTemplateProcessor` + Nagastone + StoneBricks variants; beardifier box =
  piece box, terrain adjustment BEARD_BOX, ground level delta 3. The terrace processor turns a template's
  sandstone slab into: (double slab) stone brick slab when the world block is stone bricks / mossy / cracked /
  stone brick slab / mossy stone brick slab, nothing when the world block is plain air (cave air is kept), otherwise
  stone bricks; (single slab) nothing over air, otherwise a stone brick slab.
- Hedge pieces (`TFStructureComponentTemplate`): template with NagastoneVariants, then the big template with
  BlockIgnore AIR + BlockRot 0.5, both with flags 18.
- Boss spawner: `CourtyardMain.addChildren` picks from template pool `twilightforest:courtyard/center` at
  `getWorldPos(22, 1, 22)` facing a random rotation of SOUTH (`FrontAndTop(UP, dir)`), junction name
  `"twilightforest:center"`, as a `TwilightJigsawPiece` (F7 `twilightforest:TFJigsawTemplate`). Legacy saves without
  `placeSpawner = false` place `twilightforest:naga_boss_spawner` at `(22, 3, 22)` in `postProcess` (keep for
  old-save parity only).

Piece type ids owned here (all `structure_piece_type`): `twilightforest:TFNCMn` (CourtyardMain),
`twilightforest:TFNCCp`, `twilightforest:TFNCCpP`, `twilightforest:TFNCCr`, `twilightforest:TFNCLn`,
`twilightforest:TFNCT`, `twilightforest:TFNCIs`, `twilightforest:TFNCPd`, `twilightforest:TFNCTe`,
`twilightforest:TFNCHe`, `twilightforest:TFNCPa`, `twilightforest:TFNCWl`, `twilightforest:TFNCWP`,
`twilightforest:TFNCWC`, `twilightforest:TFNCWA`. The structure type id `twilightforest:naga_courtyard` is owned
here too.

### 3.2 Template slot list (re-design brief, R11/R19)

The 32 courtyard `.nbt` templates are All Rights Reserved and are re-designed as `.gft.json` under
`assets/structures/naga/courtyard/` (template ids unchanged, `twilightforest:courtyard/<name>`). The Java fixes
their grid; keep these constraints:

| Template | Slot / constraints |
|---|---|
| `courtyard/hedge_end`, `hedge_end_pillar`, `hedge_corner`, `hedge_t`, `hedge_line`, `hedge_intersection` | one 12-block cell, origin at the cell origin, hedge (`twilightforest:hedge`) walls of the connection shape; base orientation per the rotation table above; naga stone accents where TF uses them (NagastoneVariants randomises `twilightforest:naga_stone*` variants) |
| `..._big` variants of the six above + `hedge_between_big` | overlay leaves/hedge "floof" placed at 50 % integrity, air ignored, same footprint |
| `courtyard/hedge_between` (+ `_big`) | 1-block padder between hedge pieces (placed at -1 / +5 / -7 offsets; NONE or CLOCKWISE_90) |
| `courtyard/pathway` | path tile 1 block below the hedge base, ~7 x 7 (placed at -1/-7/+5 offsets around a cell; naga stone / stone brick path) |
| `courtyard/terrace_fire`, `terrace_duct`, `terrace_channel`, `terrace_reservoir`, `terrace_statue`, `terrace_hedge` (+ `terrace_hedge_big`) | 12 x 12 sunken terrace placed 3 blocks lower at `(-6, -3, -6)`; uses sandstone slabs as "terrain-fitting" markers for the terrace processor; statue is the rare (1/150) variant |
| `courtyard/courtyard_wall`, `_padding`, `_corner`, `_corner_inner` (+ each `_decayed`) | perimeter wall segments of 12 / padding / corners; decayed overlays at 10 % integrity with cobble variants |
| `courtyard/spawner` | the centre piece, pool `twilightforest:courtyard/center` (template definition `template_definition/twilightforest:courtyard/spawner`, weight 100); must contain a jigsaw connector named `twilightforest:center` (pointing down/UP-front so `FrontAndTop(UP, dir)` fits) and the block `twilightforest:naga_boss_spawner` |

The only template definition owned here is `template_definition/twilightforest:courtyard/spawner`
(`{"twilightforest:courtyard/center": 100}`).

## 4. Tag, map decoration and strings

- `banner_pattern/#twilightforest:pattern_item/naga` = `[twilightforest:naga]` (the banner pattern itself is U5's;
  the tag lives here, R6).
- Map decoration `twilightforest:naga_courtyard` (the landmark icon on magic maps; U13 renders magic maps).
- Lang keys owned: `entity.twilightforest.naga` "Naga", `item.twilightforest.naga_spawn_egg` "Naga Spawn Egg",
  `subtitles.twilightforest.entity.naga.hiss` "Naga hisses", `subtitles.twilightforest.entity.naga.hurt`
  "Naga hurts", `subtitles.twilightforest.entity.naga.rattle` "Naga rattles".

## 5. Bedrock adaptation notes

- Boss: `boss::registerBoss({entityId "twilightforest:naga", homeStructure "twilightforest:naga_courtyard",
  lootTable "twilightforest:entities/naga", deathChestBlock random twilight oak / canopy chest, bar {0x5E9916},
  deathTicks 124})`. The NOTCHED_10 overlay maps to Bedrock's 10-notch boss bar overlay.
- Shield logic: Bedrock players block with `Player::isBlocking`; the knockback packet `MovePlayerPacket` becomes
  `movement::applyMotion` (companion) or SetActorMotion for vanilla clients.
- The difficulty health boost reads the world difficulty at spawn (not regional difficulty).
- Block destruction honours the `mobGriefing` gamerule (`EventHooks.canEntityGrief`).
- The home box (46 / 7) uses the restriction point set by the spawner (`entity::setHome`).

## 6. Selftests to write (`naga.*`, dx 2000-2099)

`naga.spawn_health` (120 / 200 / 250 per difficulty), `naga.segments` (12 at full health, count formula after
damage), `naga.state_machine` (seeded: CIRCLE -> INTIMIDATE -> CHARGE -> CIRCLE; shield block -> DAZE 60-99 ticks),
`naga.home_invulnerable` (damage from outside the 46/7 box is ignored), `naga.loot` (scales 6-11 + trophy),
`naga.courtyard_layout` (golden volume hash of a seeded courtyard, R19), `naga.spawner` (range 50).
