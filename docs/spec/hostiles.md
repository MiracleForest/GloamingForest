# hostiles (U17): Forest and hollow-hill hostiles

Behaviour spec of unit **U17** (`hostiles`), written by F15 from the upstream 4.8 (`9e40a757`). Java paths are
relative to `src/main/java/gloamingforest/`; data paths to `src/generated/resources/` (prefix `main/` for
`src/main/resources/`).

| Unit fact | Value |
|---|---|
| Selftests | `hostiles.*`, testPos dx `1700-1799` (R12) |
| World data | `gf:hostiles:*` (not needed by upstream behaviour) |
| Net channels | `hostiles.*` (none needed; breathing flags are entity properties) |
| Contracts | entity, projectile, spawn, loot, combat, geo |
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
| `block/#c:ores` | `#c:ores/{coal,copper,diamond,emerald,gold,iron,lapis,netherite_scrap,quartz,redstone}` = vanilla `#minecraft:{coal,copper,diamond,emerald,gold,iron,lapis,redstone}_ores`, `minecraft:ancient_debris`, `minecraft:nether_quartz_ore` | `minecraft:coal_ore`, `deepslate_coal_ore`, `copper_ore`, `deepslate_copper_ore`, `diamond_ore`, `deepslate_diamond_ore`, `emerald_ore`, `deepslate_emerald_ore`, `gold_ore`, `deepslate_gold_ore`, `nether_gold_ore`, `iron_ore`, `deepslate_iron_ore`, `lapis_ore`, `deepslate_lapis_ore`, `redstone_ore`, `deepslate_redstone_ore`, `quartz_ore` (Java `nether_quartz_ore`), `ancient_debris`, plus Bedrock's separate lit states `minecraft:lit_redstone_ore` and `minecraft:lit_deepslate_redstone_ore` | `#gloamingforest:druid_projectile_replaceable` (druid bolt) |

## 1. Common rules

- Registration (`init/GloamingEntities.java`): tracking range 80, update interval 3, velocity updates; projectiles
  `nature_bolt` and `slime_blob` use `makeCastedBuilder` (category MISC, tracking 150, intervals 5 and 3, no egg).
- Attributes in `events/RegistrationEvents.java#addEntityAttributes`, placements in `#registerSpawnPlacements`
  (ON_GROUND, `MOTION_BLOCKING_NO_LEAVES`).
- Vanilla bases used and their defaults (Java 1.21.1): `Spider.createAttributes()` max_health 16, movement_speed 0.3;
  `AbstractSkeleton.createAttributes()` movement_speed 0.25 (health 20, attack 2); `Monster.createMonsterAttributes()`
  attack_damage 2, follow_range 16.
- Vanilla spider goals (kept by king spider; hedge and swarm spiders replace two of them): 1 Float; 2 AvoidEntity
  Armadillo 6.0 (1.0 / 1.2, only when the armadillo is not scared); 3 LeapAtTarget 0.4; 4 SpiderAttackGoal (melee 1.0,
  long memory, gives up in light); 5 WaterAvoidingRandomStroll 0.8; 6 LookAtPlayer 8.0; 6 RandomLookAround; targets
  1 HurtBy, 2 SpiderTargetGoal Player (dark only), 3 SpiderTargetGoal IronGolem (dark only). Vanilla spider
  `finalizeSpawn` adds a skeleton jockey with chance 1/100 and on Hard a random effect (speed, strength, regeneration
  or invisibility).
- Shared goals come from F8 (`gf::entity::goals::charge`, `avoidAny`, `simplifiedAttack`, the breath goal) and the
  shared hostile-mount rules (`gf::entity::mount`, upstream `events/HostileMountEvents.java`, also used by U27's yetis).
- The hedge-maze light exception (`LegacyLandmarkPlacements.pickLandmarkForChunk(cx, cz) == hedge_maze`) is
  `gf::core::landmark::pickForChunk` (F6) compared to `gloamingforest:hedge_maze`.

## 2. Entities

### 2.1 `gloamingforest:hostile_wolf` (HostileWolf)

Source: `entity/monster/HostileWolf.java`, `client/model/entity/HostileWolfModel.java`, `client/renderer/entity/HostileWolfRenderer.java` (shadow 0.5).

| Field | Value |
|---|---|
| Base / size | `Monster`, 0.6 x 0.85, riding offset 0 |
| Attributes | movement_speed 0.3, max_health 20.0, attack_damage 2.0 (`Mob.createMobAttributes()` base) |
| Variant | vanilla `WolfVariant` holder, synced; default and fallback `minecraft:pale`; **no spawn-time variant roll** (stays pale unless loaded from NBT); saved as string `variant` |
| Texture | variant's angry texture while `isAggressive()`, else wild texture |
| Sound volume | 0.4 for all its sounds; step `minecraft:entity.wolf.step` vol 0.15 |
| Target sound | `setTarget(new != current)`: plays the target sound at volume 4.0, voice pitch |
| Despawn | despawns in Peaceful |
| Spawn egg | `gloamingforest:hostile_wolf_spawn_egg` 0xd7d3d3 / 0xab1e14 |

AI goals: 1 Float; 2 LeapGoal 0.4 (LeapAtTarget that sets aggressive on start and clears it on stop, so the angry face
stays while leaping); 3 MeleeAttack 1.0 (long memory); 4 WaterAvoidingRandomStroll 1.0; 5 LookAtPlayer 8.0;
5 RandomLookAround. Targets: 1 HurtByTarget (ignores damage from HostileWolf); 2 NearestAttackable Player (must
see); 3 NearestAttackable Animal with `Wolf.PREY_SELECTOR` (sheep, rabbits, foxes); 4 NearestAttackable Turtle with
`Turtle.BABY_ON_LAND_SELECTOR`; 5 NearestAttackable AbstractSkeleton.

Tail: angle (render bob) 1.5393804 with a target, else PI/5; `tail.xRot = angle`; `tail.yRot` = 0 while aggressive,
else `cos(limbSwing x 0.6662) x 1.4 x amount`. Body pose: body xRot PI/2, positions as in the model.

Sounds: ambient `entity.gloamingforest.hostile_wolf.ambient` ("Hostile Wolf growls"), hurt
`entity.gloamingforest.hostile_wolf.hurt`, death `entity.gloamingforest.hostile_wolf.death`, target
`entity.gloamingforest.hostile_wolf.target` ("Hostile Wolf takes notice").

Spawn (`checkWolfSpawnRules`): not Peaceful, (chunk's landmark is the hedge maze **or** dark enough), and
`checkMobSpawnRules`. Placed by structures (hedge maze and fallen trunk spawners, by string).

Loot `gloamingforest:entities/hostile_wolf`: 1 pool referencing `minecraft:entities/wolf` (nothing in vanilla 1.21,
kept as a reference).

### 2.2 `gloamingforest:mist_wolf` (MistWolf extends HostileWolf)

Source: `entity/monster/MistWolf.java`, `client/model/entity/MistWolfModel.java`, `client/renderer/entity/MistWolfRenderer.java`.

| Field | Value |
|---|---|
| Size | 1.4 x 1.9, eye height 1.45 |
| Attributes | HostileWolf + max_health 30.0, attack_damage 6.0 |
| Goals | inherits HostileWolf's goals and targets |
| Voice pitch | `(r1 - r2) x 0.2 + 0.6` |
| Render | `mistwolf.png`, scale 1.9, shadow 1.0; colour multiplier from light `b = maxLocalRawBrightness / 15`: rgb = `min(1, b x 3 + 0.25)`, alpha = `min(1, b x 2 + 0.3)` (translucent in the dark) |
| Spawn egg | `gloamingforest:mist_wolf_spawn_egg` 0x3a1411 / 0xe2c88a |

Blinding bite: after a successful melee hit on a living entity, when the wolf's block light
`getMaxLocalRawBrightness(blockPosition) < 0.10` (i.e. 0) and the block at its position is not solid, apply
Blindness amplifier 0 for EASY 0 s (none), HARD 15 s, otherwise 7 s.

Sounds: `entity.gloamingforest.mist_wolf.ambient` ("Mist Wolf growls"), `entity.gloamingforest.mist_wolf.hurt`,
`entity.gloamingforest.mist_wolf.death`, `entity.gloamingforest.mist_wolf.target` ("Mist Wolf takes notice").

Spawn: `Monster::checkMonsterSpawnRules`; biome dark_forest monster weight 5, 1-1 (spawn cost charge 0.75, budget 0.2).

Loot `gloamingforest:entities/mist_wolf`: references `minecraft:entities/wolf`.

Bedrock: the light-dependent translucency is a client-mod render tint (`gf::client::render` per-entity colour);
vanilla clients see the opaque texture.

### 2.3 `gloamingforest:winter_wolf` (WinterWolf extends HostileWolf, IBreathAttacker)

Source: `entity/monster/WinterWolf.java`, `entity/ai/goal/BreathAttackGoal.java`, `client/renderer/entity/WinterWolfRenderer.java`.

| Field | Value |
|---|---|
| Size | 1.4 x 1.9, eye 1.45 |
| Attributes | HostileWolf + max_health 30.0, attack_damage 6.0 |
| Synced | `BREATH_FLAG` boolean |
| Voice pitch | `(r1 - r2) x 0.2 + 0.6` |
| Render | `winterwolf.png`, scale 1.9, shadow 1.0, hostile wolf model |
| Spawn egg | `gloamingforest:winter_wolf_spawn_egg` 0xdfe3e5 / 0xb2bcca |

AI goals (replaces HostileWolf's): 0 Float; 2 BreathAttack (range 5.0, 30 ticks, chance 0.1); 3 MeleeAttack 1.0 (no
long memory); 6 WaterAvoidingRandomStroll 1.0. Targets: 1 HurtByTarget; 2 NearestAttackable Player (must see).

**BreathAttackGoal** (shared with the fire beetle; flags MOVE, LOOK, JUMP): canUse when the last-hurt-by mob exists,
is within range, visible, alive, not creative/spectator, and `nextFloat() < chance`; aim point = its eye position at
start. start: duration = max, breathing flag on. continue while duration > 0, both alive, in range, visible, valid.
tick: duration - 1; look at the aim point and turn the body toward it (max 100 degrees yaw and pitch per tick); after
the first 5 ticks, every tick: find the entity the head looks at (ray from (x, y + 0.25, z) along the view vector
range 30 against entities in the bounding box moved 3 blocks forward inflated 0.5, nearest hit; pickable, living,
not creative/spectator, multipart parts excluded) and call `doBreathAttack` on it, game event PROJECTILE_SHOOT.
stop: duration 0, flag off.

Winter wolf breath: `doBreathAttack` deals 2.0 vanilla mob-attack damage. While breathing (each tick): client spawns
10 `gloamingforest:snow` particles (U28's particle, by string) at 0.5 blocks ahead, y + 1.25, velocity look vector with
gaussian spread `0.0075 x (5 + rand x 2.5)` times `3.0 + rand x 0.15`; plays `entity.gloamingforest.winter_wolf.shoot`
("Winter Wolf shoots") at volume rand x 0.5, pitch rand x 0.5.

Sounds: `entity.gloamingforest.winter_wolf.ambient` ("Winter Wolf growls"), `entity.gloamingforest.winter_wolf.hurt`,
`entity.gloamingforest.winter_wolf.death`, `entity.gloamingforest.winter_wolf.target`.

Spawn (`canSpawnHere`, Java precedence kept): `(not Peaceful && biome == gloamingforest:snowy_forest) ||
isDarkEnoughToSpawn`. Biome snowy_forest monster weight 5, 1-1 (charge 0.6, budget 0.15).

Loot `gloamingforest:entities/winter_wolf`: `gloamingforest:arctic_fur` 0..2, looting +0..1.

### 2.4 `gloamingforest:king_spider` (KingSpider extends Spider)

Source: `entity/monster/KingSpider.java`, renderer `GloamingSpiderRenderer` (`kingspider.png`, shadow 1.25, scale 1.9).

| Field | Value |
|---|---|
| Size | 1.6 x 1.6 |
| Attributes | Spider + max_health 30.0, movement_speed 0.35, attack_damage 6.0 |
| Goals | vanilla spider goals (light-dependent) |
| Climbing | `onClimbable` always false (never climbs walls) |
| Rider | `finalizeSpawn`: always creates a `gloamingforest:skeleton_druid`, finalizes it as JOCKEY, and makes it ride the top of the current passenger stack |
| Passenger attachment | (0, height x 0.85, 0) |
| Spawn egg | `gloamingforest:king_spider_spawn_egg` 0x2c1a0e / 0xffc017 |

Sounds: ambient `entity.gloamingforest.king_spider.ambient`, hurt `entity.gloamingforest.king_spider.hurt`, death
`entity.gloamingforest.king_spider.death`, step `entity.gloamingforest.king_spider.step` vol 0.15 (upstream maps them to
`entity.spider.*`).

Spawn: `Monster::checkMonsterSpawnRules`; dark_forest weight 1, 1-1 (charge 0.85, budget 0.25).

Loot `gloamingforest:entities/king_spider`: references `minecraft:entities/spider` (string 0..2, spider eye 1/3 when
killed by a player).

### 2.5 `gloamingforest:hedge_spider` (HedgeSpider extends Spider)

Source: `entity/monster/HedgeSpider.java`, renderer `GloamingSpiderRenderer` (`hedgespider.png`, shadow 0.8, scale 1.0).

| Field | Value |
|---|---|
| Size / attributes | 1.4 x 0.9; vanilla `Spider.createAttributes()` (16 HP, 0.3) |
| Goals | vanilla spider goals except: every MeleeAttackGoal removed and replaced by MeleeAttack 1.0 long memory at 4 (does not turn docile in light); the priority-2 player target replaced by NearestAttackable Player (must see) at 2 (light independent) |
| Spawn egg | `gloamingforest:hedge_spider_spawn_egg` 0x235f13 / 0x562653 |

Sounds: `entity.gloamingforest.hedge_spider.ambient`, `.hurt`, `.death`, `.step` (vol 0.15) - full ids
`entity.gloamingforest.hedge_spider.ambient`, `entity.gloamingforest.hedge_spider.hurt`,
`entity.gloamingforest.hedge_spider.death`, `entity.gloamingforest.hedge_spider.step`.

Spawn (`canSpawn`): not Peaceful and (hedge maze chunk or dark enough); **no** `checkMobSpawnRules`. Hedge maze
spawners (`HedgeMazeComponent`) place hedge spiders by string.

Loot `gloamingforest:entities/hedge_spider`: references `minecraft:entities/spider`.

### 2.6 `gloamingforest:swarm_spider` (SwarmSpider extends Spider)

Source: `entity/monster/SwarmSpider.java`, renderer `GloamingSpiderRenderer` (`swarmspider.png`, shadow 0.25, scale 0.5).

| Field | Value |
|---|---|
| Size | 0.8 x 0.4 |
| Attributes | Spider + max_health 3.0, attack_damage 1.0 (speed 0.3) |
| XP | `xpReward` 2 |
| Goals | same replacements as the hedge spider |
| Attack | `doHurtTarget` succeeds only with chance 1/4 (`nextInt(4) == 0`), then vanilla melee |
| Voice pitch | `(r1 - r2) x 0.2 + 1.5` |
| Spawn cluster | 6 |
| Jockey | after vanilla `finalizeSpawn`: if it already has a passenger (vanilla 1/100 skeleton) or with chance 1/200, create a **baby** `gloamingforest:skeleton_druid`, finalize it as JOCKEY, eject existing passengers and mount it |
| Spawn egg | `gloamingforest:swarm_spider_spawn_egg` 0x32022e / 0x17251e |

Sounds: `entity.gloamingforest.swarm_spider.ambient`, `entity.gloamingforest.swarm_spider.hurt`,
`entity.gloamingforest.swarm_spider.death`, `entity.gloamingforest.swarm_spider.step` (vol 0.15).

Spawn (`getCanSpawnHere`): not Peaceful, (hedge maze chunk or dark), `checkMobSpawnRules`. Placed by the hollow hill,
hedge maze, hollow tree, fallen trunk and lich tower spawners (by string).

Loot `gloamingforest:entities/swarm_spider`: references `minecraft:entities/spider`.

### 2.7 `gloamingforest:skeleton_druid` (SkeletonDruid extends AbstractSkeleton)

Source: `entity/monster/SkeletonDruid.java`, `client/model/entity/SkeletonDruidModel.java` (SkeletonModel), renderer `GloamingBipedRenderer` (`skeletondruid.png`, shadow 0.5).

| Field | Value |
|---|---|
| Size | 0.6 x 1.99, riding offset -0.7 |
| Attributes | `AbstractSkeleton.createAttributes()` (20 HP, 0.25, attack 2) |
| Equipment | adult: `minecraft:golden_hoe`; baby: `minecraft:stick` (replaces the vanilla bow) |
| Baby | synced `DATA_BABY_ID`, saved `IsBaby`; +50 % speed (`ADD_MULTIPLIED_BASE` modifier `gloamingforest:baby_speed_boost`, transient); size x 0.5 with eye height 0.93; XP reward x 2.5 |
| Spawn egg | `gloamingforest:skeleton_druid_spawn_egg` 0xa3a3a3 / 0x2a3b17 |

Goals: vanilla AbstractSkeleton goals (2 RestrictSun, 3 FleeSun 1.0, 3 AvoidEntity Wolf 6.0 (1.0/1.2),
5 WaterAvoidingRandomStroll 1.0, 6 LookAtPlayer 8.0, 6 RandomLookAround; targets 1 HurtBy, 2 Player, 3 IronGolem,
3 baby Turtle) plus the weapon goal at 4 chosen by `reassessWeaponGoal` (server): holding any hoe -> RangedAttackGoal
(speed 1.25, interval 60, radius 5.0); otherwise the vanilla choice (bow goal or MeleeAttack 1.2). Druids burn in
daylight like skeletons.

Ranged attack (hoe only): spawn a `gloamingforest:nature_bolt`, play `entity.gloamingforest.skeleton_druid.shoot`
("Skeleton Druid shoots") vol 1.0, pitch `1 / (rand x 0.4 + 0.8)`; aim `tx = target.x - x`, `ty = target.y +
target.eyeHeight - 2.7 - y`, `tz`; `shoot(tx, ty + sqrt(tx^2 + tz^2) x 0.2, tz, velocity 0.6, inaccuracy 6.0)`.
Holding a stick: no ranged attack. Any other item: vanilla skeleton behaviour.

Sounds: ambient `entity.gloamingforest.skeleton_druid.ambient` (stray ambient in upstream), hurt
`entity.gloamingforest.skeleton_druid.hurt`, death `entity.gloamingforest.skeleton_druid.death`, step
`entity.gloamingforest.skeleton_druid.step`.

Spawn (`checkDruidSpawnRules`): not Peaceful, light check copied from vanilla with an upstream edit (`sky light >
rand(32)` fails; then `maxLocalRawBrightness <= rand(12)` instead of rand(8)), `checkMobSpawnRules`. Biomes:
dark_forest weight 5 1-1 (charge 0.8, budget 0.2), spooky_forest weight 5 1-1 (charge 0.95, budget 0.25). Also the
druid hut spawner (U19) and king/swarm spider jockeys.

Loot `gloamingforest:entities/skeleton_druid`: `minecraft:bone` 0..2 + looting 0..1; `gloamingforest:torchberries`
0..2 + looting 0..1.

### 2.8 `gloamingforest:nature_bolt` (NatureBolt projectile)

Source: `entity/projectile/NatureBolt.java`, `entity/projectile/GloamingThrowable.java`; rendered as the item
`minecraft:wheat_seeds` (ThrownItemRenderer).

- ThrowableProjectile, 0.25 x 0.25, gravity 0.003 per tick.
- Trail: every tick 5 `minecraft:happy_villager` particles at position + 0.5 x (r1 - r2) per axis.
- Hit block (when mob griefing is allowed): bonemealable and valid -> perform bonemeal (server); else if the block is
  solid, has no block entity, is solid-render, is in `#gloamingforest:druid_projectile_replaceable` (F9 tag: leaves,
  logs, planks, carver replaceables, `#c:ores`, ...) and may be destroyed by the bolt -> set
  `minecraft:birch_leaves[persistent=true]`.
- Hit entity: a living entity that is not the owner or the owner's vehicle takes 2.0 damage of type
  `gloamingforest:leaf_brain` (U21's damage type, indirect: direct = bolt, attacker = owner); if damaged and not
  Peaceful, Poison amplifier 0 for 3 s (Hard 7 s).
- Any hit (server): entity event 3 -> 8 `block` particles of `minecraft:oak_leaves` (gaussian 0.05, up 0..0.2), then
  discard.

### 2.9 `gloamingforest:redcap` (Redcap)

Source: `entity/monster/Redcap.java`, `entity/ai/goal/{RedcapBaseGoal,RedcapShyGoal,RedcapLightTNTGoal}.java`,
`entity/ai/goal/AvoidAnyEntityGoal.java`, `client/model/entity/RedcapModel.java` (FixedHumanoidModel), renderer
`GloamingBipedRenderer` with inner/outer armour layers (`redcap.png`, shadow 0.4).

| Field | Value |
|---|---|
| Size | 0.9 x 1.4, riding offset -0.4 |
| Attributes | max_health 20.0, movement_speed 0.28 (attack 2) |
| Equipment | main hand `minecraft:iron_pickaxe`, feet `minecraft:iron_boots`; difficulty enchantments; drop chance 0.2 each |
| Held stacks | `heldPick` (iron pickaxe), `heldTNT` (1 x `minecraft:tnt`), `heldFlint` (`minecraft:flint_and_steel`) swapped into the main hand by goals |
| Saved | `TNTLeft` (count of heldTNT) |
| Shy | `lastHurtByPlayerTime <= 0` (not hurt by a player in the last 100 ticks) |
| Spawn egg | `gloamingforest:redcap_spawn_egg` 0x3b3a6c / 0xab1e14 |

AI goals: 0 Float; 1 AvoidAnyEntity PrimedTnt distance 2.0 (walk 1.0, sprint 2.0); 2 RedcapShy 1.0; 3 RedcapLightTNT
1.0; 5 MeleeAttack 1.0 (no long memory); 6 WaterAvoidingRandomStroll 1.0; 7 LookAtPlayer 8.0; 7 RandomLookAround.
Targets: 1 HurtBy; 2 NearestAttackable Player (must see).

- `isTargetLookingAtMe(target)`: angle from target to redcap `atan2(dz, dx)` in degrees - 90, difference
  `|(target.yRot - angle) % 360|`; true when < 60 or > 300 (120-degree cone).
- `findBlockTNTNearby(r)`: first `minecraft:tnt` block in the cube +-r around the redcap (x, then y, then z loops).
- **RedcapShy** (MOVE, LOOK; lefty fixed per goal instance with 50 %): usable with a target, shy, distance 3..6, and the
  target looking at it; destination = point on a circle of radius 5 around the target at the current angle + 1 rad
  (lefty) or - 1 rad, at the redcap's feet height; continue while navigating, shy, 3 < d < 6 and watched; look at the
  target (30/30).
- **RedcapLightTNT** (MOVE, LOOK): needs mob griefing; 20-tick cooldown after each run; a TNT block within 8 blocks;
  start: hold flint and steel; tick: look at it; within 2.4 blocks of the TNT's corner: play ambient sound, prime it
  (`TntBlock.onCaughtFire` with igniter = redcap), swing, set air, game event PRIME_FUSE, stop navigation; else walk
  to it at speed 1.0; stop: hold the pickaxe again.

Sounds: ambient `entity.gloamingforest.redcap.ambient` ("Redcap chuckles"), hurt `entity.gloamingforest.redcap.hurt`
("Redcap screeches in pain"), death `entity.gloamingforest.redcap.death` ("Redcap groans in agony").

Spawn: ON_GROUND, `Monster::checkMonsterSpawnRules`; hollow hill controlled spawns and spawners (by string).

Loot `gloamingforest:entities/redcap`: `minecraft:coal` 0..1 + looting 0..1.

### 2.10 `gloamingforest:redcap_sapper` (RedcapSapper extends Redcap)

Source: `entity/monster/RedcapSapper.java`, `entity/ai/goal/RedcapPlantTNTGoal.java` (texture `redcapsapper.png`).

| Field | Value |
|---|---|
| Attributes | Redcap + max_health 30.0, armor 2.0 |
| Equipment | main hand `gloamingforest:ironwood_pickaxe`, feet `gloamingforest:ironwood_boots`; 3 TNT |
| Extra goal | 4 RedcapPlantTNT |
| Spawn egg | `gloamingforest:redcap_sapper_spawn_egg` 0x575d21 / 0xab1e14 |

**RedcapPlantTNT**: usable with a target within distance^2 < 25, TNT left, the target **not** looking at it, mob
griefing allowed, no primed TNT within 8 blocks and no TNT block within 5; start: hold TNT; if its own block is empty,
use one TNT, play ambient, place `minecraft:tnt` there, game event BLOCK_PLACE (the light goal then ignites it);
stop: hold the pickaxe.

Loot `gloamingforest:entities/redcap_sapper`: 1 pool referencing `gloamingforest:entities/redcap`.

### 2.11 `gloamingforest:wraith` (Wraith)

Source: `entity/monster/Wraith.java`, `entity/EnforcedHomePoint.java`, `entity/ai/control/NoClipMoveControl.java`,
`entity/ai/goal/SimplifiedAttackGoal.java`, `client/model/entity/WraithModel.java`, `client/renderer/entity/WraithRenderer.java` (`ghost.png`, shadow 0.5).

| Field | Value |
|---|---|
| Base | `FlyingMob` + `Enemy` (not a PathfinderMob); **noPhysics** (passes through blocks) |
| Size | 0.6 x 2.1, fire immune |
| Attributes | max_health 20.0, movement_speed 0.5, attack_damage 5.0 |
| Home | synced optional GlobalPos; set to its spawn position when spawned by STRUCTURE or SPAWNER; radius 20; saved `HomePos` (legacy `Home` list read as Gloaming dimension) |
| Other | `isSteppingCarefully` true; despawns in Peaceful; cannot ride |
| Spawn egg | `gloamingforest:wraith_spawn_egg` 0x505050 / 0x838383 |

AI goals: 2 MoveTowardsHome 0.85 (outside home area and no target: wanted position = home pos + random direction +
(rand5, rand5, rand5) if loaded); 4 SimplifiedAttack; 5 FlyTowardsTarget (sets wanted position to the target at
speed 0.5, one-shot); 6 RandomFloatAround (no target and inside home: when the wanted point is < 1 or > 60 blocks away,
pick a point +-16 on each axis at speed 0.5); 7 LookAround (ghast look: face motion, or the target within 64 blocks).
Targets: 1 NearestAttackable Player (must see = false).

- **NoClipMoveControl**: in MOVE_TO, every `2 + rand(5)` ticks add `(d / |d|) x 0.1 x speed` to the motion.
- **SimplifiedAttackGoal** (F8 `goals::simplifiedAttack`): updates every tick; when the target is in melee range
  and visible and the cooldown is 0: cooldown 20 (adjusted tick delay), swing, `doHurtTarget`.
- `hurt`: when damaged by a living non-creative attacker other than its vehicle/passengers, it targets the attacker.
- `doHurtTarget`: first `gloamingforest:haunt` damage equal to attack_damage, then the vanilla melee hit.

Model: arms forward (`xRot = -PI/2`) with attack swing and `AnimationUtils.bobArms`; rendered at **alpha 0.6**.

Sounds: `entity.gloamingforest.wraith.ambient` ("Wraith gasps"), `entity.gloamingforest.wraith.hurt`,
`entity.gloamingforest.wraith.death`.

Spawn (`Wraith.checkMonsterSpawnRules`): not Peaceful, dark enough, `checkMobSpawnRules`. Large hollow hill
controlled spawns weight 2, hollow hill spawners and the graveyard feature (by string).

Loot `gloamingforest:entities/wraith`: `minecraft:glowstone_dust` 0..2 + looting 0..1.

Bedrock: no-clip flight needs a custom actor with collision disabled (`minecraft:physics` has_collision false) and
the F8 move control; translucency is a client render state (vanilla clients: alpha texture).

### 2.12 `gloamingforest:fire_beetle` (FireBeetle, IBreathAttacker)

Source: `entity/monster/FireBeetle.java`, `client/model/entity/FireBeetleModel.java`, renderer `GloamingGenericMobRenderer` (`firebeetle.png`, shadow 0.8).

| Field | Value |
|---|---|
| Size | 1.1 x 0.5, **fire immune** |
| Attributes | max_health 25.0, movement_speed 0.23, attack_damage 4.0 |
| Synced | `BREATHING` boolean |
| Max head pitch | 500 (unrestricted) |
| Spawn egg | `gloamingforest:fire_beetle_spawn_egg` 0x1d0b00 / 0xcb6f25 |

AI goals: 0 Float; 2 BreathAttack (5.0, 30 ticks, 0.1); 3 MeleeAttack 1.0; 6 WaterAvoidingRandomStroll 1.0. Targets:
1 HurtBy; 2 NearestAttackable Player.

Breath: `doBreathAttack(target)`: if the target is not fire immune and takes 2 damage of type
`gloamingforest:scorched`, set it on fire for 10 s. `doHurtTarget` while breathing: 2 scorched damage only (no
ignition); otherwise normal melee. While breathing each tick: 2 `minecraft:flame` particles 0.9 ahead at y + 0.25,
velocity look + gaussian `0.0075 x (5 + rand x 2.5)` times `0.15 + rand x 0.15`; sound
`entity.gloamingforest.fire_beetle.shoot` ("Fire Beetle spews flames") vol rand x 0.5, pitch rand x 0.5; game event
PROJECTILE_SHOOT.

Model: six legs with the vanilla spider-style gait (`legZ = PI/11`, yaw offsets 0.3926991 x {2, 1, -2}, swing
`cos(limbSwing x 0.6662 x 2 + phase) x 0.4` and lift `|sin(limbSwing x 0.6662 + phase) x 0.4|`).

Sounds: `entity.gloamingforest.fire_beetle.ambient` ("Fire Beetle chitters"), `entity.gloamingforest.fire_beetle.hurt`,
`entity.gloamingforest.fire_beetle.death`, `entity.gloamingforest.fire_beetle.step` (vol 0.15).

Spawn: `Monster::checkMonsterSpawnRules`; hollow hills, labyrinth (by string).

Loot `gloamingforest:entities/fire_beetle`: `minecraft:gunpowder` 0..2 + looting 0..1.

### 2.13 `gloamingforest:slime_beetle` (SlimeBeetle, RangedAttackMob)

Source: `entity/monster/SlimeBeetle.java`, `client/model/entity/SlimeBeetleModel.java`, `client/renderer/entity/SlimeBeetleRenderer.java` (`slimebeetle.png`, shadow 0.6).

| Field | Value |
|---|---|
| Size | 0.9 x 0.5 |
| Attributes | max_health 25.0, movement_speed 0.23, attack_damage 4.0 |
| Spawn egg | `gloamingforest:slime_beetle_spawn_egg` 0x0c1606 / 0x60a74c |

AI goals: 0 Float; 2 AvoidEntity Player 3.0 (1.25 / 2.0); 3 RangedAttack (speed 1.0, interval 30, radius 10.0);
6 WaterAvoidingRandomStroll 1.0; 7 LookAtPlayer 8.0; 8 RandomLookAround. Targets: 1 HurtBy; 2 Player.

Ranged attack: spawn `gloamingforest:slime_blob`, play `entity.gloamingforest.slime_beetle.squish` ("Slime Beetle
shoots") vol 1, pitch `1 / (rand x 0.4 + 0.8)`; aim `ty = target.y + eyeHeight - 1.1 - projectile.y`, + 0.2 x
horizontal distance; velocity 0.6, inaccuracy 6.0.

Model / render: same leg gait as the fire beetle; tail wiggle `tailBottom.xRot = cos(age x 0.3335) x 0.15`,
`tailTop.xRot = cos(age x 0.4445) x 0.2`, `slimeCenter.xRot = cos(age x 0.5555 + 0.25) x 0.25`; the opaque pass hides
the slime; a second translucent pass (`OuterTailLayer`, `entityTranslucent`) draws the slime tail.

Sounds: `entity.gloamingforest.slime_beetle.ambient` ("Slime Beetle chitters"), `entity.gloamingforest.slime_beetle.hurt`,
`entity.gloamingforest.slime_beetle.death`, `entity.gloamingforest.slime_beetle.step` (vol 0.15).

Spawn: `Monster::checkMonsterSpawnRules`; hollow hills, labyrinth, knight stronghold (by string).

Loot `gloamingforest:entities/slime_beetle`: `minecraft:slime_ball` 0..2 + looting 0..1.

### 2.14 `gloamingforest:slime_blob` (SlimeProjectile)

Source: `entity/projectile/SlimeProjectile.java`; rendered as `minecraft:slime_ball` item.

- 0.25 x 0.25, gravity 0.006, tracking 150, interval 3.
- Trail: 2 `minecraft:item_slime` particles per tick.
- Any damage to it: it dies (returns true).
- Hit a living entity (server): 4.0 `thrown` damage (direct = blob, attacker = owner).
- Any hit or death: play vanilla `entity.slime.squish` vol 1, pitch `1 / (rand x 0.4 + 0.8)`, discard, entity event 3
  -> 8 `item_slime` particles (gaussian 0.05, up 0..0.2).

### 2.15 `gloamingforest:pinch_beetle` (PinchBeetle, IHostileMount)

Source: `entity/monster/PinchBeetle.java`, `entity/ai/goal/ChargeAttackGoal.java`, `events/HostileMountEvents.java`,
`util/entities/EntityUtil.java#properlyApplyCustomDamageSource`, `client/model/entity/PinchBeetleModel.java`, renderer
`GloamingGenericMobRenderer` (`pinchbeetle.png`, shadow 0.6).

| Field | Value |
|---|---|
| Size | 1.2 x 0.5; **2.2 x 1.6 while carrying a passenger** (dimensions refreshed every aiStep) |
| Attributes | max_health 40.0, movement_speed 0.23, attack_damage 4.0, armor 2.0 |
| Passenger attachment | (0, eyeHeight, 0.75) |
| Spawn egg | `gloamingforest:pinch_beetle_spawn_egg` 0xbc9327 / 0x241609 |

AI goals: 0 Float; 2 ChargeAttack 1.5 (cannot break blocks); 4 MeleeAttack 1.0; 6 WaterAvoidingRandomStroll 1.0;
7 LookAtPlayer 8.0; 8 RandomLookAround. Targets: 1 HurtBy; 2 NearestAttackable Player.

- **ChargeAttackGoal** (F8 `goals::charge`): needs a target at distance^2 16..64, on ground, visible, chance 1/10;
  charge point = 2.1 blocks beyond the target along the line; windup `15 + rand(30)` ticks with sprinting and walk
  animation speed + 0.8 per tick, then path to the point at the given speed; hits once (`doHurtTarget`) when within
  `(2 x width)^2 + target width` of the target's feet; stop clears sprinting.
- **Grab** (`doHurtTarget`): with no passenger, unless the victim's vehicle type is in
  `#gloamingforest:rides_obstruct_snatching`, the victim is dismounted and force-mounted on the beetle. Then
  `gloamingforest:clamped` damage = attack_damage modified by weapon enchantments, knockback from the knockback
  attribute (x 0.5 along its yaw, beetle motion x 0.6 horizontally), post-attack enchantment effects.
- While carrying: looks at the passenger (100/100), always targets it, drops a passenger whose vehicle changed, and
  releases (and stops targeting) an invulnerable (creative) player; no knockback taken; death dismounts all.
- Riding a boat (`startRiding(Boat)`): the boat is killed and drops 3 of its planks and 2 sticks (if
  `doEntityDrops`), sound `minecraft:entity.zombie.break_wooden_door`, and the ride is refused.
- `canRiderInteract` true (the held player can still hit it).
- Hostile-mount rules for its rider (F8 shared, `HostileMountEvents`): no `in_wall` damage for a held player, no
  teleporting, no dismount (except invulnerable players or a forced `hostileDismount`), shift is reset every tick.

Model: pincers open while carrying (right pincer yRot -170 degrees, left 20 degrees) and closed otherwise (135 / 45
degrees; the Jappa values are not ported); spider-style legs.

Sounds: `entity.gloamingforest.pinch_beetle.ambient` ("Pinch Beetle chitters"), `entity.gloamingforest.pinch_beetle.hurt`,
`entity.gloamingforest.pinch_beetle.death`, `entity.gloamingforest.pinch_beetle.step` (vol 0.15), and
`entity.gloamingforest.pinch_beetle.attack` ("Pinch Beetle clamps") which is the hurt sound of the `clamped` damage
effect (`gloamingforest:pinch` DamageEffects extension, `GloamingEnumExtensions.java`).

Spawn: `Monster::checkMonsterSpawnRules`; hollow hills, labyrinth, dark tower (by string).

Loot `gloamingforest:entities/pinch_beetle`: empty (no pools).

Bedrock: the grab uses `gf::entity::mount::grab`; the held player's position comes from the server
(SetActorLink + motion); vanilla clients can press sneak but the server re-links them.

## 3. Damage types (owned by U17)

All five are `data/gloamingforest/damage_type/*.json`; death messages are F10's (`death.attack.*`, R9), damage-type
tags are F1 imports.

| Id | message_id | Effects | Exhaustion | Scaling | Tags | Used by |
|---|---|---|---|---|---|---|
| `gloamingforest:ant` | `gloamingforest.ant` ("%1$s was squashed like an ant by %2$s") | hurt | 0.1 | when caused by living non-player | `#neoforge:is_physical` | giant miner melee (U29) |
| `gloamingforest:chilling_breath` | `gloamingforest.chillingBreath` ("%1$s was frozen to death by the Snow Queen") | freezing | 0.0 | when caused by living non-player | `#neoforge:is_magic` | Snow Queen breath (U28) |
| `gloamingforest:clamped` | `gloamingforest.clamped` ("%1$s was squeezed to death by %2$s") | `gloamingforest:pinch` (hurt sound `entity.gloamingforest.pinch_beetle.attack`) | 0.1 | when caused by living non-player | `#neoforge:is_physical`, `#minecraft:no_knockback` | pinch beetle |
| `gloamingforest:haunt` | `gloamingforest.haunt` ("%1$s joined the %2$s's haunt") | hurt | 0.1 | when caused by living non-player | `#neoforge:is_magic` | wraith, knight phantom (U25) |
| `gloamingforest:scorched` | `gloamingforest.scorched` ("%1$s was scorched by %2$s") | burning | 0.1 | when caused by living non-player | `#minecraft:is_fire`, `#minecraft:ignites_armor_stands` | fire beetle |

Bedrock: damage types map to the F10 combat contract (`gf::combat::hurt(victim, amount, typeId, attacker, direct)`),
which owns the death message and the type tags.

## 4. Entity tags owned by U17

| Tag | Members (upstream data) | Consumer |
|---|---|---|
| `entity_type/#gloamingforest:bosses` | naga, lich, minoshroom, hydra, knight_phantom, ur_ghast, alpha_yeti, snow_queen, plateau_boss | `#c:bosses` (F1) and mod-compat tags; keep members as strings |
| `entity_type/#gloamingforest:lifedrain_drops_no_flesh` | `#minecraft:skeletons`, `#minecraft:frog_food`, blaze, breeze, iron_golem, phantom, shulker, skeleton_horse, snow_golem, vex, wither, carminite_golem, death_tome, ice_crystal, knight_phantom, lich, mosquito_swarm, snow_guardian, stable_ice_core, unstable_ice_core, wraith | lifedrain scepter (U9) does not drop rotten flesh from these |
| `entity_type/#gloamingforest:multiplayer_inclusive_entities` | naga, lich, minoshroom, hydra, ur_ghast, alpha_yeti, snow_queen, plateau_boss | F8 multiplayer fight adjuster (`events/EntityEvents.java`) |
| `entity_type/#gloamingforest:rides_obstruct_snatching` | pinch_beetle, yeti, alpha_yeti | pinch beetle grab and `ThrowRiderGoal` (U27): victims riding these are not snatched |
| `entity_type/#gloamingforest:sortable_entities` | `minecraft:chest_minecart`, `minecraft:hopper_minecart`, `minecraft:llama`, `minecraft:trader_llama`, `minecraft:donkey`, `minecraft:mule` | sorting log core (U3) treats them as inventories within 2 / 16 blocks |

Other units add members only through union fragments (R6); U17 ships these five as its own data.

## 5. Spawning summary

| Entity | Placement predicate | Natural biome spawns | Structures (by string, owned there) |
|---|---|---|---|
| hostile_wolf | `checkWolfSpawnRules` | none | hedge maze, fallen trunk spawners |
| mist_wolf | monster rules | dark_forest 5, 1-1 | - |
| winter_wolf | `canSpawnHere` | snowy_forest 5, 1-1 | - |
| king_spider | monster rules | dark_forest 1, 1-1 | - |
| hedge_spider | `HedgeSpider.canSpawn` | none | hedge maze, lich tower |
| swarm_spider | `getCanSpawnHere` | none | hollow hills, hedge maze, hollow tree, fallen trunk, lich tower |
| skeleton_druid | `checkDruidSpawnRules` | dark_forest 5, spooky_forest 5 | druid hut |
| redcap | monster rules | none | hollow hills |
| redcap_sapper | monster rules | none | hollow hills, knight stronghold |
| wraith | `Wraith.checkMonsterSpawnRules` | none | large hollow hill, graveyard |
| fire_beetle, slime_beetle, pinch_beetle | monster rules | none | hollow hills, labyrinth, stronghold, dark tower |

## 6. Recipes (compat, not ported)

`gloamingforest:jeed/blindness` (`jeed:effect_provider`: `minecraft:blindness` from `gloamingforest:mist_wolf_spawn_egg`
and `gloamingforest:duskberry`) and `gloamingforest:jeed/poison` (`minecraft:poison` from
`gloamingforest:skeleton_druid_spawn_egg` and `gloamingforest:blightberry`) load only with the JEED mod: **not ported**
on Bedrock; U17 owns the ids only.

## 7. Spawn eggs

`gloamingforest:hostile_wolf_spawn_egg`, `gloamingforest:mist_wolf_spawn_egg`, `gloamingforest:winter_wolf_spawn_egg`,
`gloamingforest:king_spider_spawn_egg`, `gloamingforest:hedge_spider_spawn_egg`, `gloamingforest:swarm_spider_spawn_egg`,
`gloamingforest:skeleton_druid_spawn_egg`, `gloamingforest:redcap_spawn_egg`, `gloamingforest:redcap_sapper_spawn_egg`,
`gloamingforest:wraith_spawn_egg`, `gloamingforest:fire_beetle_spawn_egg`, `gloamingforest:slime_beetle_spawn_egg`,
`gloamingforest:pinch_beetle_spawn_egg` (colours in each entity table). Bedrock: `minecraft:spawn_egg` base/overlay.

## 8. Sound events (all owned by U17)

Upstream's `sounds.json` maps several to vanilla events (spiders -> `entity.spider.*`, druid -> `entity.stray.*` and
`entity.skeleton.step`, wolf deaths -> `entity.wolf.death`, shoots -> `entity.ghast.shoot`, slime squish ->
`entity.slime.squish_small`); map those to the vanilla Bedrock sounds in `tools/sounds/recipes/hostiles.json`, the
rest are synthesized.

| Event | Subtitle | Event | Subtitle |
|---|---|---|---|
| `entity.gloamingforest.fire_beetle.ambient` | Fire Beetle chitters | `entity.gloamingforest.pinch_beetle.ambient` | Pinch Beetle chitters |
| `entity.gloamingforest.fire_beetle.hurt` | Fire Beetle hurts | `entity.gloamingforest.pinch_beetle.attack` | Pinch Beetle clamps |
| `entity.gloamingforest.fire_beetle.death` | Fire Beetle dies | `entity.gloamingforest.pinch_beetle.hurt` | Pinch Beetle hurts |
| `entity.gloamingforest.fire_beetle.shoot` | Fire Beetle spews flames | `entity.gloamingforest.pinch_beetle.death` | Pinch Beetle dies |
| `entity.gloamingforest.fire_beetle.step` | footsteps | `entity.gloamingforest.pinch_beetle.step` | footsteps |
| `entity.gloamingforest.hedge_spider.ambient` | Hedge Spider hisses | `entity.gloamingforest.redcap.ambient` | Redcap chuckles |
| `entity.gloamingforest.hedge_spider.hurt` | Hedge Spider hurts | `entity.gloamingforest.redcap.hurt` | Redcap screeches in pain |
| `entity.gloamingforest.hedge_spider.death` | Hedge Spider dies | `entity.gloamingforest.redcap.death` | Redcap groans in agony |
| `entity.gloamingforest.hedge_spider.step` | footsteps | `entity.gloamingforest.skeleton_druid.ambient` | Skeleton Druid rattles |
| `entity.gloamingforest.hostile_wolf.ambient` | Hostile Wolf growls | `entity.gloamingforest.skeleton_druid.hurt` | Skeleton Druid hurts |
| `entity.gloamingforest.hostile_wolf.hurt` | Hostile Wolf hurts | `entity.gloamingforest.skeleton_druid.death` | Skeleton Druid dies |
| `entity.gloamingforest.hostile_wolf.death` | Hostile Wolf dies | `entity.gloamingforest.skeleton_druid.shoot` | Skeleton Druid shoots |
| `entity.gloamingforest.hostile_wolf.target` | Hostile Wolf takes notice | `entity.gloamingforest.skeleton_druid.step` | footsteps |
| `entity.gloamingforest.king_spider.ambient` | King Spider hisses | `entity.gloamingforest.slime_beetle.ambient` | Slime Beetle chitters |
| `entity.gloamingforest.king_spider.hurt` | King Spider hurts | `entity.gloamingforest.slime_beetle.hurt` | Slime Beetle hurts |
| `entity.gloamingforest.king_spider.death` | King Spider dies | `entity.gloamingforest.slime_beetle.death` | Slime Beetle dies |
| `entity.gloamingforest.king_spider.step` | footsteps | `entity.gloamingforest.slime_beetle.squish` | Slime Beetle shoots |
| `entity.gloamingforest.mist_wolf.ambient` | Mist Wolf growls | `entity.gloamingforest.slime_beetle.step` | footsteps |
| `entity.gloamingforest.mist_wolf.hurt` | Mist Wolf hurts | `entity.gloamingforest.swarm_spider.ambient` | Swarm Spider hisses |
| `entity.gloamingforest.mist_wolf.death` | Mist Wolf dies | `entity.gloamingforest.swarm_spider.hurt` | Swarm Spider hurts |
| `entity.gloamingforest.mist_wolf.target` | Mist Wolf takes notice | `entity.gloamingforest.swarm_spider.death` | Swarm Spider dies |
| `entity.gloamingforest.winter_wolf.ambient` | Winter Wolf growls | `entity.gloamingforest.swarm_spider.step` | footsteps |
| `entity.gloamingforest.winter_wolf.hurt` | Winter Wolf hurts | `entity.gloamingforest.wraith.ambient` | Wraith gasps |
| `entity.gloamingforest.winter_wolf.death` | Winter Wolf dies | `entity.gloamingforest.wraith.hurt` | Wraith hurts |
| `entity.gloamingforest.winter_wolf.shoot` | Winter Wolf shoots | `entity.gloamingforest.wraith.death` | Wraith dies |
| `entity.gloamingforest.winter_wolf.target` | Winter Wolf takes notice | | |

## 9. Bedrock adaptation notes

- Spider-derived mobs (king, hedge, swarm) cannot extend the vanilla Bedrock spider runtime: build custom entities
  with `minecraft:navigation.climb` (except the king spider, which must not climb), the vanilla-equivalent goals
  with upstream's replacements, and the light rules translated to the Gloaming monster light (0..7, block light <= 7).
- Skeleton druid: custom humanoid entity with sun burning (`minecraft:burns_in_daylight`), hoe-dependent ranged goal
  via `gf::projectile::shoot("gloamingforest:nature_bolt", ...)`.
- Breath attacks (winter wolf, fire beetle) run as F8 goals; the breathing flag is an actor property used by the
  client animation and by the client-side particle emitter; vanilla clients get server-sent particles
  (`gf::fx::particle`, throttled to the same count).
- Redcap TNT: block edits from goals happen in the server tick (no interaction rollback issue). Mob griefing =
  the `mobGriefing` game rule.

## 10. Acceptance (selftests `hostiles.*`, dx 1700-1799)

Each mob spawns with upstream's attributes (F13 goldens); each special against a SimulatedPlayer: mist wolf blindness at
light 0 (and none on Easy), winter wolf and fire beetle breath damage (fire beetle ignites for 10 s, scorched death
message), pinch beetle grabs the player (player rides it, cannot dismount, released when creative), redcap sapper
plants and a redcap lights TNT, skeleton druid nature bolt poisons and turns a log into persistent birch leaves,
slime blob deals 4, swarm spider 1/4 hit chance (seeded), king spider spawns with a druid rider, wraith passes
through a wall and returns home. Client: one screenshot per mob (mist wolf translucency in the dark, slime beetle
translucent tail, pinch beetle open pincers).
