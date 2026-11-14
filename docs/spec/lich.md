# U21 `lich`: Lich tower and Lich

Behaviour spec translated from the upstream 4.8 source (commit `9e40a757`). It is the
checklist the U21 worker implements and the W2 audit (A3) compares against. Numbers are upstream's; "Bedrock:" paragraphs
mark adaptation notes.

| | |
|---|---|
| Unit | U21 `lich` (W1 batch 1) |
| Selftests | `lich.*`, testPos dx `2100-2199` (owners rows `selftest lich.*`, `selftest_dx 2100-2199`) |
| World DB | `gf:lich:*` |
| Net channels | `lich.*` |
| Config | none (upstream has no Lich key; `bosses` (F8) holds boss drop chests and the multiplayer adjuster) |
| Contracts | landmark (F6), structure + template (F7), boss + entity + projectile (F8), loot + combat + fx (F10) |
| Java sources | `entity/boss/Lich.java`, `entity/ai/goal/Lich{PopMobs,AbsorbMinions,Shadows,Minions}Goal.java`, `entity/monster/{LichMinion,DeathTome,RisingZombie}.java`, `entity/projectile/{LichBolt,LichBomb,TomeBolt,GloamingThrowable}.java`, `block/entity/spawner/LichSpawnerBlockEntity.java`, `init/{GloamingEntities,GloamingAttributes,GloamingDamageTypes}.java`, `loot/conditions/IsMinionCondition.java`, `client/renderer/entity/LichRenderer.java`, `client/particle/AngryLichParticle.java`, `world/components/structures/type/LichTowerStructure.java`, `world/components/structures/lichtowerrevamp/*.java` (20 files), `world/components/structures/lichtower/*.java` (16 legacy piece classes, implemented by F7) |
| Data | `loot_table/chests/tower_*.json`, `loot_table/entities/{lich,lich_minion,death_tome*,rising_zombie}.json`, `damage_type/{leaf_brain,lich_bolt,lich_bomb,lost_words,schooled}.json`, `worldgen/structure/lich_tower.json`, `worldgen/structure_set/lich_tower.json`, `gloaming/template_definition/lich_tower/**` (165 files), tags `damage_type/breaks_lich_shields`, `entity_type/lich_deflects_phase_2`, `entity_type/lich_poppables`, `banner_pattern/pattern_item/lich` |

Not owned here (string references only, R2): `gloamingforest:lich_boss_spawner`, `gloamingforest:lich_trophy`, the
scepters `gloamingforest:gloaming_scepter` / `lifedrain_scepter` / `zombie_scepter` / `fortification_scepter` (U9; the
Lich only holds them as display items), the attribute `gloamingforest:max_shield_strength` and the fortification shield
attachment (U9), the sounds `entity.gloamingforest.fortification_shield.add` / `.block` / `.break` (U9; GloamingSounds
`SHIELD_ADD` / `SHIELD_BLOCK` / `SHIELD_BREAK`) and the particles `gloamingforest:shield_break` and
`gloamingforest:magic_effect` (U9), the `gloamingforest:ominous_flame` particle, ominous candles and lightable blocks
(U22), the candelabra, skull candles, sinister spawner, chiseled canopy shelf, wrought iron fence, magic painting (U22),
mason jar / firefly jar (U6/U4), `gloamingforest:bold_stone_pillar` and `twisted_stone_pillar` (U5), the Kobold hint mob
(U17), the `tower_paintings` / `tower_boss_paintings` painting tags (U26), the processors (F7),
`gloamingforest:progress_naga` (U14).

## 1. Entity `gloamingforest:lich` (Gloaming Lich)

`GloamingEntities.LICH`: MONSTER, 0.8 x 2.5, eye height 1.95, not fire immune, tracking 80, interval 3. Spawn egg
`gloamingforest:lich_spawn_egg` (`0xaca489` / `0x360472`, "Gloaming Lich Spawn Egg"). Placement ON_GROUND /
MOTION_BLOCKING_NO_LEAVES / `checkMonsterSpawnRules`. Name `entity.gloamingforest.lich` "Gloaming Lich". Entity tags:
`minecraft:skeletons`, `#gloamingforest:bosses`; `#gloamingforest:lich_poppables` excludes it (the tag removes `#c:bosses`, which contains `#gloamingforest:bosses`).

### 1.1 Attributes and constants

| Item | Value |
|---|---|
| max health | 100 (MAX_HEALTH) |
| attack damage | 3.0 |
| movement speed | 0.45 |
| follow range | 35.0 |
| `gloamingforest:clone_count` | 2 (MAX_SHADOW_CLONES) |
| `gloamingforest:max_shield_strength` | 6 (MAX_SHIELD_STRENGTH; attribute owned by U9) |
| `gloamingforest:minion_count` | 9 (MAX_MINIONS_TO_SUMMON) |
| MAX_ACTIVE_MINIONS | 3 |
| xp reward | 217 |
| home radius | 30 (`getHomeRadius`), home structure `gloamingforest:lich_tower` |
| death container | 50/50 `gloamingforest:canopy_chest` / `gloamingforest:gloaming_oak_chest` |
| boss spawner | `gloamingforest:lich_boss_spawner` |
| death animation | PARTICLE_BURST_COOLDOWN 23; POINT_A = 115; POINT_B = 131; POINT_C = 163; DURATION = 295 ticks (`BossSpec.deathTicks = 295`) |

Constructor: shield strength and minions-to-summon are initialised from the attributes (6, 9).
`finalizeSpawn` (not a clone): hold `gloamingforest:fortification_scepter`, play
`entity.gloamingforest.fortification_shield.add` (1.5, voice pitch), swing the main hand.

### 1.2 Synced data and saved data

Synced: `MASTER_LICH` (optional UUID; present = shadow clone), `SHIELD_STRENGTH` (int, default 6), `MINIONS_LEFT`
(int, default 9), `ATTACK_TYPE` (int: 0 bolt, 1 bomb), `TELEPORT_INVISIBILITY` (int ticks). Saved: `MasterLich`,
`SummonedClones` (UUID list), `ShieldStrength`, `MinionsToSummon`, `BabyMinionsSummoned`, `HitsWithoutTeleport`.
Server fields: `attackCooldown`, `popCooldown`, `heldScepterTime`, `spawnTime`, `previousPhase` (1).

### 1.3 Phases

`getPhase()`: 1 when a shadow clone or `shieldStrength > 0`; else 2 when `minionsToSummon > 0` or it still has
minions; else 3. Phase 1 reflects bolts and keeps the shield up, phase 2 summons minions, phase 3 melee.

### 1.4 Goals

| Prio | Goal | Behaviour |
|---|---|---|
| 0 | FloatGoal | |
| 0 | AttemptToGoHomeGoal(1.25) | only when `isOutsideHomeRange(position)`; every tick (`requiresUpdateEveryTick`); skipped while teleport-invisible; when the path is null, stuck or unreachable: `teleportToNewTarget(target, 20, null)` else `teleportHome()` |
| 1 | RandomLookAroundIfBoredGoal, AlwaysWatchTargetGoal | F8 goals |
| 1 | LichPopMobsGoal | 1.6 |
| 1 | LichAbsorbMinionsGoal | 1.7 |
| 2 | LichShadowsGoal(attackRange 30) | phase 1 |
| 3 | LichMinionsGoal | phase 2 |
| 4 | MeleeAttackGoal(0.75, followEvenIfNotSeen true) | only in phase 3; skipped while teleport-invisible; when the target is out of melee reach and navigation is done and `moveTo(target)` fails: `teleportToSightOfEntity(target)`; on start equips a golden sword |
| - | restriction goals | `addRestrictionGoals` (F8 home) |
| target 1 | HurtByTargetGoal | ignores hurts from a Lich sharing its master |
| target 2 | NearestAttackableTargetGoal(Player, mustSee false) | |

`isOutsideHomeRange(pos)`: no restriction point -> false; no target and `y < home.y - 2` -> true; else
`distSq(home centre, pos) > 30^2` or (phase 3 and `y < home.y - 3`).

### 1.5 Server step (`customServerAiStep`)

- Outside the home range and not teleport-invisible: `teleportHome()`.
- `attackCooldown--` while > 0 and `spawnTime <= 0`.
- `popCooldown--` while > 0, health < max and no scepter time left.
- Scepter time 0, pop cooldown < 30 and holding the lifedrain scepter -> hold the zombie scepter (phase 2) or a
  golden sword.
- `heldScepterTime--` while > 0.
- With a target: while `spawnTime > 0` and `spawnTime-- < 40`, `turnNearbyCandles(40 - spawnTime, done)` (done on
  the last tick).

`aiStep` (both sides): while teleport-invisible, keep looking at the target, decrement the counter, and when it
reaches 0 play the appear particles (1.10) and `entity.gloamingforest.lich.teleport` (1.125, 1.125); return. Dying ->
return. Phase 3 -> one `gloamingforest:angry_lich` particle per tick at random x/z within 0.65, y = eye + 0.25 +
rand * 0.5, gaussian velocity 0.02; return. Otherwise hand particles at body yaw `(cos * 0.65, height * 0.94, sin *
0.65)`: `factor = (80 - attackCooldown) / 10`, count = `nextInt(factor)` (1 if factor <= 0); colour sparkle
`(1 - (cooldown + 1) / 60)^2` times `(0.37, 0.99, 0.89)` for a bolt, `(0.99, 0.47, 0.0)` for a bomb, particle
`gloamingforest:magic_effect` (colour option) with gaussian 0.025 jitter.

### 1.6 Pop mobs (`LichPopMobsGoal`, flags LOOK + MOVE, every tick)

`canUse`: not a clone, health < max, `popCooldown == 0`, and a mob of `#gloamingforest:lich_poppables` in line of
sight within the box inflated (32, 16, 32). Start: `setScepterTime()` (hold lifedrain scepter for `20 + nextInt(20)`
ticks); stop: reset scepter time. Tick (not teleport-invisible, scepter time elapsed, server): the first poppable
mob in sight is discarded; LifedrainScepterItem shatter particles; its death sound at its position (HOSTILE, 1.0,
its pitch); `entity.gloamingforest.lich.pop_mob` at the Lich and at the mob (3.0, `0.4 + rand * 0.2`); magic trail
mob eye -> Lich eye colour (1.0, 0.5, 0.5); heal 2.0; swing; `popCooldown = 40`; game event ENTITY_DIE.

### 1.7 Absorb minions (`LichAbsorbMinionsGoal`)

`canUse`: health < max / 2 and it owns minions. Same scepter timing; tick: discard the first own minion within
(32, 16, 32), shatter particles, the minion's death sound, pop sound x2, trail (1.0, 0.5, 0.5), **heal by the
minion's current health**, swing, `popCooldown = 40`, ENTITY_DIE.

### 1.8 Phase 1: shadows (`LichShadowsGoal`)

`canUse`: phase 1, `tickCount > 20`, has a target. Start: hold `gloamingforest:gloaming_scepter`. Stop: despawn
clones. Tick: the master copies its attack cooldown and teleport invisibility to every clone; skip while
invisible. Master: at `attackCooldown == 60` -> `teleportToNewTarget(target, 30, this)` else `teleportHome()`; at
cooldown 0 and target within 30 -> attack with itself and every clone (clones adopt the target). `attack(lich)`:
`nextAttackType == 0` -> LichBolt else LichBomb via `launchProjectileAt`; swing; next type = bolt with
probability 2/3 (`nextInt(3) > 0`), bomb 1/3; cooldown 100. A clone without a master looks for one (a non-clone
Lich within (32, 16, 32) that `wantsNewClone`) and discards itself when its master is gone, dead or past phase 1.

Clones (`checkAndSpawnClones`, called after a successful teleport in phase 1): while own clones < `clone_count`,
spawn `new Lich(level, master)` at `findVecInLOSOf(target)`, target set, attack cooldown `60 + nextInt(3) -
nextInt(3)`, holding the gloaming scepter, same teleport invisibility; its UUID is added to `SummonedClones`.

### 1.9 Phase 2: minions (`LichMinionsGoal`, ATTACK_RANGE 20)

`canUse`: phase 2 and not a clone. Start: hold the zombie scepter. Tick (skip while invisible, needs a target):
- every tick where `attackCooldown % 15 == 0`: when minions-to-summon > 0 and own minions < 3: spawn one at
  `findVecInLOSOf(target)` (`LichMinion(level, this)`, `finalizeMobSpawn` MOB_SUMMONED, target set, spawn anim,
  `entity.gloamingforest.minion.summon` (1.0, `((r1 - r2) * 0.7 + 1) * 0.75`), swing, magic trail with random dim
  grey `0.125 + rand * 0.0625` per channel), then decrement minions-to-summon. `checkAndSpawnMinions` decrements
  the counter unconditionally after `spawnMinionAt`, so it is decremented even when `findVecInLOSOf` finds no spot
  (returns null) and `spawnMinionAt` places nothing: failed attempts still use up the summons (9 by default, from the
  `gloamingforest:minion_count` attribute), which shortens phase 2.
- strafing (bow-skeleton style): `seeTime` counts line-of-sight ticks; within 20 blocks and `seeTime >= 20` stop
  navigation and strafe; every 20 strafe ticks 30 % flip clockwise and 30 % flip backwards; beyond 15 blocks never
  backwards, closer than 5 always backwards; strafe `(back ? -0.5 : 0.5, cw ? 0.5 : -0.5)` and look at the target.
- at cooldown 0: distance < 2 -> melee (`doHurtTarget`), swing, cooldown 20; within 20 with line of sight -> bolt
  or bomb (50/50 for the next one), swing, cooldown 60; else `teleportToNewTarget(target, 20, null)`, cooldown 20.

### 1.10 Teleporting

- `teleportToNewTarget(target, range, shadowsGoal)`: pick a random living player within `range` as the target
  (else keep the given one); `teleportToSightOfEntity(target)`; on success every clone also re-targets and
  teleports, and in phase 1 `checkAndSpawnClones(target)`.
- `findVecInLOSOf(entity)`: up to 100 tries of `target + (gauss * 16, +2, gauss * 16)` through
  `randomTeleport(..., false)` (collision and ground check), line of sight from there, then teleport back; tries
  0..84 skip spots below the target's y; accept a spot that is clear, sees the target, is inside the home range and
  at least 5 blocks (distSq >= 25) from the target.
- `teleportToNoChecks`: disappear particles, `teleportTo`, `entity.gloamingforest.lich.teleport` (0.75, 0.75),
  game event TELEPORT, entity event 46 suppressed, `TELEPORT_INVISIBILITY = 20`, clear jumping and fire.
- While teleport-invisible: invisible, hides its held item client-side, cannot be targeted
  (`canBeSeenAsEnemy` false) and ignores damage that does not bypass invulnerability.
- `teleportHome()`: places `gloamingforest:bold_stone_pillar` two blocks below the home point when that is air,
  steps one block down when the block below the home point is air, then teleports there (centre of block).
- Particles: disappear = 128 SMOKE (64 for clones) in a 2-block spread; appear = 64 particles flying outwards at
  0.0625 of their offset, `gloamingforest:ominous_flame` (SMOKE for clones). Sent with upstream's ParticlePacket to
  tracking players (F10 `fx::particle`).
- Clones (`isShadowClone`) are invisible for their first 10 ticks.

### 1.11 Damage (`hurt`)

1. Teleport-invisible and the source does not bypass invulnerability -> ignored.
2. `minecraft:in_wall` with a target -> `teleportToNewTarget(target, 20, null)`.
3. Shadow clone and not bypassing -> play `entity.gloamingforest.lich_clone.hurt` (1.0, pitch * 2) and ignore.
4. Attacker is a Lich -> ignore (unreflected bolts).
5. Shield up (> 0) and not bypassing: damage of `#gloamingforest:breaks_lich_shields` with amount > 2 removes one
   shield, emits 16 `gloamingforest:shield_break` particles (below), plays the sound
   `entity.gloamingforest.fortification_shield.break` at volume `1.5 + 0.25 * (6 - new)` (+ 0.5 more when it
   reaches 0), pitch * 1.25, and fires ENTITY_DAMAGE; anything else plays the sound
   `entity.gloamingforest.fortification_shield.block` (0.75, pitch * 1.75), fires ENTITY_DAMAGE and, when the
   causing entity is living, records it as last hurt-by. No health damage either way.
   The particles are `FortificationShieldAttachment.addShieldBreakParticles(src, lich)` (server side, sent to the
   players tracking the Lich; U21 reproduces it, R2), drawn from the Lich's random. The source position starts as
   `src.getSourcePosition()`, becomes the direct entity's eye position when that entity is living, and then the
   owner's eye position when the causing entity is a `TraceableEntity` with a living owner. Let
   `base = position + (0, 0.65 * height, 0)`. With a source position: `offset` = the horizontal unit vector from
   `base` towards it, `centre = base + 0.55 * offset`; per particle `h = rand - 0.5`, then
   `d = (0.85 * offset.z * h, 0.85 * (rand - 0.5), -0.85 * offset.x * h)`, at `centre + d` with velocity `0.5 * d`.
   Without one: per particle at `base + d` with `d = (rand - 0.5, 0.25 * (rand - 0.5), rand - 0.5)` (drawn in that
   order) and velocity `0.33 * d`.
6. Otherwise vanilla damage; on success, when `nextInt(phase == 3 ? 6 : 3) <= hitsWithoutTeleport++` and alive:
   reset the counter and `teleportToNewTarget(target, 20, null)`.

`die`: the master despawns its clones (128 SMOKE each, discarded) and, if a shield was left, sets it to 0 and plays
`entity.gloamingforest.fortification_shield.break` (1.2, pitch * 2). Clones drop no loot (`getDefaultLootTable` null),
create no spawner and spawn no loot chest.

`deflection(projectile)`: projectiles of `#gloamingforest:lich_deflects_phase_2` owned by a player, a Lich or no
one are deflected in phases 2-3: velocity `(lich motion + (0.5 - rand, 0.75, 0.5 - rand)) * (0.75, 1.5, 0.75)`, owner
becomes the Lich, sound `entity.gloamingforest.fortification_shield.block` (0.5, pitch * 1.5), swing.

### 1.12 Candles (`turnNearbyCandles`, `lightNearbyCandles`)

- Spawn sequence (`setExtinguishTimer` = 40 ticks, started by the spawner): each tick
  `range = (int)(tick / 2.5 + 2)`, `yRange = (int)(tick / 2 + 2)`; every block in home +- (range, -3..yRange,
  range) with chance `1/max(40 - tick, 2)` (all on the last tick): a lit vanilla candle with an ominous counterpart
  becomes that ominous candle (U22, same candle count) with `block.gloamingforest.ominous_fire.light` (BLOCKS, 0.5,
  `(r1 - r2) * 0.2 + 0.75`); a lightable block (candelabra, skull candles) at lighting NORMAL becomes OMINOUS with
  `block.gloamingforest.candelabra.ominous` (same volume and pitch).
- Restore: when the spawner is placed back (`placeSpawner`) or the non-clone Lich is removed, every block within
  home +-40 (y -3..+40): ominous candles revert to the lit vanilla candle (`minecraft:block.fire.ambient`, 0.25,
  1.5); OMINOUS lightables revert to NORMAL (fire ambient, 0.25, 1.15).

Bedrock: candles are U22 blocks; switch them by block id and state (`lighting` state frozen in blockstates.md).

### 1.13 Boss bar

Hidden for clones. Phase 1: progress = shield / `max_shield_strength`, colour `0xFFD800`, overlay NOTCHED_6.
Phase 2: health fraction, colour `0xBE23FF`; phase 3: `0xFF0000`; overlay from BaseTFBoss (progress style). On a
phase change `updateStyle(colour, overlay, previousPhase != 1)`.

### 1.14 Death animation (295 ticks)

- Ticks 0..115: hurt sound every 23 ticks (death sound at 115) played locally; 12 particles (bone item / smoke) on
  a burst tick else 3; bursts add 7 bone/cloud particles and 6 (18 at 115) smoke particles flying outward at 0.0125.
  At 115: 32 bone/cloud particles. The held item is removed from tick 115. Before 115 the head turns (max 30 deg per
  tick) toward the target, the last attacking player or the nearest player within 20.
- Tick 131: 3 CLOUD particles at the feet. From 131 on, 33 % per tick a SMOKE particle near the feet.
- After 163: `gloamingforest:ominous_flame` spiral (5 per tick) from the body (y + 0.45) to the boss chest:
  `factor = (deathTime - 163) / 132`, position = lerp(start, chest, min(2 factor, 1)), radius
  `1.75 * (cos((factor + 0.5) * 2 PI) + 1) / 2`, angle `(2 factor^2 + i) * 2 PI`, y - 0.25.
- Renderer: scale 1.125; shaking before 115 (`sin(t^2) * 0.01` jitter); after 115 sinks by `-1.8 * (min((t - 115) /
  16, 1))^3`; no shadow after 115 or for clones; no tip-over.

### 1.15 Sounds

| Event | Use |
|---|---|
| `entity.gloamingforest.lich.ambient` | ambient (masters only); "Lich breathes" |
| `entity.gloamingforest.lich.hurt` | hurt; death sound at deathTime <= 1 (non-clones); "Lich hurts" |
| `entity.gloamingforest.lich.death` | death sound once deathTime > 1, and for clones; "Lich dies" |
| `entity.gloamingforest.lich.shoot` | `launchProjectileAt`: entity volume, pitch `(r1 - r2) * 0.2 + 1` (x 0.85 for bombs); "Lich shoots" (3 variants) |
| `entity.gloamingforest.lich.teleport` | teleport out (0.75, 0.75) and in (1.125, 1.125); "Lich teleports" |
| `entity.gloamingforest.lich.pop_mob` | pop / absorb (3.0, 0.4-0.6); "Lich absorbs mob" (3 variants) |
| `entity.gloamingforest.lich_clone.hurt` | clone hit; "Lich clone ignores attack" |
| `entity.gloamingforest.minion.ambient` | Lich minion ambient; "Lich Minion groans" |
| `entity.gloamingforest.minion.hurt` | Lich minion hurt; "Lich Minion hurts" |
| `entity.gloamingforest.minion.death` | Lich minion death; "Lich Minion dies" |
| `entity.gloamingforest.minion.step` | Lich minion step; generic footsteps subtitle `subtitles.block.generic.footsteps` |
| `entity.gloamingforest.minion.summon` | minion summoned; "Lich summons new minion" |
| `entity.gloamingforest.death_tome.ambient` | Death Tome ambient (not while on a lectern); "Death Tome flips pages" (2 variants) |
| `entity.gloamingforest.death_tome.hurt` | Death Tome hurt; "Death Tome creases" (3 variants) |
| `entity.gloamingforest.death_tome.death` | Death Tome death; "Death Tome falls apart" |

All 15 events are owned by U21 and re-synthesised through `tools/sounds/recipes/lich.json` (R10).

### 1.16 Rendering (client mod)

Texture `textures/entity/gloaminglich64.png`; layers: shield layer (fortification shields orbiting while shield >
0, U9's renderer) and an eyes layer `gloaminglich64_eyes.png` drawn only for visible shadow clones; clones render
with upstream's translucent SHADOW_CLONE render type. Particle `gloamingforest:angry_lich`: a heart-style particle,
lifetime 10, `yd -= 0.05`, colour white, scale 0.75 (sprite from the RP). Vanilla clients: clones are a separate
translucent texture variant (actor property), the angry particle is a Bedrock particle JSON.

### 1.17 Loot `gloamingforest:entities/lich`

- pool 1: rolls `gloamingforest:multiplayer_rolls` (default 1, per extra player -2..1): one of
  `gloaming_scepter`, `lifedrain_scepter`, `zombie_scepter`, `fortification_scepter`
- pool 2: rolls multiplayer (default 2-4, per player 0-1): golden sword / helmet / chestplate / leggings / boots
  enchanted with levels 10-40
- pool 3: ender pearl x1-4, +0-1 per looting, multiplayer addition 0-2 per player
- pool 4: bone x5-9, +0-1 per looting
- pool 5: `gloamingforest:lich_trophy`
- pool 6: `gloamingforest:exanimate_essence` x1-3, +0-1 per looting, multiplayer addition 0-1

## 2. Boss spawner wiring (`LichSpawnerBlockEntity`)

`boss::registerSpawner({blockId "gloamingforest:lich_boss_spawner", entityId "gloamingforest:lich", range 9,
particle "gloamingforest:ominous_flame", spawnFn})`: a player counts only when within 9 blocks **and** its y >
spawner y - 4. `spawnFn`: drop down through air from the spawner position to the first solid block (not below the
build limit), spawn one block above it with a random yaw, `finalizeMobSpawn` SPAWNER, `attackCooldown = 40`,
`setExtinguishTimer()` (40), restriction point = that position.

## 3. Shadow clones, minions and other mobs

### 3.1 `gloamingforest:lich_minion` (Lich Minion)

Zombie subclass: MONSTER 0.6 x 1.95, no spawn egg (colours 0, 0), zombie attributes. Tags `minecraft:zombies`.
Goals: ZombieAttackGoal(1.0) prio 2, WaterAvoidingRandomStrollGoal(1.0) prio 7; targets: HurtByTargetGoal (never
a Lich) prio 1, NearestAttackableTargetGoal(Player, mustSee) prio 2. When hurt by a Lich: return to the previous
target and gain Speed III (amplifier 2) and Strength II (amplifier 1) for 200 ticks. Each `aiStep`: without a
master, adopt a non-clone Lich within (32, 16, 32) that wants minions (< 3), black magic trail, take its target;
with no master or a dead master, `kill()`. `finalizeSpawn`: on HARD, while babies summoned < `minion_count / 4`,
21 % (`nextInt(100) <= 20`) to be a baby (counted on the master); `ZombieGroupData(baby, canJoinChicken true)`.
No default equipment, cannot pick up loot. Sounds `minion.*`. Loot `gloamingforest:entities/lich_minion`: empty.

### 3.2 `gloamingforest:death_tome` (Death Tome)

MONSTER 0.75 x 1.5, egg `gloamingforest:death_tome_spawn_egg` (`0x774e22` / `0xdbcdbe`). Attributes: health 20,
speed 0.25, flying speed 0.6, attack 4. FlyingMoveControl(maxTurn 10, hoversInPlace false). Tags:
`minecraft:powder_snow_walkable_mobs`, `minecraft:fall_damage_immune`.
- Goals: Float 0; RangedAttackGoal(speed 1, interval 100, radius 5) 4; WaterAvoidingRandomFlyingGoal(1.0) 5;
  LookAtPlayer(8) 6; RandomLookAround 6. Targets: HurtBy 1; NearestAttackable(Player, mustSee) 2, but while on a
  lectern only the nearest non-sneaking, non-creative/spectator player within 3 blocks who is **looking at the
  tome** (view dot > 0.75 at eye, mid and base heights, visual clip) counts.
- Navigation: flying, stable destination needs a non-air block below; cannot open doors or float, can pass doors.
- Lectern mimic (`on_lectern`, saved): while on a lectern it faces the lectern's FACING, ticks only its target
  selector; on acquiring a target it leaves the lectern, gets motion (0, 0.25, 0) and fires one bolt; it is
  persistent, has no ambient sound and flips pages rarely (1/120 vs 1/30 per tick, client). Leaving the lectern
  block also clears the flag.
- `aiStep`: falling speed is damped (`y *= 0.6` while descending); 1 ENCHANT particle per tick rising 0.5.
- `hurt`: fire damage doubled; on any damage > 0 it leaves the lectern and drops
  `gloamingforest:entities/death_tome_hurt` (empty or 1 paper).
- Ranged attack: `TomeBolt` aimed at `target eye - 1.1` with height compensation `horizontalDist * 0.2`, speed 0.6,
  inaccuracy 6; game event PROJECTILE_SHOOT.
- No fall damage, cannot ride. Sounds `death_tome.*`.
- Loot `gloamingforest:entities/death_tome`: paper x3 (+0-1 looting); one of writable book (w2, quality 3), book
  (w19), `gloamingforest:entities/death_tome_books`; killed by player: 2.5 % (+ looting bonus from 3 % + 0.5 % per
  extra level) `gloamingforest:magic_map_focus`.
- `gloamingforest:entities/death_tome_books`: book enchanted with levels 1-10 (w32), 11-20 (w8), 21-30 (w4), 31-40 (w1).

### 3.3 `gloamingforest:tome_bolt` (Death Tome Bolt)

MISC 0.25 x 0.25, tracking 150, interval 5. Gravity 0.003; 5 CRIT trail particles per tick. On hitting a living
entity: 3 damage of `gloamingforest:lost_words` or `gloamingforest:schooled` (50/50); on success Slowness II
(amplifier 1) for 2 / 6 / 8 seconds on EASY / NORMAL / HARD (PEACEFUL counts as hard). Hitting the lectern block the
owner stands on is ignored. On any hit: entity event 3 (8 paper item particles) and discard. Renders as a paper item.

### 3.4 `gloamingforest:lich_bolt` (Lich Bolt)

MISC 0.25 x 0.25, tracking 150, interval 2. Gravity 0.001; trail 5 ENTITY_EFFECT particles coloured `(0.17, 0.80,
0.69) * (0.5..1)`. Pickable with pick radius 1.0: when hurt by an entity it is re-shot along that entity's look
vector at speed 1.5, inaccuracy 0.1, and the direct attacker (living) becomes its owner (this is how players
reflect bolts to break the shield). Hitting a block or an entity: entity event DEATH (8 ender pearl item
particles) and discard. On a living entity: 6 damage `gloamingforest:lich_bolt` (indirect, attributed to
`gloamingforest:lich`). `canHitEntity`: never a teleport-invisible Lich, never a phase-1 Lich unless a player owns
the bolt; never other lich bolts, lich bombs or `gloamingforest:wand_bolt`. Ignores explosions.

### 3.5 `gloamingforest:lich_bomb` (Explosive Lich Bolt)

MISC 0.25 x 0.25, tracking 150, interval 3. Gravity 0.001; renders on fire; 1 FLAME trail particle per tick moving
against the velocity. Pickable (radius 1.0): any non-explosion hit with a direct entity explodes it. On any impact:
explosion power 2.0, no fire, interaction NONE (no block damage), damage source `gloamingforest:lich_bomb`.
`canHitEntity`: never a Lich (or one that is invisible, or when the owner is not a player), never bolts/bombs/wand
bolts. Ignores explosions.

Both projectiles leave `launchProjectileAt` from the hand position `(cos(bodyYaw) * 0.65, height * 0.82, sin * 0.65)`
toward the target's mid-height at velocity 0.5, inaccuracy 1.0. Entity tag `minecraft:impact_projectiles` contains
both; `#gloamingforest:lich_deflects_phase_2` = `[gloamingforest:wand_bolt, gloamingforest:lich_bolt,
gloamingforest:lich_bomb]`.

### 3.6 `gloamingforest:rising_zombie` ("Zombie")

MONSTER 0.6 x 1.95, eye 1.74, passenger attachment 2.0125, riding offset -0.7, client tracking 16, **no spawn
egg**, zombie attributes, tag `minecraft:zombies`. Spawned by the tower yard's controlled spawns (label 0).
- Synced `RISING_TICKS` (0 = buried and invisible). Dimensions scale `(1, risingTicks / 130)`.
- While buried, every 10 ticks: the nearest player within `follow range / 2` who is **looking at it** (view dot > 0.5
  at eye, mid, base heights, visual clip) starts the rise (`RISING_TICKS = 1`).
- Rising: +1 per tick; every 10 ticks below 130 the break sound of the block below (BLOCKS, 1.0, 0.7-0.85);
  client: 5-14 block particles of the block below. At 130 it converts to a vanilla `minecraft:zombie` keeping
  health and yaw.
- Immobile, no knockback, not pushable, not moved by fluids, cannot ride or use portals, immune only to `in_wall`.
  Hurt/death sounds: vanilla zombie.
- Loot `gloamingforest:entities/rising_zombie`: the vanilla `minecraft:entities/zombie` table.

### 3.7 Attributes

| Id | Range | Default | Translation |
|---|---|---|---|
| `gloamingforest:clone_count` | 0..1024, synced | 2 | `attribute.name.lich.clone_count` |
| `gloamingforest:minion_count` | 0..1024, synced | 9 | `attribute.name.lich.minion_count` |

Bedrock: no custom attributes exist; store them as actor data (`entity::data`) with these defaults. In Java only vanilla
means change them (`/attribute`, or the `attributes` tag of `/summon`), which Bedrock does not have. Upstream registers no
`/gloamingforest` subcommand for them (owners.tsv has no `subcommand` row for them), so U21 adds none (R14). Platform
deviation (with or without the client mod, and the same for U9's `gloamingforest:max_shield_strength` that the Lich
reads): U21 records it in `docs/units/lich.md` (section "Not ported") for `docs/spec/not-ported.md` section 4, as a row
the client mod does not restore; A7/A8 fold it in W2.

### 3.8 Damage types (data)

| Id | message id | exhaustion | scaling | tags (DamageTypeTagGenerator) | Source |
|---|---|---|---|---|---|
| `gloamingforest:lich_bolt` | `gloamingforest.lichBolt` | 0 | when caused by living non-player | bypasses armor, is projectile, witch resistant to, `#gloamingforest:breaks_lich_shields`, `neoforge:is_magic`, bypasses wolf armor | Lich bolt |
| `gloamingforest:lich_bomb` | `gloamingforest.lichBomb` | 0 | same | bypasses armor, projectile, witch resistant, magic, bypasses wolf armor, is explosion | Lich bomb |
| `gloamingforest:lost_words` | `gloamingforest.lostWords` | 0.1 | same | bypasses armor, projectile, witch resistant, magic, bypasses wolf armor | Tome bolt |
| `gloamingforest:schooled` | `gloamingforest.schooled` | 0.1 | same | same as lost_words | Tome bolt |
| `gloamingforest:leaf_brain` | `gloamingforest.leafBrain` | 0.1 | same | same as lost_words | Skeleton Druid's nature bolt (2 damage, U17 fires it by id) |

Death messages (`death.attack.gloamingforest.*`) are F10's lang keys. `#gloamingforest:breaks_lich_shields` =
`[gloamingforest:lich_bolt, gloamingforest:gloaming_scepter, minecraft:magic, minecraft:indirect_magic,
minecraft:sonic_boom]`.

### 3.9 Loot condition `gloamingforest:is_minion`

`IsMinionCondition(inverse)`: true when THIS_ENTITY is a Carminite Ghastling whose `isMinion()` equals `!inverse`.
Used by `gloamingforest:entities/carminite_ghastling` (U26). Implement with `loot::registerCondition` reading
`boss::isMinion(actor)`; the codec field is `inverse` (bool).

## 4. Structure `gloamingforest:lich_tower`

| Field | Value |
|---|---|
| structure type | `gloamingforest:lich_tower` (`LichTowerStructure`, a `ControlledSpawningStructure`) |
| structure set | `gloamingforest:lich_tower`: `gloamingforest:landmark_grid`, grid lock `gloamingforest:lich_tower` |
| biomes | `#gloamingforest:valid_lich_tower_biomes` |
| step / terrain | `surface_structures`, `beard_thin` |
| advancement lock | `gloamingforest:progress_naga` (F6 progression: the tower is protected and hinted until then) |
| hint | Kobold carrying the written book "lichtower" (4 pages `gloamingforest.book.lichtower.1..4`, author `gloamingforest.book.author`, generation 3) |
| decoration clearance | radius 0, no surface decorations, underground decorations yes, **no vegetation**, adjust elevation yes |
| spawn overrides | all categories empty (`full` box) |
| controlled spawns | label 0 (yard): `gloamingforest:rising_zombie` w2 1-2. Label 1 (interior): zombie w10 1-2, skeleton w10 1-2, creeper w1 1, enderman w1 1-2, `gloamingforest:death_tome` w10 2-3, witch w1 1. Label 2: no spawns. Pieces report their label through `SpawnIndexProvider` (below). |
| map icon | `gloamingforest:lich_tower` |
| centre in chunk | true |
| height | `WorldUtil.adjustForTerrain(context, x, z, 32, 4)` |
| terraformer | `yClampedGradient(yBase - 2, yBase - 1, 1, 0) * BoxDensityFunction.combine(trimBoxes, -5, -5, BURY)` where trim boxes = the foyer, the base trim, ground-level wing beards (`isTrim`), and terrain-adjusting yard boxes moved 5 down; `yBase` = the first piece's minY |

### 4.1 Generation algorithm (revamp; translate the 20 classes exactly)

1. `getFirstPiece`: direction = random rotation of SOUTH; place point = start + 24 blocks in that direction;
   template `gloamingforest:lich_tower/tower_foyer` joined at junction `gloamingforest:lich_tower/vestibule`
   (`FrontAndTop(UP, direction)`) -> `LichTowerFoyer(putChest true, chestSide random)`.
2. `generateFromStartingPiece`: add the foyer, run its jigsaws, then `LichYardBox.beginYard`.
3. Foyer jigsaws: target `gloamingforest:lich_tower/tower_base` -> `LichTowerBase` (template `tower_base`,
   junction `tower_base`); target `gloamingforest:shelf` -> 50 % a `LichTowerFoyerDecor` (template
   `lich_tower/foyer_decor`, junction `gloamingforest:shelf`) unless another shelf is within Manhattan 1.5.
   Foyer data markers `chest_a` / `chest_b` (optionally `@<direction>`): the chosen side gets a chest with
   `gloamingforest:chests/tower_room` over canopy planks. Foyer: BEARD_BOX, ground delta 1, spawn label 2.
4. Tower base: bounding box raised 30 blocks; processors + a trim processor that keeps stone bricks under polished
   andesite stairs; jigsaws: `tower_below` -> `LichTowerSegment.buildTowerBySegments(..., 12..15 segments)`;
   `bridge` -> the first bridge jigsaw is the keepsake-casket wing (pool `lich_tower/9x9/special`), other bridges
   only below y 6: `LichTowerWingBridge.tryRoomAndBridge(fromCentral true, roomMax 4, generateGround true)`;
   `decor` -> centre decor from pool `lich_tower/center_decor`; `tower_trim` -> `LichTowerBaseTrim`
   (`lich_tower/central_trim`). Data marker `candle:<n>`: n = 2 -> 3 lit candles; else 1/3 chance of 1-2 lit
   candles. BEARD_BOX, ground delta 1, sort key 1, spawn label 1.
5. Segments: template `lich_tower/tower_slice` stacked by `tower_below` / `tower_above`; `putWings` when
   `index > segments >> 1`, `putGallery` on the last one; a mob bridge every few slices (`mobBridge` starts at
   `0..5`, after a bridge resets to `2..5`, else counts down; bridge when 0). Stair decay processor level
   `ceil((genDepth - 3) * 0.5)` (0.025 .. 0.2 vertical decay of gloaming oak / canopy slabs and banisters). On top:
   `LichBossRoom` (`lich_tower/tower_boss_room`) plus a `LichTowerSegment` boundary piece
   (`lich_tower/tower_boss_boundary`, no wings). The topmost slice's jigsaws run first (gallery), then the others
   bottom-up. Segment jigsaws: `bridge` (wings only): top slice -> 1/10 chance on jigsaw index 2 of a
   `LichTowerMagicGallery` (pool `lich_tower/gallery`, junction `bridge_center`); other slices ->
   `tryRoomAndBridge(fromCentral true, 4, ground false)`; `gloamingforest:mob_bridge` -> a mob bridge from pool
   `lich_tower/mob_bridge`, flipped 50 %, wood palette inverted 50 % (gloaming oak <-> canopy). Data markers
   `<dir>:<w>:<h>` hang a vanilla painting from `#gloamingforest:tower_paintings` (rarity 1/10, 2 tries).
   Segments: terrain NONE, spawn label 1.
6. Boss room: after placement 20 lit candles on a ring (seeded by the centre position 9 above the source jigsaw;
   angle += rand, radius 7..11, y 0..2) where the spot and the block below are air. Exactly:
   - `random.setSeed(center.asLong())`, then per candle `angle += nextFloat()` and `range = Mth.clampedLerp(7f, 11f,
     nextFloat())` (float).
   - `x = Math.round(Mth.cos(angle) * range)` and `z = Math.round(Mth.sin(angle) * range)`, then `y = nextInt(3)`.
   - The cosine and sine are table lookups, and the round is Java's float round (ties toward +infinity); terrain.md
     section 3 has both. Placement is only inside the chunk bounds, with flag UPDATE_CLIENTS. Paintings from
   `#gloamingforest:tower_boss_paintings` (rarity 1/3, 3 tries). Jigsaw `tower_below` -> `LichBossRoof`
   (`lich_tower/tower_boss_roof`). No spawns (Deny). The `gloamingforest:lich_boss_spawner` sits in this template.
7. Wings (`LichTowerWingBridge.tryRoomAndBridge`): from the central tower try shuffled `bridge_from_central`
   bridges (reservoir order by weight), from rooms shuffled `room_bridge` then `end_bridge` (tiny rooms allowed);
   fallback: central -> `bridge_from_central_fallback` (enclosed), rooms -> `room_bridge_fallback` (`no_bridge`)
   else a cover (`door_stopper`, or `door_stopper_fallback` = cobble wall when there is no window clearance 1..3
   blocks out). A bridge places a room (`tryGenerateRoom`): sizes from `roomMax - 1` down to 1 (0 when tiny), pool
   `lich_tower/3x3|5x5|7x7|9x9` by size 0..3, junction `gloamingforest:lich_tower/room`; a room may not collide
   (except the keepsake override). Central bridges remove banisters left/right of the source jigsaw.
8. Rooms (`LichTowerWingRoom`): processors default + room spawner processor, waterlogging ignored.
   - Ground: `generateGround` when allowed and the junction y < 4 (stone brick corner pillars 2 high, trim beard).
   - Ladders: when the space 1.5 x room height above is free, a ladder jigsaw from `ladder_below/<n>` (size 1: 0,2;
     size 2: 0,1,3,4; size 3: 1,2,4,5) leads to a room of the same size from pool `lich_tower/<size>/ladder_<n>`
     (one reroll if it repeats the room or its parent); the upper room gets a 3-high ladder plus 4 air blocks; the
     lower room a ladder column topped by cobblestone. If the room above fails, the roof fallback is used.
   - Further bridges (rooms of size >= 1): terminate when `genDepth > 30` or `nextInt(stackIndex * 2 + 1) == 0`
     (stack index 0 = has room above only, 1 = above and below, 2 = otherwise), or too close to the ground (ground
     room, or rooms taller than 11 with the jigsaw below y 7) -> cover; else `tryRoomAndBridge` with
     `roomMaxSize = size - nextInt(2)`, so the next room has size `roomMaxSize - 1` down to 1 (at least one smaller).
   - Roof (`gloamingforest:lich_tower/roof`, no ladder up): side roof when the space above is taken, shuffled
     `<size>/roof` or `<size>/side_roof`, fallback `<size>/roof_fallback`; roofs use SoftReplaceProcessor.
   - Beard (`gloamingforest:lich_tower/beard`, unless entered from below): ground rooms use `<size>/trim`
     (BEARD_BOX, `isTrim`), others shuffled `<size>/beard` then `<size>/beard_fallback`; beards ground delta 4.
   - Decor (`gloamingforest:lich_tower/decor`): a `LichTowerRoomDecor` from `lich_tower/room_decor`; decor
     markers `sapling` (random plant) and `tree` (one of 4 upstream tree features, fallback plant).
   - Hanging ceiling markers: `rope<chance>>...` / `chain<chance>>...` hang rope (`gloamingforest:rope`) or chain
     1-2 long; only one hanger per x and z column (`allowedCeilingPlacements`).
9. Yard (`LichYardBox.beginYard`): path length 24..31 from the foyer's `gloamingforest:lich_tower/path` jigsaw;
   perimeter fence centred there (`LichPerimeterFence`, templates `lich_tower/outer_fence_1..7`, junctions
   `fence_source`, `fence_edge_left`, `fence_edge_right`; fence posts with 25 % chance one leash position; runs
   toward the ground-level wing trims 64 blocks left/right, folding and homing in height within 20 blocks;
   a 7x7x7 tree clearance at the ladder column); main path box (feather 2.5, noise scale 0.35, spruce fences along
   the edges), a cross path 24 each side at 20-80 % along, two side paths, candle lights (`LichYardLights`: 12.5 %
   per surface air block within 3 of the long edges, wrought iron fence + lit candle), and the dirt yard
   (feather 8, scale 0.1, coarse dirt / dirt / rooted dirt by simplex noise, 1.25 % dead bushes). Every path adds 5
   graves (`LichYardGrave` from pool `lich_tower/grave`: grave 100 / grave_open 50 / grave_ajar 25, 2-4 blocks
   beside the path, dirt filled down to the ocean floor). Yard pieces: spawn label 0.

Piece sort keys (placement order): yard dirt `Integer.MIN_VALUE`, paths `MIN_VALUE + 255`, base trim -2, beards -1,
base 1, bridges and room decor 2.

Default processors on every tower piece: JigsawReplacement, ignore structure blocks, StoneBricksVariants,
CobbleVariants, InfestBlocksProcessor, UpdateMarkingProcessor for birch fence, polished andesite stairs, stone
brick / mossy stone brick / cobblestone / mossy cobblestone walls, `gloamingforest:wrought_iron_fence`,
`gloamingforest:canopy_fence`, `gloamingforest:twisted_stone_pillar`. Room spawner processor (`SpawnerProcessor.compile(range 2, maxWidth 0.8)`):
for every vanilla or sinister spawner in the template sets `SpawnRange` 2, initial `Delay = MinSpawnDelay * 0.25`, and,
when the template gives no `SpawnData`, picks spider / cave spider / swarm spider / hedge spider 1 each, skeleton 4,
zombie 4 (1/3 spiders, 1/3 skeleton, 1/3 zombie) with custom spawn rules (block light 0-7, sky light 0-15); mobs wider
than 0.8 get a `generic.scale` attribute of `0.8 / width`. Central mob-bridge spawner processor
(`compile(range 4)`): `SpawnRange` 4, no width cap, skeleton 2, zombie 1, swarm spider 1.

### 4.2 Piece type ids

Legacy piece types (owned here by owners.tsv; the classes are F7's shared `structure/bases/Tower.h`, also used by
the dark tower, mushroom tower and aurora palace). U21 makes sure these ids are registered with F7's factories so
those structures and any saved starts load; new lich towers never generate the legacy `TowerMainComponent`:

| Id | Class |
|---|---|
| `gloamingforest:GloamingLTMai` | TowerMainComponent (legacy main tower: 15 wide, 55-86 tall, spiral stairs, lich spawner on top) |
| `gloamingforest:GloamingLTWin` | TowerWingComponent |
| `gloamingforest:GloamingLTBri` | TowerBridgeComponent |
| `gloamingforest:GloamingLTOut` | TowerOutbuildingComponent |
| `gloamingforest:GloamingLTBea` | TowerBeardComponent |
| `gloamingforest:GloamingLTBA` | TowerBeardAttachedComponent |
| `gloamingforest:GloamingLTRoo` | TowerRoofComponent |
| `gloamingforest:GloamingLTRAS` | TowerRoofAttachedSlabComponent |
| `gloamingforest:GloamingLTRF` | TowerRoofFenceComponent |
| `gloamingforest:GloamingLTRGF` | TowerRoofGableForwardsComponent |
| `gloamingforest:GloamingLTRP` | TowerRoofPointyComponent |
| `gloamingforest:GloamingLTRPO` | TowerRoofPointyOverhangComponent |
| `gloamingforest:GloamingLTRS` | TowerRoofSlabComponent |
| `gloamingforest:GloamingLTRSF` | TowerRoofSlabForwardsComponent |
| `gloamingforest:GloamingLTRSt` | TowerRoofStairsComponent |
| `gloamingforest:GloamingLTRStO` | TowerRoofStairsOverhangComponent |

Revamp piece types registered by `GloamingStructurePieceTypes`, with names built as `"GloamingLT" + suffix`. **owners.tsv does not
list them**: `tools/spec/owners.ps1` does not match the string concatenation. The ownership below is **proposed, pending
an owners.tsv fix by F0/A8**. It is tracked in [issue #4](https://github.com/MiracleForest/GloamingForest/issues/4).
The existing rules would assign the `lichtowerrevamp` package to U21 once the regex is fixed. Until owners.tsv has the
rows, these ids are outside the manifest (R1), so U21 should wait for the fix or confirm with the coordinator
before registering them. The ids are `gloamingforest:GloamingLTTFoy` (LichTowerFoyer), `GloamingLTCTBase` (LichTowerBase), `GloamingLTCTTrim`
(LichTowerBaseTrim), `GloamingLTCTSeg` (LichTowerSegment), `GloamingLTMobBridge` (LichTowerSpawnerBridge), `GloamingLTBridge`
(LichTowerWingBridge), `GloamingLTTRoof` (LichTowerWingRoof), `GloamingLTTBeard` (LichTowerWingBeard), `GloamingLTTRoom`
(LichTowerWingRoom), `GloamingLTTDecor` (LichTowerRoomDecor), `GloamingLTTGallery` (LichTowerMagicGallery), `GloamingLTTFoyD`
(LichTowerFoyerDecor), `GloamingLTTBoss` (LichBossRoom), `GloamingLTTBossRoof` (LichBossRoof), `GloamingLTFence`
(LichPerimeterFence), `GloamingLTPath` (LichYardBox), `GloamingLTGrave` (LichYardGrave), `GloamingLTLight` (LichYardLights).

The structure type id `gloamingforest:lich_tower` is owned here.

### 4.3 Templates (re-design brief, R11/R19)

Upstream's lich tower `.nbt` files are All Rights Reserved: every template is re-designed in the `.gft.json` format,
keeping the ids, the jigsaw junction names and orientations below, and the data-marker vocabulary of 4.4. Room
interiors (`lich_tower/3x3..9x9` rooms) are designed by U22 under `assets/structures/lich/rooms/**`; U21 designs the
rest under `assets/structures/lich/**` and owns all template definitions.

Templates referenced directly by code (no template definition): `lich_tower/tower_foyer`, `tower_base`,
`foyer_decor`, `central_trim`, `tower_slice`, `tower_boss_room`, `tower_boss_boundary`, `tower_boss_roof`,
`outer_fence_1` .. `outer_fence_7` (n = blocks per fence step; 7 is the full piece and the start).

Junction (jigsaw `name`) vocabulary: `gloamingforest:lich_tower/vestibule`, `tower_base`, `tower_below`,
`tower_above`, `tower_trim`, `bridge`, `bridge_center`, `room`, `roof`, `beard`, `decor`, `path`, `grave`,
`fence_source`, `fence_edge_left`, `fence_edge_right`, `gloamingforest:shelf`, `gloamingforest:mob_bridge`,
`gloamingforest:ladder_below/0..5`. Template sizes follow the pool names: 3x3, 5x5, 7x7 and 9x9 interior rooms
(size index 0..3), roofs/beards/trims matching each size, gallery roofs in even and odd widths
(`rollGalleryRoof` picks by the parity of the gallery's smaller horizontal span).

### 4.4 Data-marker vocabulary (room templates)

Grammar: `[modifier>]label[|label...][@direction]`, one alternative picked at random, parameters split by `:`.
Modifiers: `rope<chance%>[len]` / `chain<chance%>[len]` (hang from the ceiling, length `a` or `a-b`, default 1-2,
one per column), `pedestal` (twisted stone pillar below), `below` (place one block lower). Direction rotates
state-bearing blocks (default 180 deg).

| Label | Result |
|---|---|
| `air`, `empty` | nothing |
| `bookshelf`, `canopy_shelf` / `canopy_bookshelf`, `stone_brick_slab`, `canopy_slab`, `canopy_stairs`, `terrorcotta_arcs`, `firefly_jar`, `lava`, `water` | that block |
| `mason_jar[:table[:rot]]` | mason jar filled from `jar` (`chests/tower_jars`), `room`, `library`, `potion`, `enchanting`, a loot table id (`.` as separator) or an item id; item rotation 0-15; a canopy bookshelf above becomes a top canopy slab |
| `creeper_head`, `skeleton_skull`, `wither_skull`, `zombie_head` `[:rot]` | vanilla head, rotation `n` or `n+extra` |
| `creeper_candle`, `skeleton_candle`, `wither_candle`, `zombie_candle` `[:count[:rot]]` | skull candle (U22) lit NORMAL, 1-3 candles |
| `candle` / `candles` / `<color>_candle` `[:count]` | lit candles, count `n` or `a-b` (default 1-3, max 4) |
| `spawner[:mobs[:range]]` | vanilla spawner; mobs comma list (`hedge_spider`, `swarm_spider` or entity ids) else 10 % spider, cave spider, hedge spider, swarm spider each, 30 % skeleton, 40 % zombie; light 0-7 block / 0-15 sky; spawn range 1-16 |
| `sinister_spawner[:mobs[:range[:scan]]]` | `gloamingforest:sinister_spawner` (U22) with ominous flame particles, loot `gloamingforest:blocks/ominous_spawner_drops`, scan range 1-32 (default = spawn range) |
| `brewing_stand` | 3 bottles of the same potion (25 % splash): 1/7 water, 3/7 healing, 2/7 regeneration, 1/7 strong healing; 1-5 blaze powder; fuel 10-20 |
| `lectern` | 50 % a lectern with a writable book, 50 % an empty lectern with a Death Tome mimic on it |
| `empty_lectern`, `candled_lectern` | lectern; candled: 75 % candles above, 25 % a skeleton skull candle |
| `chiseled_canopy_shelf` | `gloamingforest:chiseled_canopy_bookshelf`; 1/12 hostile (spawner of Death Tomes, books only); each of 6 slots filled 2/3, non-hostile 1/16 enchanted book (levels 1-40) |
| `chest[:table]`, `trapped_chest[:table]` | chest with `room` (`chests/tower_room`), `library`, `potion`, `enchanting` or a table id |
| `water_cauldron[:n]` | cauldron with level 1-3 (0 -> empty cauldron) |
| `zombie_trap` | wrought iron fence post (capped if air above) with a leashed persistent zombie beside it, inside the chunk |
| `wrought_iron_post` | wrought iron fence POST state, marked for post-processing |
| any other label | the block with that id (`.` or `:` separator) |

Foyer markers: `chest_a`, `chest_b`. Base markers: `candle:1`, `candle:2`. Segment / gallery / boss room markers:
`<direction>:<width>:<height>` paintings; gallery `painting` = the room's magic painting (U22 entity
`gloamingforest:magic_painting` with the variant named after the gallery: castaway_paradise, darkness,
lucid_lands, music_in_the_mire, the_hostile_paradise).

### 4.5 Chest loot tables (owned here)

- `gloamingforest:chests/tower_enchanting`: lapis x4-12; experience bottle x5-15; book x16-32 (one roll each).
- `gloamingforest:chests/tower_foyer`: 3-4 rolls of rotten flesh 1-2, bone 1-4, charcoal 3-5, raven feather 2-3,
  torchberries 1-3, stick 3-5 (w6 each), bread 2-3, potato 3-5, `chests/suspicious_stew` (w3 each),
  `gloamingforest:charm_of_keeping_1` (w1), diamond (w1).
- `gloamingforest:chests/tower_grave_lower`: `chests/just_bones`; leather boots (10-20 % damage); iron leggings
  (10-20 % damage).
- `gloamingforest:chests/tower_grave_upper`: `chests/just_bones`; skeleton skull; iron chestplate (10-20 % damage);
  one of diamond / golden apple / charm of keeping 1.
- `gloamingforest:chests/tower_jars`: one of chain, ender pearl, spider eye, fermented spider eye, brown mushroom,
  red mushroom, raven feather, liveroot, bone meal, ink sac, paper, rotten flesh, zombie head, skeleton skull, or
  empty (w8).
- `gloamingforest:chests/tower_library`: 3 rolls of paper 24-56, book 5-12, ink sac 2-6, book enchanted 5-10,
  canopy bookshelf 3-12, bookshelf 3-12, writable book 1-2, ladder 5-10; 1 roll of empty (w3), brittle potion
  flask 1-2 (w2), experience bottle 4-6 (w2), crown splinter, diamond, writable book (w2), book enchanted 20-30
  (w2), book with `gloamingforest:renewal` I.
- `gloamingforest:chests/tower_potion`: 4 rolls of glistering melon 3-6, ghast tear 1-3, fermented spider eye 1-4,
  blaze powder 4-9, glass bottle 1-4, magma cream 1-3, sugar 2-7, gunpowder 8-20, nether wart 6-13; 2 rolls of
  golden carrot 1-3, redstone 4-7, glowstone 2-6, splash healing, strength, fire resistance; 1 roll of golden apple
  1-2, blaze rod 1-4, strong healing, swiftness, regeneration, crown splinter (w75 each), brewing stand,
  `gloamingforest:greater_potion_flask` (w25 each); brittle potion flask x3-5.
- `gloamingforest:chests/tower_room`: 4 rolls of `useless` (w25), bone 4-16, clay ball 6-12, string 3-8, torch
  9-24, candle 2-5, suspicious stew table, gold nugget 3-9, iron nugget 6-20, bread 2-6 (w75); 2 rolls of
  transformation powder 1-3, magic map, firefly 3-9, exanimate essence 1-3, brittle flask 3-5, amethyst 4-13,
  ender pearl 1-3 (twice), healing / regeneration / strength potions, golden sword enchanted randomly
  (`#minecraft:on_random_loot`), book enchanted 20-30; 1 roll of empty, liveroot 3-5, diamond, crown splinter,
  obsidian 1-4, golden helmet enchanted 10-15, charm of life 1, book enchanted 20-30, golden axe enchanted 20 (w75
  each), enchanted golden apple, moonworm queen, peacock feather fan (w25 each).

## 5. Tags, decoration, strings

- `entity_type/#gloamingforest:lich_poppables` = `#minecraft:skeletons`, zombie, enderman, spider, creeper,
  `gloamingforest:swarm_spider`, minus `#c:bosses`.
- `entity_type/#gloamingforest:lich_deflects_phase_2` and `damage_type/#gloamingforest:breaks_lich_shields` (3.5,
  3.8).
- `banner_pattern/#gloamingforest:pattern_item/lich` = `[gloamingforest:lich]`.
- Particle `gloamingforest:angry_lich` (1.16). Map decoration `gloamingforest:lich_tower`.
- Lang keys owned: entity names (`entity.gloamingforest.death_tome` "Death Tome", `lich` "Gloaming Lich",
  `lich_bolt` "Lich Bolt", `lich_bomb` "Explosive Lich Bolt", `lich_minion` "Lich Minion", `rising_zombie`
  "Zombie", `tome_bolt` "Death Tome Bolt"), the two spawn eggs, and the 14 subtitles of 1.15.

## 6. Bedrock adaptation notes

- Boss: `registerBoss({entityId "gloamingforest:lich", homeStructure "gloamingforest:lich_tower", lootTable
  "gloamingforest:entities/lich", bar {0xFFD800}, deathTicks 295})`; the bar colour and overlay change per phase
  (`bar(mob)`); vanilla clients get the nearest BossBarColor (yellow / purple / red).
- Clones are separate actors flagged by entity data (`MASTER_LICH`); hide their bar and make them translucent.
- Projectile reflection: Bedrock has no "hurt a projectile" hook; F8 `projectile` must expose a hit-by-melee
  callback (or attack interaction on the projectile actor) to re-shoot bolts along the attacker's view.
- Rising zombies need the "player is looking at me" test server-side from the player's head rotation.
- Ominous candles / lightables use U22's ids and states only (R2).

## 7. Selftests (`lich.*`, dx 2100-2199)

`lich.phases` (shield 6 -> phase 1; shield 0 with minions left -> 2; none -> 3), `lich.shield_break` (lich_bolt
damage 6 breaks one shield, melee only plays the block sound), `lich.clone_cap` (at most 2 clones), `lich.minions`
(at most 3 alive, 9 total), `lich.bolt_reflect`, `lich.death_tome_lectern`, `lich.rising_zombie_converts` (130
ticks), `lich.loot`, `lich.tower_layout` (golden volume hash of a seeded tower, R19), `lich.spawner_range` (player
below spawner y - 4 does not trigger).

## 8. Template definitions and pools (`gloaming/template_definition/lich_tower/**`, 165 registry entries)

Each row is one owned `registry_entry` (`template_definition/...`); the JSON maps template -> pool weight. Pools are
sampled with a `WeightedRandomList` (entries sorted by template id), or, for bridges, roofs and beards, as a
weight-biased shuffled sequence (reservoir sampling, key `-ln(rand) / weight`, ascending).

| Template definition (registry entry) | Pools (weight, placement) |
|---|---|
| `template_definition/gloamingforest:lich_tower/3x3/curved_roof` | `lich_tower/3x3/roof` 100; `lich_tower/3x3/side_roof` 100 |
| `template_definition/gloamingforest:lich_tower/3x3/double` | `lich_tower/3x3` 100 |
| `template_definition/gloamingforest:lich_tower/3x3/fence_roof` | `lich_tower/3x3/roof` 100; `lich_tower/3x3/side_roof` 100 |
| `template_definition/gloamingforest:lich_tower/3x3/flat_roof` | `lich_tower/3x3/roof_fallback` 100 |
| `template_definition/gloamingforest:lich_tower/3x3/flat_side_roof` | `lich_tower/3x3/roof_fallback` 100 |
| `template_definition/gloamingforest:lich_tower/3x3/lava_well` | `lich_tower/room_decor` 100 |
| `template_definition/gloamingforest:lich_tower/3x3/lookout` | `lich_tower/3x3` 100 |
| `template_definition/gloamingforest:lich_tower/3x3/pointed_roof` | `lich_tower/3x3/roof` 100; `lich_tower/3x3/side_roof` 100 |
| `template_definition/gloamingforest:lich_tower/3x3/pyramid_roof` | `lich_tower/3x3/roof` 100; `lich_tower/3x3/side_roof` 100 |
| `template_definition/gloamingforest:lich_tower/3x3/sapling` | `lich_tower/room_decor` 100 |
| `template_definition/gloamingforest:lich_tower/3x3/short_lookout` | `lich_tower/3x3` 100 |
| `template_definition/gloamingforest:lich_tower/3x3/slabs_roof` | `lich_tower/3x3/roof` 100; `lich_tower/3x3/side_roof` 100 |
| `template_definition/gloamingforest:lich_tower/3x3/taller_double` | `lich_tower/3x3` 100 |
| `template_definition/gloamingforest:lich_tower/3x3/tree` | `lich_tower/center_decor` 100 |
| `template_definition/gloamingforest:lich_tower/3x3/water_fountain` | `lich_tower/center_decor` 100; `lich_tower/room_decor` 100 |
| `template_definition/gloamingforest:lich_tower/3x3/water_well` | `lich_tower/center_decor` 100; `lich_tower/room_decor` 100 |
| `template_definition/gloamingforest:lich_tower/3x3/wither_rose` | `lich_tower/center_decor` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/altar` | `lich_tower/5x5` 100; `lich_tower/5x5/ladder_0` 100; `lich_tower/5x5/ladder_2` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/beard_checkered` | `lich_tower/5x5/beard` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/beard_chiseled` | `lich_tower/5x5/beard` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/beard_chunks` | `lich_tower/5x5/beard` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/beard_flat` | `lich_tower/5x5/beard_fallback` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/beard_staggered` | `lich_tower/5x5/beard` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/beard_trim` | `lich_tower/5x5/trim` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/curved_roof` | `lich_tower/5x5/roof` 100; `lich_tower/5x5/side_roof` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/desk` | `lich_tower/5x5` 100; `lich_tower/5x5/ladder_0` 100; `lich_tower/5x5/ladder_2` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/elbow_junction` | `lich_tower/5x5` 100; `lich_tower/5x5/ladder_0` 100; `lich_tower/5x5/ladder_2` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/fence_roof` | `lich_tower/5x5/roof` 100; `lich_tower/5x5/side_roof` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/flat_roof` | `lich_tower/5x5/roof_fallback` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/flat_side_roof` | `lich_tower/5x5/roof_fallback` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/full_junction` | `lich_tower/5x5` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/full_junction_2` | `lich_tower/5x5` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/full_junction_3` | `lich_tower/5x5` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/ladder` | `lich_tower/5x5` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/lectern` | `lich_tower/5x5/ladder_0` 100; `lich_tower/5x5/ladder_2` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/library` | `lich_tower/5x5` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/lone_chest` | `lich_tower/5x5` 100; `lich_tower/5x5/ladder_0` 100; `lich_tower/5x5/ladder_2` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/pointed_roof` | `lich_tower/5x5/roof` 100; `lich_tower/5x5/side_roof` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/potion` | `lich_tower/5x5` 100; `lich_tower/5x5/ladder_0` 100; `lich_tower/5x5/ladder_2` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/pyramid_roof` | `lich_tower/5x5/roof` 100; `lich_tower/5x5/side_roof` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/slabs_roof` | `lich_tower/5x5/roof` 100; `lich_tower/5x5/side_roof` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/spawner_1` | `lich_tower/5x5` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/spawner_2` | `lich_tower/5x5` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/spawner_3` | `lich_tower/5x5` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/spawner_4` | `lich_tower/5x5` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/spawner_5` | `lich_tower/5x5` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/straight_junction` | `lich_tower/5x5` 100; `lich_tower/5x5/ladder_0` 100; `lich_tower/5x5/ladder_2` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/t_junction` | `lich_tower/5x5` 100; `lich_tower/5x5/ladder_0` 100; `lich_tower/5x5/ladder_2` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/trinity` | `lich_tower/5x5` 100; `lich_tower/5x5/ladder_0` 100; `lich_tower/5x5/ladder_2` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/webbed_spawner` | `lich_tower/5x5` 100 |
| `template_definition/gloamingforest:lich_tower/5x5/zombie_trap` | `lich_tower/5x5` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/altar` | `lich_tower/7x7/ladder_0` 100; `lich_tower/7x7/ladder_4` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/altars` | `lich_tower/7x7` 100; `lich_tower/7x7/ladder_0` 100; `lich_tower/7x7/ladder_4` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/barbed_webs` | `lich_tower/7x7` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/beard_checkered` | `lich_tower/7x7/beard` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/beard_chiseled` | `lich_tower/7x7/beard` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/beard_chunks` | `lich_tower/7x7/beard` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/beard_flat` | `lich_tower/7x7/beard_fallback` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/beard_staggered` | `lich_tower/7x7/beard` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/beard_trim` | `lich_tower/7x7/trim` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/book_staircases` | `lich_tower/7x7` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/cactus` | `lich_tower/7x7` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/corner_caging` | `lich_tower/7x7` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/curved_roof` | `lich_tower/7x7/roof` 100; `lich_tower/7x7/side_roof` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/desk` | `lich_tower/7x7` 100; `lich_tower/7x7/ladder_0` 100; `lich_tower/7x7/ladder_4` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/elbow_chest` | `lich_tower/7x7/ladder_1` 100; `lich_tower/7x7/ladder_3` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/elbow_junction` | `lich_tower/7x7` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/fence_roof` | `lich_tower/7x7/roof` 100; `lich_tower/7x7/side_roof` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/flat_roof` | `lich_tower/7x7/roof_fallback` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/flat_side_roof` | `lich_tower/7x7/roof_fallback` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/full_junction` | `lich_tower/7x7` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/full_junction_1` | `lich_tower/7x7` 100; `lich_tower/7x7/ladder_0` 100; `lich_tower/7x7/ladder_4` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/full_junction_2` | `lich_tower/7x7` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/garden_lab` | `lich_tower/7x7` 100; `lich_tower/7x7/ladder_3` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/grave` | `lich_tower/7x7` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/guarded_chest` | `lich_tower/7x7/ladder_1` 100; `lich_tower/7x7/ladder_3` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/jar_study` | `lich_tower/7x7/ladder_1` 100; `lich_tower/7x7/ladder_3` 100; `lich_tower/7x7/ladder_4` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/library_hall` | `lich_tower/7x7` 100; `lich_tower/7x7/ladder_1` 100; `lich_tower/7x7/ladder_3` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/nursery` | `lich_tower/7x7` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/pedestal_junction` | `lich_tower/7x7` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/pedestal_library` | `lich_tower/7x7` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/pointed_roof` | `lich_tower/7x7/roof` 100; `lich_tower/7x7/side_roof` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/potion` | `lich_tower/7x7` 100; `lich_tower/7x7/ladder_1` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/potion_lab` | `lich_tower/7x7/ladder_1` 100; `lich_tower/7x7/ladder_3` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/pyramid_roof` | `lich_tower/7x7/roof` 100; `lich_tower/7x7/side_roof` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/ritual` | `lich_tower/7x7` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/shelved_jars` | `lich_tower/7x7` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/slabs_roof` | `lich_tower/7x7/roof` 100; `lich_tower/7x7/side_roof` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/straight_junction` | `lich_tower/7x7` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/t_junction` | `lich_tower/7x7` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/tiered_library` | `lich_tower/7x7` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/tiered_study` | `lich_tower/7x7` 100 |
| `template_definition/gloamingforest:lich_tower/7x7/walled_library` | `lich_tower/7x7` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/altar` | `lich_tower/9x9` 100; `lich_tower/9x9/ladder_1` 100; `lich_tower/9x9/ladder_5` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/archives` | `lich_tower/9x9` 100; `lich_tower/9x9/ladder_1` 100; `lich_tower/9x9/ladder_5` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/beard_checkered` | `lich_tower/9x9/beard` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/beard_chiseled` | `lich_tower/9x9/beard` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/beard_chunks` | `lich_tower/9x9/beard` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/beard_flat` | `lich_tower/9x9/beard_fallback` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/beard_staggered` | `lich_tower/9x9/beard` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/beard_trim` | `lich_tower/9x9/trim` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/cauldron_keep` | `lich_tower/9x9` 100; `lich_tower/9x9/ladder_1` 100; `lich_tower/9x9/ladder_2` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/center_decor` | `lich_tower/9x9` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/centerpiece` | `lich_tower/9x9/ladder_1` 100; `lich_tower/9x9/ladder_5` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/classic_library` | `lich_tower/9x9/ladder_2` 20 |
| `template_definition/gloamingforest:lich_tower/9x9/curved_roof` | `lich_tower/9x9/roof` 100; `lich_tower/9x9/side_roof` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/elbow_junction` | `lich_tower/9x9` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/enchanting_prison` | `lich_tower/9x9` 100; `lich_tower/9x9/ladder_1` 100; `lich_tower/9x9/ladder_5` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/fence_roof` | `lich_tower/9x9/roof` 100; `lich_tower/9x9/side_roof` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/flat_roof` | `lich_tower/9x9/roof_fallback` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/flat_side_roof` | `lich_tower/9x9/roof_fallback` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/full_junction` | `lich_tower/9x9` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/holding` | `lich_tower/9x9` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/keepsake_casket` | `lich_tower/9x9/special` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/large_study` | `lich_tower/9x9` 100; `lich_tower/9x9/ladder_1` 100; `lich_tower/9x9/ladder_5` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/lectern_hall` | `lich_tower/9x9` 100; `lich_tower/9x9/ladder_1` 100; `lich_tower/9x9/ladder_5` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/library_junction` | `lich_tower/9x9` 100; `lich_tower/9x9/ladder_2` 100; `lich_tower/9x9/ladder_4` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/lockup` | `lich_tower/9x9` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/mossy_junction` | `lich_tower/9x9` 100; `lich_tower/9x9/ladder_1` 100; `lich_tower/9x9/ladder_5` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/pointed_roof` | `lich_tower/9x9/roof` 100; `lich_tower/9x9/side_roof` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/pyramid_roof` | `lich_tower/9x9/roof` 100; `lich_tower/9x9/side_roof` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/seven` | `lich_tower/9x9/ladder_2` 1; `lich_tower/9x9/ladder_4` 1 |
| `template_definition/gloamingforest:lich_tower/9x9/slabs_roof` | `lich_tower/9x9/roof` 100; `lich_tower/9x9/side_roof` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/straight_junction` | `lich_tower/9x9` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/study` | `lich_tower/9x9/ladder_1` 100; `lich_tower/9x9/ladder_2` 100; `lich_tower/9x9/ladder_4` 100; `lich_tower/9x9/ladder_5` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/t_junction` | `lich_tower/9x9` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/tiered_library` | `lich_tower/9x9` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/tiered_spawner` | `lich_tower/9x9` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/tiered_study` | `lich_tower/9x9` 100 |
| `template_definition/gloamingforest:lich_tower/9x9/winding_ways` | `lich_tower/9x9` 100 |
| `template_definition/gloamingforest:lich_tower/bridge_spawner` | `lich_tower/mob_bridge` 100 |
| `template_definition/gloamingforest:lich_tower/bridge_spawner_bend` | `lich_tower/mob_bridge` 100 |
| `template_definition/gloamingforest:lich_tower/bridge_spawner_ropes` | `lich_tower/mob_bridge` 100 |
| `template_definition/gloamingforest:lich_tower/bridge_spawner_wide` | `lich_tower/mob_bridge` 100 |
| `template_definition/gloamingforest:lich_tower/bridge_spawner_zag` | `lich_tower/mob_bridge` 100 |
| `template_definition/gloamingforest:lich_tower/bridge_spawner_zig` | `lich_tower/mob_bridge` 100 |
| `template_definition/gloamingforest:lich_tower/central_bridge` | `lich_tower/bridge_from_central` 100; `lich_tower/bridge_from_central_fallback` 100 |
| `template_definition/gloamingforest:lich_tower/central_bridge_open` | `lich_tower/bridge_from_central` 100 |
| `template_definition/gloamingforest:lich_tower/end_bridge_1` | `lich_tower/end_bridge` 100 |
| `template_definition/gloamingforest:lich_tower/end_bridge_2` | `lich_tower/end_bridge` 100 |
| `template_definition/gloamingforest:lich_tower/end_bridge_3` | `lich_tower/end_bridge` 100 |
| `template_definition/gloamingforest:lich_tower/end_bridge_4` | `lich_tower/end_bridge` 100 |
| `template_definition/gloamingforest:lich_tower/end_bridge_5` | `lich_tower/end_bridge` 100 |
| `template_definition/gloamingforest:lich_tower/gallery/castaway_paradise` | `lich_tower/gallery` 100 |
| `template_definition/gloamingforest:lich_tower/gallery/darkness` | `lich_tower/gallery` 100 |
| `template_definition/gloamingforest:lich_tower/gallery/fence_roof_even` | `lich_tower/gallery_even` 100 |
| `template_definition/gloamingforest:lich_tower/gallery/fence_roof_odd` | `lich_tower/gallery_odd` 100 |
| `template_definition/gloamingforest:lich_tower/gallery/lucid_lands` | `lich_tower/gallery` 100 |
| `template_definition/gloamingforest:lich_tower/gallery/music_in_the_mire` | `lich_tower/gallery` 100 |
| `template_definition/gloamingforest:lich_tower/gallery/slabs_roof_even` | `lich_tower/gallery_even` 100 |
| `template_definition/gloamingforest:lich_tower/gallery/slabs_roof_odd` | `lich_tower/gallery_odd` 100 |
| `template_definition/gloamingforest:lich_tower/gallery/stairs_roof_even` | `lich_tower/gallery_even` 100 |
| `template_definition/gloamingforest:lich_tower/gallery/stairs_roof_odd` | `lich_tower/gallery_odd` 100 |
| `template_definition/gloamingforest:lich_tower/gallery/the_hostile_paradise` | `lich_tower/gallery` 100 |
| `template_definition/gloamingforest:lich_tower/grave` | `lich_tower/grave` 100 |
| `template_definition/gloamingforest:lich_tower/grave_ajar` | `lich_tower/grave` 25 |
| `template_definition/gloamingforest:lich_tower/grave_open` | `lich_tower/grave` 50 |
| `template_definition/gloamingforest:lich_tower/no_bridge` | `lich_tower/room_bridge_fallback` 100 |
| `template_definition/gloamingforest:lich_tower/room_bridge_1` | `lich_tower/room_bridge` 100 |
| `template_definition/gloamingforest:lich_tower/room_bridge_2` | `lich_tower/room_bridge` 100 |
| `template_definition/gloamingforest:lich_tower/room_bridge_3` | `lich_tower/room_bridge` 100 |
| `template_definition/gloamingforest:lich_tower/room_bridge_4` | `lich_tower/room_bridge` 100 |
| `template_definition/gloamingforest:lich_tower/room_bridge_5` | `lich_tower/room_bridge` 100 |
| `template_definition/gloamingforest:lich_tower/wall_bars` | `lich_tower/door_stopper` 100 |
| `template_definition/gloamingforest:lich_tower/wall_cobble` | `lich_tower/door_stopper` 100; `lich_tower/door_stopper_fallback` 100 |

| Pool | Members (weight) |
|---|---|
| `gloamingforest:lich_tower/3x3` | 3x3/double 100, 3x3/lookout 100, 3x3/short_lookout 100, 3x3/taller_double 100 |
| `gloamingforest:lich_tower/3x3/roof` | 3x3/curved_roof 100, 3x3/fence_roof 100, 3x3/pointed_roof 100, 3x3/pyramid_roof 100, 3x3/slabs_roof 100 |
| `gloamingforest:lich_tower/3x3/roof_fallback` | 3x3/flat_roof 100, 3x3/flat_side_roof 100 |
| `gloamingforest:lich_tower/3x3/side_roof` | 3x3/curved_roof 100, 3x3/fence_roof 100, 3x3/pointed_roof 100, 3x3/pyramid_roof 100, 3x3/slabs_roof 100 |
| `gloamingforest:lich_tower/5x5` | 5x5/altar 100, 5x5/desk 100, 5x5/elbow_junction 100, 5x5/full_junction 100, 5x5/full_junction_2 100, 5x5/full_junction_3 100, 5x5/ladder 100, 5x5/library 100, 5x5/lone_chest 100, 5x5/potion 100, 5x5/spawner_1 100, 5x5/spawner_2 100, 5x5/spawner_3 100, 5x5/spawner_4 100, 5x5/spawner_5 100, 5x5/straight_junction 100, 5x5/t_junction 100, 5x5/trinity 100, 5x5/webbed_spawner 100, 5x5/zombie_trap 100 |
| `gloamingforest:lich_tower/5x5/beard` | 5x5/beard_checkered 100, 5x5/beard_chiseled 100, 5x5/beard_chunks 100, 5x5/beard_staggered 100 |
| `gloamingforest:lich_tower/5x5/beard_fallback` | 5x5/beard_flat 100 |
| `gloamingforest:lich_tower/5x5/ladder_0` | 5x5/altar 100, 5x5/desk 100, 5x5/elbow_junction 100, 5x5/lectern 100, 5x5/lone_chest 100, 5x5/potion 100, 5x5/straight_junction 100, 5x5/t_junction 100, 5x5/trinity 100 |
| `gloamingforest:lich_tower/5x5/ladder_2` | 5x5/altar 100, 5x5/desk 100, 5x5/elbow_junction 100, 5x5/lectern 100, 5x5/lone_chest 100, 5x5/potion 100, 5x5/straight_junction 100, 5x5/t_junction 100, 5x5/trinity 100 |
| `gloamingforest:lich_tower/5x5/roof` | 5x5/curved_roof 100, 5x5/fence_roof 100, 5x5/pointed_roof 100, 5x5/pyramid_roof 100, 5x5/slabs_roof 100 |
| `gloamingforest:lich_tower/5x5/roof_fallback` | 5x5/flat_roof 100, 5x5/flat_side_roof 100 |
| `gloamingforest:lich_tower/5x5/side_roof` | 5x5/curved_roof 100, 5x5/fence_roof 100, 5x5/pointed_roof 100, 5x5/pyramid_roof 100, 5x5/slabs_roof 100 |
| `gloamingforest:lich_tower/5x5/trim` | 5x5/beard_trim 100 |
| `gloamingforest:lich_tower/7x7` | 7x7/altars 100, 7x7/barbed_webs 100, 7x7/book_staircases 100, 7x7/cactus 100, 7x7/corner_caging 100, 7x7/desk 100, 7x7/elbow_junction 100, 7x7/full_junction 100, 7x7/full_junction_1 100, 7x7/full_junction_2 100, 7x7/garden_lab 100, 7x7/grave 100, 7x7/library_hall 100, 7x7/nursery 100, 7x7/pedestal_junction 100, 7x7/pedestal_library 100, 7x7/potion 100, 7x7/ritual 100, 7x7/shelved_jars 100, 7x7/straight_junction 100, 7x7/t_junction 100, 7x7/tiered_library 100, 7x7/tiered_study 100, 7x7/walled_library 100 |
| `gloamingforest:lich_tower/7x7/beard` | 7x7/beard_checkered 100, 7x7/beard_chiseled 100, 7x7/beard_chunks 100, 7x7/beard_staggered 100 |
| `gloamingforest:lich_tower/7x7/beard_fallback` | 7x7/beard_flat 100 |
| `gloamingforest:lich_tower/7x7/ladder_0` | 7x7/altar 100, 7x7/altars 100, 7x7/desk 100, 7x7/full_junction_1 100 |
| `gloamingforest:lich_tower/7x7/ladder_1` | 7x7/elbow_chest 100, 7x7/guarded_chest 100, 7x7/jar_study 100, 7x7/library_hall 100, 7x7/potion 100, 7x7/potion_lab 100 |
| `gloamingforest:lich_tower/7x7/ladder_3` | 7x7/elbow_chest 100, 7x7/garden_lab 100, 7x7/guarded_chest 100, 7x7/jar_study 100, 7x7/library_hall 100, 7x7/potion_lab 100 |
| `gloamingforest:lich_tower/7x7/ladder_4` | 7x7/altar 100, 7x7/altars 100, 7x7/desk 100, 7x7/full_junction_1 100, 7x7/jar_study 100 |
| `gloamingforest:lich_tower/7x7/roof` | 7x7/curved_roof 100, 7x7/fence_roof 100, 7x7/pointed_roof 100, 7x7/pyramid_roof 100, 7x7/slabs_roof 100 |
| `gloamingforest:lich_tower/7x7/roof_fallback` | 7x7/flat_roof 100, 7x7/flat_side_roof 100 |
| `gloamingforest:lich_tower/7x7/side_roof` | 7x7/curved_roof 100, 7x7/fence_roof 100, 7x7/pointed_roof 100, 7x7/pyramid_roof 100, 7x7/slabs_roof 100 |
| `gloamingforest:lich_tower/7x7/trim` | 7x7/beard_trim 100 |
| `gloamingforest:lich_tower/9x9` | 9x9/altar 100, 9x9/archives 100, 9x9/cauldron_keep 100, 9x9/center_decor 100, 9x9/elbow_junction 100, 9x9/enchanting_prison 100, 9x9/full_junction 100, 9x9/holding 100, 9x9/large_study 100, 9x9/lectern_hall 100, 9x9/library_junction 100, 9x9/lockup 100, 9x9/mossy_junction 100, 9x9/straight_junction 100, 9x9/t_junction 100, 9x9/tiered_library 100, 9x9/tiered_spawner 100, 9x9/tiered_study 100, 9x9/winding_ways 100 |
| `gloamingforest:lich_tower/9x9/beard` | 9x9/beard_checkered 100, 9x9/beard_chiseled 100, 9x9/beard_chunks 100, 9x9/beard_staggered 100 |
| `gloamingforest:lich_tower/9x9/beard_fallback` | 9x9/beard_flat 100 |
| `gloamingforest:lich_tower/9x9/ladder_1` | 9x9/altar 100, 9x9/archives 100, 9x9/cauldron_keep 100, 9x9/centerpiece 100, 9x9/enchanting_prison 100, 9x9/large_study 100, 9x9/lectern_hall 100, 9x9/mossy_junction 100, 9x9/study 100 |
| `gloamingforest:lich_tower/9x9/ladder_2` | 9x9/cauldron_keep 100, 9x9/classic_library 20, 9x9/library_junction 100, 9x9/seven 1, 9x9/study 100 |
| `gloamingforest:lich_tower/9x9/ladder_4` | 9x9/library_junction 100, 9x9/seven 1, 9x9/study 100 |
| `gloamingforest:lich_tower/9x9/ladder_5` | 9x9/altar 100, 9x9/archives 100, 9x9/centerpiece 100, 9x9/enchanting_prison 100, 9x9/large_study 100, 9x9/lectern_hall 100, 9x9/mossy_junction 100, 9x9/study 100 |
| `gloamingforest:lich_tower/9x9/roof` | 9x9/curved_roof 100, 9x9/fence_roof 100, 9x9/pointed_roof 100, 9x9/pyramid_roof 100, 9x9/slabs_roof 100 |
| `gloamingforest:lich_tower/9x9/roof_fallback` | 9x9/flat_roof 100, 9x9/flat_side_roof 100 |
| `gloamingforest:lich_tower/9x9/side_roof` | 9x9/curved_roof 100, 9x9/fence_roof 100, 9x9/pointed_roof 100, 9x9/pyramid_roof 100, 9x9/slabs_roof 100 |
| `gloamingforest:lich_tower/9x9/special` | 9x9/keepsake_casket 100 |
| `gloamingforest:lich_tower/9x9/trim` | 9x9/beard_trim 100 |
| `gloamingforest:lich_tower/bridge_from_central` | central_bridge 100, central_bridge_open 100 |
| `gloamingforest:lich_tower/bridge_from_central_fallback` | central_bridge 100 |
| `gloamingforest:lich_tower/center_decor` | 3x3/tree 100, 3x3/water_fountain 100, 3x3/water_well 100, 3x3/wither_rose 100 |
| `gloamingforest:lich_tower/door_stopper` | wall_bars 100, wall_cobble 100 |
| `gloamingforest:lich_tower/door_stopper_fallback` | wall_cobble 100 |
| `gloamingforest:lich_tower/end_bridge` | end_bridge_1 100, end_bridge_2 100, end_bridge_3 100, end_bridge_4 100, end_bridge_5 100 |
| `gloamingforest:lich_tower/gallery` | gallery/castaway_paradise 100, gallery/darkness 100, gallery/lucid_lands 100, gallery/music_in_the_mire 100, gallery/the_hostile_paradise 100 |
| `gloamingforest:lich_tower/gallery_even` | gallery/fence_roof_even 100, gallery/slabs_roof_even 100, gallery/stairs_roof_even 100 |
| `gloamingforest:lich_tower/gallery_odd` | gallery/fence_roof_odd 100, gallery/slabs_roof_odd 100, gallery/stairs_roof_odd 100 |
| `gloamingforest:lich_tower/grave` | grave 100, grave_ajar 25, grave_open 50 |
| `gloamingforest:lich_tower/mob_bridge` | bridge_spawner 100, bridge_spawner_bend 100, bridge_spawner_ropes 100, bridge_spawner_wide 100, bridge_spawner_zag 100, bridge_spawner_zig 100 |
| `gloamingforest:lich_tower/room_bridge` | room_bridge_1 100, room_bridge_2 100, room_bridge_3 100, room_bridge_4 100, room_bridge_5 100 |
| `gloamingforest:lich_tower/room_bridge_fallback` | no_bridge 100 |
| `gloamingforest:lich_tower/room_decor` | 3x3/lava_well 100, 3x3/sapling 100, 3x3/water_fountain 100, 3x3/water_well 100 |
