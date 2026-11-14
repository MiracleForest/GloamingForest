# Flora, critters and berries (U6 `flora`)

Behaviour spec of unit **U6** (wave W1), translated from the upstream 4.8 (`9e40a757`, MC 1.21.1) by F16. It is
the checklist the U6 worker implements and the A2 audit compares against. Sections 1-5 are the behaviour; Appendix A
lists every id U6 owns with upstream's registration, block states, loot, recipes, features, sounds and tags; Appendix B the
strings.

## 1. Scope

- **Ground flora**: `moss_patch`, `clover_patch`, `mayapple`, `fiddlehead`, `mushgloom`, `fallen_leaves`,
  `root` (block), `liveroot_block`, `root_strand`, `torchberry_plant`, `huge_lily_pad`, `huge_water_lily`, and the
  potted `potted_mayapple` / `potted_fiddlehead` / `potted_mushgloom`.
- **Berry bushes**: overworld-type `raspberry_bush`, `blueberry_bush`, `blackberry_bush`, `maloberry_bush`; dark
  tower `blightberry_bush`, `duskberry_bush`, `skyberry_bush`, `stingberry_bush`; oreberry bushes `iron_oreberry`,
  `gold_oreberry`, `copper_oreberry`, `essence_oreberry`; the berry items, `copper_nugget`, `torchberries`,
  `liveroot`.
- **Critters** (block + block entity + renderer): `firefly`, `cicada`, `moonworm`; **jars** `firefly_jar`,
  `cicada_jar`, `mason_jar` (block entities `jar`, `mason_jar`), `firefly_particle_spawner`.
- Worldgen features (16 configured / 17 placed, 8 feature types) for the above; 4 particles, 12 sound events, 2 stats,
  1 trigger (`kill_bug`), 2 damage types, data component `jar_lid`, config keys `client.silentCicadas`,
  `client.silentCicadasOnHead` (section `flora`), net channels `flora.fallen_leaf`, `flora.mason_jar_item`,
  `flora.moving_cicada_sound`.

Not U6: trollvidr / trollber / unripe trollber (U27, placed by U6's `underground_plants` type by state), uberous soil
(U29, string id `gloamingforest:uberous_soil`), the big mushgloom feature (`gloamingforest:big_mushgloom`, U27, grown
by mushgloom only through its configured feature), saplings and leaves (U3).

The recipes `gloamingforest:jeed/{jump_boost,mining_fatigue,night_vision,strength,wither}` in Appendix A are
JEED-compat data (`jeed:effect_provider`, condition `neoforge:mod_loaded jeed`): inert, see not-ported.md.

Contracts: families+blockevents, features, inventory, progression, data, client.render, fx.

## 2. Upstream sources

`init/GloamingBlocks.java`, `init/GloamingItems.java`, `init/GloamingBlockEntities.java`, `init/GloamingFeatures.java`,
`init/GloamingConfiguredFeatures.java`, `init/GloamingPlacedFeatures.java`, `init/GloamingParticleType.java`, `init/GloamingSounds.java`,
`init/GloamingSoundTypes.java` (JAR sound type), `init/GloamingStats.java`, `init/GloamingAdvancements.java`, `init/GloamingDamageTypes.java`,
`init/GloamingDataComponents.java`, `block/GloamingBushBlock.java`, `block/SnowLoggable.java`, `block/BerryBushBlock.java`,
`block/DarkTowerBerryBushBlock.java`, `block/OreBerryBlock.java`, `block/CritterBlock.java`, `block/CicadaBlock.java`,
`block/FireflyBlock.java`, `block/MoonwormBlock.java`, `block/entity/{Cicada,Firefly,Moonworm}BlockEntity.java`,
`block/JarBlock.java`, `block/CicadaJarBlock.java`, `block/FireflyJarBlock.java`, `block/MasonJarBlock.java`,
`block/entity/JarBlockEntity.java`, `block/entity/MasonJarBlockEntity.java`, `item/JarItem.java`,
`block/AbstractParticleSpawnerBlock.java`, `block/FireflySpawnerBlock.java`, `block/FallenLeavesBlock.java`,
`block/GloamingPlantBlock.java`, `block/PatchBlock.java`, `block/MossPatchBlock.java`, `block/MayappleBlock.java`,
`block/FiddleheadBlock.java`, `block/MushgloomBlock.java`, `block/HugeLilyPadBlock.java`, `item/HugeLilyPadItem.java`,
`block/HugeWaterLilyBlock.java`, `block/LiverootBlock.java`, `block/RootStrandBlock.java`,
`block/TorchberryPlantBlock.java`, `item/EssenceBerryItem.java`, `world/components/feature/{BerryBush,
EnchantedForestVines,FallenLeaves,HugeLilypad,HugeWaterLily,UndergroundPlant}Feature.java`,
`world/components/feature/config/BerryBushConfig.java`, `client/renderer/block/{Firefly,Cicada,Moonworm,Jar}Renderer.java`,
`client/model/entity/{Firefly,Cicada,Moonworm}Model.java`, `client/particle/FireflyParticle.java`,
`client/particle/LeafParticle.java`, `client/MovingCicadaSoundInstance.java`, `client/event/ColorHandler.java`,
`events/MiscEvents.java` (cicada on head), `network/{CreateMovingCicadaSound,SpawnFallenLeafFrom,SetMasonJarItem}Packet.java`,
`advancements/KillBugTrigger.java`, `dispenser/GloamingDispenserBehaviors.java`, `events/RegistrationEvents.java`.

## 3. Behaviour

### 3.1 Berry bushes (`GloamingBushBlock` base, snow-loggable)

States: `age` 0..3 (`AGE_3`, default 0) and `layers` 0..8 (`SnowLoggable.SNOW_LAYERS`, default 0). Properties
(Appendix A): sound GRASS (oreberries METAL), `destroyTime(0.4)`, random ticks, dynamic shape, `noOcclusion`; shade
brightness 1.0. Shapes: age 0 box(4,0,4, 12,8,12) without snow, box(4,0,4, 12,10,12) plus the snow layer box
(0,0,0, 16, 2*layers, 16) with snow; age 1 box(2,0,2, 14,12,14) without snow, box(2,0,2, 14,14,14) plus the snow
layer box with snow; age 2-3 full block (`SMALL_BUSH_SHAPES` / `MEDIUM_BUSH_SHAPES`). Push reaction DESTROY while age < 2.

- Survival: the block below must match the bush's "placed on" tag, or be the same bush at age >= 2 (bushes stack).
  Tags: overworld bushes `#gloamingforest:gf_berry_bushes_survive`; dark tower bushes
  `#gloamingforest:dark_tower_berry_bushes_survive`; oreberries `#gloamingforest:oreberry_bushes_survive`.
- Growth (random tick): if age < 3, with chance **1/20** and `canGrowAt` -> age + 1 (when reaching age 2 while
  snow-logged, the snow moves up: the block above becomes snow, or a snow-loggable above gets 1 layer). Then
  `meltSnow`: block light > 11 and 0 < layers < 8 -> drops the snow layers and clears them.
- `BerryBushBlock` (overworld + dark tower): after the base tick, if the bush column below is < 2 tall and with
  chance 1/20 and `canGrowAt`, `tryGrowUpwards`: with chance 1/3, when age >= 2, places a new age-0 bush above (air)
  or replaces a snow layer above (keeping its layers). `canGrowAt` additionally needs raw brightness >= **8**.
  Shears mine it at fixed progress **0.2**. Bone meal: valid if age < 2 or the block above is air/snow; always
  succeeds; age < 2 -> age = min(age + 1 + rand(2), 2), then `tryGrowUpwards`.
- `DarkTowerBerryBushBlock`: cannot grow while snow-logged; dies (destroyed with drops, scheduled 1 tick after a
  neighbour update) when the block below is in `#gloamingforest:dark_tower_berry_bushes_die`.
- `OreBerryBlock`: an entity (not an item entity) inside takes **1.0** damage of type `gloamingforest:oreberry`; at
  age >= 2 entities collide with box(0.001,0,0.001, 15.999,15.999,15.999); path type DANGER_OTHER, not pathfindable.
  Non-essence oreberries survive only below raw brightness **13** and grow only below **10**; `essence_oreberry`
  ignores light.
- Harvest: use on age 3 with an empty hand or any main-hand item other than snow layers (`useItemOn` passes to
  `useWithoutItem`; snow layers add a layer instead; sneaking with an item skips the block) -> rolls the bush's berry loot table
  (`gloamingforest:blocks/<bush>_berries`, e.g. `blocks/raspberry_bush_berries`, `blocks/iron_oreberry_bush_berries`,
  `blocks/essence_berry_bush_berries`; Appendix A; block loot context with an empty tool) straight into the player's inventory, sound
  `block.sweet_berry_bush.pick_berries` (volume 1, pitch 0.8 + rand*0.4), age -> 2, game event block_change.
- Snow: using snow layers on a bush adds one layer (max 8) if snow can survive there and the player would not be
  stuck; consumes one (not in creative), sound snow place, makes the dirt below snowy. Breaking a snow-logged bush
  without sneaking only removes the snow when the look ray hits below the snow surface (or turns the bush into the
  snow block, dropping the bush, when hitting above) (`SnowLoggable.handleBreakingLogic`).
- Items: berries are foods (Appendix A: nutrition / saturation / effects per berry); `essence_berry` right-click
  consumes one and spawns an XP orb worth **6..19** at the player's feet + 1.
- Worldgen (`BerryBushFeature`, config `{bush, generates_on, can_generate_snowy}`): needs `generates_on` below the
  origin; picks LARGE (weight 1), MEDIUM (2), SMALL (4), TINY (3). Each placed bush: age 3 with chance **0.2** else 2
  (TINY: age rand(4); SMALL ring: rand(4)); a target is skipped unless it is in `#gf_berry_bushes_replace`, not in
  `#minecraft:features_cannot_replace`, has no fluid, and (for age < 2) stands on `generates_on`. In snowy biomes
  (if allowed) bushes not on another bush get 1 snow layer and grown bushes get a snow block above. LARGE = 3x3 at
  y-2, taxicab < 4 in y-1..0 within +-2, 3x3 at y+1; MEDIUM = layers y-1..2 with radius min(2-dy, 2), corners
  included with 50%; SMALL = centre + 4 orthogonal neighbours at y-1..0 with 50%.
- Oreberry bushes generate with feature type `oreberry_bushes` = `UndergroundPlantFeature` with max count 1.

### 3.2 Ground plants

- `moss_patch` (`MossPatchBlock` < `PatchBlock`): instabreak, MOSS sound, no collision, ignited by lava; states north/east/south/west = the horizontal neighbour is the same patch block; shape = a 1-px-high
  plate whose inset on each unconnected side is 1 + ((seed >> {12,15,18,21}) & 3) px from `state.getSeed(pos)`
  (x+ uses bits 12, x- bits 15, z+ 18, z- 21); placeable only on a sturdy top face; 1/10 animate tick a `mycelium`
  particle at y+0.1. `clover_patch` is a plain `PatchBlock` (placeable like a bush, on dirt-type blocks).
- `mayapple` box(4,0,4, 13,6,13), `fiddlehead` box(3,0,3, 13,14,13) (GRASS sound, `replaceable`): bushes; fiddlehead
  and its potted form are tinted with the biome grass colour.
- `mushgloom` (`MushgloomBlock`, a MushroomBlock with feature `big_mushgloom`): light **3**, FUNGUS sound, box(2,0,2,
  14,8,14); survives on any sturdy top face or `gloamingforest:uberous_soil`; **cannot** be bone-mealed.
- `fallen_leaves` (`FallenLeavesBlock`): `layers` 1..8; shape box(0,0,0, 16, 2*layers, 16) (layer 1: 0.2 px), no
  collision; survives on a sturdy top face or above water; may be placed on water / ice when the block above has no
  fluid. Using fallen leaves on it adds a layer (up to 8, only on the top face when replacing the clicked block);
  replaceable by other blocks only at 1 layer; never replaced when liquid is below. Entities inside with layers > 2
  are slowed: `makeStuckInBlock` factor (1 - 0.05*(layers-2), 1, same). Moving living entities: 50% per tick a
  `gloamingforest:fallen_leaf` particle kicked up (client); the server sends `SpawnFallenLeafFromPacket(pos,
  motion)` to trackers. Ambient: 1/50 animate tick a leaf particle falls from up to 10 blocks above (the first leaves
  block above, if the sky is not visible). Leaf particle colour = oak-leaf biome colour +- rand(0x22)-0x11 per
  channel. Tinted with the foliage colour. Fuel/compost: Appendix A notes.
- `root` (plain block, `strength(2.0, 3.0)`, WOOD) and `liveroot_block` (`LiverootBlock`, light-green map): stripping
  a liveroot block with an axe turns it into `root`, drops **1 liveroot** (Fortune: x(max(0, rand(lvl+2)-1)+1),
  the ore-drops formula), plays `item.axe.strip`, costs 1 durability.
- `root_strand` (instabreak, HANGING_ROOTS, no collision, climbable `#minecraft:climbable`): hangs under a block in
  `#gloamingforest:plants_hang_on` or another root strand; shape box(2,0,2, 14,16,14). Bone meal (valid when the
  first non-strand block below is air/replaceable) extends the strand one block down.
- `torchberry_plant` (`TorchberryPlantBlock`): hangs under `#plants_hang_on`; state `has_torchberries`; light **7**
  with berries, **1** without; shape box(1,2,1, 15,16,15). Right-click with berries: removes them, sound
  `block.gloamingforest.torchberry.harvest`, drops 1 `torchberries` item entity at (x+0.5, y, z+0.5), awards stat
  `gloamingforest:torchberries_harvested`. Bone meal (only without berries) always adds berries.
- `huge_lily_pad` (`HugeLilyPadBlock`, a WaterlilyBlock, 2x2): states `facing` (horizontal) and `piece`
  (nw/ne/se/sw); shape box(0,0,0, 16,1,16); breaking any piece destroys the other three (no drops for them). Item
  `HugeLilyPadItem`: right-click on water/ice (source-only ray) places the 2x2 at the hit block +1 if the target,
  east, south and south-east are water or ice with air above and the player may build at all four; `facing` = the
  player's direction, pieces NW at the hit, NE east, SE south-east, SW south; sound `block.lily_pad.place`;
  `placed_block` triggers for all four. Tint 0x208030 in the world, 0x71C35C as item.
- `huge_water_lily` (WaterlilyBlock): shape box(1.6,1.6,1.6, 14.4,14.4,14.4).
- Potted plants: `potted_mayapple`, `potted_fiddlehead`, `potted_mushgloom` (`FlowerPotBlock` of vanilla
  `flower_pot`; `addPlant` registration).

### 3.3 Critters (`CritterBlock`: firefly, cicada, moonworm)

- Common: instabreak, no collision, no terrain particles, push reaction DESTROY, SLIME_BLOCK sound; state `facing`
  (6 dirs, default up; moonworm also `waterlogged`); shape 0.6x0.6x0.15 plate against the supporting face
  (e.g. up: AABB(0.2,0,0.2, 0.8,0.15,0.8)). Placement: the clicked face if the block behind supports its centre or
  is leaves, else the first nearest-looking direction that works. Breaks (to air) when its support goes.
- Equipable on the head (HEAD slot), dispensers equip them.
- Squishing: a projectile (whose type is not in `#gloamingforest:dont_kill_bugs`) or a falling block inside the
  critter removes it (to water if waterlogged), plays `block.gloamingforest.bug.squish` (1, 1), rolls the squish loot
  table (`gloamingforest:blocks/{firefly,cicada,moonworm}_squish`, Appendix A) at the block, and spawns 50 slime-block particles; if the
  projectile's owner is a server player: stat `gloamingforest:bugs_squished` +1 and trigger
  `gloamingforest:kill_bug` (with the bug's block state).
- Mason jar on a critter (empty jar, i.e. no stored item): firefly -> a `firefly_jar` item, cicada -> a `cicada_jar`
  item (keeping the mason jar's `jar_lid`), one jar consumed, the critter removed (cicada sound stopped).
- Firefly: light **15**. Client tick: if a player is within 16 blocks, 1/20 chance per tick of a `firefly` particle
  at a random point in the block; glow pulses (intensity +-0.05 per tick, pauses rand(50) ticks when dark); random
  yaw wiggle (delay 200 + rand(200), desired yaw rand(15) - rand(15), 1 degree per tick).
- Cicada: client tick: same yaw wiggle; singing cycle: wait 100 + rand(100) ticks, then sing for 100 ticks (1/5 chance
  per tick of a `note` particle in the block) starting with `block.gloamingforest.cicada` (volume 1, pitch
  1 + (rand - rand)*0.2) unless `silentCicadas`. Worn on the head, a looping cicada sound follows the wearer
  (server sends `CreateMovingCicadaSoundPacket(entityId)` to trackers when a cicada is equipped in HEAD; the
  client sound instance starts after 100 + rand(100) ticks and stops when unequipped) unless `silentCicadas` or
  `silentCicadasOnHead`.
- Moonworm: light **14**, `forceSolidOff`, waterloggable by sources only; flowing water destroys it (with drops),
  lava fizzes it away. Client: yaw starts at rand(4)*90 and slowly turns 1 degree per tick towards a new rand(4)*90
  target every 200 + rand(200) ticks.
- Renderers: upstream models (`GloamingModelLayers.FIREFLY/CICADA/MOONWORM`, textures `model/firefly-tiny.png`,
  `cicada-model.png`, `moonworm.png`) rotated to `facing`, plus a per-block random 0/90/180/270 rotation; the
  firefly renders a translucent glow layer with the glow intensity. Items render the same models
  (`BugModelAnimationHelper` animation).

### 3.4 Jars

- `JarBlock` base (`firefly_jar`: light 15; `cicada_jar`: random ticks; `mason_jar`: random ticks): JAR sound type,
  `strength(0.3, 3.0)`, `noOcclusion`, waterloggable; shape box(3,0,3, 13,14,13) + lid box(4,14,4, 12,16,12).
  Block entity keeps the **lid** item (default: gloaming oak log; cicada jar canopy log); the lid is the
  `gloamingforest:jar_lid` data component on the item (`JarItem` default instance sets the default lid). Lids are any
  item registered with `JarBlockEntity.addLid` (all overworld and upstream logs, stripped logs, cinder log, bamboo blocks,
  and **pumpkin only during Halloween week**). Using a registered lid item on the top 2 px (hit y >= 14/16) swaps the
  lid (no item consumed), sound `block.gloamingforest.jar.lid_swap`, wobble POSITIVE. Empty-handed (both hands) use:
  sound `block.gloamingforest.jar.wiggle`, wobble NEGATIVE. Drops keep the lid (dynamic drop `gloamingforest:jar_lid`);
  pick-block gives the jar with its components.
- `firefly_jar` / `cicada_jar`: sneak-use releases the critter: the jar is removed, drops the critter item and an
  empty `mason_jar` with the same lid. Animate tick: firefly jar 2 `firefly` particles around (0.5, 0.4, 0.5) +-0.2;
  cicada jar a `note` particle each tick and 1/75 chance of the cicada sound (unless `silentCicadas`).
- `mason_jar` (`MasonJarBlock`, `MasonJarBlockEntity`, 1 slot `ItemStackHandler`): item use (server):
  empty hand -> if empty: wiggle; sneaking: action-bar message "<item name> x<count>" + wiggle; otherwise takes the
  whole stack into the hand, sound `...jar.remove`. Holding an item -> if nothing fits: wiggle; else the item
  rotation is set from the player's yaw (16 segments, +180), stat `minecraft:used` for the item, inserts as much as
  fits (creative keeps the stack), sound `...jar.insert` with pitch 0.7 + 0.5*(inserted / max stack size). Only
  items that can go inside containers are valid. Comparator: lerpDiscrete(count / max stack, 0, 15). The jar emits
  the **light level of a stored block item** (dynamic light). Breaking keeps the contents in the item
  (`minecraft:container` component, tooltip "<name> x<count>"); placing the item restores contents and sets the item
  rotation from the player's yaw. Loot fill from a chest-type loot table (merge into the stored stack). Hoppers may
  insert/extract from the top only. Every change syncs `SetMasonJarItemPacket(pos, item, rotation)` to chunk
  trackers. Renderer: the stored item at (0.5, 0.4375, 0.5), scale 0.5, rotated by the segment; jar wobble animation
  like the decorated pot (amplitude 0.125).
- `firefly_particle_spawner` (`FireflySpawnerBlock`, light 15, JAR sound, shape box(3,0,3, 13,14,13), waterloggable):
  state `particle_radius` 1..10. Use with a firefly (not sneaking, radius < 10): radius + 1, consumes it, action bar
  `misc.gloamingforest.firefly_spawner_radius`; sneaking with radius > 1: radius - 1 and drops a firefly at y+1.
  Animate tick: ceil(radius/2) `particle_spawner_firefly` particles at random non-full blocks within the radius cube,
  plus 1/5 chance of a `firefly` particle in the jar.

### 3.5 Particles

| Particle | Provider | Behaviour |
|---|---|---|
| `gloamingforest:firefly` | StationaryProvider | no motion, life 10 + rand(21), colour r 0.5..0.75, g 0.6..0.85, b 0, size 0.2..0.3, translucent, full bright, alpha fades in/out over half-lives |
| `gloamingforest:wandering_firefly` | WanderingProvider | motion x0.1 plus random drift (+-3.9*r*r*0.1 horizontally, -0.25*r*r*0.1 vertically), life 30 + rand(21), removed where sky light <= 12 |
| `gloamingforest:particle_spawner_firefly` | ParticleSpawnerProvider | as wandering, without the sky-light check |
| `gloamingforest:fallen_leaf` | LeafParticle.Factory (`LeafParticleData` r,g,b) | life (160 + rand*30), opaque, full bright, drifts back towards its spawn x/z, falls with drag 0.7 and gravity 0.02, spins 5 deg/tick, two crossed quads, fades in/out over 20 ticks |

### 3.6 Features (Appendix A JSON)

Feature types: `berry_bush` (3.1), `enchanted_forest_vines` (for each column of the chunk: from y -10 to the max
MOTION_BLOCKING height of the 4 neighbours; 1/7 chance per air block in the `enchanted_forest` biome to place vines
on all acceptable non-down faces (not rainbow oak leaves); if all faces are logs/leaves an extra 1/8 chance; never
free-standing), `fallen_leaves` (at WORLD_SURFACE_WG on dirt or water: a pile of height 1..6 with concentric circles
of decreasing height (random -0..2), or a flat 5x5 scatter (1/3 per cell, y +-2) on water), `huge_lily_pad` (10
attempts within +-7 / +-3, needs 2x2 water with air above, random facing), `huge_water_lily` (4 attempts, air above
water), `underground_plants` (walks down from the origin to the build bottom; on solid blocks or 1/6 chance jumps
x/z by rand(4) - rand(4); places the configured state where it survives, outside landmark bounding boxes; trollvidr
has a 1/10 chance to become unripe trollber), `underground_plants_in_structure` (same, also inside landmarks),
`oreberry_bushes` (same, max 1 placed). Configured / placed features are in Appendix A (berry bush variants, lampposts
`firefly_lamppost` / `cicada_lamppost`, `flower_placer(_alt)`, `mushgloom_cluster`, `plant_roots`, etc.).

## 4. Bedrock adaptation notes

- Critters, jars and mason jars need per-block data: lid, stored item, rotation, glow/yaw are client-only animation
  state. Store lid / contents in BlockData (or the S3 container backend, `inventory::registerBlock("gloamingforest:mason_jar",
  {.slots = 1, ...})`), render the critter models and jar contents with `client::render::registerBlockRenderer`;
  vanilla clients see a static model (geometry from F2) and no contents.
- Projectile squish: `blockevents::onProjectileHit` / `onEntityInside` on the critter ids; falling blocks via
  `onEntityInside`. Trigger through `progression::trigger(player, "gloamingforest:kill_bug", {"block": id})` and the
  stat through `stats::award`.
- Snow-logging: families `snowloggable` (F11) supplies the `layers` state and the snow interactions; berry bush growth
  and harvest in `blockevents::onRandomTick` and `interactions::onUseOn`. Keep integer states as lists (age 0..3,
  layers 0..8, particle_radius 1..10) and watch the permutation budget (bush: 4 x 9 = 36 permutations).
- Mason jar dynamic light: Bedrock blocks cannot change light per instance without a state; add an integer light state
  (0..15) only if the budget allows, otherwise document light 0 (not-ported.md).
- Fallen leaf kick-up: the server sends `flora.fallen_leaf` to companions only; vanilla clients get no leaf particles
  (or a vanilla particle as best effort).
- Cicada on head: `flora.moving_cicada_sound` to companions; vanilla: a periodic `fx::soundTo` while worn (best effort).
- Huge lily pad: a 2x2 multi-block with the `piece` state, placed by the item's `use` (raycast on water); breaking one
  piece removes the rest via `blockevents::onRemoved`.
- Tints (grass, foliage, lily pad) through `client::tint::registerBlock`; vanilla clients get the textures' base
  colours.

## 5. Acceptance checklist (selftests `flora.*`, dx 600-699)

- Every block/item of Appendix A with upstream properties and loot (oreberry/berry tables, squish drops, jar drops keep the
  lid, mason jar keeps contents).
- Bush growth chances (1/20, upward 1/3), light gates (>= 8; oreberry < 10 growth / < 13 survival), bone meal,
  harvest to age 2 with the berry loot, dark tower bush death on the die tag, oreberry 1.0 damage.
- Critter placement/support, squish by projectile with stat + `kill_bug`, mason jar capture, jar release by sneaking.
- Mason jar insert/extract rules, comparator output, sounds and pitches; lid swap with any registered log.
- Fallen leaves layering, slow-down factor, torchberry harvest stat, liveroot stripping, root strand bone meal, huge
  lily pad 2x2 placement/break.
- Features place with upstream's counts on a fixed seed (golden volume hash per feature).

## Appendix A: Owned ids (generated inventory)

Every id this unit owns in `docs/spec/owners.tsv`, with the upstream 4.8 data the W1 worker must reproduce. Registration statements are upstream's own Java (`init/upstream*.java`), condensed (namespaces such as `MapColor.` dropped); loot, recipe, tag, advancement and data rows are summarised from upstream's generated JSON (`src/generated/resources`). Loot notation: `self` = drops the block itself when it survives the explosion; `alternatives(a → b)` = first matching child; `[if ...]` = conditions; `{...}` = functions. In summaries `mc:` is `minecraft:` and ids without a namespace are `gloamingforest:`.

| Kind | Count |
|---|---|
| `advancement` | 15 |
| `block` | 34 |
| `block_entity` | 5 |
| `block_item` | 25 |
| `config_key` | 2 |
| `config_section` | 1 |
| `configured_feature` | 16 |
| `damage_type` | 2 |
| `data_component` | 1 |
| `e2e` | 1 |
| `feature_type` | 8 |
| `item` | 21 |
| `lang` | 62 |
| `loot_table` | 50 |
| `net` | 4 |
| `particle` | 4 |
| `path` | 20 |
| `placed_feature` | 17 |
| `recipe` | 20 |
| `selftest` | 1 |
| `selftest_dx` | 1 |
| `sound_event` | 12 |
| `stat` | 2 |
| `tag` | 7 |
| `trigger` | 1 |
| `worlddb` | 1 |

### Unit facts

| Kind | Ids |
|---|---|
| Selftest glob (`selftest`) | `flora.*` |
| Selftest dx range (`selftest_dx`) | `600-699` |
| World DB keys (`worlddb`) | `gf:flora:*` |
| E2E scenario (`e2e`) | `flora` |
| Net channels (`net`) | `flora.*`, `flora.fallen_leaf`, `flora.mason_jar_item`, `flora.moving_cicada_sound` |
| Config section (`config_section`) | `flora` |
| Owned paths (`path`) | `**/*.d/flora.json`, `assets/data/*/flora.json`, `assets/gen/audio/flora/**`, `assets/gen/geo/flora/**`, `assets/gen/import/flora/**`, `assets/lang/*/flora.json`, `assets/pack/{bp,rp}/**/flora/**`, `assets/pack/rp/texts.d/*/flora.lang`, `assets/structures/flora/**`, `assets/tags/**/*.d/flora.json`, `docs/units/flora.md`, `src/client/flora/**`, `src/core/flora/**`, `src/feature/ext/flora/**`, `src/feature/flora/**`, `tests/flora/**`, `tools/e2e/*/scenarios/flora{,.*}.ps1`, `tools/import/overrides/flora.json`, `tools/sounds/recipes/flora.json`, `tools/structures/flora/**` |

### Blocks (34)

Item column: `BlockItem` = upstream registers a plain block item with the block (`GloamingBlocks.register`), the id is also a `block_item` row; `-` = no item of the same id (the block is placed by another item, or has none).

Block states come from upstream's blockstate JSON (`multipart:` lists only the property values the model parts test). Notes collect `RegistrationEvents` (fire, axe stripping, flower pots, jar lids), dispenser behaviours and the `neoforge:compostables` / `neoforge:furnace_fuels` data maps.

| Block | English name | upstream registration (`GloamingBlocks`) | Block states | Item | Loot (`blocks/<id>`) | Notes |
|---|---|---|---|---|---|---|
| `gloamingforest:blackberry_bush` | Blackberry Bush | `register("blackberry_bush", new BerryBushBlock(GloamingLootTables.BLACKBERRY_BUSH_DROPS, Properties.of().sound(GRASS).destroyTime(0.4F).randomTicks().dynamicShape().noOcclusion()));` | age: 0/1/2/3; layers: 0/1/2/3/4/5/6/7/8 | BlockItem | rolls 1: table blocks/blackberry_bush_berries [pool if state age=3] ;; rolls 1: blackberry_bush | flammable (encouragement 4, flammability 25); compost chance 0.5; block tags: #mc:replaceable_by_trees, #druid_projectile_replaceable, #worldgen_replaceables |
| `gloamingforest:blightberry_bush` | Blightberry Bush | `register("blightberry_bush", new DarkTowerBerryBushBlock(GloamingLootTables.BLIGHTBERRY_BUSH_DROPS, Properties.of().sound(GRASS).destroyTime(0.4F).randomTicks().dynamicShape().noOcclusion()));` | age: 0/1/2/3; layers: 0/1/2/3/4/5/6/7/8 | BlockItem | rolls 1: table blocks/blightberry_bush_berries [pool if state age=3] ;; rolls 1: blightberry_bush | compost chance 0.5 |
| `gloamingforest:blueberry_bush` | Blueberry Bush | `register("blueberry_bush", new BerryBushBlock(GloamingLootTables.BLUEBERRY_BUSH_DROPS, Properties.of().sound(GRASS).destroyTime(0.4F).randomTicks().dynamicShape().noOcclusion()));` | age: 0/1/2/3; layers: 0/1/2/3/4/5/6/7/8 | BlockItem | rolls 1: table blocks/blueberry_bush_berries [pool if state age=3] ;; rolls 1: blueberry_bush | flammable (encouragement 4, flammability 25); compost chance 0.5; block tags: #mc:replaceable_by_trees, #druid_projectile_replaceable, #worldgen_replaceables |
| `gloamingforest:cicada` | Cicada | `register("cicada", new CicadaBlock(Properties.of().instabreak().noCollission().noTerrainParticles().pushReaction(DESTROY).sound(SLIME_BLOCK)));` |  | BlockItem | self | dispenser: equips it on an entity in front, like armor (ArmorItem.dispenseArmor) (GloamingDispenserBehaviors); block tags: #create:tree_attachments; item tags: #curios:head |
| `gloamingforest:cicada_jar` | Cicada Jar | `BLOCKS.register("cicada_jar", new CicadaJarBlock(Properties.of().noOcclusion().noTerrainParticles().randomTicks().sound(GloamingSoundTypes.JAR).strength(0.3F, 3.0F)));` |  | - | rolls 1: cicada_jar {copy_components jar_lid} [pool if survives_explosion] |  |
| `gloamingforest:clover_patch` | Clover Patch | `register("clover_patch", new PatchBlock(Properties.of().ignitedByLava().noCollission().noOcclusion().instabreak().mapColor(PLANT).pushReaction(DESTROY).sound(GRASS)));` |  | BlockItem | rolls 1: clover_patch [pool if neoforge:can_item_perform_ability{"ability":"shears_dig"}] | flammable (encouragement 60, flammability 100); compost chance 0.3; block tags: #mc:replaceable_by_trees, #druid_projectile_replaceable, #portal/decoration, #worldgen_replaceables; item tags: #createaddition:plants |
| `gloamingforest:copper_oreberry` | Copper Oreberry Bush | `register("copper_oreberry", new OreBerryBlock(false, GloamingLootTables.COPPER_OREBERRY_BUSH_DROPS, Properties.of().sound(METAL).destroyTime(0.4F).randomTicks().dynamicShape().noOcclusion()));` | age: 0/1/2/3; layers: 0/1/2/3/4/5/6/7/8 | BlockItem | rolls 1: table blocks/copper_oreberry_bush_berries [pool if state age=3] ;; rolls 1: copper_oreberry |  |
| `gloamingforest:duskberry_bush` | Duskberry Bush | `register("duskberry_bush", new DarkTowerBerryBushBlock(GloamingLootTables.DUSKBERRY_BUSH_DROPS, Properties.of().sound(GRASS).destroyTime(0.4F).randomTicks().dynamicShape().noOcclusion()));` | age: 0/1/2/3; layers: 0/1/2/3/4/5/6/7/8 | BlockItem | rolls 1: table blocks/duskberry_bush_berries [pool if state age=3] ;; rolls 1: duskberry_bush | compost chance 0.5 |
| `gloamingforest:essence_oreberry` | Essence Berry Bush | `register("essence_oreberry", new OreBerryBlock(true, GloamingLootTables.ESSENCE_BERRY_BUSH_DROPS, Properties.of().sound(METAL).destroyTime(0.4F).randomTicks().dynamicShape().noOcclusion()));` | age: 0/1/2/3; layers: 0/1/2/3/4/5/6/7/8 | BlockItem | rolls 1: table blocks/essence_berry_bush_berries [pool if state age=3] ;; rolls 1: essence_oreberry |  |
| `gloamingforest:fallen_leaves` | Fallen Leaves | `BLOCKS.register("fallen_leaves", new FallenLeavesBlock(Properties.of().ignitedByLava().instabreak().mapColor(PLANT).noCollission().noOcclusion().replaceable().pushReaction(DESTROY).sound(AZALEA_LEAVES)));` | layers: 1/2/3/4/5/6/7/8 | - | rolls 1: alternatives(alternatives(fallen_leaves {count 1} [if state layers=1] → fallen_leaves {count 2} [if state layers=2] → fallen_leaves {count 3} [if state layers=3] → fallen_leaves {count 4} [if state layers=4] → fallen_leaves {count 5} [if state layers=5] → fallen_leaves {count 6} [if state layers=6] → fallen_leaves {count 7} [if state layers=7] → fallen_leaves {count 8} [if state layers=8]) [if neoforge:can_item_perform_ability{"ability":"shears_dig"}]) | flammable (encouragement 60, flammability 100); compost chance 0.1; block tags: #mc:replaceable_by_trees, #druid_projectile_replaceable, #portal/decoration, #worldgen_replaceables; item tags: #createaddition:plants |
| `gloamingforest:fiddlehead` | Fiddlehead Fern | `register("fiddlehead", new FiddleheadBlock(Properties.of().ignitedByLava().instabreak().mapColor(PLANT).noCollission().noOcclusion().pushReaction(DESTROY).replaceable().sound(GRASS)));` |  | BlockItem | rolls 1: fiddlehead [pool if neoforge:can_item_perform_ability{"ability":"shears_dig"}] | flammable (encouragement 60, flammability 100); pottable as `gloamingforest:potted_fiddlehead`; compost chance 0.3; block tags: #mc:replaceable_by_trees, #druid_projectile_replaceable, #ice_bomb_replaceables, #portal/decoration, #portal/generated_decoration, #worldgen_replaceables; item tags: #alexscaves:ferns, #createaddition:plants |
| `gloamingforest:firefly` | Firefly | `register("firefly", new FireflyBlock(Properties.of().instabreak().lightLevel((state) -> 15).noCollission().noTerrainParticles().pushReaction(DESTROY).sound(SLIME_BLOCK)));` |  | BlockItem | self | dispenser: equips it on an entity in front, like armor (ArmorItem.dispenseArmor) (GloamingDispenserBehaviors); block tags: #alexscaves:underzealot_light_sources, #create:tree_attachments, #mc:replaceable_by_trees, #druid_projectile_replaceable, #worldgen_replaceables; item tags: #curios:head |
| `gloamingforest:firefly_jar` | Firefly Jar | `BLOCKS.register("firefly_jar", new FireflyJarBlock(Properties.of().lightLevel((state) -> 15).noOcclusion().noTerrainParticles().sound(GloamingSoundTypes.JAR).strength(0.3F, 3.0F)));` |  | - | rolls 1: firefly_jar {copy_components jar_lid} [pool if survives_explosion] | block tags: #alexscaves:gloomoth_light_sources |
| `gloamingforest:firefly_particle_spawner` | Firefly Particle Spawner | `register("firefly_particle_spawner", new FireflySpawnerBlock(Properties.of().lightLevel((state) -> 15).noOcclusion().noTerrainParticles().sound(GloamingSoundTypes.JAR).strength(0.3F, 3.0F)));` |  | BlockItem | rolls 1: firefly_particle_spawner {explosion_decay} ;; rolls 1: alternatives(alternatives(firefly {count 0} [if state particle_radius=1] → firefly {count 1} [if state particle_radius=2] → firefly {count 2} [if state particle_radius=3] → firefly {count 3} [if state particle_radius=4] → firefly {count 4} [if state particle_radius=5] → firefly {count 5} [if state particle_radius=6] → firefly {count 6} [if state particle_radius=7] → firefly {count 7} [if state particle_radius=8] → firefly {count 8} [if state particle_radius=9] → firefly {count 9} [if state particle_radius=10])) | block tags: #alexscaves:gloomoth_light_sources |
| `gloamingforest:gold_oreberry` | Gold Oreberry Bush | `register("gold_oreberry", new OreBerryBlock(false, GloamingLootTables.GOLD_OREBERRY_BUSH_DROPS, Properties.of().sound(METAL).destroyTime(0.4F).randomTicks().dynamicShape().noOcclusion()));` | age: 0/1/2/3; layers: 0/1/2/3/4/5/6/7/8 | BlockItem | rolls 1: table blocks/gold_oreberry_bush_berries [pool if state age=3] ;; rolls 1: gold_oreberry |  |
| `gloamingforest:huge_lily_pad` | Huge Lily Pad | `BLOCKS.register("huge_lily_pad", new HugeLilyPadBlock(Properties.of().instabreak().mapColor(PLANT).pushReaction(DESTROY).sound(LILY_PAD)));` | facing: east/north/south/west; piece: ne/nw/se/sw | - | self | compost chance 0.85; block tags: #mc:frog_prefer_jump_to, #mc:inside_step_sound_blocks, #mc:mineable/axe, #mc:sword_efficient, #mineable_with_block_and_chain; item tags: #createaddition:plants |
| `gloamingforest:huge_water_lily` | Huge Water Lily | `BLOCKS.register("huge_water_lily", new HugeWaterLilyBlock(Properties.of().instabreak().mapColor(PLANT).noCollission().pushReaction(DESTROY).sound(LILY_PAD)));` |  | - | self | compost chance 0.65; item tags: #createaddition:plants |
| `gloamingforest:iron_oreberry` | Iron Oreberry Bush | `register("iron_oreberry", new OreBerryBlock(false, GloamingLootTables.IRON_OREBERRY_BUSH_DROPS, Properties.of().sound(METAL).destroyTime(0.4F).randomTicks().dynamicShape().noOcclusion()));` | age: 0/1/2/3; layers: 0/1/2/3/4/5/6/7/8 | BlockItem | rolls 1: table blocks/iron_oreberry_bush_berries [pool if state age=3] ;; rolls 1: iron_oreberry |  |
| `gloamingforest:liveroot_block` | Liveroots | `register("liveroot_block", new LiverootBlock(Properties.of().ignitedByLava().mapColor(COLOR_LIGHT_GREEN).sound(WOOD).strength(2.0F, 3.0F)));` |  | BlockItem | rolls 1: alternatives(liveroot_block [if match_tool(mc:silk_touch>=1)] → liveroot {bonus mc:fortune mc:ore_drops {}} [if survives_explosion]) | flammable (encouragement 5, flammability 20); compost chance 0.65; block tags: #c:ores_in_ground/root, #create:tree_attachments, #mc:features_cannot_replace, #mc:mineable/axe, #mc:moss_replaceable, #mineable_with_block_and_chain, #ore_meter_targetable, #plants_hang_on, #small_lakes_dont_replace, #tree_roots_skip; item tags: #createaddition:plants |
| `gloamingforest:maloberry_bush` | Maloberry Bush | `register("maloberry_bush", new BerryBushBlock(GloamingLootTables.MALOBERRY_BUSH_DROPS, Properties.of().sound(GRASS).destroyTime(0.4F).randomTicks().dynamicShape().noOcclusion()));` | age: 0/1/2/3; layers: 0/1/2/3/4/5/6/7/8 | BlockItem | rolls 1: table blocks/maloberry_bush_berries [pool if state age=3] ;; rolls 1: maloberry_bush | flammable (encouragement 4, flammability 25); compost chance 0.5; block tags: #mc:replaceable_by_trees, #druid_projectile_replaceable, #worldgen_replaceables |
| `gloamingforest:mason_jar` | Mason Jar | `BLOCKS.register("mason_jar", new MasonJarBlock(Properties.of().noOcclusion().noTerrainParticles().randomTicks().sound(GloamingSoundTypes.JAR).strength(0.3F, 3.0F)));` |  | - | rolls 1: mason_jar {copy_components mc:custom_name,mc:container,mc:lock,mc:container_loot,jar_lid} [pool if survives_explosion] |  |
| `gloamingforest:mayapple` | Mayapple | `register("mayapple", new MayappleBlock(Properties.of().ignitedByLava().instabreak().mapColor(PLANT).noCollission().noOcclusion().pushReaction(DESTROY).sound(GRASS)));` |  | BlockItem | rolls 1: mayapple [pool if neoforge:can_item_perform_ability{"ability":"shears_dig"}] | flammable (encouragement 60, flammability 100); pottable as `gloamingforest:potted_mayapple`; compost chance 0.3; block tags: #mc:replaceable_by_trees, #druid_projectile_replaceable, #ice_bomb_replaceables, #portal/decoration, #portal/generated_decoration, #gf_berry_bushes_replace, #worldgen_replaceables; item tags: #createaddition:plants |
| `gloamingforest:moonworm` | Moonworm | `register("moonworm", new MoonwormBlock(Properties.of().forceSolidOff().instabreak().lightLevel((state) -> 14).noCollission().noTerrainParticles().pushReaction(DESTROY).sound(SLIME_BLOCK)));` |  | BlockItem | self | dispenser: equips it on an entity in front, like armor (ArmorItem.dispenseArmor) (GloamingDispenserBehaviors); block tags: #alexscaves:underzealot_light_sources; item tags: #curios:head |
| `gloamingforest:moss_patch` | Moss Patch | `register("moss_patch", new MossPatchBlock(Properties.of().ignitedByLava().instabreak().mapColor(PLANT).noCollission().noOcclusion().pushReaction(DESTROY).sound(MOSS)));` |  | BlockItem | rolls 1: moss_patch [pool if neoforge:can_item_perform_ability{"ability":"shears_dig"}] | flammable (encouragement 60, flammability 100); compost chance 0.5; block tags: #mc:replaceable_by_trees, #druid_projectile_replaceable, #portal/decoration, #worldgen_replaceables |
| `gloamingforest:mushgloom` | Mushgloom | `register("mushgloom", new MushgloomBlock(Properties.of().instabreak().lightLevel((state) -> 3).noCollission().noOcclusion().mapColor(PLANT).pushReaction(DESTROY).sound(FUNGUS)));` |  | BlockItem | self | pottable as `gloamingforest:potted_mushgloom`; compost chance 0.65; block tags: #farmersdelight:compost_activators, #mc:replaceable_by_trees, #druid_projectile_replaceable, #portal/decoration, #portal/generated_decoration, #worldgen_replaceables; item tags: #c:mushrooms, #createaddition:plants |
| `gloamingforest:potted_fiddlehead` | Potted Fiddlehead | `BLOCKS.register("potted_fiddlehead", new FlowerPotBlock(() -> (FlowerPotBlock) Blocks.FLOWER_POT, FIDDLEHEAD, Properties.ofFullCopy(Blocks.FLOWER_POT)));` |  | - | rolls 1: mc:flower_pot [pool if survives_explosion] ;; rolls 1: fiddlehead [pool if survives_explosion] | flower pot holding `gloamingforest:fiddlehead`; block tags: #mc:flower_pots, #dark_tower_allowed_pots |
| `gloamingforest:potted_mayapple` | Potted Mayapple | `BLOCKS.register("potted_mayapple", new FlowerPotBlock(() -> (FlowerPotBlock) Blocks.FLOWER_POT, MAYAPPLE, Properties.ofFullCopy(Blocks.FLOWER_POT)));` |  | - | rolls 1: mc:flower_pot [pool if survives_explosion] ;; rolls 1: mayapple [pool if survives_explosion] | flower pot holding `gloamingforest:mayapple`; block tags: #mc:flower_pots, #dark_tower_allowed_pots |
| `gloamingforest:potted_mushgloom` | Potted Mushgloom | `BLOCKS.register("potted_mushgloom", new FlowerPotBlock(() -> (FlowerPotBlock) Blocks.FLOWER_POT, MUSHGLOOM, Properties.ofFullCopy(Blocks.FLOWER_POT)));` |  | - | rolls 1: mc:flower_pot [pool if survives_explosion] ;; rolls 1: mushgloom [pool if survives_explosion] | flower pot holding `gloamingforest:mushgloom`; block tags: #mc:flower_pots, #dark_tower_allowed_pots |
| `gloamingforest:raspberry_bush` | Raspberry Bush | `register("raspberry_bush", new BerryBushBlock(GloamingLootTables.RASPBERRY_BUSH_DROPS, Properties.of().sound(GRASS).destroyTime(0.4F).randomTicks().dynamicShape().noOcclusion()));` | age: 0/1/2/3; layers: 0/1/2/3/4/5/6/7/8 | BlockItem | rolls 1: table blocks/raspberry_bush_berries [pool if state age=3] ;; rolls 1: raspberry_bush | flammable (encouragement 4, flammability 25); compost chance 0.5; block tags: #mc:replaceable_by_trees, #druid_projectile_replaceable, #worldgen_replaceables |
| `gloamingforest:root` | Roots | `register("root", new Block(Properties.of().ignitedByLava().mapColor(WOOD).sound(WOOD).strength(2.0F, 3.0F)));` |  | BlockItem | rolls 1: alternatives(root [if match_tool(mc:silk_touch>=1)] → mc:stick {count uniform(3-5); explosion_decay}) | flammable (encouragement 5, flammability 20); compost chance 0.5; block tags: #c:ore_bearing_ground/root, #create:tree_attachments, #mc:mineable/axe, #mc:moss_replaceable, #mc:replaceable_by_trees, #druid_projectile_replaceable, #mineable_with_block_and_chain, #ore_magnet/ore_safe_replace_block, #plants_hang_on, #small_lakes_dont_replace, #tree_roots_skip, #worldgen_replaceables; item tags: #createaddition:plants |
| `gloamingforest:root_strand` | Root Strands | `register("root_strand", new RootStrandBlock(Properties.of().ignitedByLava().instabreak().mapColor(PLANT).noCollission().noOcclusion().pushReaction(DESTROY).sound(HANGING_ROOTS)));` |  | BlockItem | rolls 1: alternatives(root_strand [if neoforge:can_item_perform_ability{"ability":"shears_dig"}] → mc:stick {count uniform(1-3); explosion_decay}) | flammable (encouragement 60, flammability 100); compost chance 0.3; block tags: #mc:climbable, #mc:replaceable_by_trees, #druid_projectile_replaceable, #worldgen_replaceables; item tags: #createaddition:plants |
| `gloamingforest:skyberry_bush` | Skyberry Bush | `register("skyberry_bush", new DarkTowerBerryBushBlock(GloamingLootTables.SKYBERRY_BUSH_DROPS, Properties.of().sound(GRASS).destroyTime(0.4F).randomTicks().dynamicShape().noOcclusion()));` | age: 0/1/2/3; layers: 0/1/2/3/4/5/6/7/8 | BlockItem | rolls 1: table blocks/skyberry_bush_berries [pool if state age=3] ;; rolls 1: skyberry_bush | compost chance 0.5 |
| `gloamingforest:stingberry_bush` | Stingberry Bush | `register("stingberry_bush", new DarkTowerBerryBushBlock(GloamingLootTables.STINGBERRY_BUSH_DROPS, Properties.of().sound(GRASS).destroyTime(0.4F).randomTicks().dynamicShape().noOcclusion()));` | age: 0/1/2/3; layers: 0/1/2/3/4/5/6/7/8 | BlockItem | rolls 1: table blocks/stingberry_bush_berries [pool if state age=3] ;; rolls 1: stingberry_bush | compost chance 0.5 |
| `gloamingforest:torchberry_plant` | Torchberry Plant | `register("torchberry_plant", new TorchberryPlantBlock(Properties.of().ignitedByLava().instabreak().lightLevel(value -> value.getValue(TorchberryPlantBlock.HAS_BERRIES) ? 7 : 1).noCollission().mapColor(PLANT).pushReaction(DESTROY).sound(HANGING_ROOTS)));` | has_torchberries: false/true | BlockItem | rolls 1: torchberry_plant [if neoforge:can_item_perform_ability{"ability":"shears_dig"}] ;; rolls 1: torchberries [if state has_torchberries=true] | flammable (encouragement 60, flammability 100); compost chance 0.5; block tags: #mc:replaceable_by_trees, #druid_projectile_replaceable, #worldgen_replaceables; item tags: #createaddition:plants |

### Items (21)

| Item | English name | upstream registration (`GloamingItems`) | Notes |
|---|---|---|---|
| `gloamingforest:blackberry` | Blackberry | `ITEMS.register("blackberry", new StackableEffectItem());` | compost chance 0.3; item tags: #c:foods/berry |
| `gloamingforest:blightberry` | Blightberry | `ITEMS.register("blightberry", new StackableEffectItem( new StackableEffectInstance(MobEffects.REGENERATION, 8), new StackableEffectInstance(MobEffects.POISON, 5, 0.75F), new StackableEffectInstance(MobEffects.WITHER, 5, 0.15F) ));` | compost chance 0.3; item tags: #c:foods/berry |
| `gloamingforest:blueberry` | Blueberry | `ITEMS.register("blueberry", new StackableEffectItem());` | compost chance 0.3; item tags: #c:foods/berry |
| `gloamingforest:cicada_jar` | Cicada Jar | `ITEMS.register("cicada_jar", new JarItem(GloamingBlocks.CICADA_JAR.get(), new Item.Properties()));` |  |
| `gloamingforest:copper_berry` | Copper Oreberry | `ITEMS.register("copper_berry", new Item(new Item.Properties()));` |  |
| `gloamingforest:copper_nugget` | Copper Nugget | `ITEMS.register("copper_nugget", new Item(new Item.Properties()));` | item tags: #c:nuggets, #c:nuggets/copper |
| `gloamingforest:duskberry` | Duskberry | `ITEMS.register("duskberry", new StackableEffectItem( new StackableEffectInstance(MobEffects.NIGHT_VISION, 15), new StackableEffectInstance(MobEffects.BLINDNESS, 3, 0.75F) ));` | compost chance 0.3; item tags: #c:foods/berry |
| `gloamingforest:essence_berry` | Concentrated Essence Berry | `ITEMS.register("essence_berry", new EssenceBerryItem(new Item.Properties()));` |  |
| `gloamingforest:fallen_leaves` | Fallen Leaves | `ITEMS.register("fallen_leaves", new BlockItem(GloamingBlocks.FALLEN_LEAVES.get(), new Item.Properties()) { @Override public InteractionResult useOn(UseOnContext context) { return context.getLevel().getBlockState(context.getClickedPos()).is(this.getBlock()) ? super.useOn(context) : InteractionResult.PASS; } @Override public InteractionResultHolder<ItemStack> use(Level level, Player player, InteractionHand hand) { BlockHitResult fluidHitResult = getPlayerPOVHitResult(level, player, ClipContext.Fluid.SOURCE_ONLY); BlockHitResult placeBlockResult = fluidHitResult.withPosition(fluidHitResult.getBlockPos().above()); InteractionResult result = super.useOn(new UseOnContext(player, hand, placeBlockResult)); return new InteractionResultHolder<>(result, player.getItemInHand(hand)); } });` | flammable (encouragement 60, flammability 100); compost chance 0.1; item tags: #createaddition:plants |
| `gloamingforest:firefly_jar` | Firefly Jar | `ITEMS.register("firefly_jar", new JarItem(GloamingBlocks.FIREFLY_JAR.get(), new Item.Properties()));` |  |
| `gloamingforest:gold_berry` | Gold Oreberry | `ITEMS.register("gold_berry", new Item(new Item.Properties()));` |  |
| `gloamingforest:huge_lily_pad` | Huge Lily Pad | `ITEMS.register("huge_lily_pad", new HugeLilyPadItem(GloamingBlocks.HUGE_LILY_PAD.get(), new Item.Properties()));` | compost chance 0.85; item tags: #createaddition:plants |
| `gloamingforest:huge_water_lily` | Huge Water Lily | `ITEMS.register("huge_water_lily", new PlaceOnWaterBlockItem(GloamingBlocks.HUGE_WATER_LILY.get(), new Item.Properties()));` | compost chance 0.65; item tags: #createaddition:plants |
| `gloamingforest:iron_berry` | Iron Oreberry | `ITEMS.register("iron_berry", new Item(new Item.Properties()));` |  |
| `gloamingforest:liveroot` | Liveroot | `ITEMS.register("liveroot", new Item(new Item.Properties()));` | compost chance 0.5; item tags: #createaddition:plants |
| `gloamingforest:maloberry` | Maloberry | `ITEMS.register("maloberry", new StackableEffectItem());` | compost chance 0.3; item tags: #c:foods/berry |
| `gloamingforest:mason_jar` | Mason Jar | `ITEMS.register("mason_jar", new JarItem.MasonJarItem(GloamingBlocks.MASON_JAR.get(), new Item.Properties()));` |  |
| `gloamingforest:raspberry` | Raspberry | `ITEMS.register("raspberry", new StackableEffectItem());` | compost chance 0.3; item tags: #c:foods/berry |
| `gloamingforest:skyberry` | Skyberry | `ITEMS.register("skyberry", new StackableEffectItem( new StackableEffectInstance(MobEffects.JUMP, 8), new StackableEffectInstance(MobEffects.MOVEMENT_SLOWDOWN, 3, 0.75F) ));` | compost chance 0.3; item tags: #c:foods/berry |
| `gloamingforest:stingberry` | Stingberry | `ITEMS.register("stingberry", new StackableEffectItem( new StackableEffectInstance(MobEffects.DAMAGE_BOOST, 10), new StackableEffectInstance(MobEffects.DIG_SLOWDOWN, 10, 0.75F) ));` | compost chance 0.3; item tags: #c:foods/berry |
| `gloamingforest:torchberries` | Torchberries | `ITEMS.register("torchberries", new Item(new Item.Properties().food(new FoodProperties.Builder().alwaysEdible().effect(new MobEffectInstance(MobEffects.GLOWING, 100, 0), 0.75F).build())));` | compost chance 0.3; item tags: #c:foods/berry, #createaddition:plant_foods |

### Block entity types (5)

| Id | upstream registration (`GloamingBlockEntities.java`) |
|---|---|
| `gloamingforest:cicada` | `BLOCK_ENTITIES.register("cicada", () -> BlockEntityType.Builder.of(CicadaBlockEntity::new, GloamingBlocks.CICADA.get()).build(null));` |
| `gloamingforest:firefly` | `BLOCK_ENTITIES.register("firefly", () -> BlockEntityType.Builder.of(FireflyBlockEntity::new, GloamingBlocks.FIREFLY.get()).build(null));` |
| `gloamingforest:jar` | `BLOCK_ENTITIES.register("jar", () -> BlockEntityType.Builder.of(JarBlockEntity::new, GloamingBlocks.FIREFLY_JAR.get(), GloamingBlocks.CICADA_JAR.get()).build(null));` |
| `gloamingforest:mason_jar` | `BLOCK_ENTITIES.register("mason_jar", () -> BlockEntityType.Builder.of(MasonJarBlockEntity::new, GloamingBlocks.MASON_JAR.get()).build(null));` |
| `gloamingforest:moonworm` | `BLOCK_ENTITIES.register("moonworm", () -> BlockEntityType.Builder.of(MoonwormBlockEntity::new, GloamingBlocks.MOONWORM.get()).build(null));` |

### Particle types (4)

| Id | upstream registration (`GloamingParticleType.java`) |
|---|---|
| `gloamingforest:fallen_leaf` | `PARTICLE_TYPES.register("fallen_leaf", new ParticleType<>(false) { @Override public MapCodec<LeafParticleData> codec() { return LeafParticleData.CODEC; } @Override public StreamCodec<? super RegistryFriendlyByteBuf, LeafParticleData> streamCodec() { return LeafParticleData.STREAM_CODEC; } });` |
| `gloamingforest:firefly` | `PARTICLE_TYPES.register("firefly", new SimpleParticleType(false));` |
| `gloamingforest:particle_spawner_firefly` | `PARTICLE_TYPES.register("particle_spawner_firefly", new SimpleParticleType(false));` |
| `gloamingforest:wandering_firefly` | `PARTICLE_TYPES.register("wandering_firefly", new SimpleParticleType(false));` |

### Data components (1)

| Id | upstream registration (`GloamingDataComponents.java`) |
|---|---|
| `gloamingforest:jar_lid` | `register("jar_lid", JarLid.CODEC);` |

### Feature types (8)

| Id | upstream registration (`GloamingFeatures.java`) |
|---|---|
| `gloamingforest:berry_bush` | `FEATURES.register("berry_bush", new BerryBushFeature(BerryBushConfig.CODEC));` |
| `gloamingforest:enchanted_forest_vines` | `FEATURES.register("enchanted_forest_vines", new EnchantedForestVinesFeature(NoneFeatureConfiguration.CODEC));` |
| `gloamingforest:fallen_leaves` | `FEATURES.register("fallen_leaves", new FallenLeavesFeature(NoneFeatureConfiguration.CODEC));` |
| `gloamingforest:huge_lily_pad` | `FEATURES.register("huge_lily_pad", new HugeLilypadFeature(NoneFeatureConfiguration.CODEC));` |
| `gloamingforest:huge_water_lily` | `FEATURES.register("huge_water_lily", new HugeWaterLilyFeature(NoneFeatureConfiguration.CODEC));` |
| `gloamingforest:oreberry_bushes` | `FEATURES.register("oreberry_bushes", new UndergroundPlantFeature(BlockStateConfiguration.CODEC, 1));` |
| `gloamingforest:underground_plants` | `FEATURES.register("underground_plants", new UndergroundPlantFeature(BlockStateConfiguration.CODEC));` |
| `gloamingforest:underground_plants_in_structure` | `FEATURES.register("underground_plants_in_structure", new UndergroundPlantFeature(BlockStateConfiguration.CODEC, true));` |

### Stats (2)

| Id | upstream registration (`GloamingStats.java`) |
|---|---|
| `gloamingforest:bugs_squished` (Bugs Squashed) | `makeTFStat("bugs_squished");` |
| `gloamingforest:torchberries_harvested` (Torchberries Harvested) | `makeTFStat("torchberries_harvested");` |

### Advancement triggers (1)

| Id | upstream registration (`GloamingAdvancements.java`) |
|---|---|
| `gloamingforest:kill_bug` | `TRIGGERS.register("kill_bug", KillBugTrigger::new);` |

### Damage types (2)

- `gloamingforest:oreberry`: `{"exhaustion":0.1,"message_id":"gloamingforest.oreberry","scaling":"when_caused_by_living_non_player"}` death.attack.gloamingforest.oreberry = "%1$s was stabbed to death by an oreberry bush"; death.attack.gloamingforest.oreberry.player = "%1$s was stabbed to death by an oreberry bush while escaping %2$s";
- `gloamingforest:squish`: `{"exhaustion":0.0,"message_id":"gloamingforest.squish","scaling":"when_caused_by_living_non_player"}` death.attack.gloamingforest.squish = "%1$s was squashed by the Snow Queen"; death.attack.gloamingforest.squish.player = "%1$s was squashed by the Snow Queen while escaping %2$s";

### Configured features (16)

- `gloamingforest:blackberry_bushes`: `{"type":"gloamingforest:berry_bush","config":{"bush":{"Name":"gloamingforest:blackberry_bush","Properties":{"age":"0","layers":"0"}},"can_generate_snowy":true,"generates_on":"gloamingforest:gf_berry_bushes_survive"}}`
- `gloamingforest:blueberry_bushes`: `{"type":"gloamingforest:berry_bush","config":{"bush":{"Name":"gloamingforest:blueberry_bush","Properties":{"age":"0","layers":"0"}},"can_generate_snowy":true,"generates_on":"gloamingforest:gf_berry_bushes_survive"}}`
- `gloamingforest:cicada_lamppost`: `{"type":"gloamingforest:lampposts","config":{"state":{"Name":"gloamingforest:cicada_jar","Properties":{"waterlogged":"false"}}}}`
- `gloamingforest:enchanted_forest_vines`: `{"type":"gloamingforest:enchanted_forest_vines","config":{}}`
- `gloamingforest:fallen_leaves`: `{"type":"gloamingforest:fallen_leaves","config":{}}`
- `gloamingforest:fiddlehead`: `{"type":"minecraft:random_patch","config":{"feature":{"feature":{"type":"minecraft:simple_block","config":{"to_place":{"type":"minecraft:simple_state_provider","state":{"Name":"gloamingforest:fiddlehead"}}}},"placement":[{"type":"minecraft:block_predicate_filter","predicate":{"type":"minecraft:matching_blocks","blocks":"minecraft:air"}}]},"tries":96,"xz_spread":7,"y_spread":3}}`
- `gloamingforest:firefly_lamppost`: `{"type":"gloamingforest:lampposts","config":{"state":{"Name":"gloamingforest:firefly_jar","Properties":{"waterlogged":"false"}}}}`
- `gloamingforest:flower_placer`: `{"type":"minecraft:flower","config":{"feature":{"feature":{"type":"minecraft:simple_block","config":{"to_place":{"type":"minecraft:noise_provider","noise":{"amplitudes":[1.0],"firstOctave":0},"scale":0.020833334,"seed":2345,"states":[{"Name":"minecraft:poppy"},{"Name":"minecraft:dandelion"},{"Name":"minecraft:red_tulip"},{"Name":"minecraft:orange_tulip"},{"Name":"minecraft:pink_tulip"},{"Name":"minecraft:white_tulip"},{"Name":"minecraft:cornflower"},{"Name":"minecraft:lily_of_the_valley"},{"Name":"minecraft:blue_orchid"},{"Name":"minecraft:allium"},{"Name":"minecraft:azure_bluet"},{"Name":"minecraft:oxeye_daisy"}]}}},"placement":[{"type":"minecraft:block_predicate_filter","predicate":{"type":"minecraft:matching_blocks","blocks":"minecraft:air"}}]},"tries":32,"xz_spread":7,"y_spread":7}}`
- `gloamingforest:flower_placer_alt`: `{"type":"minecraft:flower","config":{"feature":{"feature":{"type":"minecraft:simple_block","config":{"to_place":{"type":"minecraft:noise_provider","noise":{"amplitudes":[1.0],"firstOctave":0},"scale":0.020833334,"seed":2345,"states":[{"Name":"minecraft:white_tulip"},{"Name":"minecraft:pink_tulip"},{"Name":"minecraft:orange_tulip"},{"Name":"minecraft:red_tulip"},{"Name":"minecraft:dandelion"},{"Name":"minecraft:poppy"},{"Name":"minecraft:oxeye_daisy"},{"Name":"minecraft:azure_bluet"},{"Name":"minecraft:allium"},{"Name":"minecraft:blue_orchid"},{"Name":"minecraft:lily_of_the_valley"},{"Name":"minecraft:cornflower"}]}}},"placement":[{"type":"minecraft:block_predicate_filter","predicate":{"type":"minecraft:matching_blocks","blocks":"minecraft:air"}}]},"tries":32,"xz_spread":7,"y_spread":7}}`
- `gloamingforest:huge_lily_pad`: `{"type":"gloamingforest:huge_lily_pad","config":{}}`
- `gloamingforest:huge_water_lily`: `{"type":"gloamingforest:huge_water_lily","config":{}}`
- `gloamingforest:maloberry_bushes`: `{"type":"gloamingforest:berry_bush","config":{"bush":{"Name":"gloamingforest:maloberry_bush","Properties":{"age":"0","layers":"0"}},"can_generate_snowy":true,"generates_on":"gloamingforest:gf_berry_bushes_survive"}}`
- `gloamingforest:mayapple`: `{"type":"minecraft:random_patch","config":{"feature":{"feature":{"type":"minecraft:simple_block","config":{"to_place":{"type":"minecraft:simple_state_provider","state":{"Name":"gloamingforest:mayapple"}}}},"placement":[{"type":"minecraft:block_predicate_filter","predicate":{"type":"minecraft:matching_blocks","blocks":"minecraft:air"}}]},"tries":96,"xz_spread":7,"y_spread":3}}`
- `gloamingforest:mushgloom_cluster`: `{"type":"minecraft:random_patch","config":{"feature":{"feature":{"type":"minecraft:simple_block","config":{"to_place":{"type":"minecraft:simple_state_provider","state":{"Name":"gloamingforest:mushgloom"}}}},"placement":[{"type":"minecraft:block_predicate_filter","predicate":{"type":"minecraft:matching_blocks","blocks":"minecraft:air"}}]},"tries":96,"xz_spread":7,"y_spread":3}}`
- `gloamingforest:plant_roots`: `{"type":"gloamingforest:underground_plants","config":{"state":{"Name":"gloamingforest:root_strand"}}}`
- `gloamingforest:raspberry_bushes`: `{"type":"gloamingforest:berry_bush","config":{"bush":{"Name":"gloamingforest:raspberry_bush","Properties":{"age":"0","layers":"0"}},"can_generate_snowy":true,"generates_on":"gloamingforest:gf_berry_bushes_survive"}}`

### Placed features (17)

- `gloamingforest:blackberry_bushes`: `{"feature":"gloamingforest:blackberry_bushes","placement":[{"type":"minecraft:heightmap","heightmap":"OCEAN_FLOOR_WG"},{"type":"minecraft:rarity_filter","chance":200},{"type":"minecraft:in_square"},{"type":"minecraft:heightmap","heightmap":"MOTION_BLOCKING"},{"type":"gloamingforest:no_structure","additional_clearance":0,"occupies_surface":true,"occupies_underground":false,"occupies_vegetation":false,"structures_allowed":[]},{"type":"minecraft:biome"}]}`
- `gloamingforest:blueberry_bushes`: `{"feature":"gloamingforest:blueberry_bushes","placement":[{"type":"minecraft:heightmap","heightmap":"OCEAN_FLOOR_WG"},{"type":"minecraft:rarity_filter","chance":50},{"type":"minecraft:in_square"},{"type":"minecraft:heightmap","heightmap":"MOTION_BLOCKING"},{"type":"gloamingforest:no_structure","additional_clearance":0,"occupies_surface":true,"occupies_underground":false,"occupies_vegetation":false,"structures_allowed":[]},{"type":"minecraft:biome"}]}`
- `gloamingforest:enchanted_forest_vines`: `{"feature":"gloamingforest:enchanted_forest_vines","placement":[{"type":"minecraft:heightmap","heightmap":"MOTION_BLOCKING"},{"type":"minecraft:biome"}]}`
- `gloamingforest:fallen_leaves`: `{"feature":"gloamingforest:fallen_leaves","placement":[{"type":"minecraft:rarity_filter","chance":1},{"type":"minecraft:in_square"},{"type":"minecraft:heightmap","heightmap":"OCEAN_FLOOR"},{"type":"gloamingforest:no_structure","additional_clearance":0,"occupies_surface":true,"occupies_underground":false,"occupies_vegetation":false,"structures_allowed":[]},{"type":"minecraft:biome"}]}`
- `gloamingforest:fiddlehead`: `{"feature":"gloamingforest:fiddlehead","placement":[{"type":"minecraft:heightmap","heightmap":"WORLD_SURFACE_WG"},{"type":"minecraft:biome"}]}`
- `gloamingforest:flower_placer`: `{"feature":"gloamingforest:flower_placer","placement":[{"type":"minecraft:heightmap","heightmap":"WORLD_SURFACE_WG"},{"type":"minecraft:count","count":3},{"type":"minecraft:in_square"},{"type":"minecraft:rarity_filter","chance":2},{"type":"minecraft:biome"},{"type":"gloamingforest:no_structure","additional_clearance":0,"occupies_surface":false,"occupies_underground":false,"occupies_vegetation":true,"structures_allowed":[]}]}`
- `gloamingforest:flower_placer_alt`: `{"feature":"gloamingforest:flower_placer_alt","placement":[{"type":"minecraft:heightmap","heightmap":"WORLD_SURFACE_WG"},{"type":"minecraft:count","count":3},{"type":"minecraft:in_square"},{"type":"minecraft:rarity_filter","chance":2},{"type":"minecraft:biome"},{"type":"gloamingforest:no_structure","additional_clearance":0,"occupies_surface":false,"occupies_underground":false,"occupies_vegetation":true,"structures_allowed":[]}]}`
- `gloamingforest:huge_lily_pad`: `{"feature":"gloamingforest:huge_lily_pad","placement":[{"type":"minecraft:heightmap","heightmap":"WORLD_SURFACE_WG"},{"type":"minecraft:rarity_filter","chance":20},{"type":"minecraft:in_square"},{"type":"minecraft:count","count":10},{"type":"minecraft:biome"}]}`
- `gloamingforest:huge_water_lily`: `{"feature":"gloamingforest:huge_water_lily","placement":[{"type":"minecraft:heightmap","heightmap":"WORLD_SURFACE_WG"},{"type":"minecraft:rarity_filter","chance":25},{"type":"minecraft:in_square"},{"type":"minecraft:count","count":5},{"type":"minecraft:biome"}]}`
- `gloamingforest:maloberry_bushes`: `{"feature":"gloamingforest:maloberry_bushes","placement":[{"type":"minecraft:heightmap","heightmap":"OCEAN_FLOOR_WG"},{"type":"minecraft:rarity_filter","chance":25},{"type":"minecraft:in_square"},{"type":"minecraft:heightmap","heightmap":"MOTION_BLOCKING"},{"type":"gloamingforest:no_structure","additional_clearance":0,"occupies_surface":true,"occupies_underground":false,"occupies_vegetation":false,"structures_allowed":[]},{"type":"minecraft:biome"}]}`
- `gloamingforest:mayapple`: `{"feature":"gloamingforest:mayapple","placement":[{"type":"minecraft:heightmap","heightmap":"WORLD_SURFACE_WG"},{"type":"minecraft:biome"},{"type":"gloamingforest:no_structure","additional_clearance":0,"occupies_surface":false,"occupies_underground":false,"occupies_vegetation":true,"structures_allowed":[]}]}`
- `gloamingforest:mushgloom_cluster`: `{"feature":"gloamingforest:mushgloom_cluster","placement":[{"type":"minecraft:heightmap","heightmap":"WORLD_SURFACE_WG"},{"type":"minecraft:rarity_filter","chance":5},{"type":"minecraft:in_square"},{"type":"minecraft:block_predicate_filter","predicate":{"type":"minecraft:would_survive","state":{"Name":"gloamingforest:mushgloom"}}},{"type":"minecraft:biome"},{"type":"gloamingforest:no_structure","additional_clearance":0,"occupies_surface":false,"occupies_underground":false,"occupies_vegetation":true,"structures_allowed":[]}]}`
- `gloamingforest:plant_roots`: `{"feature":"gloamingforest:plant_roots","placement":[{"type":"minecraft:count","count":4},{"type":"minecraft:height_range","height":{"type":"minecraft:uniform","max_inclusive":{"absolute":10},"min_inclusive":{"above_bottom":0}}},{"type":"minecraft:rarity_filter","chance":1},{"type":"minecraft:in_square"},{"type":"minecraft:heightmap","heightmap":"MOTION_BLOCKING"},{"type":"gloamingforest:no_structure","additional_clearance":0,"occupies_surface":false,"occupies_underground":true,"occupies_vegetation":false,"structures_allowed":[]},{"type":"minecraft:biome"}]}`
- `gloamingforest:raspberry_bushes`: `{"feature":"gloamingforest:raspberry_bushes","placement":[{"type":"minecraft:heightmap","heightmap":"OCEAN_FLOOR_WG"},{"type":"minecraft:rarity_filter","chance":50},{"type":"minecraft:in_square"},{"type":"minecraft:heightmap","heightmap":"MOTION_BLOCKING"},{"type":"gloamingforest:no_structure","additional_clearance":0,"occupies_surface":true,"occupies_underground":false,"occupies_vegetation":false,"structures_allowed":[]},{"type":"minecraft:biome"}]}`
- `gloamingforest:snowy_blueberry_bushes`: `{"feature":"gloamingforest:blueberry_bushes","placement":[{"type":"minecraft:heightmap","heightmap":"OCEAN_FLOOR_WG"},{"type":"minecraft:rarity_filter","chance":100},{"type":"minecraft:in_square"},{"type":"minecraft:heightmap","heightmap":"MOTION_BLOCKING"},{"type":"gloamingforest:no_structure","additional_clearance":0,"occupies_surface":true,"occupies_underground":false,"occupies_vegetation":false,"structures_allowed":[]},{"type":"minecraft:biome"}]}`
- `gloamingforest:swamp_blackberry_bushes`: `{"feature":"gloamingforest:blackberry_bushes","placement":[{"type":"minecraft:heightmap","heightmap":"OCEAN_FLOOR_WG"},{"type":"minecraft:rarity_filter","chance":25},{"type":"minecraft:in_square"},{"type":"minecraft:heightmap","heightmap":"MOTION_BLOCKING"},{"type":"gloamingforest:no_structure","additional_clearance":0,"occupies_surface":true,"occupies_underground":false,"occupies_vegetation":false,"structures_allowed":[]},{"type":"minecraft:biome"}]}`
- `gloamingforest:swamp_raspberry_bushes`: `{"feature":"gloamingforest:raspberry_bushes","placement":[{"type":"minecraft:heightmap","heightmap":"OCEAN_FLOOR_WG"},{"type":"minecraft:rarity_filter","chance":200},{"type":"minecraft:in_square"},{"type":"minecraft:heightmap","heightmap":"MOTION_BLOCKING"},{"type":"gloamingforest:no_structure","additional_clearance":0,"occupies_surface":true,"occupies_underground":false,"occupies_vegetation":false,"structures_allowed":[]},{"type":"minecraft:biome"}]}`

### Sound events (12)

`sounds.json` content (vanilla `event` references can be played with the matching Bedrock vanilla sound; upstream file names are listed only as names: the audio itself is All Rights Reserved and is re-synthesised by F3/the owner).

| Event | Subtitle | sounds.json entries |
|---|---|---|
| `block.gloamingforest.bug.squish` | subtitles.gloamingforest.block.bug.squish = Bug gets squished :( | event entity.slime.squish_small |
| `block.gloamingforest.cicada` | subtitles.gloamingforest.block.cicada = Cicada screams | gloamingforest:mob/cicada1, gloamingforest:mob/cicada2 |
| `block.gloamingforest.jar.break` | subtitles.block.generic.break | gloamingforest:random/jar/jar1, gloamingforest:random/jar/jar2, gloamingforest:random/jar/jar3 |
| `block.gloamingforest.jar.fall` |  | gloamingforest:random/jar/jar1, gloamingforest:random/jar/jar2, gloamingforest:random/jar/jar3 |
| `block.gloamingforest.jar.hit` | subtitles.block.generic.hit | gloamingforest:random/jar/jar1, gloamingforest:random/jar/jar2, gloamingforest:random/jar/jar3 |
| `block.gloamingforest.jar.insert` | subtitles.gloamingforest.block.jar.insert = Item inserted into jar | gloamingforest:random/jar/jar_in |
| `block.gloamingforest.jar.lid_swap` | subtitles.gloamingforest.block.jar.lid_swap = Changed jar lid | gloamingforest:random/jar/jar_swap |
| `block.gloamingforest.jar.place` | subtitles.block.generic.place | gloamingforest:random/jar/jar_place |
| `block.gloamingforest.jar.remove` | subtitles.gloamingforest.block.jar.remove = Item removed from jar | gloamingforest:random/jar/jar_out |
| `block.gloamingforest.jar.step` | subtitles.block.generic.footsteps | gloamingforest:random/jar/jar1, gloamingforest:random/jar/jar2, gloamingforest:random/jar/jar3 |
| `block.gloamingforest.jar.wiggle` | subtitles.gloamingforest.block.jar.wiggle = Jar wiggles | gloamingforest:random/jar/jar_wiggle1, gloamingforest:random/jar/jar_wiggle2, gloamingforest:random/jar/jar_wiggle3 |
| `block.gloamingforest.torchberry.harvest` | subtitles.gloamingforest.block.torchberry.harvest = Torchberries pop | event block.sweet_berry_bush.pick_berries |

### Tags (7)

| Tag | Members (upstream generated JSON) |
|---|---|
| `block/#gloamingforest:dark_tower_berry_bushes_die` | #mc:nylium |
| `block/#gloamingforest:dark_tower_berry_bushes_survive` | #c:netherracks, #c:ores_in_ground/netherrack, mc:blackstone, mc:soul_sand, mc:soul_soil |
| `block/#gloamingforest:oreberry_bushes_survive` | #c:stones, #mc:stone_bricks, #c:ores_in_ground/stone, #c:ores_in_ground/deepslate, #c:ores_in_ground/netherrack, #c:cobblestones, #c:netherracks, giant_cobblestone, mc:polished_andesite, mc:polished_diorite, mc:polished_granite, mc:smooth_stone, mc:infested_chiseled_stone_bricks, mc:infested_cracked_stone_bricks, mc:infested_mossy_stone_bricks, mc:infested_stone_bricks |
| `block/#gloamingforest:plants_hang_on` | #mc:dirt, mc:moss_block, mangrove_root, root, liveroot_block |
| `block/#gloamingforest:gf_berry_bushes_replace` | #mc:replaceable, #mc:flowers, mayapple |
| `block/#gloamingforest:gf_berry_bushes_survive` | #mc:dirt, mc:snow_block |
| `entity_type/#gloamingforest:dont_kill_bugs` | moonworm_shot |

### Loot tables (50)

Block loot tables summarised in the Loot column of the block table: `gloamingforest:blocks/blackberry_bush`, `gloamingforest:blocks/blightberry_bush`, `gloamingforest:blocks/blueberry_bush`, `gloamingforest:blocks/cicada`, `gloamingforest:blocks/cicada_jar`, `gloamingforest:blocks/clover_patch`, `gloamingforest:blocks/copper_oreberry`, `gloamingforest:blocks/duskberry_bush`, `gloamingforest:blocks/essence_oreberry`, `gloamingforest:blocks/fallen_leaves`, `gloamingforest:blocks/fiddlehead`, `gloamingforest:blocks/firefly`, `gloamingforest:blocks/firefly_jar`, `gloamingforest:blocks/firefly_particle_spawner`, `gloamingforest:blocks/gold_oreberry`, `gloamingforest:blocks/huge_lily_pad`, `gloamingforest:blocks/huge_water_lily`, `gloamingforest:blocks/iron_oreberry`, `gloamingforest:blocks/liveroot_block`, `gloamingforest:blocks/maloberry_bush`, `gloamingforest:blocks/mason_jar`, `gloamingforest:blocks/mayapple`, `gloamingforest:blocks/moonworm`, `gloamingforest:blocks/moss_patch`, `gloamingforest:blocks/mushgloom`, `gloamingforest:blocks/potted_fiddlehead`, `gloamingforest:blocks/potted_mayapple`, `gloamingforest:blocks/potted_mushgloom`, `gloamingforest:blocks/raspberry_bush`, `gloamingforest:blocks/root`, `gloamingforest:blocks/root_strand`, `gloamingforest:blocks/skyberry_bush`, `gloamingforest:blocks/stingberry_bush`, `gloamingforest:blocks/torchberry_plant`

| Table | Summary |
|---|---|
| `gloamingforest:blocks/blackberry_bush_berries` | rolls 1: blackberry |
| `gloamingforest:blocks/blightberry_bush_berries` | rolls 1: blightberry |
| `gloamingforest:blocks/blueberry_bush_berries` | rolls 1: blueberry |
| `gloamingforest:blocks/cicada_squish` | rolls 1: mc:gray_dye |
| `gloamingforest:blocks/copper_oreberry_bush_berries` | rolls 1: copper_berry |
| `gloamingforest:blocks/duskberry_bush_berries` | rolls 1: duskberry |
| `gloamingforest:blocks/essence_berry_bush_berries` | rolls 1: essence_berry |
| `gloamingforest:blocks/firefly_squish` | rolls 1: mc:glowstone_dust |
| `gloamingforest:blocks/gold_oreberry_bush_berries` | rolls 1: gold_berry |
| `gloamingforest:blocks/iron_oreberry_bush_berries` | rolls 1: iron_berry |
| `gloamingforest:blocks/maloberry_bush_berries` | rolls 1: maloberry |
| `gloamingforest:blocks/moonworm_failed_to_place` | rolls 1: moonworm |
| `gloamingforest:blocks/moonworm_squish` | rolls 1: mc:lime_dye |
| `gloamingforest:blocks/raspberry_bush_berries` | rolls 1: raspberry |
| `gloamingforest:blocks/skyberry_bush_berries` | rolls 1: skyberry |
| `gloamingforest:blocks/stingberry_bush_berries` | rolls 1: stingberry |

### Recipes (20)

Recipe-unlock advancements (generated by F1 into the recipe JSON, R21) are listed with their recipe; criteria are the `has_*` triggers besides `has_the_recipe`.

| Recipe | Type | Ingredients | Result | Unlock advancement (criteria) |
|---|---|---|---|---|
| `gloamingforest:berry_torch` | mc:crafting_shaped | '∴' / '\|' ; \|=#c:rods/wooden, ∴=torchberries | mc:torch x5 | `gloamingforest:recipes/decorations/berry_torch` (has_item: has torchberries) |
| `gloamingforest:cicada_jar` | mc:crafting_shapeless | cicada + mason_jar | cicada_jar | `gloamingforest:recipes/decorations/cicada_jar` (has_item: has cicada) |
| `gloamingforest:copper_ingot_to_nuggets` | mc:crafting_shapeless | #c:ingots/copper | copper_nugget x9 | `gloamingforest:recipes/misc/copper_ingot_to_nuggets` (has_item: has #c:ingots/copper) |
| `gloamingforest:copper_nuggets_to_ingot` | mc:crafting_shapeless | #c:nuggets/copper + #c:nuggets/copper + #c:nuggets/copper + #c:nuggets/copper + #c:nuggets/copper + #c:nuggets/copper + #c:nuggets/copper + #c:nuggets/copper + #c:nuggets/copper | mc:copper_ingot | `gloamingforest:recipes/misc/copper_nuggets_to_ingot` (has_item: has #c:nuggets/copper) |
| `gloamingforest:firefly_jar` | mc:crafting_shapeless | firefly + mason_jar | firefly_jar | `gloamingforest:recipes/decorations/firefly_jar` (has_item: has firefly) |
| `gloamingforest:firefly_particle_spawner` | mc:crafting_shapeless | firefly_jar + firefly + mc:poppy | firefly_particle_spawner | `gloamingforest:recipes/decorations/firefly_particle_spawner` (has_jar: has firefly_jar) |
| `gloamingforest:jeed/jump_boost` | jeed:effect_provider | {"effect":"minecraft:jump_boost","providers":[{"item":"gloamingforest:skyberry"}]} [conditions [{"type":"neoforge:mod_loaded","modid":"jeed"}]] | - | - |
| `gloamingforest:jeed/mining_fatigue` | jeed:effect_provider | {"effect":"minecraft:mining_fatigue","providers":[{"item":"gloamingforest:stingberry"}]} [conditions [{"type":"neoforge:mod_loaded","modid":"jeed"}]] | - | - |
| `gloamingforest:jeed/night_vision` | jeed:effect_provider | {"effect":"minecraft:wither","providers":[{"item":"gloamingforest:duskberry"}]} [conditions [{"type":"neoforge:mod_loaded","modid":"jeed"}]] | - | - |
| `gloamingforest:jeed/strength` | jeed:effect_provider | {"effect":"minecraft:strength","providers":[{"item":"gloamingforest:stingberry"}]} [conditions [{"type":"neoforge:mod_loaded","modid":"jeed"}]] | - | - |
| `gloamingforest:jeed/wither` | jeed:effect_provider | {"effect":"minecraft:wither","providers":[{"item":"gloamingforest:blightberry"}]} [conditions [{"type":"neoforge:mod_loaded","modid":"jeed"}]] | - | - |
| `gloamingforest:mason_jar` | mc:crafting_shaped | 'GLG' / 'G G' / 'GGG' ; G=mc:glass, L=gloaming_oak_log | mason_jar x4 | `gloamingforest:recipes/decorations/mason_jar` (has_gf_oak: has gloaming_oak_log) |
| `gloamingforest:material/blasted_copper_nugget` | mc:blasting | copper_berry (xp 1.0, 100 t) | copper_nugget | `gloamingforest:recipes/misc/material/blasted_copper_nugget` (has_item: has copper_berry) |
| `gloamingforest:material/blasted_gold_nugget` | mc:blasting | gold_berry (xp 1.0, 100 t) | mc:gold_nugget | `gloamingforest:recipes/misc/material/blasted_gold_nugget` (has_item: has gold_berry) |
| `gloamingforest:material/blasted_iron_nugget` | mc:blasting | iron_berry (xp 1.0, 100 t) | mc:iron_nugget | `gloamingforest:recipes/misc/material/blasted_iron_nugget` (has_item: has iron_berry) |
| `gloamingforest:material/smelted_copper_nugget` | mc:smelting | copper_berry (xp 1.0, 200 t) | copper_nugget | `gloamingforest:recipes/misc/material/smelted_copper_nugget` (has_item: has copper_berry) |
| `gloamingforest:material/smelted_gold_nugget` | mc:smelting | gold_berry (xp 1.0, 200 t) | mc:gold_nugget | `gloamingforest:recipes/misc/material/smelted_gold_nugget` (has_item: has gold_berry) |
| `gloamingforest:material/smelted_iron_nugget` | mc:smelting | iron_berry (xp 1.0, 200 t) | mc:iron_nugget | `gloamingforest:recipes/misc/material/smelted_iron_nugget` (has_item: has iron_berry) |
| `gloamingforest:root_stick` | mc:crafting_shapeless | root_strand | mc:stick | `gloamingforest:recipes/misc/root_stick` (has_item: has root_strand) |
| `gloamingforest:waterlily_to_magenta` | mc:crafting_shapeless | huge_water_lily | mc:magenta_dye | `gloamingforest:recipes/misc/waterlily_to_magenta` (has_item: has huge_water_lily) |

### Config keys (2)

See [config.md](config.md) for the section mapping; defaults and translation ids below are upstream's.

| Upstream key | Default | Translation id | English (config screen) |
|---|---|---|---|
| `client.silentCicadas` | `false` | `silent_cicadas` | Silent Cicadas — Makes Cicadas silent for those having sound library problems or finding them annoying. |
| `client.silentCicadasOnHead` | `false` | `silent_cicadas_on_head` | Silent Cicadas on Head — Makes Cicadas silent when one is on your head. If the above option is set to true, this won't have any effect. |

## Appendix B: Strings (62 en_us keys owned)

Keys already shown above (block/item/entity names, subtitles) are not repeated. The 29 Bedrock locales are generated from upstream's lang files (R9); owners use these keys with the English text as fallback.

| Key | English |
|---|---|
| `config.gloamingforest.silent_cicadas` | Silent Cicadas |
| `config.gloamingforest.silent_cicadas_on_head` | Silent Cicadas on Head |
| `config.gloamingforest.silent_cicadas_on_head.tooltip` | Makes Cicadas silent when one is on your head. If the above option is set to true, this won't have any effect. |
| `config.gloamingforest.silent_cicadas.tooltip` | Makes Cicadas silent for those having sound library problems or finding them annoying. |

