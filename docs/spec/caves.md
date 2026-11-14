# caves (U27): Troll caves and yeti cave

Behaviour spec of unit **U27** (`caves`), written by F15 from the upstream 4.8 (`9e40a757`). Java paths are
relative to `src/main/java/gloamingforest/`; data paths to `src/generated/resources/`.

| Unit fact | Value |
|---|---|
| Selftests | `caves.*`, testPos dx `2700-2799` (R12) |
| World data | `gf:caves:*` (the yeti throw state is an entity attachment, see 5.4) |
| Net channels | `caves.*`: `caves.thrown` mirrors upstream's `UpdateThrownPacket` for companions (thrown flag, thrower id, cooldown) |
| Contracts | landmark, structure, features, boss, entity, projectile, client.render |
| Config | none |

Note on ownership: owners.tsv gives U27 the four cloud piece types (`GloamingClCa`, `GloamingCloud`, `GloamingClTr`, `GloamingTCloud`, which
live in upstream's `trollcave` package) while the `gloamingforest:giant_house` structure that uses the cloud castle is
U29's. U27 implements and registers these piece types (F7 `registerPieceType`); U29's structure creates its first
piece by the string id `gloamingforest:GloamingClCa` (R2).

**Tag namespaces.** Tags are written `#<ns>:<path>`. `#gloamingforest:` tags and the `#c:` / `#neoforge:` tags that
owners.tsv lists (`tag` rows) are upstream data (`data/<ns>/tags/**`) imported by F1; `#minecraft:` tags are vanilla Java
1.21.1 tags (upstream adds members to some). Bedrock has no Java tags: every tag here resolves through `gf::tags` (F9,
`src/feature/tags/Tags.h`), and units add members only through union fragments (R6). Upstream's damage-type and tool-tier
tags are in the `neoforge` namespace (`#neoforge:is_physical`, `#neoforge:is_magic`, `#neoforge:is_environment`,
`#neoforge:needs_wood_tool`), never `#c:`.

## 1. Structure `gloamingforest:troll_cave`

Sources: `world/components/structures/type/TrollCaveStructure.java`, `world/components/structures/trollcave/TrollCave{Main,Connect,Garden}Component.java`,
`TrollVaultComponent.java`.

| Field | Value |
|---|---|
| Structure type | `gloamingforest:troll_cave` (progression + controlled spawns + `speleothem_config`) |
| Structure set | `gloamingforest:troll_cave`, `gloamingforest:landmark_grid`, weight 1 |
| Biomes | `#gloamingforest:valid_troll_cave_biomes` (highlands) |
| Step | `underground_structures`, terrain adaptation `bury`, **not** centred in chunk |
| Progression lock | `gloamingforest:progress_merge` |
| Hint | kobold + book `gloamingforest.book.trollcave` ("Notes on the Highlands", 3 pages) |
| Decoration clearance | radius 4, surface true, underground true, vegetation true, adjust elevation false |
| Controlled spawns label 0 | `minecraft:creeper` 5 1-2, `minecraft:skeleton` 10 1-2, `gloamingforest:troll` 20 1-2, `minecraft:witch` 5 1-1 |
| Controlled spawns label 1 (clouds) | `gloamingforest:giant_miner` 10 1-1, `gloamingforest:armored_giant` 10 1-1 (U29's mobs by string) |
| Spawn overrides | all categories empty over the structure box |
| Map icon | `gloamingforest:troll_caves` |
| Speleothems | `gloamingforest:troll_cave`: triangular lattice spacing 4.5 (x_offset 3.897186, x_spacing 7.794372, z_offset 2.2498753, z_spacing 4.5); stalactites `hill_stone_stalactite`, stalactite chance 1.0, stalagmite chance 0.25 with `hill_stone_stalactite` |

First piece `TrollCaveMainComponent(0, x, y + 11, z)` (y = sea level 0 + 11).

| Piece id | Class | Summary |
|---|---|---|
| `gloamingforest:GloamingTCMai` | TrollCaveMainComponent | size 30, height 20, box from (-15, -20, -15) (30 x 20 x 30), SOUTH. Children: 4 connecting caves (size 18, height 15) at random openings on each wall (`getValidOpening`: offset = size / 4, x/z along the wall in [offset, size - offset), y for the CW90/CCW90 walls `rand(offset) - rand(offset)`), each added only without collision; a troll vault at the box centre. postProcess: `hollowCaveMiddle` (ellipsoidal hollowing: `dist = sqrt(ex x ey x ez)` with ey doubled from the floor, threshold size / 5; blocks in `#gloamingforest:cannot_troll_cave_hollow` kept; `dist > threshold` -> air, `dist == threshold` 1/4 base stone -> `gloamingforest:trollsteinn`), speleothems over the lattice (stalactites only under solid ceilings, stalagmites from the floor) with `decoRNG = seed + minX x 321534781 ^ minZ x 756839`, 32 uberous soil patches (`gloamingforest:uberous_soil_patch_big`, U29's feature by string) at the surface above random cave points (searching up from sea level + 15 for 15 blocks) |
| `gloamingforest:GloamingTCCon` | TrollCaveConnectComponent | connecting cave (size 18 or 20, height 15) with `openingTowards[4]` (saved `openingTowards0..3`). Children (depth < 3): per side, 50 % try a garden cave (size 30, height 15; not within 30 blocks of another garden) else a smaller cave (size 20, height 15). postProcess: hollow, wall decorations on closed sides (1/2 bracket mushrooms: rows every 2 blocks of `huge_mushgloom` 1/3 else brown/red mushroom blocks, width/depth 1-3; else 1/2 two stone formations: 1/8 obsidian, 1/4 trollsteinn, else stone), speleothems, then a treasure crate (obsidian box with a chest of `gloamingforest:troll_garden`) when it has exactly one exit and 1/3, else 1/3 an obsidian monolith (height 7 + rand(8)) |
| `gloamingforest:GloamingTCGard` | TrollCaveGardenComponent | garden cave; skipped entirely when its box is outside the highlands / highlands underground biomes; hollow, treasure crate (`troll_garden`), 24 `gloamingforest:troll_cave_dirt` and 16 `gloamingforest:troll_cave_mycelium` blobs, 16 small uberous soil patches (`gloamingforest:uberous_soil_patch_small`) inside and at the surface above, 8 mycelium + `gloamingforest:troll_big_mushglooms`, 16 mycelium + `gloamingforest:troll_huge_brown_mushrooms` / `gloamingforest:troll_huge_red_mushrooms` (50/50), speleothems. No children ("add a cloud" is a no-op in upstream) |
| `gloamingforest:GloamingTCVa` | TrollVaultComponent | 12 x 12 x 12 cube of `gloamingforest:giant_obsidian` (U29's block) aligned to multiples of 4 around the main cave centre (x, z rounded down to 4, y to 4; box (-8, 0, -8)), hollow 4..7, cobblestone platform 5..6, two double chests: (5,6,5)-(5,6,6) with `gloamingforest:troll_vault` and `gloamingforest:troll_vault_with_lamp`, and (6,6,5)-(6,6,6) with `gloamingforest:troll_garden`; no controlled spawns |

Golden hash per piece (R19). Strings: `structure.gloamingforest.troll_cave` "Troll Cave".

### 1.1 Cloud pieces (owned here, used by U29's `gloamingforest:giant_house`)

| Piece id | Class | Summary |
|---|---|---|
| `gloamingforest:GloamingClCa` | CloudCastleComponent | coordinates rounded down to multiples of 4, box (-8, -4, -8) size 64 x 16 x 64; children: a `GloamingCloud` at (minX, minY + 3, minZ) and two cloud trees at y 168: x-tree at (minX + 8 + (rand bool ? 32 : -16), minZ + (rand(5) - rand(5)) x 4), z-tree mirrored. postProcess: fluffy cloud slab (8,0,8)-(23,3,23), giant cobblestone walls (8,4,8)-(23,15,23), giant log roof (8,16,8)-(23,19,23), hollow (12,4,12)-(19,15,19), door (8,4,12)-(12,11,15); spawns one `gloamingforest:giant_miner` at (14,4,14) and one `gloamingforest:armored_giant` at (17,4,17), persistent, finalized STRUCTURE (only when the chunk being generated contains the point) |
| `gloamingforest:GloamingCloud` | CloudComponent | box aligned to chunks: min = (center - 32) & ~15, size 96 + 15 horizontally, y maxY - 8..maxY; per generated chunk a procedural cloud: region seed `(rx x 3129871 ^ rz x 116129781)`, `seed = seed^2 x 42317861 + seed x 7`, three blob centres (num0..num3 from seed bits), 4 x 4 cells of 4 x 4 columns with `dist = min(d0/4, d2/3.5, d3/4.5)`, `cv = dist - 7 - r x 3`; top `gloamingforest:fluffy_cloud` in the centre chunk else `gloamingforest:wispy_cloud`, fluffy below to depth 4 (+1 if r > 0.6, +1 if r > 0.9), wispy bottom; fringe wispy only; only replaces air (exact algorithm in upstream) |
| `gloamingforest:GloamingClTr` | CloudTreeComponent | coordinates rounded to 4, box (-8, 0, -8) 20 x 28 x 20: giant leaves (0,12,0)-(19,19,19), (4,20,4)-(15,23,15), crosses (8,24,4)-(11,27,15) and (4,24,8)-(15,27,11); giant log trunk (8,0,8)-(11,23,11); fluffy cloud base (8,-4,8)-(11,-1,11) |
| `gloamingforest:GloamingTCloud` | TrollCloudComponent | legacy piece (never generated in 4.8, deserialised only): white stained glass box of `size` x 7 x `size` with a quartz core; saved `size`, `height` |

## 2. Structure `gloamingforest:yeti_cave`

Sources: `world/components/structures/type/YetiCaveStructure.java`, `world/components/structures/YetiCaveComponent.java`,
`HollowHillComponent.java` (F7 base).

| Field | Value |
|---|---|
| Structure type / set | `gloamingforest:yeti_cave`, placement `landmark_grid`, weight 1 |
| Biomes | `#gloamingforest:valid_yeti_cave_biomes` (snowy forest) |
| Step | `surface_structures`, terrain adaptation none, not centred in chunk |
| Progression lock | `gloamingforest:progress_lich` |
| Hint | kobold + book `gloamingforest.book.yeticave` ("Notes on an Icy Cave", 3 pages) |
| Decoration clearance | radius 2, surface true, underground false, vegetation true, adjust elevation false |
| Controlled spawns label 0 | `gloamingforest:yeti` 5 1-2 |
| Map icon | `gloamingforest:yeti_lair` |
| Speleothems | `gloamingforest:yeti_cave`: lattice spacing 3.5 (x_offset 3.0311449, x_spacing 6.0622897, z_offset 1.7499031, z_spacing 3.5); base stalactites ice, packed ice, blue ice; stalactite chance 1.0, stalagmite chance 0 |

Piece `gloamingforest:GloamingYeti` (`YetiCaveComponent extends HollowHillComponent`, hill size 2: radius 34, created at
(x, y + 4, z), box min y lowered by 1 and every local y shifted +1). postProcess: drain water within Chebyshev radius 34
of the centre down 6 below local y 4 (water -> cave air; dirt / snow block below the first solid -> packed ice); over
the lattice (from local y 4) within 24 blocks (x and z): stalactites at +`15 - minDelta / 6` and stalagmites at
`-4 + minDelta / 6` when rolled; `gloamingforest:alpha_yeti_boss_spawner` (U5's block) at local (34, 1, 34).

Terraformer (F6 `addTerraformer`, density, riser 5, centre = box centre + 5 y): exterior "cut pyramid" = -1 x
`AbsoluteDifference.max(40, centre)` + `yClampedGradient(0, 29, 40, 32)` + `yClampedGradient(26, 27, 0, -100000)`;
floor/ceiling = 32 x max(`yClampedGradient(15, 27, -1, 1)`, `yClampedGradient(-6, 6, 1, -1)`) + clamp(`AbsoluteDifference.min(64, centre)`, 1, 64);
walls `yClampedGradient(2, 26, -9.5, -2.5)`; interior carver = min(`AbsoluteDifference.min(64)`, -20 +
`AbsoluteDifference.max(64)`); interior = max(floorCeiling, walls + carver); entrance slope = `yClampedGradient(-8, 21,
-46, -48)` + `AbsoluteDifference.max(64)`; smoothed = max(interior, (15 + slope) / 64); result = lerp(clamp(exterior x
0.020833334, 0, 1), 0, 16 x smoothed). Needs F5's `AbsoluteDifferenceFunction`-style helpers (ask F5/A8 if missing).

Strings: `structure.gloamingforest.yeti_cave` "Yeti Cave".

## 3. Blocks

Sources: `block/TrollsteinnBlock.java`, `block/TrollRootBlock.java`, `block/UnripeTorchClusterBlock.java`, vanilla
`HugeMushroomBlock`, `init/GloamingBlocks.java`, `client/model/block/` (TrollsteinnModel).

| Block | Properties | States | Light | Tool / tags |
|---|---|---|---|---|
| `gloamingforest:trollsteinn` | map STONE, instrument BASEDRUM, sound STONE, strength 2.0 / 6.0, requires correct tool, random ticks | `down`, `up`, `north`, `south`, `west`, `east` (booleans: that face is lit) | 0 | pickaxe, `#neoforge:needs_wood_tool`, `#minecraft:moss_replaceable`, `#minecraft:overworld_carver_replaceables` |
| `gloamingforest:trollvidr` | map PLANT, instabreak, no collision, push DESTROY, sound FLOWERING_AZALEA | none | 0 | shears drop |
| `gloamingforest:unripe_trollber` | as trollvidr + random ticks | none | 0 | shears drop |
| `gloamingforest:trollber` | as trollvidr | none | 15 | shears drop |
| `gloamingforest:huge_mushgloom` | `HugeMushroomBlock`: map ORANGE, ignited by lava, sound SHROOMLIGHT, strength 0.2 | 6 face booleans (vanilla huge mushroom) | 5 | axe; `#gloamingforest:cannot_troll_cave_hollow` |
| `gloamingforest:huge_mushgloom_stem` | `HugeMushroomBlock`: map ORANGE, ignited by lava, sound NYLIUM, strength 0.2 | 6 face booleans | 5 | axe |

Block items: `gloamingforest:trollsteinn`, `gloamingforest:trollvidr`, `gloamingforest:unripe_trollber`,
`gloamingforest:trollber`, `gloamingforest:huge_mushgloom`, `gloamingforest:huge_mushgloom_stem`.

- **Trollsteinn**: placement sets each face flag to (raw brightness of that neighbour > 7), computing the client's
  sky darkening like the server; random tick re-evaluates all six faces with `getMaxLocalRawBrightness` and updates when
  changed. Comparator output = brightest neighbour light (0..15). Client: 1/2 per tick a purple dust sparkle (rgb 0.25,
  0, 0.5, size 1) on each non-solid face whose neighbour light <= 7. Model `TrollsteinnModel`: per face the lit texture
  (`trollsteinn_light`) when the face flag is set, else the dark texture (F1 imports the textures; U27 owns the model:
  64 permutations, Bedrock per-face material instances by state). Loot `gloamingforest:blocks/trollsteinn` (survives
  explosion, itself).
- **Troll roots** (`TrollRootBlock`, trollvidr / trollber / unripe trollber): survive only under base stone or another
  troll root block (`canSurvive` checks the block above); pop when the block above changes; shape (0.1, 0, 0.1)-(0.9,
  1, 0.9). Using a trollber (empty hand): becomes trollvidr (client update flag), sound
  `block.gloamingforest.torchberry.harvest` (U6's event, vol 1, pitch 1), drops 1 `gloamingforest:torchberries` item
  entity at the block, stat `gloamingforest:torchberries_harvested` + 1 (U6's stat, by string).
- **Unripe trollber**: random tick with raw brightness >= 6 at its position ripens to trollber. Trolls dying nearby
  also ripen them (5.1).
- Loot: `gloamingforest:blocks/trollvidr` and `gloamingforest:blocks/unripe_trollber` (shears only, itself);
  `gloamingforest:blocks/trollber`: shears -> itself, else 4-8 `gloamingforest:torchberries` with fortune ore bonus.
  `gloamingforest:blocks/huge_mushgloom` and `gloamingforest:blocks/huge_mushgloom_stem`: silk touch -> itself, else
  -6..2 (clamped at 0) `gloamingforest:mushgloom` with explosion decay.

Tag `block/#gloamingforest:cannot_troll_cave_hollow`: `minecraft:red_mushroom_block`, `minecraft:brown_mushroom_block`,
`gloamingforest:huge_mushgloom` (the cave hollowing keeps them).

## 4. Features

| Id | Kind | Definition |
|---|---|---|
| `gloamingforest:big_mushgloom` | feature type | `BigMushgloomFeature` (AbstractHugeMushroomFeature): height `2 + rand(2)`, radius 0 for height <= 2 else foliage radius; cap 1 or 2 layers of the cap state shaped by `FeatureLogic.getSphericalMushroomBlockState`, placed where not solid-render |
| `gloamingforest:troll_cave_big_mushgloom` | feature type | `TrollBigMushgloomFeature`: same but the cap skips `#minecraft:features_cannot_replace` instead of solid blocks |
| `gloamingforest:troll_cave_huge_brown_mushroom` | feature type | `TrollHugeBrownMushroomFeature`: vanilla brown cap copy (no corners, horizontal states), skipping `features_cannot_replace` |
| `gloamingforest:troll_cave_huge_red_mushroom` | feature type | `TrollHugeRedMushroomFeature`: vanilla red cap copy over y height-3..height (edges only below the top, radius - 1 on top), face states by inner radius `foliage - 2` |
| `gloamingforest:troll_vines` | feature type | `UndergroundPlantFeature(spawnInStructure = true)`: from the origin downward to the min build height, at air positions (re-rolling x/z +-3 on non-air or 1/6), place the state if it can survive (trollvidr becomes `unripe_trollber` 1/10) |
| `gloamingforest:troll_big_mushglooms` | configured | `troll_cave_big_mushgloom`, cap `huge_mushgloom[up=true, down=false, sides true]`, stem `huge_mushgloom_stem[up=false, down=false, sides true]`, foliage radius 1 |
| `gloamingforest:troll_roots` | configured | `troll_vines` with `gloamingforest:trollvidr` |
| `gloamingforest:troll_mushglooms` | configured | `gloamingforest:underground_plants_in_structure` (another unit's type) with `gloamingforest:mushgloom` |
| `gloamingforest:troll_cave_dirt` | configured | `gloamingforest:mycelium_blob` (disk) of `minecraft:dirt`, radius 2-5, half height 0, target stone or deadrock |
| `gloamingforest:troll_cave_mycelium` | configured | `mycelium_blob` of `minecraft:mycelium[snowy=false]`, radius 3-5, target stone or deadrock |
| `gloamingforest:dark_mushglooms` | configured | `gloamingforest:dark_forest_placer` (random patch: 50 tries, xz spread 7, y spread 3) of `gloamingforest:mushgloom` on replaceable blocks above `#minecraft:dirt` |
| `gloamingforest:big_mushgloom` | placed | `gloamingforest:mushroom/big_mushgloom`: rarity 1, in square, heightmap OCEAN_FLOOR, `gloamingforest:no_structure` (occupies surface), biome |
| `gloamingforest:dark_mushglooms` | placed | rarity 12, in square, WORLD_SURFACE_WG, biome (dark forest and dark forest centre, VEGETAL_DECORATION) |
| `gloamingforest:sparse_mushglooms` | placed | `gloamingforest:mushgloom_cluster`: WORLD_SURFACE_WG, rarity 15, in square, would-survive mushgloom, biome (highlands) |
| `gloamingforest:troll_mushglooms` | placed | WORLD_SURFACE_WG, height uniform bottom..absolute 15, count 3, `no_structure` (underground), biome (highlands, highlands underground) |
| `gloamingforest:troll_roots` | placed | WORLD_SURFACE_WG, count on every layer 12, biome (highlands, highlands underground; UNDERGROUND_DECORATION step 7) |

Feature types are registered through F5 `gf::features::registerType`; `gloamingforest:troll_huge_brown_mushrooms` /
`gloamingforest:troll_huge_red_mushrooms` configured features and the mushgloom cluster are other units' ids used by
string.

## 5. Mobs

### 5.1 `gloamingforest:troll` (Troll, RangedAttackMob)

Source: `entity/monster/Troll.java`, `client/model/entity/TrollModel.java`, renderer `GloamingBipedRenderer` (`troll.png`, shadow 0.625).

| Field | Value |
|---|---|
| Size | 1.4 x 2.4 |
| Attributes | max_health 30.0, movement_speed 0.25, attack_damage 7.0 |
| Synced | `ROCK_FLAG`; follow range + 8 (`gloamingforest:rock_follow_boost`) while holding a rock |
| Saved | `HasRock`, `RockCooldown`, `RockState` (block state) |
| Spawn egg | `gloamingforest:troll_spawn_egg` 0x9ea98f / 0xb0948e |

Goals: 1 Float; 2 RestrictSun; 3 FleeSun 1.0; 4 combat goal (RangedAttack speed 1.0, interval 20-60, radius 15 while
holding a rock, else MeleeAttack 1.2); 5 WaterAvoidingRandomStroll 1.0; 6 LookAtPlayer 8; 6 RandomLookAround.
Targets: 1 HurtBy (ignores trolls); 2 NearestAttackable Player.

- Rock pick-up (server tick, with a target, no rock): cooldown `300 + rand(100)` ticks counts down; then the enderman
  take-block copy: random block in x/z +-2, y 0..3; if it is `#minecraft:base_stone_overworld` and the ray from the
  troll hits that block, remember its state and remove it (game event BLOCK_DESTROY); if a rock is held: flag on,
  sound `entity.gloamingforest.troll.grab_rock`, and a `gloamingforest:thrown_block` with that state rides the troll
  (attachment (0, height x 1.25, 0), yaw follows the body).
- Ranged attack (rock): new thrown block toward `target minY + height / 3` + 0.2 x horizontal distance, velocity
  1.6, inaccuracy `4 - difficulty id`; sound `entity.gloamingforest.troll.throw_rock` (vol 1, pitch `1 / (r x 0.4 +
  0.8)`); game event PROJECTILE_SHOOT; clears the rock, discards the riding thrown block, cooldown `300 + rand(100)`.
- Death: every 5 death ticks ripen `unripe_trollber` within 12 blocks where `|x + y + z| % 5 == deathTime / 5` with
  50 % chance (mob-spawn particles).
- Sounds: `entity.gloamingforest.troll.ambient` ("Troll grumbles"), `entity.gloamingforest.troll.hurt`,
  `entity.gloamingforest.troll.death`.

Loot `gloamingforest:entities/troll`: `gloamingforest:magic_beans` with chance 0.025 (0.035 + 0.01 per looting level
above 1 when looting).

### 5.2 `gloamingforest:thrown_block` (ThrownBlock)

GloamingThrowable, 1.0 x 1.0, tracking 80, interval 2, fire immune, no egg; carries a block state (default stone; saved
`BlockState`, sent as the add-entity data id). Hit a living non-troll entity (server): 6 `gloamingforest:thrown_block`
damage, entity event 3 (20 block particles), discard. Hit a block: event 3, game event BLOCK_DESTROY, discard (the
rock is lost). Display name = the block's name. Renderer: the block model rotated by yaw - 90, shadow 0.5.

### 5.3 `gloamingforest:yeti` (Yeti, IHostileMount)

Source: `entity/monster/Yeti.java`, `entity/ai/goal/ThrowRiderGoal.java`, `client/model/entity/YetiModel.java`, renderer `GloamingBipedRenderer` (`yeti2.png`, shadow 0.625).

| Field | Value |
|---|---|
| Size | 1.4 x 2.4 |
| Attributes | max_health 20.0, movement_speed 0.38, attack_damage 0.0, follow_range 4.0 (+ 8 while angry, `gloamingforest:anger_follow_boost`) |
| Synced / saved | `ANGER_FLAG` / `Angry` |
| Passenger attachment | (0, height, 0.4); rider can interact |
| Voice pitch | vanilla + 0.55 |
| Spawn egg | `gloamingforest:yeti_spawn_egg` 0xdedede / 0x4675bb |

Goals: 0 Float; 1 ThrowRiderGoal (speed 1.0; grab sound `entity.gloamingforest.yeti.grab` vol 1 pitch 1.25 + r x 0.5
when it picks someone up; throw sound `entity.gloamingforest.yeti.throw` on release); 2 WaterAvoidingRandomStroll; 3
LookAtPlayer 8; 3 RandomLookAround. Targets: 1 HurtBy; 2 NearestAttackable Player. Hurt by any entity (non-creative):
becomes angry. Looks at its rider (100/100).

**ThrowRiderGoal** (MeleeAttackGoal subclass, shared with the alpha yeti): usable with no passenger, a target that is
not in `#c:bosses` and whose `yeti_throwing` cooldown is 0, and the melee goal usable; start: throw timer `10 +
rand(30)`, timeout `80 + rand(40)`; tick: timeout - 1; while carrying, throw timer - 1, else melee tick; attack (in
range, attack cooldown ready, no passenger, internal cooldown 3 ticks): swing, and unless the victim's vehicle is in
`#gloamingforest:rides_obstruct_snatching`, dismount the victim and force-mount it; continue while (carrying and
throw timer > 0) or (not carrying, timeout > 0 and the melee goal continues); stop while carrying: forced dismount
(`hostileDismount`), throw vector `(look.x x 2, 0.9, look.z x 2)`; players get the `yeti_throwing` attachment set
(thrown = true, thrower, vector, cooldown 200) and a push + motion packet; other riders just a push.

Spawn (`yetiSnowyForestSpawnHandler`): not Peaceful; in `gloamingforest:snowy_forest` just `checkMobSpawnRules`;
elsewhere the light check (`sky light > rand(32)` -> only in snowy forest; else brightness (10 when thundering) <=
rand(8) or snowy forest) and `checkMobSpawnRules`. Biome snowy_forest monster weight 5, 1-1 (charge 0.6, budget 0.15).

Model: angry pose (arms up) when the flag is set. Sounds: ambient `entity.gloamingforest.yeti.growl`, hurt
`entity.gloamingforest.yeti.hurt`, death `entity.gloamingforest.yeti.death`.

Loot `gloamingforest:entities/yeti`: `gloamingforest:arctic_fur` 0..2 + looting 0..1.

### 5.4 Attachment `gloamingforest:yeti_throwing` (YetiThrowAttachment)

Per player: `thrown` (bool), `thrower` (entity), `throwCooldown` (int, THROW_COOLDOWN 200), `throwVector`. Tick (both
sides): when thrown and on ground / swimming / in water, clear thrown; while cooldown > 0, at `cooldown == 199` (server)
push the stored vector again and send a motion packet, then clear the vector; cooldown - 1. Changes are sent to the
player and trackers (`UpdateThrownPacket`: player id, thrown, thrower id, cooldown). F8's hostile-mount rules convert
fall damage of a thrown player into `gloamingforest:yeeted` damage attributed to the thrower (amount unchanged).
Bedrock: a per-player record in U27 (server), synced on `caves.thrown` to companions; vanilla clients just receive the
motion (`SetActorMotionPacket`).

### 5.5 Boss `gloamingforest:alpha_yeti` (AlphaYeti extends BaseTFBoss, RangedAttackMob, IHostileMount)

Source: `entity/boss/AlphaYeti.java`, `entity/ai/goal/{YetiRampageGoal,YetiTiredGoal,ThrowRiderGoal,AttemptToGoHomeGoal}.java`,
`block/entity/spawner/AlphaYetiSpawnerBlockEntity.java`, `client/model/entity/AlphaYetiModel.java`, renderer `GloamingBipedRenderer` (`yetialpha.png`, shadow 1.75).

| Field | Value |
|---|---|
| Size | 3.8 x 5.0 |
| Attributes | max_health 200.0, movement_speed 0.38, attack_damage 1.0, follow_range 40.0, knockback_resistance 0.5 |
| XP | 317 |
| Synced | `RAMPAGE_FLAG`, `TIRED_FLAG`; server `canRampage` (set by any successful hit, cleared when tired) |
| Boss bar | colour 0xB4F0F0 |
| Home | spawner, radius 30, structure `gloamingforest:yeti_cave`; death chest `gloamingforest:canopy_chest`; spawner `gloamingforest:alpha_yeti_boss_spawner` |
| Sounds | volume 4.0, pitch 0.5 + r x 0.5; ambient `entity.gloamingforest.alpha_yeti.growl`, hurt `entity.gloamingforest.alpha_yeti.hurt`, death `entity.gloamingforest.alpha_yeti.death`; new target: `entity.gloamingforest.alpha_yeti.alert` (vol 4, pitch 0.5 + r x 0.5) |
| Spawn egg | `gloamingforest:alpha_yeti_spawn_egg` 0xcdcdcd / 0x29486e; placement monster rules |

Goals: 0 Float; 1 YetiTired (100 ticks, not interruptible: pant `entity.gloamingforest.alpha_yeti.pant` every 10 ticks,
vol 4); 3 YetiRampage (timeout 10, duration 180); 4 RangedAttack (speed 1.0, interval 40, radius 40; only 49/50 per
check, with a target at distance^2 >= 16); 4 ThrowRiderGoal 1.0 (grab `entity.gloamingforest.alpha_yeti.grab` / throw
`entity.gloamingforest.alpha_yeti.throw`, vol 4, pitch 0.75 + r x 0.25); 5 AttemptToGoHome 1.25 (outside the home
area: random position toward home within 16/7); 5 WaterAvoidingRandomStroll 2.0; 6 LookAtPlayer 8; 7 RandomLookAround.
Targets: 1 HurtBy; 2 NearestAttackable Player.

- **Ranged phase** (not `canRampage`): ranged attack throws a `gloamingforest:thrown_ice` (U8's ice bomb entity, by
  string) at `target minY + height / 3` + 0.2 x distance, velocity 1.6, inaccuracy `14 - difficulty id x 4`, sound
  `entity.gloamingforest.alpha_yeti.ice` (vol 1); projectiles do **no damage** to it unless it can rampage or is tired
  (`#minecraft:is_projectile` cancelled).
- **Rampage** (YetiRampageGoal, MOVE + JUMP): while it has a target and `canRampage`, the timeout counts down 10 checks;
  start: rampaging, roar `entity.gloamingforest.alpha_yeti.roar` (vol 4), game event ENTITY_ACTION; 180 ticks: look at
  the target, jump (0.4 up) whenever on ground (HIT_GROUND event), destroy blocks in its box inflated (1, 2, 1) moved
  up 2 (mob griefing, destroyable); every 10 ticks drop a random ice block within 30 (hang 80), every 20 ticks the ice
  above the target (hang 40), in the last 40 ticks every 10 ticks a random ice block within 15 (hang 40); every 20
  ticks throw an ice bomb in a random direction (horizontal 0.5-1.0, up 0.5-0.8, speed 0.4-0.7) with the ice sound;
  stop: tired, timeout reset. Collisions while rampaging count; at 15 collisions destroy blocks in its box. Landing
  while rampaging: ice sound and 5 `mob_attack` damage + 0.4 up to living entities within (5, 0, 5).
- Falling ice (`makeBlockFallAbove`): the first `#minecraft:ice` block with air below within 1..24 above the column
  becomes a `gloamingforest:falling_ice` entity (block set to air).
- **Tired**: splash particles at eye height (20 per tick, client); vulnerable to projectiles; after 100 ticks clears
  tired.
- Client while rampaging: 20 `gloamingforest:snow` particles per tick on a rotating ring of radius 3 (U28's particle),
  walk animation speed + 0.6.
- Rider handling as the yeti (looks at its rider, rider can interact).

Spawner (R20, `AlphaYetiSpawnerBlockEntity`): `boss::registerSpawner({blockId: gloamingforest:alpha_yeti_boss_spawner,
entityId: gloamingforest:alpha_yeti, range: 9, particle: minecraft:falling_water})` with in-range "nearest player
within 9 whose y > spawner y - 4".

Loot `gloamingforest:entities/alpha_yeti`: `gloamingforest:alpha_yeti_fur` 6 + looting 0..1 + multiplayer 1..3 per
extra player; `gloamingforest:ice_bomb` 6 + looting 0..1 + multiplayer 0..2; `gloamingforest:alpha_yeti_trophy`.

### 5.6 `gloamingforest:falling_ice` (FallingIce)

`make(...)` MISC 1.0 x 1.0, fire immune, no egg (colours 0). Modified falling block: default state packed ice; hangs
without gravity for `hangTime` ticks (default 100), then falls (-0.04 per tick, x 0.98 drag); client: 10 block-particle
warning trail per tick below it; lands like a falling block (places the block if possible, else discards; never
drops an item); despawns after 1000 ticks or out of the world after 100. Fall damage: `realDist = ceil(dist - 5)`;
if >= 0, damage = `min(floor(realDist x {0, 0.5, 1, 2}[difficulty]), 100)` of type `gloamingforest:falling_ice` to all
non-spectator entities in its box inflated (1, 0, 1) except alpha yetis; always 200 block particles and the packed-ice
break sound (vol 3, pitch 0.5). Saved `BlockState`, `Time`, `BlockEntityData`. Not attackable; pickable while alive.
Renderer: falling-block copy, shadow 0.5.

## 6. Loot tables (chests)

- `gloamingforest:troll_garden`: pool 1 (4): red mushroom 1-4, brown mushroom 1-4, wheat seeds 1-6, carrot 1-6, potato
  1-6, melon seeds 1-6, bone meal 1-12; pool 2 (2): `gloamingforest:uberous_soil` 1-6; pool 3 (1):
  `gloamingforest:magic_beans`.
- `gloamingforest:troll_vault`: pool 1 (4): coal 1-16, `gloamingforest:torchberries` 1-16, emerald 1-6; pool 2 (2):
  `gloamingforest:trollsteinn` 1-6, obsidian 1-6.
- `gloamingforest:troll_vault_with_lamp`: pool referencing `gloamingforest:troll_vault` + 1 `gloamingforest:lamp_of_cinders`.

## 7. Damage types (owned by U27)

| Id | message_id (en_us) | Exhaustion | Tags |
|---|---|---|---|
| `gloamingforest:falling_ice` | `fallingBlock` (vanilla "%1$s was squashed by a falling block") | 0.1 | `#minecraft:bypasses_enchantments`, `#neoforge:is_environment`, `#neoforge:is_physical` |
| `gloamingforest:thrown_block` | `gloamingforest.thrownBlock` ("%1$s was squashed by a thrown block") | 0.1 | `#minecraft:damages_helmet`, `#minecraft:is_projectile`, `#neoforge:is_physical` |
| `gloamingforest:yeeted` | `gloamingforest.yeeted` ("%1$s was yeeted for the last time") | 0.1 | `#minecraft:bypasses_armor`, `#minecraft:is_fall`, `#neoforge:is_physical`, `#minecraft:avoids_guardian_thorns` |

All effects hurt, scaling `when_caused_by_living_non_player`.

## 8. Map decorations

`gloamingforest:troll_caves` and `gloamingforest:yeti_lair` (`MapDecorationType(asset, item frame true, colour -1,
exploration false, track count true)`), shown by the magic map (U13) via the structures' `structure_icon`.

## 9. Recipe (compat, not ported)

`gloamingforest:jeed/frosted` (`jeed:effect_provider`: `gloamingforest:frosted` from the alpha yeti spawn egg, ice bomb,
ice bow, ice sword and the four yeti armour pieces) loads only with JEED: **not ported**; U27 owns the id only.

## 10. Sound events (owned by U27)

| Event | Subtitle |
|---|---|
| `entity.gloamingforest.alpha_yeti.alert` | Alpha Yeti takes notice |
| `entity.gloamingforest.alpha_yeti.growl` | Alpha Yeti growls |
| `entity.gloamingforest.alpha_yeti.hurt` | Alpha Yeti hurts |
| `entity.gloamingforest.alpha_yeti.death` | Alpha Yeti dies |
| `entity.gloamingforest.alpha_yeti.grab` | Alpha Yeti grabs |
| `entity.gloamingforest.alpha_yeti.throw` | Alpha Yeti throws |
| `entity.gloamingforest.alpha_yeti.ice` | Alpha Yeti throws ice |
| `entity.gloamingforest.alpha_yeti.pant` | Alpha Yeti pants |
| `entity.gloamingforest.alpha_yeti.roar` | Alpha Yeti roars |
| `entity.gloamingforest.troll.ambient` | Troll grumbles |
| `entity.gloamingforest.troll.hurt` | Troll hurts |
| `entity.gloamingforest.troll.death` | Troll dies |
| `entity.gloamingforest.troll.grab_rock` | Troll grabs rock |
| `entity.gloamingforest.troll.throw_rock` | Troll throws rock |
| `entity.gloamingforest.yeti.growl` | Yeti growls |
| `entity.gloamingforest.yeti.hurt` | Yeti hurts |
| `entity.gloamingforest.yeti.death` | Yeti dies |
| `entity.gloamingforest.yeti.grab` | Yeti grabs |
| `entity.gloamingforest.yeti.throw` | Yeti throws |

## 11. Spawn eggs and placements

`gloamingforest:troll_spawn_egg`, `gloamingforest:yeti_spawn_egg`, `gloamingforest:alpha_yeti_spawn_egg`. Placements
ON_GROUND: troll and alpha yeti monster rules, yeti `yetiSnowyForestSpawnHandler`.

## 12. Bedrock adaptation notes

- Trollsteinn face states: 6 booleans = 64 permutations (check F1's permutation budget); the lit texture per face is a
  material-instance swap in the block geometry; vanilla clients see the same (pure data), the sparkle particles are
  server-sent only to companions (client-side in upstream).
- Yeti throws are client-authoritative motion on Java; with the client mod the companion applies the push; vanilla
  clients receive `SetActorMotionPacket` (deviation listed in README: exact only with the client mod).
- Hostile-mount rules (no dismount, no sneak, in-wall immunity) come from F8 `gf::entity::mount`.

## 13. Acceptance (selftests `caves.*`, dx 2700-2799)

Per the unit record: golden hashes of the troll cave pieces and the yeti cave; trollsteinn face states follow light
(random tick) and comparator output; trollber harvest gives torchberries and the stat; unripe trollber ripens at light
>= 6; troll grabs a stone block and throws it (6 damage); yeti grabs and throws a SimulatedPlayer (motion applied,
cooldown 200, fall becomes `yeeted`); alpha yeti: projectiles ignored until hit, rampage after a melee hit (falling
ice, ice bombs, jumps), tired 100 ticks, death places the canopy chest; client screenshot of trollsteinn lit/unlit.
