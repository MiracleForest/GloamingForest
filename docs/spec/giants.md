# giants (U29): Beanstalk, cloud castle and giants

Behaviour spec of unit **U29** (`giants`), written by F15 from The Twilight Forest 4.8 (`9e40a757`). Java paths are
relative to `src/main/java/twilightforest/`; data paths to `src/generated/resources/`.

| Unit fact | Value |
|---|---|
| Selftests | `giants.*`, testPos dx `2900-2999` (R12) |
| World data | `gf:giants:*`: `gf:giants:growers` = list of active beanstalk growers (dimension, pos) so growth resumes after a reload; per-grower state lives in BlockData (key `grower`) |
| Net channels | `giants.*` (optional: `giants.rumble` camera shake start / stop for companions; cloud precipitation is computed client side from blocks) |
| Contracts | landmark, structure, families+blockevents, item, loot, entity, client.render (+ fx, progression, combat by string) |
| Config | section `giants` (R13), 3 keys (section 9) |

**Tag namespaces.** Tags are written `#<ns>:<path>`. `#twilightforest:` tags and the `#c:` / `#neoforge:` tags that
owners.tsv lists (`tag` rows) are TF data (`data/<ns>/tags/**`) imported by F1; `#minecraft:` tags are vanilla Java
1.21.1 tags (TF adds members to some). Bedrock has no Java tags: every tag here resolves through `gf::tags` (F9,
`src/feature/tags/Tags.h`), and units add members only through union fragments (R6). TF's damage-type and tool-tier
tags are in the `neoforge` namespace (`#neoforge:is_physical`, `#neoforge:is_magic`, `#neoforge:is_environment`,
`#neoforge:needs_wood_tool`), never `#c:`.

## 1. Structure `twilightforest:giant_house`

Sources: `world/components/structures/type/GiantHouseStructure.java`; the pieces are U27's (`twilightforest:TFClCa`
CloudCastleComponent, `twilightforest:TFCloud`, `twilightforest:TFClTr`; see `docs/spec/caves.md` section 1.1), used here
by piece-type id through F7 (R2).

| Field | Value |
|---|---|
| Structure type | `twilightforest:giant_house` (`GiantHouseStructure extends ProgressionStructure implements ConfigurableSpawns`) |
| Structure set | `twilightforest:giant_house`, placement `twilightforest:landmark_grid` (`forceStructureForCenters`), weight 1 |
| Biomes | `#twilightforest:valid_giant_house_biomes` = `twilightforest:highlands` (U1 tag): every highlands landmark centre gets both this cloud castle and U27's troll cave below it |
| Step | `top_layer_modification`, terrain adjustment `none` |
| Progression lock | `twilightforest:progress_merge` |
| Hint | `twilightforest:kobold` with book `twilightforest.book.trollcave` ("Notes on the Highlands", 3 pages) |
| Decoration clearance | chunk clearance radius 1, surface decorations true, underground decorations true, vegetation true, adjust elevation false |
| Centred in chunk | **false** |
| Controlled spawns label 0 | `twilightforest:giant_miner` 10 1-1, `twilightforest:armored_giant` 10 1-1 |
| Spawn overrides | every mob category empty over the structure box |
| Map icon | none; not in `#twilightforest:landmark` |

Start position: `x = chunkX x 16`, `z = chunkZ x 16` (not centred), y = sea level (0); piece random
`RandomSource.create(seed + chunkX x 25117 + chunkZ x 151121)`. First piece: `CloudCastleComponent(1, x, y + 168, z)`
(type id `twilightforest:TFClCa`, U27), rounded to multiples of 4; its box starts 4 lower (minY 164): fluffy cloud slab y 164..167, castle walls from y 168, the `TFCloud` at minY + 3 = 167, plus the two
`TFClTr` cloud trees (all U27 pieces). The castle places one giant miner and one armored giant (persistent). The
structure is conquerable (`ConquerableStructure`) but has no boss; F6 handles the lock and hint.

Golden hash `giants.cloud_castle_hash` (R19): force-start the structure at a fixed seed and hash the castle volume
(reviewed once with screenshots).

## 2. Blocks

Sources: `init/TFBlocks.java`, `block/UberousSoilBlock.java`, `block/GrowingBeanstalkBlock.java`,
`block/entity/GrowingBeanstalkBlockEntity.java`, `block/SpecialStemLeavesBlock.java`, `block/CloudBlock.java`,
`block/WispyCloudBlock.java`, `block/GiantBlock.java`, `block/GiantLeavesBlock.java`, `asmhooks/BlockHooks.java`,
`client/event/CloudEvents.java`, `client/event/ColorHandler.java`, `client/model/block/giantblock/GiantBlockModel.java`,
`data/tags/BlockTagGenerator.java`.

| Id | Class | Properties | Tags |
|---|---|---|---|
| `twilightforest:uberous_soil` | UberousSoilBlock (BonemealableBlock) | DIRT map colour, gravel sound, strength 0.6; shape 16 x 15 x 16 (used for light occlusion) | `#minecraft:dirt`, `#minecraft:mushroom_grow_block`, `#minecraft:small_dripleaf_placeable`, mineable/shovel |
| `twilightforest:huge_stalk` | RotatedPillarBlock (`axis`) | PLANT, BASS, stem sound, strength 1.5 / 3, ignited by lava | mineable/axe |
| `twilightforest:beanstalk_leaves` | SpecialStemLeavesBlock (`distance` 1..7, `persistent`, `waterlogged`) | PLANT, azalea leaves sound, strength 0.2, random ticks, no occlusion, push DESTROY, not suffocating / view blocking / conductor, ignited by lava | `#minecraft:leaves`, mineable/hoe |
| `twilightforest:beanstalk_grower` | GrowingBeanstalkBlock + block entity | no collision, empty shape, no loot, no occlusion, no terrain particles, strength -1 / 6000000 (unbreakable) | `#c:relocation_not_supported` |
| `twilightforest:wispy_cloud` | WispyCloudBlock | SNOW map colour, HAT, wool sound, strength 0.3 / 0, no occlusion, push DESTROY, **replaceable**, force solid off; collision 16 x 14 x 16; light block 0; faces against another wispy cloud are culled; precipitation NONE; no random ticks | `#twilightforest:clouds`, `#minecraft:dampens_vibrations` |
| `twilightforest:fluffy_cloud` | CloudBlock (precipitation dynamic) | ICE, HAT, wool, strength 0.8 / 0, push DESTROY, random ticks; light block 1 | same |
| `twilightforest:rainy_cloud` | CloudBlock (RAIN) | as fluffy | same |
| `twilightforest:snowy_cloud` | CloudBlock (SNOW) | as fluffy | same |
| `twilightforest:giant_cobblestone` | GiantBlock | copy of cobblestone, strength 128 / 50, requires tool, push BLOCK | mineable/pickaxe, `#c:relocation_not_supported`, oreberry `#twilightforest:oreberry_bushes_survive` member (U6 tag) |
| `twilightforest:giant_log` | GiantBlock | copy of oak planks, strength 128 / 30, requires tool, push BLOCK | **mineable/pickaxe** (not axe), relocation not supported |
| `twilightforest:giant_leaves` | GiantLeavesBlock | copy of oak leaves, azalea leaves sound, strength 12.8 / 15 (`0.2 x 64`), requires tool, no occlusion, push BLOCK, no mob spawning, not suffocating / view blocking / conductor; support shape empty | mineable/pickaxe, portal decoration tag member (F4), relocation not supported |
| `twilightforest:giant_obsidian` | GiantBlock | copy of obsidian, strength 204800 / 8192000 (`50 x 64 x 64`, `2000 x 64 x 64`), requires tool, push BLOCK, no mob spawning | mineable/pickaxe, `#minecraft:dragon_immune`, block-and-chain never breaks (U25 tag), relocation not supported |

Block items: `twilightforest:uberous_soil`, `twilightforest:huge_stalk`, `twilightforest:beanstalk_leaves`,
`twilightforest:wispy_cloud`, `twilightforest:fluffy_cloud`, `twilightforest:rainy_cloud`, `twilightforest:snowy_cloud`,
`twilightforest:giant_cobblestone`, `twilightforest:giant_log`, `twilightforest:giant_leaves`,
`twilightforest:giant_obsidian` (11; the grower has no item).

Colours (client, F12 `tint::registerBlock`): giant leaves = biome foliage colour (item: default foliage);
beanstalk leaves = evergreen foliage colour (0x619961). Render types (model JSON): `wispy_cloud` translucent,
`giant_leaves` cutout-mipped, beanstalk leaves as vanilla leaves; the other clouds are solid.

### 2.1 Uberous soil

- `getStateForPlacement`: if the block above is solid and not a bonemealable plant (uberous soil itself excepted) the
  placed block becomes `minecraft:dirt`.
- `canSustainPlant`: only from the Y axis; any `#minecraft:crops` plant is sustained.
- `onPlace` (new block) with a non-air block above -> run the neighbour logic for the block above.
- Neighbour above changes: if the new block above is not a bonemealable plant (or is uberous soil): when solid the soil
  turns to dirt (`FarmBlock.turnToDirt`), else nothing. If it is a bonemealable plant: the soil becomes farmland
  (moisture 7) for `#minecraft:crops`, mycelium for mushrooms, grass block for bushes, moss block for moss, else dirt
  (entities pushed up); a `twilightforest:mushgloom` (U6 block) grows directly with `growMushroom` (no bonemeal); others
  get the growth particles (level event 1505, data 15) and, on the next server tick, **15 bone meal applications** with a
  fake player.
- Neighbour below changes to a bonemealable block: the soil becomes dirt and the plant below gets 15 bone meals next tick.
- Bone meal on the soil (`isBonemealSuccess` always true): spreads to one shuffled horizontal neighbour that is dirt /
  farmland (not uberous soil) with a non-solid block above, or one block up / down of it with the same rule; the new
  soil immediately runs the plant logic for its block above.
- Client `animateTick`: 1 in 5, when the local player holds `twilightforest:magic_beans`, 2 `minecraft:happy_villager`
  particles at a random point 1.25 above.
- Loot `twilightforest:blocks/uberous_soil`: `minecraft:dirt` (survives explosion). Generated by U27's troll cave
  garden and the `uberous_soil_patch_*` features (U2 / U27 data).

### 2.2 Magic beans and the beanstalk

`twilightforest:magic_beans` (`item/MagicBeansItem.java`, plain item): `useOn` with `maxY = max(clickedY + 100, 175)`:
if the clicked block is uberous soil, `y < maxY` and the block above is air: server shrinks the stack by 1, places
`beanstalk_grower` above, plays `block.twilightforest.beanstalk.grow` (BLOCKS, volume 4, pitch 1) at the soil, awards
the item-used stat and (fallback) the `use_beans` criterion of `twilightforest:beanstalk` (U14; fire it with
`progression::trigger`); returns SUCCESS on both sides, else PASS. Magic beans come from troll loot (U27) and beanstalk
leaves.

Beanstalk grower (block entity, `GrowingBeanstalkBlockEntity.tick`, every tick):

- Tick 1 (initialise): `nextLeafY = y + 10 + rand(10)`, `yOffset = rand(100)`, `cScale = r x 0.25 + 0.125`, `rScale = r x
  0.25 + 0.125`, `maxY = max(y + 100, 175)`.
- Client, ticks < 100: 20 `minecraft:block` dust particles per tick, each at `(x + r + rand(5) x +-1, y, z + r + rand(5)
  x +-1)` using the solid block below that point. Camera shake (`ClientGameEvents`, when `firstPersonEffects`): while
  `ticker < 110` (`isBeanstalkRumbling`) a player within 16 blocks gets yaw `+(r - 0.5) x I` and pitch
  `+(r x 2.5 - 1.25) x I` per tick, `I = 1 - dist^2 / 256`.
- Server, ticks > 100 and even: one layer at `layerY = y + layer` while `isAreaClearEnough` and `layerY < maxY`:
  base `radius = 4 + sin((y + yOffset) x rScale) x 3`, `x = posX - sin((y + yOffset) x cScale) x radius`, `z = posZ -
  cos((y + yOffset) x cScale) x radius` (uses the grower's own y: the spiral axis is fixed); layer `radius = 5 +
  sin((layerY + yOffset) x rScale) x 2.5`, centre `cx = x + sin((layerY + yOffset) x cScale) x radius`, `cz = z +
  cos(...) x radius`; `thickness = 2.5`, scaled by `(maxY - layerY) / 5` in the last 5 layers. For every column of the
  bounding square `[floor(x - radius - t), ceil(x + radius + t))` with `d^2 = (dx - cx)^2 + (dz - cz)^2 < t^2`: place
  stalk at layerY (counting blocked columns), at layerY + 1 if `d^2 < t`, at layerY + 2 if `d^2 < t / 2`. Placement
  (`tryToPlaceStalk`): into air, replaceable blocks (not the grower), leaves or fluffy cloud -> `huge_stalk`; above y 150
  also removes wispy / fluffy cloud blocks in the 7 blocks above (0..6); a blocked non-stalk block at the layer counts
  `blocksSkipped`, and 15 skips in one layer stop the growth. Leaves: when `layerY == nextLeafY` a leaf blob at
  `(int)(x + sin(...) x (radius +- t), layerY, (int)(z + cos(...) x (radius +- t)))` (+ when nextLeafY was even): a
  stalk block at the centre, 3 x 3 leaf squares above and below (distance `|dx| + |dz| + 1`) and a 5 x 5 square without
  corners at the centre level (distance `max(|dx| + |dz|, 1)`), leaves only into air / leaves (`UPDATE_CLIENTS`);
  `nextLeafY = layerY + 5 + rand(10)`, bumped by 1 if it has the same parity as before. `layer++`.
- When blocked or at maxY: the grower becomes `huge_stalk` and the block entity is removed.
- Saved: `ticker`, `layer`, `isAreaClearEnough`, `nextLeafY`, `beardifierGroundDelta` (= yOffset), `cScale`, `rScale`,
  `maxY`, `blocksSkipped`.

`twilightforest:beanstalk_leaves` (`SpecialStemLeavesBlock`): vanilla leaves whose distance is measured from
`twilightforest:huge_stalk` (stem predicate) instead of `#minecraft:logs`; the scheduled tick recomputes the distance
(min over the 6 neighbours: stalk 0, leaves their distance, else 7) and random ticks decay non-persistent leaves at
distance 7 (vanilla). Loot `twilightforest:blocks/beanstalk_leaves`: shears or Silk Touch -> itself, else
`twilightforest:magic_beans` with Fortune chances 2.5 / 2.78 / 3.125 / 4.17 / 10 % (survives explosion); plus (not
shears / silk) 1-2 sticks with chances 2 / 2.22 / 2.5 / 3.33 / 10 % (explosion decay). `twilightforest:blocks/huge_stalk`
drops itself.

### 2.3 Clouds

Common (`CloudBlock`): `fallOn` -> fall damage x 0.1; landing (server `addLandingEffects`): `n = clamp(fallDistance x 2,
8, 40)` `twilightforest:cloud_puff` particles at feet level, horizontal offset `(r - 0.5) x width x 2.5`, speed `offset x
0.0035 x n`, y speed `0.0005 x n`, sent to players tracking the chunk; `stepOn` and running effects: cloud puffs from
`addEntityMovementParticles` (half the time; only if the entity moves or 1/20, on even ticks, not spectators): at feet
`+(r - 0.5) x width`, velocity `(-0.5 x motion.x, 0.015, -0.5 x motion.z)` (x2 spread / y speed when jumping); a living
entity jumping off a cloud spawns 12 such puffs (client, `EntityEvents.addCloudJumpParticles`); hit effects 1/2 chance one
puff on the hit face (speed 0.01 outwards) and destroy effects up to 4 x 4 x 4 puffs (2/3 chance per slice) instead of
block-dust (`ClientRegistrationEvents` client extensions). Precipitation: `getCurrentPrecipitation(pos, rainLevel)` =
(block precipitation, 1.0) for rainy / snowy / wispy (NONE), or for the fluffy cloud (biome precipitation at pos,
rainLevel) when it rains, else (NONE, 0).

Random tick (fluffy, rainy, snowy; skipped when the area is not loaded or `common.cloudBlockPrecipitationDistance` = 0):
with a positive level and RAIN or SNOW, `highestRainyBlock = y - 1`; for `y'` from `y - 1` down while
`y' > y - distance` (that is `distance - 1` blocks, y - 1 .. y - distance + 1): if the block at `y'` is not
MOTION_BLOCKING-opaque, `highestRainyBlock = y' - 1`, else stop. So `highestRainyBlock` is the first opaque block
below the cloud, or `y - distance` when none is found in the scanned range (one below the last scanned block; that
block itself is not tested, so ground exactly at `y - distance` is snowed on and gets `handlePrecipitation`; with
distance 1 nothing is scanned and it stays `y - 1`). Strictly above the minimum build height: SNOW -> if the
`snowAccumulationHeight` gamerule > 0 and snow can be placed at `highestRainyBlock + 1` (air or snow, block light < 10,
snow survives) add a snow layer (up to `min(rule, 8)`) or place one; then (rain and snow) call `handlePrecipitation` of
the block at `highestRainyBlock` (cauldrons fill).

`isRainingAt` hook (`BlockHooks.isRainingAt`, wraps `Level.isRainingAt`): when the level is not raining at pos and the
common distance > 0 (pos's chunk not loaded -> not raining, no chunk load), scan up y = pos.y .. pos.y + distance - 1
in that chunk, testing each block in this order: a cloud whose current precipitation is RAIN -> raining (entities get
wet, fire is put out, farmland is moistened, ...); an opaque motion-blocking block -> not raining; past the range ->
not raining. Bedrock: no single hook; the effects that matter (fire extinguishing, entity wetness for enderman /
blaze damage, farmland moisture, cauldron filling) are applied by U29 through F11 random ticks and F10 hooks on the
blocks / entities under rainy clouds (listed deviation for anything else).

Client rendering (`CloudEvents`, client config `cloudBlockPrecipitationDistance`, -1 = use the common value, 0 = off):
every 10 game ticks collect cloud blocks within `r` = 10 (fancy) / 5 (fast) columns of the camera and y from `camY -
r` to `camY + distance + r - 1` whose precipitation is not NONE and that have at least one non-opaque block below;
the stop y is the lowest non-opaque y of the same downward scan as the random tick (`y - 1` while `> y - distance`,
stopping at the first opaque block), i.e. one block higher than the server's `highestRainyBlock`. Each tick: rain splash particles (`minecraft:rain`, or `minecraft:smoke` on lava, magma,
lit campfires) at up to 100 random columns (50 with decreased particles) within 10 blocks of the camera, plus the
vanilla rain sounds (`weather.rain` 0.2 / 1.0, `weather.rain.above` 0.1 / 0.5). Each frame after weather: vanilla-style
rain / snow sheets per column from the cloud down to the stop y (alpha `((1 - d^2) x 0.5 + 0.5) x level`, d = distance
/ r), rain texture scrolling, snow texture drifting as vanilla. Bedrock: client mod draws the sheets with F12
`render::onWorld`; vanilla clients get `minecraft:rain_splash_particle` / falling snow particles under rainy and
snowy clouds sent by the server at a reduced rate (deviation).

Loot: `twilightforest:blocks/fluffy_cloud`, `twilightforest:blocks/rainy_cloud`, `twilightforest:blocks/snowy_cloud`
drop themselves (survives explosion); `twilightforest:blocks/wispy_cloud` drops itself only with Silk Touch.

### 2.4 Giant blocks (4 x 4 x 4)

`GiantBlock.getVolume(pos)` = the aligned cell from `(x & ~3, y & ~3, z & ~3)` to `(x | 3, y | 3, z | 3)`.

- Placement: refused (null state) unless all 64 positions are replaceable for the placing context; after placing, the
  server fills the whole cell with the block (`setPlacedBy`).
- Removal (`onRemove`): unless already self-destructing, if the cell is no longer completely filled, every other block of
  the same type in the cell is destroyed **without drops**: breaking one block removes the whole giant block and drops
  one item.
- Render (`GiantBlockModel`): each face shows a 4 x 4 pixel sub-square of the 16 x 16 texture chosen from the position
  (`calculateOffset` with the `magicOffsetFromDir` shifts: DOWN (0,0,2), UP (0,0,-1), N/S (0,1,0), W/E (0,1,-1); north
  and east mirrored), so the 64 blocks tile one big texture; a face is drawn only when the neighbour in that direction
  lies outside the cell (inner faces are culled even if the neighbour is another block). Bedrock: client mod = F12
  `registerBlockRenderer` with per-position UVs; vanilla clients cannot vary UVs by position and see the whole texture
  on every block (deviation) unless F1 adds a hidden position state (R1 allows adding states).
- Outline (client, `renderGiantBlockOutlines`): while the main hand holds a giant pickaxe or a giant block item, the
  vanilla block highlight is replaced by a 4 x 4 x 4 box (black, alpha 0.45) at the aligned cell of the targeted block.
- Loot: each giant block drops itself (`twilightforest:blocks/giant_cobblestone`, `twilightforest:blocks/giant_log`,
  `twilightforest:blocks/giant_leaves`, `twilightforest:blocks/giant_obsidian`, survives explosion).

## 3. Items

Sources: `item/GiantPickItem.java`, `item/GiantSwordItem.java`, `util/TFToolMaterials.java`, `events/ToolEvents.java`,
`components/entity/GiantPickaxeMiningAttachment.java`, `loot/conditions/GiantPickUsedCondition.java`,
`loot/modifiers/GiantToolGroupingModifier.java`, `events/RegistrationEvents.java` (conversions).

Tier `TFToolMaterials.GIANT`: incorrect blocks `block/#twilightforest:incorrect_for_giant_tool`, durability 1024,
speed 4.0, attack bonus 1.0, enchantability 5, repair `#twilightforest:repairs_giant_tools` (= giant cobblestone; U7 tag).

| Id | Class | Attributes (main hand) | Behaviour |
|---|---|---|---|
| `twilightforest:giant_pickaxe` | GiantPickItem (PickaxeItem) | attack damage 8 + 1 (tier) -> 10 total, attack speed -3.5 (0.5 per s), block and entity interaction range +2.5 (`twilightforest:reach_modifier`, `twilightforest:range_modifier`) | destroy speed = pickaxe speed x 64 on `GiantBlock`s and x 64 again on giant obsidian; tooltip `item.twilightforest.giant_pickaxe.desc` "Breaks giant blocks" (gray); 4 x 4 x 4 mining (below); item tags `#minecraft:pickaxes`, `#minecraft:cluster_max_harvestables` |
| `twilightforest:giant_sword` | GiantSwordItem (SwordItem) | attack damage 10 + 1 -> 12 total, speed -3.5, interaction ranges +2.5 | repairable with ironwood ingot or giant cobblestone; item tag `#minecraft:swords` |

**Giant pickaxe mining** (attachment `twilightforest:giant_pickaxe_mining`: `mining` game time, `breaking`,
`giantBlockConversion`; never saved):

1. `canAttackBlock` (each attack tick with a giant pickaxe in the main hand): when `mining != gameTime`, set `mining =
   gameTime`, `breaking = false`, `giantBlockConversion = 0`.
2. Block break by a server player holding a giant pickaxe who can harvest the block, with `mining == gameTime` and not
   `breaking`: set `breaking`; compute the block's drops with the tool; if the first drop is a block item whose block
   is a conversion source (`minecraft:cobblestone` -> giant cobblestone, `minecraft:oak_log` -> giant log,
   `minecraft:oak_leaves` -> giant leaves, `minecraft:obsidian` -> giant obsidian) **and** all 64 blocks of the cell are
   the broken block's type, `giantBlockConversion = 64`, else 0. Cancel the original break, play the destroy effect and
   destroy the block through the game mode, then destroy every other block of the cell that has the same block type
   (each with destroy particles and its own loot / tool damage); clear `breaking`.
3. Loot modifier `twilightforest:giant_block_grouping` (condition `twilightforest:giant_pick_used` with entity `this`:
   the player's `mining == gameTime` and `giantBlockConversion > 0`): for a drop list whose first item is a convertible
   block item, decrement the counter; the 64th (first) call returns one giant block item, the other 63 return nothing.
   So mining a full cell of stone with a giant pickaxe yields one `giant_cobblestone`.

Bedrock: the attachment is per-player state in the U29 module (keyed by unique id, R16); the break hook is
`interactions::onDestroy({.item = "twilightforest:giant_pickaxe"})`; the modifier and condition are registered with F10
`loot::addGlobalModifier` / `loot::registerCondition`; the reach bonus uses F9 attributes (vanilla Bedrock has no
interaction-range attribute: companions extend the pick ray locally, the server validates with `movement::validate`).

## 4. Recipes and recipe advancements

| Recipe | Type | Input | Result | Advancement (criterion) |
|---|---|---|---|---|
| `twilightforest:equipment/giant_pickaxe` | shaped (equipment) | `###`, ` X `, ` X ` (# giant cobblestone, X giant log) | giant_pickaxe | `twilightforest:recipes/tools/equipment/giant_pickaxe` (has giant cobblestone) |
| `twilightforest:equipment/giant_sword` | shaped (equipment) | `#`, `#`, `X` | giant_sword | `twilightforest:recipes/tools/equipment/giant_sword` (has giant cobblestone) |
| `twilightforest:giant_cobblestone_to_cobblestone` | shapeless (building) | giant cobblestone | 64 cobblestone | `twilightforest:recipes/building_blocks/giant_cobblestone_to_cobblestone` |
| `twilightforest:giant_log_to_oak_log` | shapeless | giant log | 64 oak log | `twilightforest:recipes/building_blocks/giant_log_to_oak_log` |
| `twilightforest:giant_leaves_to_oak_leaves` | shapeless | giant leaves | 64 oak leaves | `twilightforest:recipes/building_blocks/giant_leaves_to_oak_leaves` |
| `twilightforest:giant_obsidian_to_obsidian` | shapeless | giant obsidian | 64 obsidian | `twilightforest:recipes/building_blocks/giant_obsidian_to_obsidian` |
| `twilightforest:rainy_cloud` | shaped (building) | `ccc`, `cbc`, `ccc` (c fluffy cloud, b water bucket) | 8 rainy cloud (bucket returned) | `twilightforest:recipes/building_blocks/rainy_cloud` (has fluffy cloud) |
| `twilightforest:snowy_cloud` | shaped | same with a powder snow bucket | 8 snowy cloud | `twilightforest:recipes/building_blocks/snowy_cloud` (has fluffy cloud) |

All are vanilla crafting recipes owned by U29 (R4, owners.tsv); unlock advancements generated by F1 (R21).

## 5. Mobs

Sources: `entity/monster/GiantMiner.java`, `entity/monster/ArmoredGiant.java`, `client/renderer/entity/TFGiantRenderer.java`,
`init/TFEntities.java`, `events/RegistrationEvents.java`.

| Field | `twilightforest:giant_miner` | `twilightforest:armored_giant` (extends GiantMiner) |
|---|---|---|
| Size | 2.4 x 7.2 | 2.4 x 7.2 |
| Attributes | max health 80, speed 0.23, attack damage 2, follow range 40, step height 1.2 | same (`GiantMiner.registerAttributes`) |
| Equipment (`finalizeSpawn`) | main hand giant pickaxe | giant sword; iron helmet, chestplate, leggings, boots |
| Drop chances | 0 for every slot | 0 |
| Enchanting | `populateDefaultEquipmentEnchantments` runs but `enchantSpawnedWeapon` / `enchantSpawnedArmor` are no-ops (never enchanted) | same |
| Goals | 1 Float, 4 MeleeAttack 1.0 (no memory), 5 WaterAvoidingRandomStroll 1.0, 6 LookAtPlayer 8, 6 RandomLookAround; targets 1 HurtBy, 2 NearestAttackable Player | same |
| Attack | `doHurtTarget` uses `twilightforest:ant` (U17 damage type: "squashed like an ant"), damage = attack attribute incl. the weapon (pickaxe 2 + 9 = 11, sword 2 + 11 = 13) via F10 `combat::hurt` | same |
| Spawning | cluster max 1; `checkSpawnRules`: fewer than 5 `GiantMiner`s (armored giants included) in the box inflated (100, 10, 100); placement ON_GROUND, `MOTION_BLOCKING_NO_LEAVES`, block below in `block/#twilightforest:giants_spawnable_on` | same |
| Riding | cannot ride | same |
| Sounds | vanilla hostile defaults (no TF sound events) | same |
| Loot | `twilightforest:entities/giant_miner`: giant pickaxe, killed by player | `twilightforest:entities/armored_giant`: giant sword, killed by player |
| Egg | `twilightforest:giant_miner_spawn_egg` 0x211b52 / 0x9a9a9a | `twilightforest:armored_giant_spawn_egg` 0x239391 / 0x9a9a9a |

Render (`TFGiantRenderer`): vanilla player model (wide or slim) scaled x 4, shadow 1.8, humanoid armour layer, held items
through `GiantItemInHandLayer` (vanilla item-in-hand with the item moved `(+-1/16, 0, -0.5)` and scaled 0.25 so it fits
the hand). Skin: if `client.giantSkinUUIDs` is not empty, the profile at `abs((int) uuid.mostSignificantBits) % size`
(fetched once at config load on a worker thread; slim model when that skin is slim); else the **viewing player's own
skin** (and model). Bedrock: client mod renders the giant with the local player's skin texture (F12 entity renderer);
vanilla clients see the default Steve / Alex skin (README deviation). Progression: killing a giant miner is the `giant`
criterion of `twilightforest:giants` (U14; TF's `pickaxe` criterion actually checks magic beans: keep the data as is).
Both giants are criteria of `twilightforest:twilight_hunter`.

## 6. Particle `twilightforest:cloud_puff` (owned by U29)

`CloudPuffParticle` (sprites `minecraft:generic_7` .. `generic_0`, age-based): initial velocity `0.1 x random + given
speed`, friction 0.96, grey `1 - r x 0.3`, size x 0.875, lifetime `max(i x 2.5, 1)` with `i = (int)(8 / (r x 0.8 +
0.3))`, no physics, translucent sheet, size grows from 0 over the first 1/32 of the lifetime
(`quadSize x clamp((age + pt) / lifetime x 32, 0, 1)`). Emitted by clouds (section 2.3). Client mod: F12
`particles::registerNative`; vanilla: Bedrock particle JSON approximation in the RP.

## 7. Tags (owned by U29)

| Tag | Members | Consumers |
|---|---|---|
| `block/#twilightforest:clouds` | fluffy_cloud, wispy_cloud, rainy_cloud, snowy_cloud | `giants_spawnable_on`, `#minecraft:dampens_vibrations` |
| `block/#twilightforest:giants_spawnable_on` | `#twilightforest:clouds` | giant spawn placement |
| `block/#twilightforest:incorrect_for_giant_tool` | `#minecraft:incorrect_for_stone_tool` | giant tier (stone-tier harvest level) |

## 8. Sound event `block.twilightforest.beanstalk.grow` (owned by U29)

Subtitle `subtitles.twilightforest.block.beanstalk.grow` "Ground rumbles"; one long low rumble synthesized by F3
from `tools/sounds/recipes/giants.json` (never TF audio); played by magic beans at volume 4.

## 9. Config section `giants` (R13)

| Key | Default | Range | Meaning |
|---|---|---|---|
| `common.cloudBlockPrecipitationDistance` | 32 | 0..2^31-1 | blocks below a cloud checked by weather logic (random ticks, `isRainingAt`); 0 disables cloud weather |
| `client.cloudBlockPrecipitationDistance` | -1 | -1..2^31-1 | client render distance of cloud precipitation; -1 = use the common value, 0 = off |
| `client.giantSkinUUIDs` | `[]` | UUID strings (5 dash-separated groups) | skins giants use; empty = the viewer's skin |

Lang keys `config.twilightforest.cloud_precipitation` / `giant_skin_uuid_list` (+ `.tooltip`) belong to U29 (R9).
Client keys are per client (companion config file); the common key lives in the server config.

## 10. Bedrock adaptation notes

- Beanstalk grower: custom blocks have no block entities; the grower keeps its fields in BlockData (`grower`) and the
  position in `gf:giants:growers`; a U29 module tick drives every loaded grower (layer every 2 ticks after 100) and
  resumes after a reload. The rumble shake is sent to companions (`giants.rumble`) and run with F12 `camera::shake`.
- Cloud random ticks: F11 `blockevents::onRandomTick` on the three cloud ids.
- Giant blocks: placement through `interactions::onPlace` (refuse when the cell is not free, fill on the next tick,
  R16); cascade removal through `interactions::onDestroyed` / `blockevents::onRemoved`.
- Uberous soil plant logic: `blockevents::onNeighborChanged` + `onPlaced`; the 15 bone meals run one tick later through
  the vanilla bone-meal function on the plant (`interactions::nextTick`).
- Giants and the giant pick reach: see sections 3 and 5.

## 11. Acceptance (selftests `giants.*`, dx 2900-2999)

Per the unit record: magic beans on uberous soil place a grower and play the rumble; the beanstalk grows a spiral of
huge stalk with beanstalk leaves up to `max(y + 100, 175)` and the grower turns into stalk; beanstalk leaves decay away
from stalk and drop beans with Fortune chances; uberous soil turns into farmland under a crop and bone-meals it 15 times;
a giant pickaxe mines a full 4 x 4 x 4 cobblestone cell into one giant cobblestone, a mixed cell into normal drops;
placing a giant block fills the cell and breaking one block removes all 64 with one drop; rain falls (random tick fills a
cauldron, `isRainingAt` true) under a rainy cloud and snow layers form under a snowy cloud; falling on a cloud deals 10 %
fall damage; giant miner / armored giant spawn with their gear, deal `ant` damage and drop their tool only to players;
golden hash of the cloud castle (`giants.cloud_castle_hash`); client screenshots of the tiled giant block textures and
rain under a cloud.
