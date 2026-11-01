# fauna (U16): Passive and ambient fauna

Behaviour spec of unit **U16** (`fauna`), written by F15 from The Twilight Forest 4.8 (`9e40a757`). It is the
checklist the U16 worker implements and the A5 audit compares against. Java paths are relative to
`src/main/java/twilightforest/` of the shared checkout (`%LOCALAPPDATA%\gf-tf`); data paths are relative to
`src/generated/resources/` unless they start with `main/`.

| Unit fact | Value |
|---|---|
| Selftests | `fauna.*`, testPos dx `1600-1699` (R12) |
| World data | `gf:fauna:*` (none needed by TF; reserve for variant migration only) |
| Net channels | `fauna.*` (none needed: every state below is entity data) |
| Contracts | entity, spawn, loot, data, geo (F8, F5, F10, F9, F2) |
| Config | none (TF has no fauna config key) |

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
| `item/#c:crops/carrot`, `item/#c:crops/potato`, `item/#c:crops/beetroot`, `item/#c:crops/wheat` | `minecraft:carrot`, `minecraft:potato`, `minecraft:beetroot`, `minecraft:wheat` (one each) | the same ids on Bedrock | `#twilightforest:boar_tempt_items` (carrot, potato, beetroot), `deer_tempt_items` (wheat), `dwarf_rabbit_tempt_items` (carrot) |
| `item/#c:seeds` | `#c:seeds/{beetroot,melon,pumpkin,torchflower,wheat}` = beetroot, melon, pumpkin, torchflower and wheat seeds (no pitcher pod in 21.1) | `minecraft:beetroot_seeds`, `minecraft:melon_seeds`, `minecraft:pumpkin_seeds`, `minecraft:torchflower_seeds`, `minecraft:wheat_seeds` (same ids); a vanilla-client `minecraft:behavior.tempt` lists these items | `#twilightforest:tiny_bird_tempt_items`, `raven_tempt_items`, `squirrel_tempt_items` |

## 1. Common rules

- Every entity type below is registered by `init/TFEntities.java` `make(...)`: tracking range 80 blocks, update
  interval 3 ticks, velocity updates on, riding offset 0, not fire immune. Eye height is vanilla's default
  (0.85 x height) unless stated. A spawn egg exists because both egg colours are non-zero.
- Attributes are registered in `events/RegistrationEvents.java#addEntityAttributes`; spawn placements in
  `#registerSpawnPlacements` (all `REPLACE`, heightmap `MOTION_BLOCKING_NO_LEAVES`).
- Unlisted attributes keep vanilla defaults (`Mob.createMobAttributes()`: follow_range 16, step_height 0.6;
  `Monster.createMonsterAttributes()` adds attack_damage 2).
- Vanilla predicates used: `Animal::checkAnimalSpawnRules` (block below in `#minecraft:animals_spawnable_on` and raw
  brightness > 8), `Monster::checkMonsterSpawnRules` (not peaceful, dark enough using the Twilight dimension type:
  `monster_spawn_light_level` uniform 0..7, `monster_spawn_block_light_limit` 7, then `Mob::checkMobSpawnRules`).
- Renderers come from `client/event/ClientRegistrationEvents.java#registerEntityRenderers`; models from
  `client/model/entity/*Model.java` (converted by F2 into `assets/gen/geo/fauna/**`). Textures are under
  `assets/twilightforest/textures/entity/`.
- Sounds: every `entity.twilightforest.*` event below is owned by U16 and played through `gf::fx` by id (R10). Events
  whose TF `sounds.json` entry is an `{"type":"event"}` reference to a vanilla event (boar, bighorn, dwarf rabbit,
  kobold munch, tiny bird takeoff, deer eat uses horse eat files) should map to the matching vanilla Bedrock sound in
  the U16 synth recipe (`tools/sounds/recipes/fauna.json`) instead of a synthesized patch; the rest are synthesized
  originals (TF audio is ARR and never opened).

## 2. Entities

### 2.1 `twilightforest:boar` (Boar)

Source: `entity/passive/Boar.java`, `client/model/entity/BoarModel.java` (PigModel layout), `client/renderer/entity/BoarRenderer.java`.

| Field | Value |
|---|---|
| Base | `Animal` (not a pig: no saddle, no lightning conversion) |
| Category / size | `CREATURE`, 0.9 x 0.9 |
| Attributes | max_health 10.0, movement_speed 0.25 |
| Food / breeding | `isFood` = item tag `#twilightforest:boar_tempt_items` (`#c:crops/carrot`, `#c:crops/potato`, `#c:crops/beetroot`); offspring `twilightforest:boar` |
| Leash offset | (0, 0.6 x eyeHeight, 0.4 x width) |
| Render | texture `wildboar.png`, shadow 0.7 |
| Spawn egg | `twilightforest:boar_spawn_egg`, colours 0x83653b / 0xffefca |

AI goals (priority, goal, parameters):

| Prio | Goal |
|---|---|
| 0 | FloatGoal |
| 1 | PanicGoal speed 1.25 |
| 3 | BreedGoal 1.0 |
| 4 | TemptGoal 1.2, `minecraft:carrot_on_a_stick`, canScare false |
| 4 | TemptGoal 1.2, `#twilightforest:boar_tempt_items`, canScare false |
| 5 | FollowParentGoal 1.1 |
| 6 | WaterAvoidingRandomStrollGoal 1.0 |
| 7 | LookAtPlayerGoal Player 6.0 |
| 8 | RandomLookAroundGoal |

Sounds: ambient `entity.twilightforest.boar.ambient` (subtitle "Boar oinks"), hurt `entity.twilightforest.boar.hurt`
("Boar hurts"), death `entity.twilightforest.boar.death` ("Boar dies"), step `entity.twilightforest.boar.step`
(generic footsteps) at volume 0.15, pitch 1.0. TF maps them to `entity.pig.*`.

Loot `twilightforest:entities/boar`: 1 pool, 1 roll: `minecraft:porkchop` count uniform 1..3; furnace_smelt when the
boar is on fire or the direct attacker's main hand has an enchantment in `#minecraft:smelts_loot`; looting +0..1 per
level; with chance 0.002 **and** killed by a player, `set_name` item_name to the translation
`item.twilightforest.boarkchop` (non-italic) ("Boarkchop" easter egg).

Spawn: placement ON_GROUND, `Animal::checkAnimalSpawnRules`. Biome table (see section 5).

### 2.2 `twilightforest:bighorn_sheep` (Bighorn)

Source: `entity/passive/Bighorn.java`, `client/model/entity/BighornModel.java`, `client/renderer/entity/BighornRenderer.java`.

| Field | Value |
|---|---|
| Base | vanilla `Sheep` (keeps every sheep mechanic: EatBlockGoal grass eating and wool regrowth, shearing, dyeing, colour mixing of lambs) |
| Category / size | `CREATURE`, 0.9 x 1.3 |
| Attributes | `Sheep.createAttributes()`: max_health 8.0, movement_speed 0.23 |
| Goals | vanilla Sheep goals: 0 Float; 1 Panic 1.25; 2 Breed 1.0; 3 Tempt 1.1 `#minecraft:sheep_food` (wheat); 4 FollowParent 1.1; 5 EatBlock; 6 WaterAvoidingRandomStroll 1.0; 7 LookAtPlayer 6.0; 8 RandomLookAround |
| Spawn colour | `finalizeSpawn`: 50 % `BROWN`, else `DyeColor.byId(nextInt(16))` (so brown overall 17/32) |
| Breeding | offspring is `twilightforest:bighorn_sheep` with `getOffspringColor(this, other)` (vanilla dye mixing); breeding with a non-Bighorn logs an error and returns no child |
| Walk target | 10.0 on `minecraft:grass_block` or `minecraft:podzol` below, else vanilla light-based cost |
| Shearing | vanilla: shears (or dispenser) drop 1..3 wool of the fleece colour, sound `entity.sheep.shear`, sets Sheared; regrows on eating grass |
| Render | `SheepRenderer` with texture `bighorn.png` (body) + vanilla `SheepFurLayer` (fur tinted by colour; jeb_ rainbow), shadow 0.7 |
| Spawn egg | `twilightforest:bighorn_sheep_spawn_egg`, 0xdbceaf / 0xd7c771 |

Loot (`getDefaultLootTable`): when sheared, the type's default `twilightforest:entities/bighorn_sheep` (1 pool:
`loot_table` reference to `minecraft:entities/sheep`, i.e. mutton). When not sheared, by colour (each = 1 pool with 1
wool of that colour + 1 pool referencing `minecraft:entities/sheep`):
`twilightforest:entities/bighorn_sheep/white` (default branch), `twilightforest:entities/bighorn_sheep/orange`,
`twilightforest:entities/bighorn_sheep/magenta`, `twilightforest:entities/bighorn_sheep/light_blue`,
`twilightforest:entities/bighorn_sheep/yellow`, `twilightforest:entities/bighorn_sheep/lime`,
`twilightforest:entities/bighorn_sheep/pink`, `twilightforest:entities/bighorn_sheep/gray`,
`twilightforest:entities/bighorn_sheep/light_gray`, `twilightforest:entities/bighorn_sheep/cyan`,
`twilightforest:entities/bighorn_sheep/purple`, `twilightforest:entities/bighorn_sheep/blue`,
`twilightforest:entities/bighorn_sheep/brown`, `twilightforest:entities/bighorn_sheep/green`,
`twilightforest:entities/bighorn_sheep/red`, `twilightforest:entities/bighorn_sheep/black`.

Sounds: ambient `entity.twilightforest.bighorn_sheep.ambient` ("Bighorn Sheep bleats"), hurt
`entity.twilightforest.bighorn_sheep.hurt`, death `entity.twilightforest.bighorn_sheep.death`, step
`entity.twilightforest.bighorn_sheep.step` at volume 0.15 pitch 1.0 (TF maps to `entity.sheep.*`). Shearing and
eating use the vanilla sheep sounds.

Prey: vanilla wolves get `NonTameRandomTargetGoal(Bighorn)` at target priority 7 (`events/MiscEvents.java#addPrey`).

Bedrock: a custom entity cannot extend the vanilla sheep runtime; reproduce with `minecraft:color` (fleece colour,
0..15 = Java DyeColor ids), `minecraft:is_sheared`, a `minecraft:interact` for shears (drops via loot/`spawn_items`
and damages shears 1), dye use (`minecraft:dyeable` or an interaction handler through
`gf::interactions::onInteract`), the eat-grass behaviour (`minecraft:behavior.eat_block`, grass to dirt, regrow) and
lamb colour mixing in the breed handler. The fur layer is a second render pass tinted by the colour (client mod) or
a colour-indexed texture set (vanilla clients).

### 2.3 `twilightforest:deer` (Deer)

Source: `entity/passive/Deer.java`, `client/model/entity/DeerModel.java`, renderer `TFGenericMobRenderer` texture `wilddeer.png`, shadow 0.7.

| Field | Value |
|---|---|
| Base | `Animal`; category `CREATURE`, 0.7 x 1.8 |
| Attributes | max_health 10.0, movement_speed 0.2 |
| Food | `#twilightforest:deer_tempt_items` (`#c:crops/wheat`, `minecraft:apple`, `twilightforest:shika_senbei`) |
| Spawn egg | `twilightforest:deer_spawn_egg`, 0x7b4d2e / 0x4b241d |

AI goals: 0 Float; 1 Panic 2.0; 2 Breed 1.0; 3 Tempt 1.25 deer_tempt_items (canScare false); 4 FollowParent 1.25;
4 AvoidEntity Player, distance 16.0, walk 1.5, sprint 1.8; 5 WaterAvoidingRandomStroll 1.0; 6 LookAtPlayer 6.0;
7 RandomLookAround.

Interaction: after vanilla `mobInteract` returns PASS, holding `twilightforest:shika_senbei` while the deer is below
max health consumes one cracker and returns SUCCESS. Eating shika senbei (also as breeding food) heals 4.0 (server)
and plays `entity.twilightforest.deer.eat` ("Deer eats") at volume 1, pitch 1 + (r1 - r2) x 0.2.

Sounds: ambient `entity.twilightforest.deer.ambient` ("Deer moos"), hurt `entity.twilightforest.deer.hurt`, death
`entity.twilightforest.deer.death`; **no step sound** (`playStepSound` is empty).

Model: antlers (`right_antler`, `left_antler` children of `head`) hidden for babies.

Loot `twilightforest:entities/deer`: pool 1 `minecraft:leather` 0..2 + looting 0..1; pool 2
`twilightforest:raw_venison` 1..3, furnace_smelt (on fire or `#minecraft:smelts_loot` weapon), looting 0..1.

### 2.4 `twilightforest:penguin` (Penguin)

Source: `entity/passive/Penguin.java`, `entity/passive/Bird.java`, `client/model/entity/PenguinModel.java`, renderer `BirdRenderer` texture `penguin.png`, shadow 0.375.

| Field | Value |
|---|---|
| Base | `Bird` (walking bird; `Bird.aiStep` flap + slow fall) |
| Category / size | `CREATURE`, 0.5 x 0.9 |
| Attributes | max_health 10.0, movement_speed 0.2 |
| Food / breeding | `#twilightforest:penguin_tempt_items` (`#minecraft:fishes`); offspring `twilightforest:penguin` |
| Spawn | placement `NO_RESTRICTIONS`, predicate: block below in `#twilightforest:penguins_spawnable_on` (= `#minecraft:ice`); `checkSpawnRules` always true |
| Spawn egg | `twilightforest:penguin_spawn_egg`, 0x12151b / 0xf9edd2 |

AI goals: 0 Float; 1 Panic 1.75; 2 Breed 1.0; 3 Tempt 0.75 penguin_tempt_items; 4 FollowParent 1.15;
5 RandomStroll 1.0 (not water avoiding); 6 LookAtPlayer Player 6.0; 7 LookAtPlayer Penguin 5.0 probability 0.02;
8 RandomLookAround.

Bird physics (`Bird.aiStep`, every tick): `flapIntensity += (onGround ? -1 : 4) x 0.3`, clamped 0..1; if airborne and
`flapSpeed < 1` set 1; `flapSpeed *= 0.9`; airborne and falling: `motion.y *= 0.6`; `flapLength += flapSpeed x 2`.
`isSteppingCarefully` false. Render bob = `(sin(flapLength) + 1) x flapIntensity`; the model sets
`rightArm.zRot = bob`, `leftArm.zRot = -bob`; legs swing `cos(limbSwing) x 0.7 x amount`.

Sounds: ambient `entity.twilightforest.penguin.ambient` ("Penguin honks"), hurt
`entity.twilightforest.penguin.hurt`, death `entity.twilightforest.penguin.death`.

Loot `twilightforest:entities/penguin`: `minecraft:feather` 0..2, looting +0..1.

### 2.5 Flying birds: shared `FlyingBird` logic

Source: `entity/passive/FlyingBird.java` (used by tiny bird and raven; GloamingForest implements it once in U16).

- Synced byte `DATA_BIRDFLAGS`, bit 0 = landed. Constructed landed, age 0; `isBaby()` always false; cannot breed
  (`Bird.isFood` false, `getBreedOffspring` null).
- Goals: 0 Float; 0 Panic 1.5; 3 Tempt 1.0 with `getTemptItems()`, **canScare true**; 5 WaterAvoidingRandomStroll 1.0;
  6 LookAtPlayer 6.0; 7 RandomLookAround.
- Walk target value: 200 on `#minecraft:leaves`, 15 on `#minecraft:logs`, 9 on `#minecraft:dirt`, else light - 0.5.
- `tick`: while flying, `motion.y *= 0.6`.
- `customServerAiStep` landed: flight time = 0; take off when `isSpooked()`, in water, fluid below, or
  (1/200 per tick and the block below is not landable); take-off plays `entity.twilightforest.tiny_bird.takeoff`
  ("Bird takes off", vanilla `entity.bat.takeoff`) at volume 0.05 and the voice pitch unless silent.
- Flying (vanilla Bat copy, edits noted in TF): flight time + 1; drop the target if it is not air or at/below min
  build height; in water or above fluid: flight time = 0 and `motion.y = 0.1`; pick a new target when none, 1/30, or
  within 2 blocks: `x + rand(7) - rand(7)`, `y + rand(6) - (flightTime < 100 ? 2 : 4)`, `z + rand(7) - rand(7)`;
  steer `motion += (signum(d) x (0.5, 0.7, 0.5) - motion) x 0.1`; yaw toward motion; `zza = 0.5`; land with chance
  1/10 when the block below is landable (`!air && (leaves || top face sturdy)`), zeroing vertical motion.
- Not pushable, pushes nothing, ignores block triggers (pressure plates, tripwire), cannot ride.

Bedrock: implement with a custom move controller registered through `gf::entity::setMoveControl` (F8) and the
landed flag as an actor property synced to clients (animation controller switches landed/flying poses).

### 2.6 `twilightforest:tiny_bird` (TinyBird)

Source: `entity/passive/TinyBird.java`, `entity/passive/TinyBirdVariant.java`, `init/custom/TinyBirdVariants.java`,
`client/model/entity/TinyBirdModel.java`, `client/renderer/entity/TinyBirdRenderer.java` (+ `BirdRenderer`).

| Field | Value |
|---|---|
| Category / size | `CREATURE`, 0.3 x 0.3 |
| Attributes | max_health 4.0, movement_speed 0.2, step_height 1.0 |
| Extra goals | 4 AvoidEntity Cat 8.0 (1.0 / 1.25); 4 AvoidEntity Ocelot 8.0 (1.0 / 1.25) |
| Tempt items | `#twilightforest:tiny_bird_tempt_items` (`#c:seeds`) |
| Spooked | last hurt by a mob, or the nearest non-creative, non-spectator player within 4.0 blocks is holding a tempt item |
| Spawn egg | `twilightforest:tiny_bird_spawn_egg`, 0x33aadd / 0x1188ee |

Variant: synced entity data of type `twilightforest:tiny_bird_variant` (data serializer), registry
`tiny_bird_variant` (datapack registry under `data/twilightforest/twilight/tiny_bird_variant/`), entries
`tiny_bird_variant/twilightforest:blue` (texture `tinybirdblue.png`), `tiny_bird_variant/twilightforest:brown`
(`tinybirdbrown.png`), `tiny_bird_variant/twilightforest:gold` (`tinybirdgold.png`),
`tiny_bird_variant/twilightforest:red` (`tinybirdred.png`); none has a `biomes` filter. Default (synced data
default and fallback) is red. `finalizeSpawn` picks uniformly among variants whose biome set is empty or contains
the spawn biome. Saved as string `variant` (`twilightforest:<name>`); unknown ids keep the current value.

Sounds: ambient 1/20 `entity.twilightforest.tiny_bird.song` ("Tiny Bird sings") else
`entity.twilightforest.tiny_bird.chirp` ("Tiny Bird chirps"); hurt and death
`entity.twilightforest.tiny_bird.hurt` ("Tiny Bird squeaks in pain"); take-off as in 2.5.

Model: wings `rightWing.zRot = bob`, `leftWing.zRot = -bob` (BirdRenderer bob); feet swing
`cos(limbSwing x 0.6662) x 1.4 x amount`; foot y 23.0 landed, 22.5 flying. Shadow 0.3.

Loot `twilightforest:entities/tiny_bird`: `minecraft:feather` 0..2, looting +0..1.

Prey (`events/MiscEvents.java#addPrey`): cats get `NonTameRandomTargetGoal(TinyBird)` at target prio 1, ocelots
`NearestAttackableTargetGoal(TinyBird, mustSee)` at 1.

Bedrock: the variant is an integer actor property / `minecraft:variant` with the fixed mapping blue 0, brown 1,
gold 2, red 3 (registry order of TF's bootstrap); keep the string in saved data so renaming is safe.

### 2.7 `twilightforest:raven` (Raven)

Source: `entity/passive/Raven.java`, `client/model/entity/RavenModel.java`, renderer `BirdRenderer` texture `raven.png`, shadow 0.3.

| Field | Value |
|---|---|
| Base | `FlyingBird` (section 2.5) |
| Category / size | `CREATURE`, 0.3 x 0.5 |
| Attributes | max_health 10.0, movement_speed 0.2, step_height 1.0 |
| Tempt items | `#twilightforest:raven_tempt_items` (`#c:seeds`) |
| Spooked | only when it has a last-hurt-by mob |
| Spawn egg | `twilightforest:raven_spawn_egg`, 0x000011 / 0x222233 |

Sounds: ambient `entity.twilightforest.raven.caw` ("Raven caws"); hurt and death
`entity.twilightforest.raven.squawk` ("Raven squawks in pain").

Model: `head.zRot = -0.2617994` when netHeadYaw > 5 else 0; wings as tiny bird; leg y 21.0 landed, 20.0 flying.

Loot `twilightforest:entities/raven`: `twilightforest:raven_feather` 0..2, looting +0..1.

### 2.8 `twilightforest:squirrel` (Squirrel)

Source: `entity/passive/Squirrel.java`, `client/model/entity/SquirrelModel.java`, renderer `TFGenericMobRenderer` texture `squirrel2.png`, shadow 0.3.

| Field | Value |
|---|---|
| Base | `Animal`, cannot breed (`isFood` false, no offspring) |
| Category / size | `CREATURE`, 0.3 x 0.5 |
| Attributes | max_health 6.0, movement_speed 0.3, step_height 1.0 |
| Spawn egg | `twilightforest:squirrel_spawn_egg`, 0x904f12 / 0xeeeeee |

AI goals: 0 Float; 1 Panic 1.38; 2 Tempt 1.0 `#twilightforest:squirrel_tempt_items` (`#c:seeds`), canScare true;
3 AvoidEntity Player 2.0 (0.8 / 1.4); 3 AvoidEntity Wolf 8.0 (0.8 / 1.4); 3 AvoidEntity Cat 8.0; 3 AvoidEntity Ocelot
8.0; 3 AvoidEntity Fox 8.0 (all 0.8 / 1.4); 5 WaterAvoidingRandomStroll 1.0; 6 WaterAvoidingRandomStroll 1.25;
7 LookAtPlayer 6.0; 8 RandomLookAround.

Walk target: 12 on leaves, 15 on logs, 10 on dirt, else light - 0.5.

Sounds: ambient `entity.twilightforest.squirrel.ambient` ("Squirrel chitters"), hurt
`entity.twilightforest.squirrel.hurt`, death `entity.twilightforest.squirrel.death`.

Model tail: when limbSwingAmount > 0.2, `wiggle = min(amount, 0.6)`, `tail.xRot = 0.2 + (cos(age x 0.6662) - PI/3) x
wiggle`, `tailPart1.xRot = cos(age x 0.7774) x 1.2 x wiggle`, `tailPart2.xRot = cos(age x 0.8886 + PI/2) x 1.4 x
wiggle`; idle: `tail.xRot = 0.2 + cos(age x 0.3335) x 0.15`, `tailPart1.xRot = 0.1 + cos(age x 0.4445) x 0.2`,
`tailPart2.xRot = 0.1 + cos(age x 0.5555) x 0.25` (TF uses 2.5 instead of 0.2 only with the Jappa pack, not ported).

Loot `twilightforest:entities/squirrel`: empty (no pools).

Prey: cats (target prio 1, non-tame random), ocelots (1), foxes (6, NearestAttackableTarget), wolves (7, non-tame
random) hunt squirrels.

### 2.9 `twilightforest:dwarf_rabbit` (DwarfRabbit)

Source: `entity/passive/DwarfRabbit.java`, `entity/passive/DwarfRabbitVariant.java`,
`init/custom/DwarfRabbitVariants.java`, `client/model/entity/BunnyModel.java`, `client/renderer/entity/BunnyRenderer.java` (shadow 0.3).

| Field | Value |
|---|---|
| Base | `Animal`; category `CREATURE`, 0.4 x 0.4 |
| Attributes | max_health 3.0, movement_speed 0.3, step_height 1.0 |
| Food | `#twilightforest:dwarf_rabbit_tempt_items` (`#c:crops/carrot`, `minecraft:golden_carrot`, `minecraft:dandelion`) |
| Despawn | `removeWhenFarAway` false (never despawns) |
| Spawn egg | `twilightforest:dwarf_rabbit_spawn_egg`, 0xfefeee / 0xccaa99 |

AI goals: 0 Float; 1 Panic 2.0; 2 Breed 0.8; 2 Tempt 1.0 (canScare false); 3 AvoidEntity Player 2.0 (0.8 / 1.33);
4 AvoidEntity Ocelot / Cat / Wolf / Fox 8.0 (0.8 / 1.1); 5 WaterAvoidingRandomStroll 0.8; 6 WaterAvoidingRandomStroll
1.0; 7 LookAtPlayer 6.0; 8 RandomLookAround.

Walk target: -1 on leaves or logs (avoid), 10 on `#minecraft:dirt`, else max local raw brightness - 0.5.

Variant: data serializer `twilightforest:dwarf_rabbit_variant`, registry `dwarf_rabbit_variant`, entries
`dwarf_rabbit_variant/twilightforest:brown` (`bunnybrown.png`, default and fallback),
`dwarf_rabbit_variant/twilightforest:dutch` (`bunnydutch.png`), `dwarf_rabbit_variant/twilightforest:white`
(`bunnywhite.png`); no biome filters. Spawn: uniform among biome-valid variants. Breeding: the baby gets a random
common variant (no biome filter) with chance 1/20, else 50/50 one parent's variant. Saved as string `variant`.
Bedrock mapping: brown 0, dutch 1, white 2.

Sounds: ambient `entity.twilightforest.dwarf_rabbit.ambient` ("Dwarf Rabbit squeaks"), hurt
`entity.twilightforest.dwarf_rabbit.hurt`, death `entity.twilightforest.dwarf_rabbit.death` (TF maps to
`entity.rabbit.*`).

Loot `twilightforest:entities/dwarf_rabbit`: 1 pool referencing `minecraft:entities/rabbit` (rabbit hide, raw rabbit,
rabbit foot).

Prey: cats (1), ocelots (1), foxes (6), wolves (7).

### 2.10 `twilightforest:kobold` (Kobold)

Source: `entity/monster/Kobold.java`, `entity/ai/goal/PanicOnFlockDeathGoal.java`,
`entity/ai/goal/FlockToSameKindGoal.java`, `client/model/entity/KoboldModel.java`, renderer `TFBipedRenderer`
texture `kobold.png`, shadow 0.4.

| Field | Value |
|---|---|
| Base | `Monster`; category `MONSTER`, 0.8 x 1.1 |
| Attributes | max_health 13.0, movement_speed 0.28, attack_damage 4.0 |
| Loot pickup | `setCanPickUpLoot(true)`; only main hand, only `#twilightforest:kobold_pacification_breads` (`minecraft:bread`), only when the hand is empty and not panicked |
| Spawn cluster | `getMaxSpawnClusterSize` 1 |
| Synced data | `PANICKED` boolean |
| Saved data | `EatingTimeLeft` (int), `TimeSinceBreadLastEaten` (int) |
| Spawn egg | `twilightforest:kobold_spawn_egg`, 0x372096 / 0x895d1b |

AI goals: 0 Float; 1 PanicOnFlockDeath 2.0; 2 SeekBread; 2 RunAwayWhileHoldingBread; 3 LeapAtTarget 0.3;
4 MeleeAttack 1.0 (no long memory); 5 FlockToSameKind 1.0; 6 WaterAvoidingRandomStroll 1.0; 7 LookAtPlayer 8.0;
7 RandomLookAround. Targets: 1 HurtByTarget; 2 KoboldAttackPlayerTarget (NearestAttackableTarget Player, mustSee,
disabled while holding bread).

- **PanicOnFlockDeath** (speed 2.0): usable when its flee timer > 0 or any same-class mob within the box inflated
  (4, 2, 4) has `deathTime > 0`; target `DefaultRandomPos.getPos(mob, 5, 4)`; start: timer 40, move, set PANICKED;
  continue while timer > 0 and navigating; tick: timer - 1; stop: timer - 20, clear PANICKED.
- **FlockToSameKind** (speed 1.0): not while holding bread; 1/40 per tick; same-class mobs within (16, 4, 16); fails
  when more than 5; target = their average position; only when distance^2 >= 25; continue while 25 <= d^2 <= 256;
  re-path every 10 ticks.
- **SeekBread** (flag MOVE): not when using an item or panicked; 1/10; bread item entities within 8 blocks
  (inflate 8,8,8) and empty main hand; move to the first at speed 1.2 and look at it.
- **RunAwayWhileHoldingBread**: AvoidEntity Player 8.0, walk 1.5, sprint 1.5, only while holding bread.
- **Pick-up**: a stack larger than 1 drops `count - 1` as a new item entity; holds 1; main-hand drop chance 2.0
  (always drops, undamaged); equip game event; `lastEatenBreadTicks = 1`; `eatingTime = difficultyTime +
  nextInt(600)` with difficultyTime EASY 400, NORMAL 200, HARD 100 (default 200); clears its target.
- **Munching** (server, alive, holding bread): `lastEatenBreadTicks + 1`; `eatingTime - 1` while > 0; if the item is
  food and the kobold is not panicked: when `eatingTime <= 0` it finishes the item (bread consumed, hand gets the
  remainder); when `lastEatenBreadTicks > 60` and `nextFloat() < 0.1`: plays `entity.twilightforest.kobold.munch`
  ("Kobold munches") at volume 0.75 pitch 0.9, game event EAT, entity event 45 (8 item-crack particles of the held
  item from the mouth, sent to trackers), resets the counter.
- **Panic particles** (client): while PANICKED, 2 `minecraft:splash` per tick at eye height (sweat drops).

Sounds: ambient `entity.twilightforest.kobold.ambient` ("Kobold grumbles"), hurt `entity.twilightforest.kobold.hurt`,
death `entity.twilightforest.kobold.death`.

Model: arms held forward (`xRot = -0.15 PI`) and sway with age; jaw `xRot` 1.44 while moving upward (jumping), else
0.20944; legs swing `cos(limbSwing x 0.6662) x 1.4 x amount`.

Loot `twilightforest:entities/kobold`: pool 1 `minecraft:wheat` 0..2 + looting 0..1; pool 2 (killed by player)
`minecraft:gold_nugget` uniform -1..1 (so 0 or 1) + looting 0..1.

Hint mob: every progression-locked landmark's `hint_creature.hint_mob` is `twilightforest:kobold` (F6
`gf::landmark::trySpawnHint`): F6 creates the entity by string, places it at player pos + (rand16 - rand16,
rand4 - rand4, rand16 - rand16) when the box is clear and it can see the player, puts the structure's hint book
(`minecraft:written_book`) in its main hand with drop chance 2.0 (guaranteed), 20 tries, at most once per 1200 game
ticks per structure. U16 only has to make sure a kobold holding a non-bread item still attacks and drops it.

Spawn: placement ON_GROUND, `Monster::checkMonsterSpawnRules`.

### 2.11 `twilightforest:mosquito_swarm` (MosquitoSwarm)

Source: `entity/monster/MosquitoSwarm.java`, `client/model/entity/MosquitoSwarmModel.java`,
`client/renderer/entity/MosquitoSwarmRenderer.java` (texture `mosquitoswarm.png`, shadow 0.0, death flip 0 degrees).

| Field | Value |
|---|---|
| Base | `Monster`; category `MONSTER`, 0.7 x 1.9 |
| Attributes | max_health 12.0, movement_speed 0.23, attack_damage 3.0, step_height 2.1 |
| Spawn cluster | 1 |
| Spawn egg | `twilightforest:mosquito_swarm_spawn_egg`, 0x080904 / 0x2d2f21 |

AI goals: 0 Float; 3 MeleeAttack 1.0; 6 WaterAvoidingRandomStroll 1.0. Targets: 1 HurtByTarget; 2
NearestAttackableTarget Player (mustSee).

On a successful melee hit against a living entity: Hunger amplifier 0 for EASY 7 s, HARD 30 s, otherwise 15 s (x 20
ticks). No hurt or death sound (silent), ambient `entity.twilightforest.mosquito.ambient` ("Mosquitoes buzz"). Never
shows the fire overlay (`displayFireAnimation` false); cannot ride.

Model: no bone uses limb swing; `prepareMobModel` spins a core and six groups by `t = tickCount + partial`: core
(yRot t/5, xRot sin(t/5)/4, zRot cos(t/5)/4); group1 (y t/2, x sin(t/6)/2, z cos(t/5)/4); group2 (y sin(t/2)/3,
x t/5, z cos(t/5)/4); group3 (y sin(t/7)/3, x cos(t/4)/2, z t/5); group4 (x t/2, z sin(t/6)/2, y sin(t/5)/4); group5
(z t/2, y cos(t/5)/4, x cos(t/5)/4); group6 (z cos(t/7)/3, x cos(t/4)/2, y t/5). Use a looping Molang animation on
`query.life_time`.

Loot `twilightforest:entities/mosquito_swarm`: empty (no pools).

Spawn: placement ON_GROUND, `Monster::checkMonsterSpawnRules`.

## 3. Items

| Id | Behaviour |
|---|---|
| `twilightforest:raven_feather` | Plain item (`new Item(new Item.Properties())`, stack 64, common). Member of `#c:feathers` (F1 imported tag). Dropped by ravens; used in recipes owned by other units (arrows, etc.). |
| spawn eggs | `twilightforest:boar_spawn_egg`, `twilightforest:bighorn_sheep_spawn_egg`, `twilightforest:deer_spawn_egg`, `twilightforest:penguin_spawn_egg`, `twilightforest:tiny_bird_spawn_egg`, `twilightforest:squirrel_spawn_egg`, `twilightforest:dwarf_rabbit_spawn_egg`, `twilightforest:raven_spawn_egg`, `twilightforest:kobold_spawn_egg`, `twilightforest:mosquito_swarm_spawn_egg`: `DeferredSpawnEggItem` with the colours above (Bedrock: `minecraft:spawn_egg` with base/overlay colours). Using one on a spawner (vanilla behaviour) sets the spawner type. |

## 4. Data

- **Parrot imitations** `neoforge:parrot_imitations` (data map on entity types, U16 owns it, R7): a vanilla parrot
  near a mapped TF mob imitates it with the mapped sound. Map:

  | Entity | Imitation sound |
  |---|---|
  | `twilightforest:alpha_yeti`, `twilightforest:yeti` | `entity.twilightforest.parrot.imitate.alpha_yeti` ("Parrot coldly growls"; plays `alpha_yeti.growl`) |
  | `twilightforest:blockchain_goblin`, `twilightforest:redcap`, `twilightforest:redcap_sapper` | `entity.twilightforest.parrot.imitate.redcap` ("Parrot chuckles"; `redcap.ambient`) |
  | `twilightforest:carminite_golem` | `entity.twilightforest.parrot.imitate.carminite_golem` ("Parrot thunks"; `carminite_golem.hurt`) |
  | `twilightforest:death_tome` | `entity.twilightforest.parrot.imitate.death_tome` ("Parrot makes book noises"; `death_tome.ambient`) |
  | `twilightforest:hostile_wolf`, `twilightforest:mist_wolf`, `twilightforest:winter_wolf` | `entity.twilightforest.parrot.imitate.hostile_wolf` ("Parrot growls"; `hostile_wolf.ambient`) |
  | `twilightforest:hydra` | `entity.twilightforest.parrot.imitate.hydra` ("Parrot roars"; `hydra.growl`) |
  | `twilightforest:snow_guardian`, `twilightforest:snow_queen`, `twilightforest:stable_ice_core` | `entity.twilightforest.parrot.imitate.ice_core` ("Parrot crackles"; `ice.ambient`) |
  | `twilightforest:kobold` | `entity.twilightforest.parrot.imitate.kobold` ("Parrot grumbles"; `kobold.ambient`) |
  | `twilightforest:minoshroom`, `twilightforest:minotaur` | `entity.twilightforest.parrot.imitate.minotaur` ("Parrot moos angrily"; `minotaur.ambient`) |
  | `twilightforest:mosquito_swarm` | `entity.twilightforest.parrot.imitate.mosquito` ("Parrot buzzes"; `mosquito.ambient`) |
  | `twilightforest:naga` | `entity.twilightforest.parrot.imitate.naga` ("Parrot rattles"; `naga.hiss`) |
  | `twilightforest:knight_phantom`, `twilightforest:wraith` | `entity.twilightforest.parrot.imitate.wraith` ("Parrot gasps"; `wraith.ambient`) |
  | `twilightforest:carminite_broodling`, `twilightforest:fire_beetle`, `twilightforest:hedge_spider`, `twilightforest:helmet_crab`, `twilightforest:king_spider`, `twilightforest:pinch_beetle`, `twilightforest:swarm_spider` | `minecraft:entity.parrot.imitate.spider` |
  | `twilightforest:carminite_ghastguard`, `twilightforest:carminite_ghastling`, `twilightforest:ur_ghast` | `minecraft:entity.parrot.imitate.ghast` |
  | `twilightforest:lich` | `minecraft:entity.parrot.imitate.blaze` |
  | `twilightforest:lich_minion` | `minecraft:entity.parrot.imitate.zombie` |
  | `twilightforest:maze_slime`, `twilightforest:slime_beetle` | `minecraft:entity.parrot.imitate.slime` |
  | `twilightforest:skeleton_druid` | `minecraft:entity.parrot.imitate.skeleton` |
  | `twilightforest:towerwood_borer` | `minecraft:entity.parrot.imitate.silverfish` |

  Every `entity.twilightforest.parrot.imitate.*` event is in TF's `sounds.json` an event reference to the imitated
  mob's sound at **pitch 1.8, volume 0.6**. Bedrock: vanilla parrots imitate only vanilla mobs; reproduce Java's
  `Parrot.imitateNearbyMobs` / random-ambient imitation selection (mobs within 20 blocks, played at volume 0.7 with the
  parrot pitch) in a server tick over loaded parrots, reading this map; the entity ids are strings (R2).
- **Variant registries** `tiny_bird_variant` and `dwarf_rabbit_variant` (TF datapack registries, `TFRegistries.java`
  keys `twilightforest:tiny_bird_variant` / `twilightforest:dwarf_rabbit_variant`): codec `{texture: ResourceLocation,
  biomes?: HolderSet<Biome>}`. Load them as U16 data (`assets/data/<kind>/fauna.json`) so datapack-style additions
  keep working; the biome filter must be honoured even though TF's entries have none.
- **Data serializers** `twilightforest:tiny_bird_variant` and `twilightforest:dwarf_rabbit_variant`
  (`init/TFDataSerializers.java`) only sync the variant holder; on Bedrock this is the variant actor property.
- **JEED compat recipe** `twilightforest:jeed/hunger` (`main/resources/data/twilightforest/recipe/jeed/hunger.json`):
  `jeed:effect_provider` listing `twilightforest:mosquito_swarm_spawn_egg` as a provider of `minecraft:hunger`, loaded
  only when the JEED mod is present. JEED does not exist on Bedrock: **not ported** (record in not-ported.md); U16
  owns the id only.

## 5. Spawning (biome tables)

Natural spawns come from the biome JSONs (`data/twilightforest/worldgen/biome/*.json`, owned by U1, which registers
them through `gf::spawn::setBiomeSpawns`); U16 provides the placement rules (`gf::spawn::registerPlacement`).

| Biomes | Entries (category: entity weight min-max) |
|---|---|
| forest, dense_forest, firefly_forest, clearing, oak_savannah, enchanted_forest, mushroom_forest, dense_mushroom_forest, highlands, lake, stream | creature: bighorn_sheep 12 4-4, boar 10 4-4, minecraft:chicken 10 4-4, deer 15 4-5, minecraft:wolf 5 4-4, tiny_bird 15 4-8, squirrel 10 2-4, dwarf_rabbit 10 4-5, raven 10 1-2 |
| final_plateau | creature: raven 10 4-4 |
| glacier | creature: penguin 10 2-4 |
| dark_forest | monster: kobold 10 1-3 (spawn cost charge 0.7, energy budget 0.15) among vanilla and U17 mobs |
| underground, highlands_underground | monster: kobold 10 1-3 (charge 0.2, budget 0.15) |
| swamp | monster: mosquito_swarm 10 1-1 (charge 0.7, budget 0.15) |

Structure controlled spawns that include kobolds (knight stronghold weight 10, 2-4, and others) belong to the
structure owners and reference `twilightforest:kobold` by string.

| Entity | Placement | Predicate |
|---|---|---|
| boar, bighorn_sheep, deer, tiny_bird, squirrel, dwarf_rabbit, raven | ON_GROUND | `Animal::checkAnimalSpawnRules` |
| penguin | NO_RESTRICTIONS | block below in `#twilightforest:penguins_spawnable_on` |
| kobold, mosquito_swarm | ON_GROUND | `Monster::checkMonsterSpawnRules` |

## 6. Sound events (all owned by U16)

| Event | Subtitle (en_us) | Played by |
|---|---|---|
| `entity.twilightforest.bighorn_sheep.ambient` | Bighorn Sheep bleats | bighorn ambient |
| `entity.twilightforest.bighorn_sheep.hurt` | Bighorn Sheep hurts | bighorn hurt |
| `entity.twilightforest.bighorn_sheep.death` | Bighorn Sheep dies | bighorn death |
| `entity.twilightforest.bighorn_sheep.step` | (footsteps) | bighorn step, vol 0.15 |
| `entity.twilightforest.boar.ambient` | Boar oinks | boar ambient |
| `entity.twilightforest.boar.hurt` | Boar hurts | boar hurt |
| `entity.twilightforest.boar.death` | Boar dies | boar death |
| `entity.twilightforest.boar.step` | (footsteps) | boar step, vol 0.15 |
| `entity.twilightforest.deer.ambient` | Deer moos | deer ambient (3 variants) |
| `entity.twilightforest.deer.hurt` | Deer hurts | deer hurt (2 variants) |
| `entity.twilightforest.deer.death` | Deer dies | deer death |
| `entity.twilightforest.deer.eat` | Deer eats | shika senbei eaten (horse eat 1-5 in TF) |
| `entity.twilightforest.dwarf_rabbit.ambient` | Dwarf Rabbit squeaks | rabbit ambient |
| `entity.twilightforest.dwarf_rabbit.hurt` | Dwarf Rabbit hurts | rabbit hurt |
| `entity.twilightforest.dwarf_rabbit.death` | Dwarf Rabbit dies | rabbit death |
| `entity.twilightforest.kobold.ambient` | Kobold grumbles | kobold ambient (6 variants) |
| `entity.twilightforest.kobold.hurt` | Kobold hurts | kobold hurt (3) |
| `entity.twilightforest.kobold.death` | Kobold dies | kobold death (3) |
| `entity.twilightforest.kobold.munch` | Kobold munches | bread munch, vol 0.75 pitch 0.9 |
| `entity.twilightforest.mosquito.ambient` | Mosquitoes buzz | mosquito swarm ambient |
| `entity.twilightforest.penguin.ambient` | Penguin honks | penguin ambient (7) |
| `entity.twilightforest.penguin.hurt` | Penguin hurts | penguin hurt |
| `entity.twilightforest.penguin.death` | Penguin dies | penguin death |
| `entity.twilightforest.raven.caw` | Raven caws | raven ambient (2) |
| `entity.twilightforest.raven.squawk` | Raven squawks in pain | raven hurt and death (2) |
| `entity.twilightforest.squirrel.ambient` | Squirrel chitters | squirrel ambient (5) |
| `entity.twilightforest.squirrel.hurt` | Squirrel hurts | squirrel hurt |
| `entity.twilightforest.squirrel.death` | Squirrel dies | squirrel death |
| `entity.twilightforest.tiny_bird.chirp` | Tiny Bird chirps | tiny bird ambient (19/20) |
| `entity.twilightforest.tiny_bird.song` | Tiny Bird sings | tiny bird ambient (1/20) |
| `entity.twilightforest.tiny_bird.hurt` | Tiny Bird squeaks in pain | tiny bird hurt and death |
| `entity.twilightforest.tiny_bird.takeoff` | Bird takes off | flying-bird take-off, vol 0.05 |
| `entity.twilightforest.parrot.imitate.alpha_yeti` | Parrot coldly growls | parrot imitation |
| `entity.twilightforest.parrot.imitate.carminite_golem` | Parrot thunks | parrot imitation |
| `entity.twilightforest.parrot.imitate.death_tome` | Parrot makes book noises | parrot imitation |
| `entity.twilightforest.parrot.imitate.hostile_wolf` | Parrot growls | parrot imitation |
| `entity.twilightforest.parrot.imitate.hydra` | Parrot roars | parrot imitation |
| `entity.twilightforest.parrot.imitate.ice_core` | Parrot crackles | parrot imitation |
| `entity.twilightforest.parrot.imitate.kobold` | Parrot grumbles | parrot imitation |
| `entity.twilightforest.parrot.imitate.minotaur` | Parrot moos angrily | parrot imitation |
| `entity.twilightforest.parrot.imitate.mosquito` | Parrot buzzes | parrot imitation |
| `entity.twilightforest.parrot.imitate.naga` | Parrot rattles | parrot imitation |
| `entity.twilightforest.parrot.imitate.redcap` | Parrot chuckles | parrot imitation |
| `entity.twilightforest.parrot.imitate.wraith` | Parrot gasps | parrot imitation |

The parrot events reuse other units' events (pitch 1.8, volume 0.6); in the synth recipe they are re-pitched copies of
those events, not new material.

## 7. Bedrock adaptation notes

- Custom entities: BP entity definitions per id (`assets/pack/bp/entities/fauna/*.json`) with health, movement,
  collision box, `minecraft:type_family` (`animal`, `monster`, `mob`), breedable/tameable-free components, and the
  goals above as either native Bedrock behaviours with matching parameters or `gf::entity::addGoal` translations
  (`flockToSameKind` and `panicOnFlockDeath` are F8 shared goals: pass the speeds 1.0 and 2.0).
- Prey goals on vanilla cats, ocelots, foxes and wolves (`addPrey`): Bedrock vanilla entities cannot be edited by a
  BP override safely for vanilla clients; add the target goals server side via `gf::entity::addTargetGoal` on those
  vanilla actors when they join (F8 contract), with the priorities above.
- Kobold bread pickup and munching use the item-in-hand equipment slot; vanilla clients see the bread in hand
  (`MobEquipmentPacket`). Entity event 45 particles become `gf::fx::particle` with item-break particles.
- Mosquito swarm silence: no hurt/death sound; Bedrock plays `mob.*` defaults only if the entity defines them, so
  define none.
- Bird bob and the landed flag must be available to the client animation (`query.property` or a synced property).

## 8. Acceptance (selftests `fauna.*`, dx 1600-1699)

From the unit record: spawn each mob (10 types) and check attributes against the F13 goldens; shear a bighorn and get
1..3 wool of its colour then the sheared loot table; dye a bighorn; tiny bird and dwarf rabbit variants persist across
save/reload (string `variant`); kobold picks up bread, stops targeting, munches and eats it; mosquito hit applies
Hunger with the difficulty duration; penguin spawn predicate on ice; boar/deer/penguin breeding produces the same type;
deer heals 4 from shika senbei. Client: one screenshot per mob animating (flying and landed bird poses, squirrel tail,
mosquito swirl, kobold jaw).
