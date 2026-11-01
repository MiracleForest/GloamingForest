# U23 `labyrinth`: Labyrinth and Minoshroom

Behaviour spec translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ `9e40a757`). It is the
checklist the U23 worker implements and the W2 audit (A3) compares against. The labyrinth is fully procedural (no
`.nbt` templates): every piece is an exact translation.

| | |
|---|---|
| Unit | U23 `labyrinth` (W1 batch 2) |
| Selftests | `labyrinth.*`, testPos dx `2300-2399` (owners rows `selftest labyrinth.*`, `selftest_dx 2300-2399`) |
| World DB | `gf:labyrinth:*` |
| Net channels | `labyrinth.*` |
| Config | none |
| Contracts | landmark (F6), structure + Maze base (F7), boss + entity (F8), loot (F10), interactions |
| Java sources | `entity/boss/Minoshroom.java`, `entity/monster/{Minotaur,MazeSlime}.java`, `entity/ai/goal/{GroundAttackGoal,ChargeAttackGoal}.java`, `block/MazeSlimeBlock.java`, `block/entity/spawner/MinoshroomSpawnerBlockEntity.java`, `world/components/structures/type/LabyrinthStructure.java`, `world/components/structures/minotaurmaze/*.java` (27 files), `world/components/structures/TFMaze.java` (F7 base), `events/ToolEvents.java` (`damageNonMazebreakerToolsMore`), `client/renderer/entity/{MinoshroomRenderer,MazeSlimeRenderer}.java` |
| Data | `loot_table/entities/{minoshroom,minotaur,maze_slime}.json`, `loot_table/blocks/maze_slime_block.json`, `loot_table/labyrinth_*.json`, `recipe/{compressed_blocks/maze_slime,compressed_blocks/reversed/maze_slime_block,lead_maze_ver,magma_cream_maze_ver,sticky_piston_maze_ver}.json`, `worldgen/structure/labyrinth.json`, `worldgen/structure_set/labyrinth.json`, `damage_type/slam.json`, `tags/block/mazebreaker_accelerated_mining.json` |

Not owned here: the mazestone family, `twilightforest:minoshroom_boss_spawner`, `twilightforest:minoshroom_trophy`
(U5), the minotaur axes, `twilightforest:mazebreaker_pickaxe`, `twilightforest:meef_stroganoff`, `raw_meef`,
`maze_wafer`, `maze_map`, `maze_map_focus` (U7/U8/U10/U13), `twilightforest:axing` damage type (owner of record
in owners.tsv), `twilightforest:root_strand` (U6), fire / slime / pinch beetles (U17), `twilightforest:progress_lich`
(U14), mangrove chest (U4).

## 1. Entity `twilightforest:minoshroom` (boss)

MONSTER 1.49 x 2.5, tracking 80, interval 3; egg `twilightforest:minoshroom_spawn_egg` (`0xa81012` / `0xaa7d66`,
"Minoshroom Spawn Egg"). Placement ON_GROUND / MOTION_BLOCKING_NO_LEAVES / monster rules. Name "Minoshroom". Tags
`#twilightforest:bosses` (freeze immune via bosses).

| Item | Value |
|---|---|
| attributes | Minotaur's (speed 0.25, monster defaults: attack damage 2 + held axe modifiers, follow range 16) with max health **120** and knockback resistance **0.5** |
| xp reward | 100 |
| equipment | main hand `twilightforest:diamond_minotaur_axe` (`populateDefaultEquipmentSlots`), then difficulty enchantments; main-hand drop chance 0 |
| home radius | 20; home structure `twilightforest:labyrinth` |
| death container | `twilightforest:mangrove_chest` |
| boss spawner | `twilightforest:minoshroom_boss_spawner` |
| boss bar | `0xFF0000` (BaseTFBoss default overlay) |
| voice pitch | `(r1 - r2) * 0.2 + 0.7` |

Goals: Float 0; GroundAttackGoal 1; ChargeAttackGoal(speed 1.5, canBreak **true**) 2; MeleeAttackGoal(1.0,
false) 3; restriction goals; WaterAvoidingRandomStroll(1.0) 6; LookAtPlayer(8) 7; RandomLookAround 7. Targets:
HurtBy 1, NearestAttackable(Player, mustSee false) 2.

Synced: `CHARGING` (bool), `GROUND_ATTACK` (bool), `GROUND_CHARGE` (int max charge ticks).

### 1.1 Ground slam (`GroundAttackGoal`, flags MOVE + LOOK)

- `canUse`: `cooldown-- > 0` blocks it; needs a target at squared distance 2..9, on ground; 1/24 per tick with line
  of sight, 1/20 without.
- `start`: cooldown `200 + nextInt(200)` ticks (10-20 s); `attackTick = 30 + nextInt(30)`; `setMaxCharge(attackTick)`;
  GROUND_ATTACK true.
- `tick`: look at the target (30/30), move control WAIT; when `attackTick-- <= 0`: GROUND_ATTACK false, play
  `entity.twilightforest.minoshroom.slam` (2.0, 1.0-1.1), game event HIT_GROUND; every **player** in the box
  `blockPos +- 7.5` (y to +3) that is on the ground is pushed (0, 0.23, 0) and takes `attackDamage * 0.5` of
  `twilightforest:slam`.
- Client: while charging the animation value rises by `6 / maxCharge` per tick (0..6); when the charge ends it
  falls by 1 per tick and 80 block particles of the block below burst within +-5 x/z.

### 1.2 Charge (`ChargeAttackGoal`, shared with the Minotaur; F8 `goals::charge`)

- `canUse`: target at squared distance `16..64`, **+9 for the Minoshroom** (25..73); on ground; line of sight; then
  1/10. Charge point = target position overshot by 2.1 blocks along the line from the charger (y = target y).
- `start`: windup `15 + nextInt(30)`, sprinting.
- `tick`: look at the charge point (y - 1); during windup the walk animation speeds up (+0.8) and `CHARGING` is set;
  when windup reaches 0 the navigation moves to the charge point at speed 1.5. After windup, with `canBreak` and mob
  griefing, destroy every destroyable block without a block entity in the box `floor(bb.min - 0.75)` ..
  `floor(bb.max + 0.75)` (y `minY .. floor(maxY + 0.15)`), with drops. One hit when the squared distance to the
  target's feet is <= 5 (Minoshroom) or `(bbWidth * 2)^2 + targetWidth` (Minotaur).
- `stop`: reset, not sprinting, `CHARGING` false. While charging `aiStep` adds 0.6 to the walk animation speed.

### 1.3 Melee (`doHurtTarget`)

`EntityUtil.properlyApplyCustomDamageSource`: damage = attack damage (with enchantment modifiers) as
`twilightforest:axing` from this mob; on success with knockback > 0: play the attack sound (1.0, 1.0), knockback
`0.5 * kb` along the yaw, own motion x/z * 0.6; post-attack enchantment effects.

### 1.4 Sounds

| Event | Use |
|---|---|
| `entity.twilightforest.minoshroom.ambient` | ambient, "Minoshroom moos" |
| `entity.twilightforest.minoshroom.hurt` | hurt, "Minoshroom hurts" |
| `entity.twilightforest.minoshroom.death` | death, "Minoshroom dies" |
| `entity.twilightforest.minoshroom.attack` | melee fling, "Minoshroom attacks" |
| `entity.twilightforest.minoshroom.slam` | ground slam, "Minoshroom slams ground" |
| `entity.twilightforest.minoshroom.step` | steps (0.15, 0.8), generic footsteps subtitle |

### 1.5 Loot `twilightforest:entities/minoshroom`

- pool 1: `twilightforest:multiplayer_rolls` (default 2-5, per extra player 0-1): `twilightforest:meef_stroganoff`
  +0-1 per looting
- pool 2: `twilightforest:minoshroom_trophy`
- pool 3: `twilightforest:diamond_minotaur_axe`, multiplayer addition -2..1 per extra player

### 1.6 Spawner

`boss::registerSpawner({blockId "twilightforest:minoshroom_boss_spawner", entityId "twilightforest:minoshroom",
range 9, particle "minecraft:crimson_spore"})`, with the extra rule that the nearest player within 9 must be **below
spawner y + 4**. Boss: `registerBoss({entityId "twilightforest:minoshroom", homeStructure "twilightforest:labyrinth",
lootTable "twilightforest:entities/minoshroom", deathChestBlock "twilightforest:mangrove_chest", bar {0xFF0000}})`
(default death animation).

## 2. Entity `twilightforest:minotaur`

MONSTER 0.6 x 2.1; egg `twilightforest:minotaur_spawn_egg` (`0x3f3024` / `0xaa7d66`). Attributes: monster defaults,
max health 30, speed 0.25. Goals: Float 0; Charge(1.5, canBreak false) 2; Melee(1.0) 3; Stroll 6; LookAtPlayer 7;
RandomLook 7; targets HurtBy 1, NearestAttackable(Player) 2. Synced `CHARGING`.
Equipment: `random = nextInt(10)`; `(int)(random / (effectiveDifficulty + 1)) == 0` -> `twilightforest:golden_minotaur_axe`
else `minecraft:golden_axe` (default drop chance), then difficulty enchantments. Melee as 1.3 with the minotaur
attack sound; voice pitch as the Minoshroom.

| Event | Use |
|---|---|
| `entity.twilightforest.minotaur.ambient` | ambient, "Minotaur moos" |
| `entity.twilightforest.minotaur.hurt` | hurt, "Minotaur hurts" |
| `entity.twilightforest.minotaur.death` | death, "Minotaur dies" |
| `entity.twilightforest.minotaur.attack` | melee fling, "Minotaur attacks" |
| `entity.twilightforest.minotaur.step` | steps (0.15, 0.8), generic footsteps subtitle |

Loot `twilightforest:entities/minotaur`: `twilightforest:raw_meef` x1 (+0-1 looting), smelted when the minotaur is
on fire or killed with a `#minecraft:smelts_loot` weapon; 2.5 % (3.5 % + 1 % per extra looting level)
`twilightforest:maze_map_focus`.

## 3. Entity `twilightforest:maze_slime`

Slime subclass: MONSTER 0.52 x 0.52 (scaled by size like vanilla slimes), eye height 0.325; egg
`twilightforest:maze_slime_spawn_egg` (`0xa3a3a3` / `0x2a3b17`). Tag `minecraft:frog_food`. Attributes: monster
defaults; `setSize(size)` applies permanent modifier `twilightforest:double_health` (+2, ADD_MULTIPLIED_BASE, i.e.
three times the vanilla `size^2` health), refills health, xp = size + 3. Always deals damage (even size 1).
Spawn rule: not peaceful, `checkMobSpawnRules` and `Monster.isDarkEnoughToSpawn`. Particles: on landing `8 * size`
block particles of `twilightforest:mazestone_brick` (instead of slime particles). Sound volume `0.1 * size`.

| Event | Use |
|---|---|
| `entity.twilightforest.maze_slime.hurt` / `entity.twilightforest.maze_slime_small.hurt` | hurt (small when size 1) |
| `entity.twilightforest.maze_slime.death` / `entity.twilightforest.maze_slime_small.death` | death |
| `entity.twilightforest.maze_slime.squish` / `entity.twilightforest.maze_slime_small.squish` | squish and jump |

Subtitles: "Maze Slime hurts / dies / squishes" (the small variants share them).

Loot `twilightforest:entities/maze_slime`: size 1 only: `twilightforest:maze_slime_ball` 0-2 (+0-1 looting), or
exactly 1 when killed by a frog; killed by a player: 1.5 % (2 % + 0.5 % per extra looting level)
`twilightforest:charm_of_keeping_1`.

Rendering (client mod): slime-style model with an inner cube and a translucent outer layer (`maze_slime` layers),
TF texture. Vanilla clients: the RP geometry.

## 4. Block `twilightforest:maze_slime_block` and item `twilightforest:maze_slime_ball`

- `twilightforest:maze_slime_block` ("Maze Slime Block", block item `twilightforest:maze_slime_block`): properties
  copied from `minecraft:slime_block` (bouncy, no fall damage, slows walking, transparent, slime sounds), map colour
  STONE. Sticky for pistons; it sticks only to other maze slime blocks and to `#twilightforest:mazestone` blocks.
  Loot `twilightforest:blocks/maze_slime_block`: itself if it survives the explosion.
- `twilightforest:maze_slime_ball` ("Maze Slimeball"): plain item.
- Recipes (vanilla types, R4):
  - `twilightforest:compressed_blocks/maze_slime`: shaped `###/###/###` of maze slime balls -> maze slime block.
  - `twilightforest:compressed_blocks/reversed/maze_slime_block`: shapeless block -> 9 maze slime balls.
  - `twilightforest:lead_maze_ver`: shaped `~~ /~O /  ~` (string, O maze slime ball) -> 2 leads.
  - `twilightforest:magma_cream_maze_ver`: shapeless blaze powder + maze slime ball -> magma cream.
  - `twilightforest:sticky_piston_maze_ver`: shaped `S/P` (maze slime ball over piston) -> sticky piston.
- Recipe unlock advancements (generated, criteria "has item" + `recipe_unlocked`):
  `twilightforest:recipes/building_blocks/compressed_blocks/maze_slime` (has maze slime ball),
  `twilightforest:recipes/misc/compressed_blocks/reversed/maze_slime_block` (has maze slime block),
  `twilightforest:recipes/tools/lead_maze_ver` (has maze slime ball),
  `twilightforest:recipes/brewing/magma_cream_maze_ver` (has blaze powder),
  `twilightforest:recipes/redstone/sticky_piston_maze_ver` (has maze slime ball).

Bedrock: piston stickiness of custom blocks needs the S-spike result (F11 families has no "sticky" family); if a
custom block cannot be sticky, keep the maze slime block bouncy and record the gap in the PR.

## 5. Tag `block/#twilightforest:mazebreaker_accelerated_mining`

`[#twilightforest:mazestone, #twilightforest:castle_blocks]`. Behaviour attached to it:
- `MazebreakerPickItem.getDestroySpeed` multiplies the speed by 16 on these blocks (U8 owns the pick, matches the tag).
- `ToolEvents.damageNonMazebreakerToolsMore`: breaking a block of this tag with any other damageable tool costs that
  tool **16** extra durability. U23 owns the tag; the durability rule is registered with
  `interactions::onDestroy(Match::tag("twilightforest:mazebreaker_accelerated_mining"), ...)` by U23 (the tag owner),
  skipping `twilightforest:mazebreaker_pickaxe` (R6).

Mazestone itself (U5) is hardness 100, resistance 5.

## 6. Damage type `twilightforest:slam`

message id `twilightforest.axing` (same death message as axing), exhaustion 0.1, scaling when caused by living
non-player. Used only by the ground slam.

## 7. Structure `twilightforest:labyrinth`

| Field | Value |
|---|---|
| structure type | `twilightforest:labyrinth` (`LabyrinthStructure`: controlled spawning, configurable spawns, custom density) |
| structure set | `twilightforest:labyrinth`: `twilightforest:landmark_grid` |
| biomes | `#twilightforest:valid_labyrinth_biomes` |
| step / terrain | `underground_structures`, terrain adaptation `bury` |
| advancement lock | `twilightforest:progress_lich` |
| hint | Kobold with book "labyrinth" (5 pages `twilightforest.book.labyrinth.1..5`) |
| decoration clearance | radius 3, **surface decorations allowed**, no underground decorations, vegetation allowed, elevation not adjusted |
| controlled spawns (label 0) | `twilightforest:minotaur` w20 2-3, cave spider w10 1-2, creeper w10 1-2, `twilightforest:maze_slime` w10 2-4, enderman w1 1-2, `twilightforest:fire_beetle` w10 1-2, `twilightforest:slime_beetle` w10 1-2, `twilightforest:pinch_beetle` w10 1 |
| no-spawn pieces | boss room, vault, chest dead ends (spawn index `Integer.MAX_VALUE`) |
| map icon | `twilightforest:labyrinth` |
| centre in chunk | true; first piece `MazeRuinsComponent(0, x + 5, y, z + 5)` (centres the mound on a 4-chunk corner) |

Terraformer (hollow-hill style mound, translate exactly): radius 35; `HollowHillFunction(cx + 1, cy + 7, cz + 2, 35,
0.8).clamp(0, 2)`; ceiling cap `yClampedGradient(5, 6, -1, 1)`; inner floor `yClampedGradient(-4, 3, 26, -1) -
AbsoluteDifference.Max(32, cx + 1.5, cz + 1.5)`; entrances `max(ceiling, AbsoluteDifference.Min(32, ...) - 2)`;
interior `max(entrances, innerFloor).clamp(0, 1)` masked by `Focused(centre, 24.5, 35)`; result
`min(mound, maskedInterior)` (density function types from U2/F5).
The mound centre is `(float)(cx + 1), (float)(cy + 7), cz + 2f`, not `fromPos`, so there is no +0.5. Both
`HollowHillFunction` and `FocusedDensityFunction` compute in `float` with `Mth.sqrt` and the table `Mth.cos`
(terrain.md section 3 gives the exact definitions). The volume-hash golden depends on reproducing that float
arithmetic.

### 7.1 Pieces (all `TFStructureComponentOld`, translate exactly)

| Piece type id | Class | Content |
|---|---|---|
| `twilightforest:TFMMRuins` | MazeRuinsComponent | root, empty box at the start; children: level-1 maze at y - 14, entrance shaft at (+1, 0, +1), mound at (-14, maxY, -14) |
| `twilightforest:TFMMMound` | MazeMoundComponent | 35 x 12 x 35 box (mound shape comes from the terraformer); child: upper entrance at (+10, 0, +10) |
| `twilightforest:TFMMUE` | MazeUpperEntranceComponent | 16 x 5 x 16 ruin: dirt dome (radius 16, 0.8) replacing stone; mazestone ceiling 70 %; mosaic floor; decorative / brick / decorative walls; 20 % gravel; oak fence doorways (6-9) on all four sides; centre pit with decorative rims and 70 % iron bars |
| `twilightforest:TFMMES` | MazeEntranceShaftComponent | 6 x 6 mazestone brick shaft from sea level - 9 up to the surface, hollow 4 x 4 up to sea level + 11 |
| `twilightforest:TFMMaze` | MinotaurMazeComponent | the maze level (7.2) |
| `twilightforest:TFMMR` | MazeRoomComponent | 16 x 5 x 16 room, random orientation: border + mosaic floor, oak fence doorways where the maze opens |
| `twilightforest:TFMMRE` | MazeRoomExitComponent | level 1 room #1: mazestone brick well down 5 blocks, decorative rim, iron bars, open to level 2 |
| `twilightforest:TFMMRB` | MazeRoomBossComponent | level 2 room #1: mycelium / mushrooms by distance, four huge-mushroom corners each hiding a `twilightforest:labyrinth_room` chest, two mushroom caps, `twilightforest:minoshroom_boss_spawner` at (7, 2, 7); no spawns |
| `twilightforest:TFMMRC` | MazeRoomCollapseComponent | level 1 rooms #2-3: gravel piles and dirt / root strand ceilings by distance from centre |
| `twilightforest:TFMMMR` | MazeMushRoomComponent | level 2 rooms #2-3: mycelium, mushrooms, 5 medium mushrooms and 4 corner huge-mushroom caps |
| `twilightforest:TFMMRF` | MazeRoomFountainComponent | level 1 room #4: decorative 6 x 6 basin with water |
| `twilightforest:TFMMRV` | MazeRoomVaultComponent | level 2 room #4: sealed decorative/brick vault, pressure plates over TNT, sand above, four double chests (`twilightforest:labyrinth_vault`, one random jackpot `twilightforest:labyrinth_vault_jackpot`); no spawns |
| `twilightforest:TFMMRSC` | MazeRoomSpawnerChestsComponent | rooms #5-6: four cut-mazestone pillar cages (oak plank/stair floor and roof, iron bars), a `twilightforest:minotaur` spawner at (4, 2, 4), two `twilightforest:labyrinth_room` chests at (4, 2, 11) and (11, 2, 4), pressure plate over 4 TNT at (11, 1, 11) |
| `twilightforest:TFMMDE` | MazeDeadEndComponent | oak fence doorway (default dead end, case 0: 1/8) |
| `twilightforest:TFMMDEC` | MazeDeadEndChestComponent | double chest `twilightforest:labyrinth_dead_end` on oak plank + stairs behind cut mazestone and iron bars; no spawns (1/8) |
| `twilightforest:TFMMDETrC` | MazeDeadEndTrappedChestComponent | as above with a **trapped** double chest over 4 TNT; no spawns (1/16) |
| `twilightforest:TFMMDETC` | MazeDeadEndTripwireChestComponent | chest dead end plus tripwire across (1, 1, 2)-(3) and unstable TNT under the hook and chest (1/16) |
| `twilightforest:TFMMDET` | MazeDeadEndTorchesComponent | wall torches on three walls (1/8) |
| `twilightforest:TFMMDEF` | MazeDeadEndFountainComponent | mazestone brick back wall with a water source (1/8) |
| `twilightforest:TFMMDEFL` | MazeDeadEndFountainLavaComponent | the fountain with lava (1/8) |
| `twilightforest:TFMMDEP` | MazeDeadEndPaintingComponent | doorway and two wall torches (TF's painting is commented out) (1/8) |
| `twilightforest:TFMMDER` | MazeDeadEndRootsComponent | level 1 (1/8): dirt ceiling with root strands and gravel |
| `twilightforest:TFMMDES` | MazeDeadEndShroomsComponent | level 2 (1/8): mycelium, mushrooms, two small huge mushrooms |
| `twilightforest:TFMMC` | MazeCorridorComponent | oak fence across a straight corridor (1/5) |
| `twilightforest:TFMMCIF` | MazeCorridorIronFenceComponent | cut mazestone + iron bars gate (1/5) |
| `twilightforest:TFMMCR` | MazeCorridorRootsComponent | level 1 (1/5): roots and gravel |
| `twilightforest:TFMMCS` | MazeCorridorShroomsComponent | level 2 (1/5): mycelium and small huge mushrooms |

Corridor case 3 (painting) and case 0 place nothing.

### 7.2 Maze level (`MinotaurMazeComponent`)

- Maze 22 x 22 cells (`TFMaze`, F7), cell 5 blocks, radius 55, box `(-55, 0, -55, 112, 5, 112)`, orientation SOUTH.
- Per-level seed `minX * 90342903 + minY * 90342903 ^ minZ`.
- Maze configuration: head `twilightforest:decorative_mazestone`, wall `twilightforest:mazestone_brick`, root
  `decorative_mazestone`, pillar `twilightforest:cut_mazestone`, wall selector = mazestone random block selector
  (F7), torch rarity 0.05, tall 2, head 1, roots 1, odd bias 4.
- Rooms: 7 (`rcoords`), the first at the entrance cell (11, 11 for level 1; level 2 uses level 1's second room as its
  entrance); others at random cells 1..20 at least 7 (for room #1) or 4 cells from other rooms and never (1, 1);
  `carveRoom1` each, then `generateRecursiveBacktracker(0, 0)`.
- Room kinds by index: 0 plain room; 1 exit (level 1) / boss (level 2); 2-3 collapse / mush room; 4 fountain /
  vault; 5-6 spawner chests. Room position `min + (dx * 5 - 4, 0, dz * 5 - 4)`.
- Level 1 adds level 2 below at `(centre, minY - 10)`.
- Dead ends (`nextInt(8)`: 1 chest, 2 tripwire or trapped 50/50, 3 torches, 4 fountain, 5 lava fountain, 6 painting,
  7 roots (level 1) / shrooms (level 2), else plain) and straight corridors with walls on both sides
  (`nextInt(5)`: 1 fence, 2 iron fence, 4 roots/shrooms, else none) at `min + (dx * 5 + 1, 0, dz * 5 + 1)`.
- `postProcess`: level 2 encases its box in bedrock (y -1..6); air box (1..d, 1..4); mazestone ceiling at y 5
  (level 1 only replaces existing blocks); mazestone mosaic floor at y 0 (a one-layer `generateBox`: every block is boundary, so all mosaic; `stone` is only
  the unused inside fill); then
  `maze.copyToStructure(world, 1, 2, 1)`.

### 7.3 Chest loot (owned here)

- `twilightforest:labyrinth_dead_end`: 4 rolls of useless (w25), stick 1-6, coal 1-6, arrow 1-6, mushroom stew,
  maze wafer 1-5, paper 1-6, leather 1-2 (w75 each); 2 rolls of milk bucket, paper 1-5, iron ingot 1-3, ironwood
  ingot 1-4, firefly 1-2, charm of keeping 1; 1 roll of golden apple, red thread 5-18, blaze rod, book enchanted
  with `twilightforest:fire_react`, steeleaf ingot 1-4.
- `twilightforest:labyrinth_room`: 4 rolls of iron ingot 1-4, gunpowder 1-4, ironwood ingot 1-4, milk bucket, maze
  wafer 1-12, firefly 1-5; 2 rolls of steeleaf ingot 1-6, charm of keeping 1, steeleaf helmet / chestplate /
  leggings / boots / pickaxe, ironwood chestplate, ironwood sword, red thread 5-15; 1 roll of maze map focus, maze
  map, TNT 1-3, fire_react book, strong healing potion.
- `twilightforest:labyrinth_vault`: 4 rolls of iron ingot 1-9, emerald 1-5, ironwood ingot 1-9, maze wafer 1-12,
  strong regeneration / healing / swiftness potions; 2 rolls of bow (Infinity, Punch II), bow (Flame, Power III),
  fire_react book, steeleaf ingot 1-12, steeleaf shovel (Efficiency IV, Unbreaking II), steeleaf axe (Efficiency V),
  steeleaf chestplate (Protection III), boots (Protection II), leggings (Fire Protection IV), helmet (Respiration
  III); 1 roll of emerald block, ender chest, steeleaf pickaxe (Efficiency IV, Silk Touch), steeleaf sword
  (Knockback II, Sharpness IV), steeleaf sword (Bane of Arthropods V, Fire Aspect II).
- `twilightforest:labyrinth_vault_jackpot`: the vault table plus `twilightforest:mazebreaker_pickaxe` (Efficiency
  IV, Fortune II, Unbreaking III).

Chests are placed with `loot::placeContainer` (vanilla chest / trapped chest, F10).

## 8. Strings and decoration

Lang keys owned: `block.twilightforest.maze_slime_block`, `entity.twilightforest.{maze_slime,minoshroom,minotaur}`,
`item.twilightforest.maze_slime_ball` "Maze Slimeball", the three spawn eggs and the 11 subtitles above. Map
decoration `twilightforest:labyrinth`. Structure type id `twilightforest:labyrinth` and structure id
`twilightforest:labyrinth` are owned here.

## 9. Bedrock adaptation notes

- Ground slam targets players only and uses the player's `onGround` state as seen by the server.
- The charge breaks blocks only with the `mobGriefing` gamerule; blocks with block entities (chests) survive.
- Slime size scaling: Bedrock slimes scale through the `minecraft:scale` / variant components; the maze slime
  uses the same sizes 1, 2, 4 with 3 x vanilla health.
- Mazestone hardness 100 makes the 16-durability penalty the main deterrent; the event hook is
  `interactions::onDestroy` (R8).

## 10. Selftests (`labyrinth.*`, dx 2300-2399)

`labyrinth.minoshroom_stats` (120 HP, diamond minotaur axe, no drop), `labyrinth.ground_slam` (player within 7.5
takes attack * 0.5 slam damage), `labyrinth.minotaur_axe_roll`, `labyrinth.maze_slime_health` (3 x size^2),
`labyrinth.slime_block_sticky`, `labyrinth.tool_penalty` (16 durability on mazestone), `labyrinth.recipes`,
`labyrinth.maze_layout` (golden volume hash of a seeded maze level, R19), `labyrinth.spawner_height_rule`.
