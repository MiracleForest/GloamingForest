# hydra (U24): Hydra lair, Hydra and fire swamp

Behaviour spec of unit **U24** (`hydra`), written by F15 from The Twilight Forest 4.8 (`9e40a757`). Java paths are
relative to `src/main/java/twilightforest/`; data paths to `src/generated/resources/`.

| Unit fact | Value |
|---|---|
| Selftests | `hydra.*`, testPos dx `2400-2499` (R12) |
| World data | `gf:hydra:*` (nothing persistent beyond entity data; keep for fight-state migration) |
| Net channels | `hydra.*` (part positions go through F8's multipart sync) |
| Contracts | landmark, structure, boss, entity, projectile, features, families+blockevents, fx |
| Config | none of its own (boss drop chests and multiplayer adjuster are F8's `bosses` section) |

**Tag namespaces.** Tags are written `#<ns>:<path>`. `#twilightforest:` tags and the `#c:` / `#neoforge:` tags that
owners.tsv lists (`tag` rows) are TF data (`data/<ns>/tags/**`) imported by F1; `#minecraft:` tags are vanilla Java
1.21.1 tags (TF adds members to some). Bedrock has no Java tags: every tag here resolves through `gf::tags` (F9,
`src/feature/tags/Tags.h`), and units add members only through union fragments (R6). TF's damage-type and tool-tier
tags are in the `neoforge` namespace (`#neoforge:is_physical`, `#neoforge:is_magic`, `#neoforge:is_environment`,
`#neoforge:needs_wood_tool`), never `#c:`.

NeoForge-provided common tags used in this spec (defined by NeoForge 21.1's own data pack, not by TF, so owners.tsv has
no row for them; F1 imports their members with the vanilla tags TF references):

| Tag | NeoForge 21.1 members | Bedrock equivalent | Used by |
|---|---|---|---|
| `item/#c:dusts/redstone` | `minecraft:redstone` | `minecraft:redstone`; expanded to the item in the Bedrock recipe | encased fire jet and encased smoker recipes |

## 1. Structure `twilightforest:hydra_lair`

Sources: `world/components/structures/type/HydraLairStructure.java`, `world/components/structures/HydraLairComponent.java`,
`world/components/structures/HollowHillComponent.java` (F7 base `bases/HollowHill.h`), `init/custom/StructureSpeleothemConfigs.java`.

| Field | Value |
|---|---|
| Structure type | `twilightforest:hydra_lair` (codec: progression fields + `speleothem_config`) |
| Structure set | `twilightforest:hydra_lair`, placement `twilightforest:landmark_grid` (F6), weight 1 |
| Biomes | `#twilightforest:valid_hydra_lair_biomes` (fire swamp) |
| Step | `surface_structures`, terrain adaptation none, centred in chunk |
| Progression lock | `advancements_required: [twilightforest:progress_labyrinth]` (F6 enforces protection, hints, the block-breaking ban) |
| Hint | kobold carrying the book `twilightforest.book.hydralair` (4 pages, title "Notes on the Fire Swamp") |
| Decoration clearance | chunk clearance radius 2, adjust elevation false, surface decorations false, underground decorations false, vegetation true |
| Spawn overrides | every mob category overridden with an empty list over the structure box (no natural spawns inside) |
| Map icon | `twilightforest:hydra_lair` (map decoration, below) |
| Speleothems | `speleothem_config: twilightforest:hydra_lair` (F7 data: triangular lattice spacing 4.5, x_offset 3.897186, x_spacing 7.794372, z_offset 2.2498753, z_spacing 4.5; stalactites: ore list iron, coal, copper, glowstone, gold, redstone with ore_chance 1.0, stalactite_chance 1.0, stalagmite_chance 0.0625 using `hill_stone_stalactite`) |

Pieces: one piece of type `twilightforest:TFHydra` (`HydraLairComponent extends HollowHillComponent`), created by
`getFirstPiece` at (x - 7, y, z - 7) with hill size 2 and y + 2:

- `radius = (2 x 2 + 1) x 8 - 6 = 34`, `hdiam = (2 x 2 + 1) x 16 = 80`; bounding box from (-34, -(3 + 2), -34)
  relative, size (68, 34 / 2 = 17, 68), orientation SOUTH. No children.
- `postProcess` (exact order):
  1. `replaceBlocksDome(center at worldY(-4), radius 36.5, 1/80, box inflated 4)`: grass block and dirt -> stone.
  2. `replaceBlocksDome(center + (-16, 0, -16) at worldY(-1), radius 18.5, 1/80, box inflated 1)`: stone -> grass
     block (the arena floor patch).
  3. Speleothems over the lattice: skip points within 23 blocks (squared 529) of center + (-16, 0, -16) (the arena);
     skip when `distSq > rand x 0.9 x radiusSq` (radiusSq = 0.9 x 34^2); ceiling y =
     `floor(cos(sqrt(distSq) / 80 x PI) x 20)`; stalactite when the config rolls one; stalagmite (at y 1) when rolled
     and `(rand x 0.667 + 0.333) x distSq / radiusSq >= 0.333`.
  4. Place `twilightforest:hydra_boss_spawner` (U5's block) at local (27, 3, 27).
- The piece is a translated procedural piece (R19): exact translation, golden volume hash selftest.

Terraformer (`getStructureTerraformer`, F6 `addTerraformer`, density before noise): hill size 2, `radius = (2 x 4 +
0.8) x 8 = 70.4`, inner radius 62.4; mound = `HollowHillFunction(center at minY + 8, 70.4, 0.7)` clamped 0..1; inner
floor = `yClampedGradient(-1, 0, 1, -4)`; inner ceiling = `-1 x HollowHillFunction(center at minY + 6, 62.4, 0.675)`;
interior = max(ceiling, floor); interior mask = `FocusedDensityFunction(center at minY, 62.4 x 0.52, -62.4, 1)`;
hollow hill = min(mound, max(mask, interior)); masking sphere = `Focused(center - ceil(7.04) y, width x 0.5 + 5,
width x 0.25, 0)` clamped 0..1; cutout = `Focused(center + (-16, 0, -16), 23, -23, 0)` clamped -4..0; lair =
sphere x hollowHill + clamp(max(cutout, innerFloor), -2, 0); result = 8 x lair. (`width` = min of the structure box
x/z spans.) U24 registers this through F6 and needs F5's density helpers (`HollowHillFunction`,
`FocusedDensityFunction`; ask F5/A8 if missing, else `src/feature/ext/hydra/`).

Map decoration `twilightforest:hydra_lair`: `MapDecorationType(asset twilightforest:hydra_lair, showOnItemFrame
true, mapColor -1, explorationMapElement false, trackCount true)`, sprite `textures/map/decorations/hydra_lair.png`
(client mod); vanilla clients get the nearest vanilla decoration from F6/U13's mapping.

Strings: `structure.twilightforest.hydra_lair` "Hydra's Lair" (lang owned by U24 per R9).

## 2. Boss `twilightforest:hydra` (Hydra extends BaseTFBoss)

Sources: `entity/boss/Hydra.java`, `HydraHeadContainer.java`, `HydraHead.java`, `HydraNeck.java`, `HydraPart.java`,
`HydraSmallPart.java`, `entity/TFPart.java`, `entity/boss/BaseTFBoss.java` (F8 boss kit),
`client/renderer/entity/Hydra{,Head,Neck}Renderer.java`, `client/model/entity/Hydra{,Head,Neck}Model.java`.

| Field | Value |
|---|---|
| Category / size | `MONSTER`, 16.0 x 12.0, **fire immune**, tracking 80 / interval 3 |
| Attributes | max_health 360, movement_speed 0.28 |
| XP | 511 |
| Boss bar | colour 0x05EBB9, overlay PROGRESS, progress = health / max (F8 `boss::bar`) |
| Home | restriction point = the spawner position, radius 20; home structure `twilightforest:hydra_lair` |
| Death chest | `twilightforest:mangrove_chest` (random horizontal facing) when boss drop chests are on |
| Boss spawner block | `twilightforest:hydra_boss_spawner` (peaceful: the boss turns back into its spawner) |
| Spawn egg | `twilightforest:hydra_spawn_egg`, 0x142940 / 0x29806b |
| Spawn placement | ON_GROUND, `Mob::checkMobSpawnRules` (eggs/commands only; no natural spawn) |
| Synced | `HEAD_NAMES` (string list of 7, F8/U24 serializer), per-part data below |
| Saved | `NumHeads` (byte bitmask of active heads), `HeadNames` (list of 7 strings), BaseTFBoss home and buffered drops |

BossSpec for F8: `{entityId: twilightforest:hydra, homeStructure: twilightforest:hydra_lair, lootTable:
twilightforest:entities/hydra, deathChestBlock: twilightforest:mangrove_chest, bar: {rgb: 0x05EBB9}, deathTicks:
200}`.

### 2.1 Parts

7 head containers (`hc[0..6]`), each with a `HydraHead` (4 x 4) and five `HydraNeck` parts (2 x 2) a..e; plus body
(`HydraSmallPart` 6 x 6), left leg and right leg (2 x 3 each, registered but never positioned by TF) and tail (6 x 2).
Part order: body, leftLeg, rightLeg, tail, then per head: head, necka..necke (39 parts). Part ids are the parent id +
1 + index (`TFPart.setId(id + 1)`, `assignPartIDs`).

- Heads 0, 1, 2 start active (IDLE, 60 of 60 ticks); heads 3..6 start DEAD (20 of 20).
- `HydraPart`: synced `DATA_SIZEACTIVE`; deactivated parts have size 0 x 0; when `markedDead` the part counts
  `deathTime` and the client spawns 10 particles per tick (`explosion` 1/5 else `poof`, gaussian 0.02); at deathTime
  20 it deactivates. Parts clear fire every tick, cannot ride or use portals, save nothing.
- `HydraHead` synced: `DATA_MOUTH_POSITION` (float), `DATA_MOUTH_POSITION_LAST` (float), `DATA_STATE` (byte = state
  ordinal). Using a named `minecraft:name_tag` on a head (or its neck) names that head, consumes the tag and stores
  the name in `HEAD_NAMES[i]`; a dying head clears its name.
- Damage to a part goes to `Hydra.attackEntityFromPart`; the hydra itself only accepts damage that bypasses
  invulnerability (`/kill`), and `isInvulnerableTo` is false for `twilightforest:hydra_mortar` damage.
- Not pickable / pushable as a whole; parts are pickable; no knockback; `isOnFire` false; sound volume 2.0.

Body placement (`aiStep`): body at `pos - sin(a) x 3, y + 0.1, + cos(a) x 3` with `a = (yBodyRot + 180) deg`; tail at
10.5 instead of 3. When not hurt, entities in the body or tail boxes (except creative players) are pushed
`(dx / d, 0.2, dz / d) x 8` with `d = max(dx^2 + dz^2, 0.1)` (Ender Dragon knockback copy).

Neck anchors per head (relative, rotated by `-(yBodyRot + neckRotation)`): 0: (0, 3, -1) rot 0; 1: (-1, 3, 3) rot 90;
2: (1, 3, 3) rot -90; 3: (-1, 2.5, 3) rot 135; 4: (1, 2.5, 3) rot -135; 5: (-1, 2, 5) rot 135; 6: (1, 2, 5) rot -135.
The five neck parts are placed at factors 0, 0.25, 0.5, 0.75, 1.0 between the head end (head y - 0.5, pulled back
1.8 along yaw when pitched down, or 1 block along the look vector when pitched up) and the anchor, interpolating yaw
and pitch (pitch toward 0).

Head position: `vec = (0, 0, neckLength)` rotated X by `(xRot x PI + xSwing) / 180` and Y by `-(yBodyRot + yRot +
ySwing) x PI / 180`, head = hydra + vec + (0, 3, 0); swings `xSwing = sin(tick / periodX) x 3`, `ySwing = sin(tick /
periodY) x 5` with periodX 20 (heads 0, 3), 5 (1, 4), 7 (others) and periodY 10 (0, 4), 6 (1, 6), 5 (others); no
swing while dead. Neck length, x/y rotation and mouth openness lerp (clamped) from the previous state's pose to the
current state's over the state's duration.

### 2.2 Head state machine (`HydraHeadContainer.State`)

| State | Duration (ticks) | Automatic next |
|---|---|---|
| IDLE | 10 | IDLE |
| BITE_BEGINNING | 40 | BITE_READY |
| BITE_READY | 80 | BITING |
| BITING | 7 | BITE_ENDING |
| BITE_ENDING | 40 | ATTACK_COOLDOWN |
| FLAME_BEGINNING | 40 | FLAMING |
| FLAMING | 100 | FLAME_ENDING |
| FLAME_ENDING | 30 | ATTACK_COOLDOWN |
| MORTAR_BEGINNING | 40 | MORTAR_SHOOTING |
| MORTAR_SHOOTING | 25 | MORTAR_ENDING |
| MORTAR_ENDING | 30 | ATTACK_COOLDOWN |
| DYING | 70 | DEAD |
| DEAD | 20 | DEAD |
| ATTACK_COOLDOWN | 80 | IDLE |
| BORN | 20 | ROAR_START |
| ROAR_START | 10 | ROAR_RAWR |
| ROAR_RAWR | 50 | IDLE |

A requested next state replaces the automatic one at the end of the current state; `endCurrentAction` ends it now.
A head returning from a secondary attack skips ATTACK_COOLDOWN (goes to IDLE). The state ordinal is synced to the
head entity for the client.

Poses per head `(xRotation, yRotation, neckLength, mouthOpen)` (TF `setupStateRotations`, heads 0..6):

| State | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|---|
| IDLE, ATTACK_COOLDOWN, BORN | 60,0,7,0 | 10,60,9,0 | 10,-60,9,0 | 50,90,8,0 | 50,-90,8,0 | -10,90,9,0 | -10,-90,9,0 |
| FLAME_BEGINNING, MORTAR_BEGINNING | 50,0,8,.75 | 30,45,9,.75 | 30,-45,9,.75 | 50,90,8,.75 | 50,-90,8,.75 | -10,90,9,.75 | -10,-90,9,.75 |
| FLAMING, MORTAR_SHOOTING | 45,0,8,1 | 30,60,9,1 | 30,-60,9,1 | 50,90,8,1 | 50,-90,8,1 | -10,90,9,1 | -10,-90,9,1 |
| FLAME_ENDING, MORTAR_ENDING | 60,0,7,0 | 10,45,9,0 | 10,-45,9,0 | 50,90,8,0 | 50,-90,8,0 | -10,90,9,0 | -10,-90,9,0 |
| BITE_BEGINNING (heads 0-2) | -5,60,5,.25 | -10,60,9,.25 | -10,-60,9,.25 | | | | |
| BITE_READY | -5,60,5,1 | -10,60,9,1 | -10,-60,9,1 | | | | |
| BITING | -5,-30,5,.2 | -10,-30,5,.2 | -10,30,5,.2 | | | | |
| BITE_ENDING | 60,0,7,0 | -10,60,9,0 | -10,-60,9,0 | | | | |
| DYING | -20,0,7,0 | -20,60,9,0 | -20,-60,9,0 | -20,90,8,0 | -20,-90,8,0 | -10,90,9,0 | -10,-90,9,0 |
| DEAD | 0,179,4,0 | 0,179,4,0 | 0,-180,4,0 | 0,179,4,0 | 0,-180,4,0 | 0,179,4,0 | 0,-180,4,0 |
| ROAR_START | 60,0,7,.25 | 10,60,9,.25 | 10,-60,9,.25 | 50,90,8,.25 | 50,-90,8,.25 | -10,90,9,.25 | -10,-90,9,.25 |
| ROAR_RAWR | 60,0,9,1 | 10,60,11,1 | 10,-60,11,1 | 50,90,10,1 | 50,-90,10,1 | -10,90,11,1 | -10,-90,11,1 |

Head facing per tick (server): BITE_READY: face the target (5 degrees/tick yaw), clamp yaw offset to [-90, -60] for
heads 0 and 1 and [60, 90] for head 2 (TF's min/max as written), and set the target point 16 blocks ahead (+1.5 y);
BITING/BITE_ENDING: face the target and add PI/4 to pitch; ROAR_RAWR: face the stored target point (10/tick);
FLAME_BEGINNING/FLAMING: move the target point toward the target entity by the tracking speed (0.04 per tick, 0.1 on
Hard) and face it (5/tick); otherwise face the target entity or, idle, a point 30 blocks ahead of the body at y + 3
(1.5/tick). Pitch limit = hydra max head pitch 500.

Per-tick head work (server): activate/deactivate parts by state; respawn counter; state advance; position; facing;
attacks; sounds. Client: mouth particles 3.5 blocks ahead (+1 y): FLAME_BEGINNING 1 `flame` + 1 `smoke` (always
visible, jitter +-0.5); FLAMING 5 `twilightforest:large_flame` with spread `0.0075 x (5 + r x 2.5)` and speed
`1 + r`; BITE_BEGINNING/BITE_READY 1 `splash`; MORTAR_BEGINNING 1 `large_smoke`.

Dying head: deathTime 1 marks the head dead and clears its name; 10/20/30/40/50 mark necks a..e dead; head and necks
keep hurtTime 20 (red) while DYING.

### 2.3 Attacks

| Attack | Damage / effect |
|---|---|
| Bite (BITING state, every tick) | every living entity (not the hydra) in the head box inflated (0, 1, 0) takes 48 `twilightforest:hydra_bite` damage (attacker = hydra); players get a motion packet `(-step.x x 0.5, 0.1, -step.z x 0.5)` of the head's facing, others `living.knockback(strength = -facing.stepX, xRatio = 0.1, zRatio = -facing.stepZ)` (TF passes the step as the strength and 0.1 as the x ratio: a TF quirk, keep it; do not write `knockback(0.1, -dx, -dz)`. With vanilla `knockback` semantics only a WEST-facing head pushes: strength 1 x (1 - knockback resistance) along -x, y `min(0.4, vy / 2 + strength)` on ground; NORTH / SOUTH give strength 0 and EAST -1, which do nothing). A player blocking with a shield not on cooldown: shatter sound (`item.twilightforest.shield.shatter` for `minecraft:shield`, else `item.twilightforest.knightmetal_shield.shatter`; other units' events, vol 1.0, player voice pitch) and 112 durability damage; then always shield cooldown 200 ticks, stop using, motion (-0.5 step, 0.15) |
| Flame (FLAMING, every tick) | the entity the head looks at (ray 30 from head y + 1, block clip OUTLINE/ANY, entities in the head box moved along the look by the clipped distances inflated 3, excluding necks a-c and parts of the same hydra): if not fire immune and it takes 19 `twilightforest:hydra_fire` damage, ignite 3 s. Sound `entity.twilightforest.hydra.shoot_fire` every 5 head ticks (vol 0.5 + r, pitch 0.3 + r x 0.7), game event PROJECTILE_SHOOT |
| Mortar (MORTAR_SHOOTING, every 10 ticks of progress) | spawn a `twilightforest:hydra_mortar`; blasting mode when the head cannot see its target; sound `entity.twilightforest.hydra.shoot` vol 10.0, pitch 1 +- 0.2 |

Other sounds: `entity.twilightforest.hydra.roar` during ROAR_RAWR every tick (vol 1.25, pitch 0.7 + r x 0.3, game
event ENTITY_ACTION); `entity.twilightforest.hydra.warn` once at BITE_READY progress 60 (vol 2.0, pitch 0.7 + r x 0.3);
ambient `entity.twilightforest.hydra.growl`, hurt `entity.twilightforest.hydra.hurt`, death
`entity.twilightforest.hydra.death` (volume 2.0).

### 2.4 Brain (`customServerAiStep`, server, every tick)

1. No walking (`xxa = zza = 0`); navigation never paths. After 1000 ticks without damage, heal 1 every 5 ticks.
2. Heads with `damageTaken > 120` that are not dead: set DYING now, respawn counter 140 for that head **and** for one
   random head that can respawn (DEAD with counter -1). Respawn: when the counter reaches 0 the head goes BORN,
   damage resets. (So each slain head brings back itself and one more, up to 7.)
3. With chance 0.7: take the nearest non-spectator player within 48 (`getNearestPlayer(this, 48)`, creative players
   included in the search); if it exists and is not creative, it becomes the target and the chase timer is set to
   `100 + rand(20)`; otherwise (no player, or the nearest one is creative) yaw velocity = `(r - 0.5) x 20`. A creative
   player standing nearer hides survival players behind it: do not filter creative players out of the search.
4. Grief (mob griefing, not dying): destroy (no drops) every destroyable block in the body, tail and living heads'
   boxes; every 20 ticks, if less than 60 % of the blocks under its box are solid, destroy the layer under it.
5. With a target: look at it (10, 500); idle heads (not attacking or secondary) switch to it; if alive and visible,
   `attackEntity(target, distance)`; drop the target when the chase timer runs out, it dies, or it is farther than 48.
   Without a target: 5 % chance to re-roll the yaw velocity, turn by it, pitch 0, idle heads lose their target.
6. Secondary attacks: nearest other living entity within 20 (box around the hydra) that is not a hydra, not
   targeted by any head, visible, not creative/spectator; heads 1..6 that are alive, idle and closer to it than the
   body (XZ) start FLAME (distance < 20, 1/10) or MORTAR (8 < d < 32, 1/16) against it as a secondary attack.

`attackEntity`: heads 0..2 when idle and not "too many attacking": BITE (4 < d < 10, 1/10, more than 2 active heads, no
other head biting), else FLAME (d < 20, 1/100), else MORTAR (8 < d < 32, target not above the hydra's box, 1/160);
heads 3..6: FLAME then MORTAR with the same odds. "Too many": other attacking heads (biting counts 3) `>= 1 +
activeHeads x factor`, factor 0.3 (0.5 on Hard).

Head damage (`attackEntityFromPart`): in-wall damage destroys blocks in the part's box; ignore damage from the hydra
or its parts; ignore damage to the necks of dead heads and to dying heads; ignore attackers farther than distance^2
400 (600 for a reflected mortar); if the hit head's mouth is open > 0.5 the full damage goes to the hydra and the
head's counter, else `round(damage / 8)` (armour) to both; any damage resets the heal timer.

### 2.5 Death and despawn

`tickDeath`: deathTime + 1; at 1: cancel respawns, alive heads IDLE + hurtTime 200; every 20 ticks up to 140 the head
`deathTime / 20 - 1` goes DYING; at 200 the hydra is removed (KILLED). Client: 10 particles per tick (`explosion` or
`poof`) around the body. On death (F8): conquer `twilightforest:hydra_lair`, buffer loot, place the death chest at
the boss chest location. Peaceful: heads discarded, boss replaced by its spawner at the home point.

### 2.6 Rendering

Texture `hydra4.png` for body, heads and necks; body renderer shadow 4.0, no death flip, never culled; head model:
`head.xRot -= mouth x PI/12`, `jaw.xRot = mouth x PI/3` with mouth lerped from the synced last/current values; legs
swing `cos(limbSwing x 0.6662) x 1.4 x amount`. Heads and necks render only while active; named heads show their
name. Bedrock: one actor per part (F8 `addPart`) or a single geometry with bones driven by synced head state/pose
properties; spike S4 decides (record which in the PR).

### 2.7 Loot `twilightforest:entities/hydra`

Pool 1: `twilightforest:hydra_chop` 5..35 + looting 0..1 + `twilightforest:multiplayer_addition` 5..10 per extra
player; pool 2: `twilightforest:fiery_blood` 7..10 + looting 0..2 + multiplayer 1..3; pool 3:
`twilightforest:hydra_trophy`. (Items owned by U10 / U8 / U5; the loot function is F10's.)

## 3. Hydra mortar `twilightforest:hydra_mortar` (HydraMortar)

Source: `entity/boss/HydraMortar.java`, `client/renderer/entity/HydraMortarRenderer.java`, `client/model/entity/HydraMortarModel.java`.

- `makeCastedBuilder`: MISC, 0.75 x 0.75, tracking 150, interval 3, **fire immune**, no egg.
- Spawned 3.5 blocks along the head's look (+1 y), owner = the hydra, `shootFromRotation(head pitch, yaw, -20, 0.5,
  1.0)`; gravity 0.05; `isOnFire` true; pickable with pick radius 1.5.
- Fuse 80: counts down only while on ground; at 0 detonate. (TF multiplies a copy of the motion by 0.9 on ground and
  discards it: no friction effect.)
- Hit block: normal mortar detonates on a side/bottom face, else lands (y motion 0, on ground); a blasting mortar
  detonates on any block.
- Hit entity (server, with owner): detonates unless the entity is another mortar of a different owner, the owner,
  or a part of the owner hydra.
- Detonate: `level.explode(mortar, x, y, z, power, fire, MOB)` with explosion power 4.0 (blasting) or 0.1, **fire =
  mob griefing allowed** (`EventHooks.canEntityGrief`: every mortar explosion starts fires while mob griefing is on),
  interaction MOB (block damage only with the `mobGriefing` rule, drops decayed per `mobExplosionDropDecay`); blasting
  explosions cap block resistance at 0.8 except blocks in `#twilightforest:common_protections`; then every entity
  within 1 block that is not fire immune (or is the hydra / a hydra part) takes 18 `twilightforest:hydra_mortar`
  damage (indirect, direct = mortar, attacker = owner) and burns 5 s; discard.
- Being hit (server, attacker present): re-shoot along the attacker's look at speed 1.5, inaccuracy 0.1, airborne,
  fuse + 20; a living attacker becomes the owner (reflected mortars can damage the hydra).
- Render: model with texture `hydramortar.png`, shadow 0.5, base pass alpha 0.075; TNT-style swell in the last 10
  fuse ticks (`1 + f^4 x 0.3`); white flash overlay every other 5 fuse ticks with alpha `(1 - fuse/100) x 0.8`.

## 4. Boss spawner logic (R20)

`block/entity/spawner/HydraSpawnerBlockEntity.java` + `BossSpawnerBlockEntity.java`: U24 registers
`boss::registerSpawner({blockId: twilightforest:hydra_boss_spawner, entityId: twilightforest:hydra, range: 50,
particle: minecraft:flame})`. Base rules (F8): every tick while a living player is within range and the boss was not
spawned: client particle at a random point in the block; server (not Peaceful): create the boss, spawn at the block
below when it has no collision else at the spawner, random yaw, `finalizeSpawn(SPAWNER)`, home = spawner position;
on success destroy the spawner (no drops) and mark spawned.

## 5. Blocks

Sources: `block/FireJetBlock.java`, `block/EncasedFireJetBlock.java`, `block/TFSmokerBlock.java`,
`block/EncasedSmokerBlock.java`, `block/entity/FireJetBlockEntity.java`, `block/entity/TFSmokerBlockEntity.java`,
`enums/FireJetVariant.java`, `init/TFBlocks.java`, `init/TFBlockEntities.java`.

| Block | Properties | States | Tool | Light |
|---|---|---|---|---|
| `twilightforest:fire_jet` | map colour GRASS, sound GRASS, strength 1.5 / 6.0, random ticks | `state` = idle, popping, flame, timeout (default idle) | shovel (`#minecraft:mineable/shovel`) | 15 while `flame`, else 0 |
| `twilightforest:encased_fire_jet` | map colour SAND, sound WOOD, strength 1.5 / 6.0, requires correct tool, ignited by lava | same `state` | axe | 15 while `flame` |
| `twilightforest:smoker` | map colour GRASS, sound GRASS, strength 1.5 / 6.0 | none | shovel | 0 |
| `twilightforest:encased_smoker` | map colour SAND, sound WOOD, strength 1.5 / 6.0, requires correct tool, ignited by lava | `active` boolean (default false) | axe | 0 |

Block entities: `twilightforest:flame_jet` (fire jet + encased fire jet), `twilightforest:smoker` (smoker + encased
smoker). Both keep only an in-memory tick counter (not saved). Path type DAMAGE_FIRE for a fire jet that is not idle.

- **Fire jet random tick** (server, state idle): look for fuel at the block below (`#twilightforest:fire_jet_fuel`
  block tag = `minecraft:lava`, or a fluid in fluid tag `#twilightforest:fire_jet_fuel` = `#minecraft:lava`), else 3
  tries at random offsets (rand3 - 1, 0, rand3 - 1) on that layer; if found, set the lava to air and the jet to
  `popping`.
- **Encased fire jet neighbour change**: idle and powered -> `popping` and `block.twilightforest.jet.start` (vol 0.3,
  pitch 0.6); `timeout` and not powered -> idle. (Encased jets do not random-tick for lava: only redstone.)
- **Block entity tick (both jets)**: `popping`: counter + 1; at 80 -> `flame` (server), counter 0; every 20 ticks 8
  `minecraft:lava` particles at (0.5, 1.5, 0.5) and `block.twilightforest.jet.pop` (vol 0.2 + r x 0.2, pitch 0.9 +
  r x 0.15). `flame`: counter + 1; above 60 -> `idle` (fire jet) or `timeout` (encased), counter 0; client every 2
  ticks one `large_smoke` and five `twilightforest:large_flame` (centre (0.5, +1, 0.5) speed (0, 0.5, 0) and four at
  +-1 offsets drifting 0.05 inward); client sounds every 4 ticks `block.twilightforest.jet.active` (vol 1 + r,
  pitch 0.3 + r x 0.7) and at counter 1 `block.twilightforest.jet.start`; server every 5 ticks every entity in the AABB
  between the centres of (pos - 2, y, - 2) and (pos + 2, y + 4, + 2) that is not fire immune takes 2
  `twilightforest:fire_jet` damage (no attacker) and gets 300 fire ticks.
- **Encased smoker neighbour change** (server): not active and powered -> active; active and unpowered -> inactive;
  both play `block.twilightforest.smoker.start` (vol 0.3, pitch 0.6).
- **Smoker block entity** (client): every 4 ticks (smoker always, encased smoker while active) one
  `twilightforest:huge_smoke` at (0.5, 0.95, 0.5) with velocity `(cos(counter / 10) x 0.05, 0.25, sin(counter / 10) x 0.05)`.

Block loot (each 1 pool, survives explosion, drops itself): `twilightforest:blocks/fire_jet`,
`twilightforest:blocks/encased_fire_jet`, `twilightforest:blocks/smoker`, `twilightforest:blocks/encased_smoker`.
Block items: `twilightforest:fire_jet`, `twilightforest:encased_fire_jet`, `twilightforest:smoker`,
`twilightforest:encased_smoker` (plain block items).

Models (F1 imports): fire jet / smoker single model; encased fire jet `encased_fire_jet` (idle, timeout) /
`encased_fire_jet_on` (popping, flame); encased smoker `encased_smoker` / `encased_smoker_on`.

Bedrock: no block entities on custom blocks (S3): the jet and smoker timers run from F11 block events
(`onRandomTick`) plus a U24 ticking registry of active positions (BlockData for the counter, `gf:hydra:` not needed);
redstone comes from `gf::redstone::watch`/`power` (custom blocks get no neighbour updates, AGENTS.md). Encased jets
are also placed by U26's dark tower by string. Light 15 while flaming: a `state`-dependent `minecraft:light_emission`
permutation.

## 6. Recipes and recipe advancements

- `twilightforest:encased_fire_jet` (shaped, category redstone): `#∴#` / `∴^∴` / `uuu` with `#` =
  `twilightforest:encased_towerwood`, `∴` = `#c:dusts/redstone`, `^` = `twilightforest:fire_jet`, `u` =
  `minecraft:lava_bucket` -> 1 encased fire jet (buckets return empty, vanilla crafting remainder).
- `twilightforest:encased_smoker` (shaped, redstone): `#∴#` / `∴^∴` / `#∴#` with `^` = `twilightforest:smoker` -> 1.
- Unlock advancements (F1-generated, R21): `twilightforest:recipes/redstone/encased_fire_jet` (has
  `twilightforest:fire_jet` or has the recipe) and `twilightforest:recipes/redstone/encased_smoker` (has
  `twilightforest:smoker`), both rewarding their recipe.

## 7. Features

| Id | Kind | Definition |
|---|---|---|
| `twilightforest:fire_jet` | feature type | `FireJetFeature` (`world/components/feature/FireJetFeature.java`), config `BlockStateConfiguration` |
| `twilightforest:fire_jet` | configured feature | type `twilightforest:fire_jet`, state `twilightforest:fire_jet[state=idle]` |
| `twilightforest:smoker` | configured feature | type `twilightforest:fire_jet`, state `twilightforest:smoker` |
| `twilightforest:fire_jet` | placed feature | heightmap `WORLD_SURFACE_WG`, in_square, biome |
| `twilightforest:smoker` | placed feature | heightmap `WORLD_SURFACE_WG`, in_square, biome |

Both placed features are in the fire swamp biome's VEGETAL_DECORATION step (U1's biome JSON, by string).

FireJetFeature.place: fail unless `FeatureUtil.isAreaSuitable(origin, 5, 2, 5)`; 4 tries at origin + (r8 - r8, r4 -
r4, r8 - r8): the spot must be air, see the sky from below water, and the block below plus the four horizontal
neighbours of that block must be `#minecraft:dirt`; then: 5 x 5 grass blocks at y - 1 (flag 0), the configured state
at y - 1 centre, a reservoir at y - 2: 3 x 3 `minecraft:lava` inside, `minecraft:stone` on the 5 x 5 ring where there
is no lava, and stone under the whole 5 x 5 at y - 3. Returns true.

## 8. Damage types (owned by U24)

| Id | message_id (en_us) | Effects | Exhaustion | Tags |
|---|---|---|---|---|
| `twilightforest:fire_jet` | `twilightforest.fireJet` ("%1$s accidentally walked into a Fire Jet") | burning | 0.1 | `#minecraft:is_fire`, `#neoforge:is_environment`, `#neoforge:is_physical`, `#minecraft:ignites_armor_stands`, `#minecraft:no_knockback` |
| `twilightforest:hydra_bite` | `twilightforest.hydraBite` ("%1$s's skin was ripped off by the Hydra") | hurt | 0.0 | `#neoforge:is_physical` |
| `twilightforest:hydra_fire` | `twilightforest.hydraFire` ("%1$s was roasted alive by the Hydra") | hurt | 0.0 | `#minecraft:is_fire`, `#minecraft:ignites_armor_stands`, `#minecraft:avoids_guardian_thorns` |
| `twilightforest:hydra_mortar` | `onFire` (vanilla "%1$s burned to death") | burning | 0.0 | `#minecraft:is_fire`, `#minecraft:ignites_armor_stands`, `#minecraft:avoids_guardian_thorns` |

All scale `when_caused_by_living_non_player`. Hydra bite/fire sources name the hydra entity type as attacker even
without an attacker entity (`getEntityDamageSource(..., TFEntities.HYDRA)`), so death messages say "the Hydra".

## 9. Particles (owned by U24)

| Id | Client behaviour (`client/particle/`) |
|---|---|
| `twilightforest:large_flame` | `LargeFlameParticle`: sprite `minecraft:flame`; velocity = 0.01 x random + given; size x 5; lifetime `int(8 / (rand x 0.8 + 0.2)) + 4`; no physics; opaque sheet; size shrinks by `1 - age_frac^2 x 0.5`; brightness rises to full with age; each tick `yd += 0.004`, velocity x 0.96 (x 0.7 horizontally on ground) |
| `twilightforest:huge_smoke` | `SmokeScaleParticle`: vanilla smoke particle at scale 4.0, sprites `minecraft:generic_7..0` |

Both are emitted by id through `gf::fx::particle` (R10); F12 pre-declares the native factories.

## 10. Tags (owned by U24)

| Tag | Values | Used by |
|---|---|---|
| `block/#twilightforest:fire_jet_fuel` | `minecraft:lava` | fire jet random tick |
| `fluid/#twilightforest:fire_jet_fuel` | `#minecraft:lava` | fire jet random tick |
| `banner_pattern/#twilightforest:pattern_item/hydra` | `twilightforest:hydra` (U5's banner pattern) | the hydra banner pattern item (U5) |

## 11. Sound events (owned by U24)

| Event | Subtitle | TF source |
|---|---|---|
| `entity.twilightforest.hydra.growl` | Hydra growls | ambient (3 variants) |
| `entity.twilightforest.hydra.hurt` | Hydra hurts | hurt (4) |
| `entity.twilightforest.hydra.death` | Hydra roars in defeat | death |
| `entity.twilightforest.hydra.roar` | Hydra roars | ROAR_RAWR (2) |
| `entity.twilightforest.hydra.warn` | Hydra prepares to bite | BITE_READY tick 60 |
| `entity.twilightforest.hydra.shoot` | Hydra shoots mortar | mortar launch (vanilla ghast shoot in TF) |
| `entity.twilightforest.hydra.shoot_fire` | Hydra spews fire | flame breath (ghast shoot) |
| `block.twilightforest.jet.start` | Fire Jet activates | jet start (comparator click in TF) |
| `block.twilightforest.jet.pop` | Fire Jet pops | popping (lava pop) |
| `block.twilightforest.jet.active` | Fire Jet flares | flame (ghast shoot) |
| `block.twilightforest.smoker.start` | Smoker activates | encased smoker toggle (comparator click) |

## 12. Acceptance (selftests `hydra.*`, dx 2400-2499)

Per the unit record: a seeded boss fight (F8 `seedForTest`): 3 heads active at spawn; damaging a head past 120
kills it and 140 ticks later two heads are BORN (roar); parts follow the body (positions within tolerance of the
formulas above); a mortar lands, explodes after 80 ground ticks and can be reflected (owner changes, hydra takes
18); bite deals 48 and shatters a shield; head names via name tags persist through save/load; death conquers the
lair and places the mangrove chest. Jets: an encased fire jet fires on redstone (popping 80 -> flame 60 -> timeout ->
idle when unpowered), damages 2 and sets 300 fire ticks; a natural jet consumes lava below; encased smoker toggles
`active`. Golden volume hash of the lair piece; client screenshots of the hydra, jets and smokers.
