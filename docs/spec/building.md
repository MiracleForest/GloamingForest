# Building blocks and boss collectibles (U5 `building`)

Behaviour spec of unit **U5** (wave W1), translated from The Twilight Forest 4.8 (`9e40a757`, MC 1.21.1) by F16. It is
the checklist the U5 worker implements and the A2 audit compares against. Sections 1-5 are the behaviour; Appendix A
lists every id U5 owns with TF's registration, block states, loot, recipes, tags; Appendix B the strings.

## 1. Scope

- **Structure building blocks** of the landmarks, placed by other units' structures by string id (R2): naga courtyard
  stones (`nagastone`, `nagastone_head`, `spiral_bricks`, etched / mossy / cracked nagastone, pillars and left/right
  stairs), lich tower stones (`twisted_stone`, `twisted_stone_pillar`, `bold_stone_pillar`), labyrinth mazestone
  family, stronghold `underbrick` family, dark tower `towerwood` family (not `infested_towerwood`, U26), highlands
  `deadrock` family, final castle bricks (castle brick family, roof tile, four rune bricks), `canopy_window` and pane,
  `iron_ladder`, `rope`.
- **Storage blocks**: `knightmetal_block`, `ironwood_block`, `fiery_block`, `steeleaf_block`, `arctic_fur_block`,
  `carminite_block`.
- **Boss collectibles** (R20): the nine trophies (floor + wall block, one item each) and the `trophy` block entity, the
  nine `*_boss_spawner` blocks, the nine banner pattern items and `banner_pattern` registry entries, the five
  `*_miniature_structure` blocks.
- Config key `client.rotateTrophyHeadsGui` (section `building`), 134 recipes, 90 loot tables, 8 tags.

Not U5: the spawn logic of each boss spawner (each boss unit calls `boss::registerSpawner` for its block id, R20;
F8's boss kit runs it), the trophy pedestal (U30), `infested_towerwood` (U26), castle doors and force fields (U30),
`pattern_item/<boss>` banner-pattern tags (owned by U7/U20/U21/U24/F9 per owners.tsv; U5's items reference them).

Contracts: families+blockevents, boss, geo, recipes, client.render, enchant+effects.

## 2. TF sources

`init/TFBlocks.java`, `init/TFItems.java`, `init/TFBlockEntities.java`, `init/TFBannerPatterns.java`,
`block/NagastoneBlock.java`, `enums/NagastoneVariant.java`, `block/EtchedNagastoneBlock.java`,
`block/SpiralBrickBlock.java`, `enums/Diagonals.java`, `block/DirectionalRotatedPillarBlock.java`,
`block/TFHorizontalBlock.java`, `block/WallPillarBlock.java`, `block/ConnectableRotatedPillarBlock.java`,
`block/IronLadderBlock.java`, `block/RopeBlock.java`, `item/RopeItem.java`, `block/KnightmetalBlock.java`,
`block/FieryBlock.java`, `block/ArcticFurBlock.java`, `block/MiniatureStructureBlock.java`,
`block/BossSpawnerBlock.java`, `block/entity/spawner/*SpawnerBlockEntity.java`, `block/AbstractTrophyBlock.java`,
`block/TrophyBlock.java`, `block/TrophyWallBlock.java`, `block/entity/TrophyBlockEntity.java`, `item/TrophyItem.java`,
`enums/BossVariant.java`, `client/renderer/block/TrophyRenderer.java`, `client/ISTER.java` (trophy items),
`client/model/entity/*Model.java` trophy layers (`TFModelLayers.*_TROPHY`), `client/event/ColorHandler.java`,
`dispenser/TFDispenserBehaviors.java`, `events/RegistrationEvents.java`, `config/TFClientConfig.java`.

## 3. Behaviour

### 3.1 Plain building blocks

All numbers are in Appendix A. Families: castle bricks `strength(100.0, 50.0)`, sound DEEPSLATE_TILES, map QUARTZ,
requires the correct tool (castle roof tile COLOR_GRAY; the four rune bricks copy castle brick with map colours
LIGHT_BLUE / MAGENTA / PURPLE / YELLOW and are **tinted** solid 0xFF00FFFF (blue), 0xFFFF00FF (pink),
0xFFFFFF00 (yellow), 0xFF4B0082 (violet), also as items); mazestone `strength(100.0, 5.0)`; deadrock
`strength(100.0, 6000000.0)`; towerwood `strength(40.0, 6.0)`, sound WOOD, `ignitedByLava`, flammable 0/1
(encouragement 0, flammability 1); underbrick `strength(1.5, 6.0)`, sound DEEPSLATE_BRICKS; nagastone / lich tower
stones `strength(1.5, 6.0)`. Castle-brick stairs, nagastone stairs and others are vanilla `StairBlock`s copying their
base. Tags `#twilightforest:castle_blocks`, `#mazestone`, `#towerwood`, `#deadrock` mark the families (read by U30's
castle doors, U23's mazebreaker, U26's borer, U30's thorns by tag).

Towerwood tint (client, `ColorHandler`): `towerwood`, `cracked_towerwood`, `mossy_towerwood` (and U26's infested) are
coloured per position: f = rippleFractalNoise(2, 32.0, pos, 0.4, 1.0, 2.0), colour = HSV(0.1, 1 - f, (f + 2) / 3);
inventory items untinted.

### 3.2 Nagastone (`NagastoneBlock`)

State `variant` = `north_down`, `south_down`, `west_down`, `east_down`, `north_up`, `south_up`, `east_up`, `west_up`,
`axis_x`, `axis_y`, `axis_z`, `solid` (default). On placement and on every neighbour update the variant is recomputed:
collect up to 2 directions (in `Direction.values()` order D, U, N, S, W, E) whose neighbour is a nagastone or a
`nagastone_head` whose `facing` equals that direction. 1 connection -> the axis of that direction; 2 connections on
the same axis -> that axis; one vertical + one horizontal -> `<horizontal>_<up|down>` elbow; two horizontal on
different axes -> `solid`; 0 -> `solid`.
`nagastone_head` (`TFHorizontalBlock`): `facing` = opposite of the player's horizontal facing (faces the player).

### 3.3 Etched nagastone, pillars, spiral bricks, twisted stone pillar

- `etched_nagastone` / mossy / cracked (`EtchedNagastoneBlock`, DirectionalBlock): `facing` (6 directions) = the
  player's nearest looking direction; default `down`; rotate/mirror like DirectionalBlock.
- `nagastone_pillar` / mossy / cracked (`DirectionalRotatedPillarBlock`): `axis` from the clicked face plus `reversed`
  = clicked face points to the negative direction; mirroring flips `reversed` on y, on z for LEFT_RIGHT, on x for
  FRONT_BACK.
- `spiral_bricks` (`SpiralBrickBlock`, `noOcclusion`, waterloggable): states `axis` (x/y/z) and `diagonal`
  (`Diagonals`: top_left/top_right/bottom_left/bottom_right). Sneaking: `axis=y`, diagonal from the horizontal facing
  (north/up default -> top_right, south -> bottom_left, east -> top_left, west -> bottom_right). Otherwise `axis` =
  the horizontal facing's axis and the diagonal from stair logic: north/east use (player direction == facing, upper
  half of the clicked face), south/west use (player direction != facing, upper half). Rotation by 180 (or 90 on x /
  -90 on z) mirrors the diagonal left-right and swaps x/z on 90-degree turns.
- `twisted_stone_pillar` (`WallPillarBlock`, width 12, height 16, waterloggable, `noOcclusion`): `axis` + six
  connection booleans; it connects to another `WallPillarBlock` when the direction is along either pillar's axis.
  Shape: 12-wide pillar along the axis, extended to the block faces where connected, otherwise 'flat' caps on the
  axis ends.

### 3.4 Iron ladder and rope

- `iron_ladder` (`IronLadderBlock`, a LadderBlock, `strength(5.0, 6.0)`, METAL, requires tool, `forceSolidOff`,
  `pushReaction DESTROY`, in `#minecraft:climbable`): extra booleans `left` / `right` set when the block to the
  facing's counter-clockwise / clockwise side is an iron ladder with the same facing (connected texture).
- `rope` (`RopeBlock`, `strength(0.3, 3.0)`, WOOL, `forceSolidOff`, `noOcclusion`, `pushReaction DESTROY`, in
  `#minecraft:climbable`, skylight passes, not pathfindable, acts as **scaffolding** for climbing): states `x`, `y`
  (default true), `z`, `waterlogged`; shape = 3x3-px strands along the active axes (x: box(0,6.5,6.5, 16,9.5,9.5),
  y: box(6.5,0,6.5, 9.5,16,9.5), z: box(6.5,6.5,0, 9.5,9.5,16)); collision only for horizontal strands when the entity
  is above and not descending (walkable like a tightrope).
  - Placement: the clicked face's axis becomes the strand axis. A rope can be replaced only by another rope item; a
    rope item used on a rope adds the clicked axis to it (`x`/`y`/`z` true).
  - Support (`canConnectTo`): never downward; a neighbour is support if it is leaves, a rope having the same axis, or
    has a sturdy CENTER face. A strand survives if: x — west or east chain ends on support; y — the chain upward ends
    on support; z — north or south. The check walks along consecutive ropes (`checkConnection`).
  - Scheduled tick 1 t after placement / any neighbour change: unsupported axes are removed and one rope dropped per
    removed axis; no axis left -> the block breaks with drops.
  - `RopeItem` extension: using a rope item on an existing rope extends it — direction = the clicked face if it
    leads to support, else its opposite, else (y strand) down, else along an existing horizontal strand (nearest
    looking directions, reversed while sneaking); it walks up to `EXTEND_RANGE` = **7** horizontal blocks (vertical
    is unlimited until a non-rope), places in the first replaceable block, refuses above the build limit with the
    `build.tooHigh` message.
  - Loot: one rope per active axis (the table adds 1 for each of x, y, z and subtracts 1; explosion decay).

### 3.5 Storage blocks

- `knightmetal_block` (`KnightmetalBlock`, `strength(5.0, 40.0)`, NETHERITE_BLOCK sound, waterloggable only in a
  full water source): shape box(1,1,1, 15,15,15) (spiky), path type DAMAGE_OTHER; **entity inside -> 4.0 damage of
  type `twilightforest:knightmetal`** (U7 owns the damage type; use its id). Tooltip
  `block.twilightforest.knightmetal_block.desc`.
- `fiery_block` (`FieryBlock`, `strength(5.0, 6.0)`, METAL, `noOcclusion`, emissive, shade brightness 1.0, no
  occlusion shape, fire source (fire on top burns forever)): stepping on it hurts **1.0** damage of type
  `twilightforest:fiery` unless the entity is fire immune or a living entity wearing `twilightforest:fiery_boots`.
  Block item is fire resistant, rarity UNCOMMON.
- `arctic_fur_block` (`ArcticFurBlock`, `strength(0.8)`, WOOL, flammable 20/20): mining progress with shears is fixed
  at **0.2** per tick; falling on it causes fall damage x**0.1**; tooltip `block.twilightforest.arctic_fur_block.desc`.
- `ironwood_block` (`strength(5.0, 6.0)`, WOOD, `ignitedByLava`), `steeleaf_block` (`strength(5.0, 6.0)`,
  NETHERITE_BLOCK sound, map PLANT), `carminite_block` (`strength(1.5, 10.0)`, METAL, requires tool).

### 3.6 Trophies (`AbstractTrophyBlock`, `TrophyBlock`, `TrophyWallBlock`, `TrophyBlockEntity`)

| Boss variant | Floor / wall block | Trophy type (item backdrop model) | Pedestal comparator | Click / redstone sound (volume, pitch + rand*0.1) | Click particles (sent to players within 32) |
|---|---|---|---|---|---|
| NAGA | `naga_trophy` / `naga_wall_trophy` | GOLD (`trophy`) | 5 | `entity.twilightforest.naga.rattle` 1.25, 1.2 | 10 `crit`, spread over the block at y+0.25, gaussian velocity x0.02 |
| LICH | `lich_trophy` / `lich_wall_trophy` | GOLD | 6 | `entity.twilightforest.lich.ambient` 0.35, 1.1 | 5 `twilightforest:angry_lich` at y+0.5..0.75 |
| HYDRA | `hydra_trophy` / `hydra_wall_trophy` | GOLD | 12 | `entity.twilightforest.hydra.growl` 1.0, 1.2 | none |
| UR_GHAST | `ur_ghast_trophy` / `ur_ghast_wall_trophy` | GOLD | 13 | `entity.twilightforest.ur_ghast.ambient` 1.0, 0.6 | 10 redstone dust in the upper half |
| KNIGHT_PHANTOM | `knight_phantom_trophy` / wall | IRON (`trophy_minor`) | 8 | `entity.twilightforest.knight_phantom.ambient` 1.0, 1.1 | 10 item particles of a knightmetal sword |
| SNOW_QUEEN | `snow_queen_trophy` / wall | GOLD | 14 | `entity.twilightforest.snow_queen.ambient` 1.0, 0.9 | 20 `twilightforest:snow_warning` 5 blocks above, over a 3.25-wide area |
| MINOSHROOM | `minoshroom_trophy` / wall | IRON | 7 | `entity.twilightforest.minoshroom.ambient` 0.75, 0.7 | 10 block particles of the block below, within +-5 horizontally |
| ALPHA_YETI | `alpha_yeti_trophy` / wall | IRON | 9 | 1/50 `entity.twilightforest.alpha_yeti.roar`, else `...alpha_yeti.growl`; 0.75, 0.75 | 10 `splash` in the upper half |
| QUEST_RAM | `quest_ram_trophy` / wall | IRONWOOD (`trophy_quest`) | 1 | `entity.twilightforest.quest_ram.ambient` 1.0, 0.7 | 10 `twilightforest:magic_effect` with random colours and gaussian velocity |

- Properties: `instabreak`; floor trophy states `rotation` (0..15, from the player's yaw: floor(yaw*16/360 + 0.5) & 15)
  and `powered`; wall trophy `facing` (first horizontal nearest-looking direction whose block behind is not
  replaceable; placement fails if none) and `powered`. Wall trophies use the item's name. Not pathfindable.
- Right-click (any hand): plays the sound and the particles, succeeds. Redstone: when the neighbour signal turns on,
  play the sound; `powered` follows the signal.
- Shapes: default box(4,0,4, 12,8,12); ur-ghast box(4,8,4, 12,16,12) (floating); alpha yeti box(3.25,0,4.5,
  12.75,10,11.5) on x-facing rotations (0,1,7,8,9,15), z variant on rotations 3,4,5,11,12,13, corner box(4.5,0,4.5,
  11.5,10,11.5) on 2,6,10,14. Wall: box(4,4,8, 12,12,16) for north (rotated); alpha yeti wall box(3.25,4,8.5,
  12.75,14.5,16).
- Equipable on the head (HEAD slot); dispensers equip trophies on entities in front.
- Block entity `twilightforest:trophy`: ticks; ur-ghast trophies animate (`animatedTicks` increments every tick); the
  renderer uses animation progress x4.5 for model rotations.
- Renderer: the boss's trophy model (per-variant `TrophyBlockModel` layers, quest ram uses `QuestRamModel`), floor at
  (0.5, 0, 0.5) rotated 22.5 * rotation, wall at (0.5 - stepX*0.249, 0.25, 0.5 - stepZ*0.249). Item (ISTER): in the
  GUI a backdrop sprite model `item/trophy|trophy_minor|trophy_quest` behind the head, and the head spins
  `ClientGameEvents.time % 360` degrees when `rotateTrophyHeadsGui` (config, default **true**) and the game is not
  paused, else fixed at -45; ur-ghast items are drawn 0.5 higher, alpha yeti 0.15 lower.
- Items: `TrophyItem` (standing + wall block item, attach direction DOWN), rarity TWILIGHT (dark green name).
- Comparator: the trophy pedestal (U30) reads `pedestal_comparator_strength` above it; keep the table's values
  (exposed through F8's `boss::trophyVariant(blockId)` -> {sound, volume, pitch, particle, comparator}).

### 3.7 Boss spawners (`BossSpawnerBlock`)

Nine blocks `<boss>_boss_spawner` (NAGA, LICH, HYDRA, UR_GHAST, KNIGHT_PHANTOM, SNOW_QUEEN, MINOSHROOM, ALPHA_YETI,
FINAL_BOSS): `strength(-1.0, 3600000.8)` (unbreakable), METAL, `noOcclusion`, no loot table, render MODEL, cannot be
destroyed by entities. Shared tick (`BossSpawnerBlockEntity`, reproduced by F8's spawner kit): nothing once spawned or
while no living player is within range (default **9**, `LONG_RANGE` **50** for naga and hydra). Client: one spawner
particle per tick at a random point in the block. Server, difficulty not peaceful: spawn the boss (entity placed on
the block below when its collision shape is empty, else on the spawner; random yaw; `finalizeSpawn` with reason
SPAWNER; home/restriction point = the spawner position) and, if it was added, destroy the spawner without drops.
Per-boss rules (the boss units implement them through `registerSpawner`): naga `crit`, range 50; lich
`twilightforest:ominous_flame`, player must be above y-4; from the spawner it walks down while the block below is
air (stops at the minimum build height), then moves up one, so the lich spawns two blocks above the first non-air
block below (one above the spawner when it stands on a block); restriction point there, attack cooldown 40,
extinguish timer set;
hydra `flame`, range 50; ur-ghast redstone dust, player above y-4; knight phantoms `ominous_flame`, 6 phantoms in a
circle, player above y-2; snow queen `twilightforest:snow`, player above y-4; minoshroom `crimson_spore`, player below
y+4; alpha yeti `falling_water`, player above y-4; final boss `twilightforest:annihilate`, never spawns (PlateauBoss
placeholder).

### 3.8 Miniature structures (`MiniatureStructureBlock`)

`twilight_portal_miniature_structure` (`noCollission`, `noOcclusion`, requires tool, `strength(0.75)`) and four copies
(`naga_courtyard`, `lich_tower`, `minotaur_labyrinth`, `dark_tower`): `facing` = opposite of the player's horizontal
facing; empty visual shape, shade brightness 1.0, skylight passes. Models are TF's block models (imported). The portal
and courtyard miniatures tint `tintindex 1` with the biome foliage colour. (TF 4.8 comments out ten more miniatures:
not registered, not ported.)

### 3.9 Banner patterns

Nine `banner_pattern` registry entries `twilightforest:<boss>` (naga, lich, hydra, ur_ghast, knight_phantom,
snow_queen, minoshroom, alpha_yeti, questing_ram) with `asset_id` = the id and `translation_key`
`block.minecraft.banner.twilightforest.<boss>`; nine `BannerPatternItem`s (`<boss>_banner_pattern`, the quest ram one
is `quest_ram_banner_pattern` for pattern `questing_ram`) holding the pattern tag
`#twilightforest:pattern_item/<boss>`, stack size 1, rarity TWILIGHT. They are used in the loom like vanilla pattern
items. Eight shapeless recipes (Appendix A) craft `<boss>_banner_pattern` from `#c:paper` + the boss's trophy (the
trophy is consumed: plain vanilla shapeless recipe, no remainder). The quest ram pattern has no recipe in TF 4.8 (the
generator line is commented out) and no loot source: creative / commands only.

## 4. Bedrock adaptation notes

- Families (`assets/data/families/building.json`): stairs, `pillar` (twisted stone, bold stone pillar), `directional`
  (etched nagastone, `facing` 6 values), `horizontal` (nagastone head, miniatures), `pane` (canopy window pane),
  `waterloggable` (spiral bricks, knightmetal block, rope, twisted stone pillar). Nagastone variant, spiral diagonal,
  wall-pillar connections, iron ladder left/right and rope logic run in `blockevents::onPlaced` / `onNeighborChanged`.
- Rope collision (tightrope) and scaffolding climbing: Bedrock has no per-entity collision; use a thin collision box on
  horizontal strands and the client mod for climbing (`movement::onLocalTick`); vanilla clients can't climb ropes
  (best effort, not-ported.md lists it with climbable hollow logs).
- Knightmetal and fiery damage: `blockevents::onEntityInside` / `onSteppedOn` + `combat::hurt(victim, 4.0,
  "twilightforest:knightmetal")`.
- Arctic fur shears speed: Bedrock `minecraft:destructible_by_mining` with an item-specific speed for shears.
- Trophies: blocks with a trophy geometry per boss (F2 `tools/models/attachable.ps1 -Item <id>` for head-worn
  trophies; geometry `geometry.twilightforest.<boss>_trophy`); `rotation` 0..15 is an integer state declared as a list
  (assemble checks); sounds and particles through `fx`; head slot via `minecraft:wearable`. The GUI spin
  (`rotateTrophyHeadsGui`) is client-mod only (`client::render::registerItemRenderer`), vanilla shows a static icon.
  The ur-ghast tick animation is client rendering only.
- Boss spawners are plain unbreakable blocks with particles; F8's `boss::registerSpawner` drives them, the boss unit
  registers the per-boss rule above. Banner patterns depend on spike S6 (vanilla clients may get the item stripped).

## 5. Acceptance checklist (selftests `building.*`, dx 500-599)

- All 99 blocks with TF hardness/resistance/sound/tool; loot tables (rope one per axis; trophies drop themselves;
  spawners nothing).
- Nagastone variant computation for every 0/1/2 neighbour layout and nagastone head facing.
- Rope: placement axes, support rules, 1-tick decay dropping one rope per lost axis, item extension up to 7 blocks.
- Knightmetal block deals 4 knightmetal damage inside; fiery block 1 fiery damage unless fiery boots / fire immune.
- Trophy: rotation from yaw, wall facing, click sound + particles per variant, redstone `powered` + sound, comparator
  table, head equip, dispenser equip.
- Spawner block unbreakable and inert until a boss unit registers it.
- Banner pattern items work in a loom (or the documented S6 fallback).

## Appendix A: Owned ids (generated inventory)

Every id this unit owns in `docs/spec/owners.tsv`, with the TF 4.8 data the W1 worker must reproduce. Registration statements are TF's own Java (`init/TF*.java`), condensed (namespaces such as `MapColor.` dropped); loot, recipe, tag, advancement and data rows are summarised from TF's generated JSON (`src/generated/resources`). Loot notation: `self` = drops the block itself when it survives the explosion; `alternatives(a → b)` = first matching child; `[if ...]` = conditions; `{...}` = functions. In summaries `mc:` is `minecraft:` and ids without a namespace are `twilightforest:`.

| Kind | Count |
|---|---|
| `advancement` | 134 |
| `banner_pattern` | 9 |
| `block` | 99 |
| `block_entity` | 1 |
| `block_item` | 80 |
| `config_key` | 1 |
| `config_section` | 1 |
| `e2e` | 1 |
| `item` | 19 |
| `lang` | 112 |
| `loot_table` | 90 |
| `net` | 1 |
| `path` | 20 |
| `recipe` | 134 |
| `selftest` | 1 |
| `selftest_dx` | 1 |
| `tag` | 8 |
| `worlddb` | 1 |

### Unit facts

| Kind | Ids |
|---|---|
| Selftest glob (`selftest`) | `building.*` |
| Selftest dx range (`selftest_dx`) | `500-599` |
| World DB keys (`worlddb`) | `gf:building:*` |
| E2E scenario (`e2e`) | `building` |
| Net channels (`net`) | `building.*` |
| Config section (`config_section`) | `building` |
| Owned paths (`path`) | `**/*.d/building.json`, `assets/data/*/building.json`, `assets/gen/audio/building/**`, `assets/gen/geo/building/**`, `assets/gen/import/building/**`, `assets/lang/*/building.json`, `assets/pack/{bp,rp}/**/building/**`, `assets/pack/rp/texts.d/*/building.lang`, `assets/structures/building/**`, `assets/tags/**/*.d/building.json`, `docs/units/building.md`, `src/client/building/**`, `src/core/building/**`, `src/feature/building/**`, `src/feature/ext/building/**`, `tests/building/**`, `tools/e2e/*/scenarios/building{,.*}.ps1`, `tools/import/overrides/building.json`, `tools/sounds/recipes/building.json`, `tools/structures/building/**` |

### Blocks (99)

Item column: `BlockItem` = TF registers a plain block item with the block (`TFBlocks.register`), the id is also a `block_item` row; `-` = no item of the same id (the block is placed by another item, or has none).

Block states come from TF's blockstate JSON (`multipart:` lists only the property values the model parts test). Notes collect `RegistrationEvents` (fire, axe stripping, flower pots, jar lids), dispenser behaviours and the `neoforge:compostables` / `neoforge:furnace_fuels` data maps.

| Block | English name | TF registration (`TFBlocks`) | Block states | Item | Loot (`blocks/<id>`) | Notes |
|---|---|---|---|---|---|---|
| `twilightforest:alpha_yeti_boss_spawner` | Alpha Yeti Boss Spawner | `register("alpha_yeti_boss_spawner", new BossSpawnerBlock(BossVariant.ALPHA_YETI, Properties.of().mapColor(STONE).noLootTable().sound(METAL).noOcclusion().strength(-1.0F, 3600000.8F)));` |  | BlockItem | none (noLootTable) | block tags: #c:relocation_not_supported, #mc:dragon_immune, #mc:features_cannot_replace, #mc:wither_immune, #antibuilder_ignores, #block_and_chain_never_breaks, #carminite_reactor_immune, #common_protections, #small_lakes_dont_replace, #tree_roots_skip; item tags: #randomium:blacklist |
| `twilightforest:alpha_yeti_trophy` | Alpha Yeti Trophy | `BLOCKS.register("alpha_yeti_trophy", new TrophyBlock(BossVariant.ALPHA_YETI, 9, Properties.of().instabreak()));` |  | - | self | dispenser: equips it on an entity in front, like armor (ArmorItem.dispenseArmor) (TFDispenserBehaviors); block tags: #trophy_pedestal_activation_blocks; item tags: #curios:head, #trophies |
| `twilightforest:alpha_yeti_wall_trophy` |  | `BLOCKS.register("alpha_yeti_wall_trophy", new TrophyWallBlock(BossVariant.ALPHA_YETI, Properties.of().instabreak()));` |  | - | rolls 1: alpha_yeti_trophy [pool if survives_explosion] |  |
| `twilightforest:arctic_fur_block` | Block of Arctic Fur | `register("arctic_fur_block", new ArcticFurBlock(Properties.of().ignitedByLava().mapColor(WOOL).sound(WOOL).strength(0.8F)));` |  | BlockItem | self | flammable (encouragement 20, flammability 20); block tags: #c:storage_blocks, #c:storage_blocks/arctic_fur, #mc:dampens_vibrations, #mc:mineable/hoe, #mc:occludes_vibration_signals, #mineable_with_block_and_chain; item tags: #c:storage_blocks, #c:storage_blocks/arctic_fur |
| `twilightforest:blue_castle_rune_brick` | Blue Castle Rune Brick | `register("blue_castle_rune_brick", new Block(Properties.ofFullCopy(CASTLE_BRICK.get()).mapColor(DyeColor.LIGHT_BLUE)));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #annihilation_inclusions, #block_and_chain_never_breaks, #castle_blocks, #mazebreaker_accelerated_mining, #mineable_with_block_and_chain |
| `twilightforest:bold_castle_brick_pillar` | Bold Castle Brick Pillar | `register("bold_castle_brick_pillar", new RotatedPillarBlock(Properties.ofFullCopy(CASTLE_BRICK.get())));` | axis: x/y/z | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #block_and_chain_never_breaks, #castle_blocks, #mazebreaker_accelerated_mining, #mineable_with_block_and_chain |
| `twilightforest:bold_castle_brick_stairs` | Bold Castle Brick Stairs | `register("bold_castle_brick_stairs", new StairBlock(BOLD_CASTLE_BRICK_PILLAR.get().defaultBlockState(), Properties.ofFullCopy(BOLD_CASTLE_BRICK_PILLAR.get())));` | facing: east/north/south/west; half: bottom/top; shape: inner_left/inner_right/outer_left/outer_right/straight | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #mc:stairs, #block_and_chain_never_breaks, #castle_blocks, #mazebreaker_accelerated_mining, #mineable_with_block_and_chain; item tags: #mc:stairs |
| `twilightforest:bold_castle_brick_tile` | Bold Castle Brick Tile | `register("bold_castle_brick_tile", new Block(Properties.ofFullCopy(CASTLE_BRICK.get())));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #block_and_chain_never_breaks, #castle_blocks, #mazebreaker_accelerated_mining, #mineable_with_block_and_chain |
| `twilightforest:bold_stone_pillar` | Bold Stone Pillar | `register("bold_stone_pillar", new RotatedPillarBlock(Properties.of().instrument(BASEDRUM).mapColor(STONE).requiresCorrectToolForDrops().sound(STONE).strength(1.5F, 6.0F)));` | axis: x/y/z | BlockItem | self | block tags: #mc:mineable/pickaxe, #neoforge:needs_wood_tool, #mineable_with_block_and_chain |
| `twilightforest:canopy_window` | Canopy Window | `register("canopy_window", new TransparentBlock(Properties.of().instrument(HAT).strength(0.3F).sound(GLASS).noOcclusion().isValidSpawn((pState, pLevel, pPos, pValue) -> false).isRedstoneConductor((pState, pLevel, pPos) -> false).isSuffocating((pState, pLevel, pPos) -> false).isViewBlocking((pState, pLevel, pPos) -> false)));` |  | BlockItem | rolls 1: canopy_window [pool if match_tool(mc:silk_touch>=1)] |  |
| `twilightforest:canopy_window_pane` | Canopy Window Pane | `register("canopy_window_pane", new IronBarsBlock(Properties.of().instrument(HAT).strength(0.3F).sound(GLASS).noOcclusion()));` | multipart: north: true/false; south: true/false; west: true/false; east: true/false | BlockItem | rolls 1: canopy_window_pane [pool if match_tool(mc:silk_touch>=1)] |  |
| `twilightforest:carminite_block` | Block of Carminite | `register("carminite_block", new Block(Properties.of().mapColor(COLOR_RED).strength(1.5F, 10.0F).requiresCorrectToolForDrops().sound(METAL)));` |  | BlockItem | self | block tags: #c:storage_blocks, #c:storage_blocks/carminite, #mc:mineable/pickaxe, #mineable_with_block_and_chain; item tags: #c:storage_blocks, #c:storage_blocks/carminite |
| `twilightforest:castle_brick` | Castle Brick | `register("castle_brick", new Block(Properties.of().instrument(BASEDRUM).mapColor(QUARTZ).requiresCorrectToolForDrops().sound(DEEPSLATE_TILES).strength(100.0F, 50.0F)));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #annihilation_inclusions, #block_and_chain_never_breaks, #castle_blocks, #mazebreaker_accelerated_mining, #mineable_with_block_and_chain |
| `twilightforest:castle_brick_stairs` | Castle Brick Stairs | `register("castle_brick_stairs", new StairBlock(CASTLE_BRICK.get().defaultBlockState(), Properties.ofFullCopy(CASTLE_BRICK.get())));` | facing: east/north/south/west; half: bottom/top; shape: inner_left/inner_right/outer_left/outer_right/straight | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #mc:stairs, #block_and_chain_never_breaks, #castle_blocks, #mazebreaker_accelerated_mining, #mineable_with_block_and_chain; item tags: #mc:stairs |
| `twilightforest:castle_roof_tile` | Castle Roof Tile | `register("castle_roof_tile", new Block(Properties.ofFullCopy(CASTLE_BRICK.get()).mapColor(COLOR_GRAY)));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #annihilation_inclusions, #block_and_chain_never_breaks, #castle_blocks, #mazebreaker_accelerated_mining, #mineable_with_block_and_chain |
| `twilightforest:cracked_castle_brick` | Cracked Castle Brick | `register("cracked_castle_brick", new Block(Properties.ofFullCopy(CASTLE_BRICK.get())));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #block_and_chain_never_breaks, #castle_blocks, #mazebreaker_accelerated_mining, #mineable_with_block_and_chain |
| `twilightforest:cracked_castle_brick_stairs` | Cracked Castle Brick Stairs | `register("cracked_castle_brick_stairs", new StairBlock(CRACKED_CASTLE_BRICK.get().defaultBlockState(), Properties.ofFullCopy(CRACKED_CASTLE_BRICK.get())));` | facing: east/north/south/west; half: bottom/top; shape: inner_left/inner_right/outer_left/outer_right/straight | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #mc:stairs, #block_and_chain_never_breaks, #castle_blocks, #mazebreaker_accelerated_mining, #mineable_with_block_and_chain; item tags: #mc:stairs |
| `twilightforest:cracked_deadrock` | Cracked Deadrock | `register("cracked_deadrock", new Block(Properties.ofFullCopy(DEADROCK.get())));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #annihilation_inclusions, #block_and_chain_never_breaks, #deadrock, #mineable_with_block_and_chain, #supports_stalagmites |
| `twilightforest:cracked_etched_nagastone` | Cracked Etched Nagastone | `register("cracked_etched_nagastone", new EtchedNagastoneBlock(Properties.of().instrument(BASEDRUM).mapColor(STONE).requiresCorrectToolForDrops().sound(STONE).strength(1.5F, 6.0F)));` | facing: down/east/north/south/up/west | BlockItem | self | block tags: #mc:mineable/pickaxe, #neoforge:needs_wood_tool, #mineable_with_block_and_chain |
| `twilightforest:cracked_mazestone` | Cracked Mazestone Brick | `register("cracked_mazestone", new Block(Properties.ofFullCopy(MAZESTONE.get())));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #block_and_chain_never_breaks, #mazebreaker_accelerated_mining, #mazestone, #mineable_with_block_and_chain |
| `twilightforest:cracked_nagastone_pillar` | Cracked Nagastone Pillar | `register("cracked_nagastone_pillar", new DirectionalRotatedPillarBlock(Properties.of().instrument(BASEDRUM).mapColor(STONE).requiresCorrectToolForDrops().sound(STONE).strength(1.5F, 6.0F)));` | axis: x/y/z; reversed: false/true | BlockItem | self | block tags: #mc:mineable/pickaxe, #neoforge:needs_wood_tool, #mineable_with_block_and_chain |
| `twilightforest:cracked_nagastone_stairs_left` | Cracked Nagastone Stairs (Left) | `register("cracked_nagastone_stairs_left", new StairBlock(CRACKED_ETCHED_NAGASTONE.get().defaultBlockState(), Properties.ofFullCopy(CRACKED_ETCHED_NAGASTONE.get())));` | facing: east/north/south/west; half: bottom/top; shape: inner_left/inner_right/outer_left/outer_right/straight | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:stairs, #neoforge:needs_wood_tool, #mineable_with_block_and_chain; item tags: #mc:stairs |
| `twilightforest:cracked_nagastone_stairs_right` | Cracked Nagastone Stairs (Right) | `register("cracked_nagastone_stairs_right", new StairBlock(CRACKED_ETCHED_NAGASTONE.get().defaultBlockState(), Properties.ofFullCopy(CRACKED_ETCHED_NAGASTONE.get())));` | facing: east/north/south/west; half: bottom/top; shape: inner_left/inner_right/outer_left/outer_right/straight | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:stairs, #neoforge:needs_wood_tool, #mineable_with_block_and_chain; item tags: #mc:stairs |
| `twilightforest:cracked_towerwood` | Cracked Towerwood Planks | `register("cracked_towerwood", new Block(Properties.ofFullCopy(TOWERWOOD.get())));` |  | BlockItem | self | flammable (encouragement 0, flammability 1); block tags: #mc:mineable/axe, #mc:planks, #druid_projectile_replaceable, #mineable_with_block_and_chain, #towerwood; item tags: #mc:planks, #towerwood |
| `twilightforest:cracked_underbrick` | Cracked Underbrick | `register("cracked_underbrick", new Block(Properties.ofFullCopy(UNDERBRICK.get())));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_stone_tool, #mineable_with_block_and_chain |
| `twilightforest:cut_mazestone` | Cut Mazestone | `register("cut_mazestone", new Block(Properties.ofFullCopy(MAZESTONE.get())));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #block_and_chain_never_breaks, #mazebreaker_accelerated_mining, #mazestone, #mineable_with_block_and_chain |
| `twilightforest:dark_tower_miniature_structure` | Miniature Dark Tower | `register("dark_tower_miniature_structure", new MiniatureStructureBlock(Properties.ofFullCopy(TWILIGHT_PORTAL_MINIATURE_STRUCTURE.get())));` | facing: east/north/south/west | BlockItem | self | block tags: #mc:mineable/pickaxe, #mineable_with_block_and_chain |
| `twilightforest:deadrock` | Deadrock | `register("deadrock", new Block(Properties.of().instrument(BASEDRUM).mapColor(STONE).requiresCorrectToolForDrops().sound(STONE).strength(100.0F, 6000000.0F)));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #annihilation_inclusions, #block_and_chain_never_breaks, #deadrock, #mineable_with_block_and_chain, #supports_stalagmites |
| `twilightforest:decorative_mazestone` | Decorative Mazestone Brick | `register("decorative_mazestone", new Block(Properties.ofFullCopy(MAZESTONE.get())));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #block_and_chain_never_breaks, #mazebreaker_accelerated_mining, #mazestone, #mineable_with_block_and_chain |
| `twilightforest:encased_castle_brick_pillar` | Encased Castle Brick Pillar | `register("encased_castle_brick_pillar", new RotatedPillarBlock(Properties.ofFullCopy(CASTLE_BRICK.get())));` | axis: x/y/z | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #block_and_chain_never_breaks, #castle_blocks, #mazebreaker_accelerated_mining, #mineable_with_block_and_chain |
| `twilightforest:encased_castle_brick_stairs` | Encased Castle Brick Stairs | `register("encased_castle_brick_stairs", new StairBlock(ENCASED_CASTLE_BRICK_PILLAR.get().defaultBlockState(), Properties.ofFullCopy(ENCASED_CASTLE_BRICK_PILLAR.get())));` | facing: east/north/south/west; half: bottom/top; shape: inner_left/inner_right/outer_left/outer_right/straight | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #mc:stairs, #block_and_chain_never_breaks, #castle_blocks, #mazebreaker_accelerated_mining, #mineable_with_block_and_chain; item tags: #mc:stairs |
| `twilightforest:encased_castle_brick_tile` | Encased Castle Brick Tile | `register("encased_castle_brick_tile", new Block(Properties.ofFullCopy(CASTLE_BRICK.get())));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #block_and_chain_never_breaks, #castle_blocks, #mazebreaker_accelerated_mining, #mineable_with_block_and_chain |
| `twilightforest:encased_towerwood` | Encased Towerwood Planks | `register("encased_towerwood", new Block(Properties.ofFullCopy(TOWERWOOD.get()).mapColor(SAND)));` |  | BlockItem | self | flammable (encouragement 0, flammability 1); block tags: #mc:mineable/axe, #mineable_with_block_and_chain |
| `twilightforest:etched_nagastone` | Etched Nagastone | `register("etched_nagastone", new EtchedNagastoneBlock(Properties.of().instrument(BASEDRUM).mapColor(STONE).requiresCorrectToolForDrops().sound(STONE).strength(1.5F, 6.0F)));` | facing: down/east/north/south/up/west | BlockItem | self | block tags: #mc:mineable/pickaxe, #neoforge:needs_wood_tool, #mineable_with_block_and_chain |
| `twilightforest:fiery_block` | Block of Fiery Metal | `registerFireResistantItem("fiery_block", new FieryBlock(Properties.of().mapColor(TERRACOTTA_BLACK).noOcclusion().requiresCorrectToolForDrops().sound(METAL).strength(5.0F, 6.0F).emissiveRendering((state, world, pos) -> true)));` |  | BlockItem (fireResistant, UNCOMMON) | self | block tags: #c:storage_blocks, #c:storage_blocks/fiery, #create:passive_boiler_heaters, #farmersdelight:heat_sources, #mc:beacon_base_blocks, #mc:mineable/pickaxe, #mc:needs_iron_tool, #mc:strider_warm_blocks, #mineable_with_block_and_chain; item tags: #c:storage_blocks, #c:storage_blocks/fiery |
| `twilightforest:final_boss_boss_spawner` | Final Boss Boss Spawner | `register("final_boss_boss_spawner", new BossSpawnerBlock(BossVariant.FINAL_BOSS, Properties.of().mapColor(STONE).noLootTable().sound(METAL).noOcclusion().strength(-1.0F, 3600000.8F)));` |  | BlockItem | none (noLootTable) | block tags: #c:relocation_not_supported, #mc:dragon_immune, #mc:features_cannot_replace, #mc:wither_immune, #antibuilder_ignores, #block_and_chain_never_breaks, #carminite_reactor_immune, #common_protections, #small_lakes_dont_replace, #tree_roots_skip; item tags: #randomium:blacklist, #wip |
| `twilightforest:hydra_boss_spawner` | Hydra Boss Spawner | `register("hydra_boss_spawner", new BossSpawnerBlock(BossVariant.HYDRA, Properties.of().mapColor(STONE).noLootTable().sound(METAL).noOcclusion().strength(-1.0F, 3600000.8F)));` |  | BlockItem | none (noLootTable) | block tags: #c:relocation_not_supported, #mc:dragon_immune, #mc:features_cannot_replace, #mc:wither_immune, #antibuilder_ignores, #block_and_chain_never_breaks, #carminite_reactor_immune, #common_protections, #small_lakes_dont_replace, #tree_roots_skip; item tags: #randomium:blacklist |
| `twilightforest:hydra_trophy` | Hydra Trophy | `BLOCKS.register("hydra_trophy", new TrophyBlock(BossVariant.HYDRA, 12, Properties.of().instabreak()));` |  | - | self | dispenser: equips it on an entity in front, like armor (ArmorItem.dispenseArmor) (TFDispenserBehaviors); block tags: #trophy_pedestal_activation_blocks; item tags: #curios:head, #trophies |
| `twilightforest:hydra_wall_trophy` |  | `BLOCKS.register("hydra_wall_trophy", new TrophyWallBlock(BossVariant.HYDRA, Properties.of().instabreak()));` |  | - | rolls 1: hydra_trophy [pool if survives_explosion] |  |
| `twilightforest:iron_ladder` | Iron Ladder | `register("iron_ladder", new IronLadderBlock(Properties.of().forceSolidOff().noOcclusion().pushReaction(DESTROY).requiresCorrectToolForDrops().sound(METAL).strength(5.0F, 6.0F)));` | multipart: facing: north/east/south/west; left: false/true; right: false/true | BlockItem | self | block tags: #mc:climbable, #mc:mineable/pickaxe, #mc:needs_stone_tool, #mineable_with_block_and_chain |
| `twilightforest:ironwood_block` | Block of Ironwood | `register("ironwood_block", new Block(Properties.of().ignitedByLava().mapColor(WOOD).sound(WOOD).strength(5.0F, 6.0F)));` |  | BlockItem | self | block tags: #alexscaves:ferromagnetic_blocks, #c:storage_blocks, #c:storage_blocks/ironwood, #mc:beacon_base_blocks, #mc:mineable/axe, #mc:mineable/pickaxe, #mineable_with_block_and_chain; item tags: #alexscaves:ferromagnetic_items, #c:storage_blocks, #c:storage_blocks/ironwood |
| `twilightforest:knight_phantom_boss_spawner` | Knight Phantoms Boss Spawner | `register("knight_phantom_boss_spawner", new BossSpawnerBlock(BossVariant.KNIGHT_PHANTOM, Properties.of().mapColor(STONE).noLootTable().sound(METAL).noOcclusion().strength(-1.0F, 3600000.8F)));` |  | BlockItem | none (noLootTable) | block tags: #c:relocation_not_supported, #mc:dragon_immune, #mc:features_cannot_replace, #mc:wither_immune, #antibuilder_ignores, #block_and_chain_never_breaks, #carminite_reactor_immune, #common_protections, #small_lakes_dont_replace, #tree_roots_skip; item tags: #randomium:blacklist |
| `twilightforest:knight_phantom_trophy` | Knight Phantom Trophy | `BLOCKS.register("knight_phantom_trophy", new TrophyBlock(BossVariant.KNIGHT_PHANTOM, 8, Properties.of().instabreak()));` |  | - | self | dispenser: equips it on an entity in front, like armor (ArmorItem.dispenseArmor) (TFDispenserBehaviors); block tags: #trophy_pedestal_activation_blocks; item tags: #curios:head, #trophies |
| `twilightforest:knight_phantom_wall_trophy` |  | `BLOCKS.register("knight_phantom_wall_trophy", new TrophyWallBlock(BossVariant.KNIGHT_PHANTOM, Properties.of().instabreak()));` |  | - | rolls 1: knight_phantom_trophy [pool if survives_explosion] |  |
| `twilightforest:knightmetal_block` | Block of Knightmetal | `register("knightmetal_block", new KnightmetalBlock(Properties.of().mapColor(METAL).requiresCorrectToolForDrops().sound(NETHERITE_BLOCK).strength(5.0F, 40.0F)));` |  | BlockItem | self | block tags: #alexscaves:ferromagnetic_blocks, #c:storage_blocks, #c:storage_blocks/knightmetal, #mc:beacon_base_blocks, #mc:mineable/pickaxe, #mc:needs_iron_tool, #mineable_with_block_and_chain; item tags: #alexscaves:ferromagnetic_items, #c:storage_blocks, #c:storage_blocks/knightmetal |
| `twilightforest:lich_boss_spawner` | Lich Boss Spawner | `register("lich_boss_spawner", new BossSpawnerBlock(BossVariant.LICH, Properties.of().mapColor(STONE).noLootTable().sound(METAL).noOcclusion().strength(-1.0F, 3600000.8F)));` |  | BlockItem | none (noLootTable) | block tags: #c:relocation_not_supported, #mc:dragon_immune, #mc:features_cannot_replace, #mc:wither_immune, #antibuilder_ignores, #block_and_chain_never_breaks, #carminite_reactor_immune, #common_protections, #small_lakes_dont_replace, #tree_roots_skip; item tags: #randomium:blacklist |
| `twilightforest:lich_tower_miniature_structure` | Miniature Lich Tower | `register("lich_tower_miniature_structure", new MiniatureStructureBlock(Properties.ofFullCopy(TWILIGHT_PORTAL_MINIATURE_STRUCTURE.get())));` | facing: east/north/south/west | BlockItem | self | block tags: #mc:mineable/pickaxe, #mineable_with_block_and_chain |
| `twilightforest:lich_trophy` | Twilight Lich Trophy | `BLOCKS.register("lich_trophy", new TrophyBlock(BossVariant.LICH, 6, Properties.of().instabreak()));` |  | - | self | dispenser: equips it on an entity in front, like armor (ArmorItem.dispenseArmor) (TFDispenserBehaviors); block tags: #trophy_pedestal_activation_blocks; item tags: #curios:head, #trophies |
| `twilightforest:lich_wall_trophy` |  | `BLOCKS.register("lich_wall_trophy", new TrophyWallBlock(BossVariant.LICH, Properties.of().instabreak()));` |  | - | rolls 1: lich_trophy [pool if survives_explosion] |  |
| `twilightforest:mazestone` | Mazestone | `register("mazestone", new Block(Properties.of().instrument(BASEDRUM).mapColor(STONE).requiresCorrectToolForDrops().sound(STONE).strength(100.0F, 5.0F)));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #block_and_chain_never_breaks, #mazebreaker_accelerated_mining, #mazestone, #mineable_with_block_and_chain |
| `twilightforest:mazestone_border` | Mazestone Border | `register("mazestone_border", new Block(Properties.ofFullCopy(MAZESTONE.get())));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #block_and_chain_never_breaks, #mazebreaker_accelerated_mining, #mazestone, #mineable_with_block_and_chain |
| `twilightforest:mazestone_brick` | Mazestone Brick | `register("mazestone_brick", new Block(Properties.ofFullCopy(MAZESTONE.get())));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #block_and_chain_never_breaks, #mazebreaker_accelerated_mining, #mazestone, #mineable_with_block_and_chain |
| `twilightforest:mazestone_mosaic` | Mazestone Mosaic | `register("mazestone_mosaic", new Block(Properties.ofFullCopy(MAZESTONE.get())));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #block_and_chain_never_breaks, #mazebreaker_accelerated_mining, #mazestone, #mineable_with_block_and_chain |
| `twilightforest:minoshroom_boss_spawner` | Minoshroom Boss Spawner | `register("minoshroom_boss_spawner", new BossSpawnerBlock(BossVariant.MINOSHROOM, Properties.of().mapColor(STONE).noLootTable().sound(METAL).noOcclusion().strength(-1.0F, 3600000.8F)));` |  | BlockItem | none (noLootTable) | block tags: #c:relocation_not_supported, #mc:dragon_immune, #mc:features_cannot_replace, #mc:wither_immune, #antibuilder_ignores, #block_and_chain_never_breaks, #carminite_reactor_immune, #common_protections, #small_lakes_dont_replace, #tree_roots_skip; item tags: #randomium:blacklist |
| `twilightforest:minoshroom_trophy` | Minoshroom Trophy | `BLOCKS.register("minoshroom_trophy", new TrophyBlock(BossVariant.MINOSHROOM, 7, Properties.of().instabreak()));` |  | - | self | dispenser: equips it on an entity in front, like armor (ArmorItem.dispenseArmor) (TFDispenserBehaviors); block tags: #trophy_pedestal_activation_blocks; item tags: #curios:head, #trophies |
| `twilightforest:minoshroom_wall_trophy` |  | `BLOCKS.register("minoshroom_wall_trophy", new TrophyWallBlock(BossVariant.MINOSHROOM, Properties.of().instabreak()));` |  | - | rolls 1: minoshroom_trophy [pool if survives_explosion] |  |
| `twilightforest:minotaur_labyrinth_miniature_structure` | Miniature Minotaur Labyrinth | `register("minotaur_labyrinth_miniature_structure", new MiniatureStructureBlock(Properties.ofFullCopy(TWILIGHT_PORTAL_MINIATURE_STRUCTURE.get())));` | facing: east/north/south/west | BlockItem | self | block tags: #mc:mineable/pickaxe, #mineable_with_block_and_chain |
| `twilightforest:mossy_castle_brick` | Mossy Castle Brick | `register("mossy_castle_brick", new Block(Properties.ofFullCopy(CASTLE_BRICK.get())));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #annihilation_inclusions, #block_and_chain_never_breaks, #castle_blocks, #mazebreaker_accelerated_mining, #mineable_with_block_and_chain |
| `twilightforest:mossy_castle_brick_stairs` | Mossy Castle Brick Stairs | `register("mossy_castle_brick_stairs", new StairBlock(MOSSY_CASTLE_BRICK.get().defaultBlockState(), Properties.ofFullCopy(MOSSY_CASTLE_BRICK.get())));` | facing: east/north/south/west; half: bottom/top; shape: inner_left/inner_right/outer_left/outer_right/straight | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #mc:stairs, #block_and_chain_never_breaks, #castle_blocks, #mazebreaker_accelerated_mining, #mineable_with_block_and_chain; item tags: #mc:stairs |
| `twilightforest:mossy_etched_nagastone` | Mossy Etched Nagastone | `register("mossy_etched_nagastone", new EtchedNagastoneBlock(Properties.of().instrument(BASEDRUM).mapColor(STONE).requiresCorrectToolForDrops().sound(STONE).strength(1.5F, 6.0F)));` | facing: down/east/north/south/up/west | BlockItem | self | block tags: #mc:mineable/pickaxe, #neoforge:needs_wood_tool, #mineable_with_block_and_chain |
| `twilightforest:mossy_mazestone` | Mossy Mazestone Brick | `register("mossy_mazestone", new Block(Properties.ofFullCopy(MAZESTONE.get())));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #block_and_chain_never_breaks, #mazebreaker_accelerated_mining, #mazestone, #mineable_with_block_and_chain |
| `twilightforest:mossy_nagastone_pillar` | Mossy Nagastone Pillar | `register("mossy_nagastone_pillar", new DirectionalRotatedPillarBlock(Properties.of().instrument(BASEDRUM).mapColor(STONE).requiresCorrectToolForDrops().sound(STONE).strength(1.5F, 6.0F)));` | axis: x/y/z; reversed: false/true | BlockItem | self | block tags: #mc:mineable/pickaxe, #neoforge:needs_wood_tool, #mineable_with_block_and_chain |
| `twilightforest:mossy_nagastone_stairs_left` | Mossy Nagastone Stairs (Left) | `register("mossy_nagastone_stairs_left", new StairBlock(MOSSY_ETCHED_NAGASTONE.get().defaultBlockState(), Properties.ofFullCopy(MOSSY_ETCHED_NAGASTONE.get())));` | facing: east/north/south/west; half: bottom/top; shape: inner_left/inner_right/outer_left/outer_right/straight | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:stairs, #neoforge:needs_wood_tool, #mineable_with_block_and_chain; item tags: #mc:stairs |
| `twilightforest:mossy_nagastone_stairs_right` | Mossy Nagastone Stairs (Right) | `register("mossy_nagastone_stairs_right", new StairBlock(MOSSY_ETCHED_NAGASTONE.get().defaultBlockState(), Properties.ofFullCopy(MOSSY_ETCHED_NAGASTONE.get())));` | facing: east/north/south/west; half: bottom/top; shape: inner_left/inner_right/outer_left/outer_right/straight | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:stairs, #neoforge:needs_wood_tool, #mineable_with_block_and_chain; item tags: #mc:stairs |
| `twilightforest:mossy_towerwood` | Mossy Towerwood Planks | `register("mossy_towerwood", new Block(Properties.ofFullCopy(TOWERWOOD.get())));` |  | BlockItem | self | flammable (encouragement 0, flammability 1); block tags: #mc:mineable/axe, #mc:planks, #druid_projectile_replaceable, #mineable_with_block_and_chain, #towerwood; item tags: #mc:planks, #towerwood |
| `twilightforest:mossy_underbrick` | Mossy Underbrick | `register("mossy_underbrick", new Block(Properties.ofFullCopy(UNDERBRICK.get())));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_stone_tool, #mineable_with_block_and_chain |
| `twilightforest:naga_boss_spawner` | Naga Boss Spawner | `register("naga_boss_spawner", new BossSpawnerBlock(BossVariant.NAGA, Properties.of().mapColor(STONE).noLootTable().sound(METAL).noOcclusion().strength(-1.0F, 3600000.8F)));` |  | BlockItem | none (noLootTable) | block tags: #c:relocation_not_supported, #mc:dragon_immune, #mc:features_cannot_replace, #mc:wither_immune, #antibuilder_ignores, #block_and_chain_never_breaks, #carminite_reactor_immune, #common_protections, #small_lakes_dont_replace, #tree_roots_skip; item tags: #randomium:blacklist |
| `twilightforest:naga_courtyard_miniature_structure` | Miniature Naga Courtyard | `register("naga_courtyard_miniature_structure", new MiniatureStructureBlock(Properties.ofFullCopy(TWILIGHT_PORTAL_MINIATURE_STRUCTURE.get())));` | facing: east/north/south/west | BlockItem | self | block tags: #mc:mineable/pickaxe, #mineable_with_block_and_chain |
| `twilightforest:naga_trophy` | Naga Trophy | `BLOCKS.register("naga_trophy", new TrophyBlock(BossVariant.NAGA, 5, Properties.of().instabreak()));` |  | - | self | dispenser: equips it on an entity in front, like armor (ArmorItem.dispenseArmor) (TFDispenserBehaviors); block tags: #trophy_pedestal_activation_blocks; item tags: #curios:head, #trophies |
| `twilightforest:naga_wall_trophy` |  | `BLOCKS.register("naga_wall_trophy", new TrophyWallBlock(BossVariant.NAGA, Properties.of().instabreak()));` |  | - | rolls 1: naga_trophy [pool if survives_explosion] |  |
| `twilightforest:nagastone` | Nagastone | `register("nagastone", new NagastoneBlock(Properties.of().instrument(BASEDRUM).mapColor(STONE).requiresCorrectToolForDrops().sound(STONE).strength(1.5F, 6.0F)));` | variant: axis_x/axis_y/axis_z/east_down/east_up/north_down/north_up/solid/south_down/south_up/west_down/west_up | BlockItem | self | block tags: #mc:mineable/pickaxe, #neoforge:needs_wood_tool, #mineable_with_block_and_chain |
| `twilightforest:nagastone_head` | Nagastone Head | `register("nagastone_head", new TFHorizontalBlock(Properties.of().instrument(BASEDRUM).mapColor(STONE).requiresCorrectToolForDrops().sound(STONE).strength(1.5F, 6.0F)));` | facing: east/north/south/west | BlockItem | self | block tags: #mc:mineable/pickaxe, #neoforge:needs_wood_tool, #mineable_with_block_and_chain |
| `twilightforest:nagastone_pillar` | Nagastone Pillar | `register("nagastone_pillar", new DirectionalRotatedPillarBlock(Properties.of().instrument(BASEDRUM).mapColor(STONE).requiresCorrectToolForDrops().sound(STONE).strength(1.5F, 6.0F)));` | axis: x/y/z; reversed: false/true | BlockItem | self | block tags: #mc:mineable/pickaxe, #neoforge:needs_wood_tool, #mineable_with_block_and_chain |
| `twilightforest:nagastone_stairs_left` | Nagastone Stairs (Left) | `register("nagastone_stairs_left", new StairBlock(ETCHED_NAGASTONE.get().defaultBlockState(), Properties.ofFullCopy(ETCHED_NAGASTONE.get())));` | facing: east/north/south/west; half: bottom/top; shape: inner_left/inner_right/outer_left/outer_right/straight | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:stairs, #neoforge:needs_wood_tool, #mineable_with_block_and_chain; item tags: #mc:stairs |
| `twilightforest:nagastone_stairs_right` | Nagastone Stairs (Right) | `register("nagastone_stairs_right", new StairBlock(ETCHED_NAGASTONE.get().defaultBlockState(), Properties.ofFullCopy(ETCHED_NAGASTONE.get())));` | facing: east/north/south/west; half: bottom/top; shape: inner_left/inner_right/outer_left/outer_right/straight | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:stairs, #neoforge:needs_wood_tool, #mineable_with_block_and_chain; item tags: #mc:stairs |
| `twilightforest:pink_castle_rune_brick` | Magenta Castle Rune Brick | `register("pink_castle_rune_brick", new Block(Properties.ofFullCopy(CASTLE_BRICK.get()).mapColor(DyeColor.MAGENTA)));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #annihilation_inclusions, #block_and_chain_never_breaks, #castle_blocks, #mazebreaker_accelerated_mining, #mineable_with_block_and_chain |
| `twilightforest:quest_ram_trophy` | Questing Ram Trophy | `BLOCKS.register("quest_ram_trophy", new TrophyBlock(BossVariant.QUEST_RAM, 1, Properties.of().instabreak()));` |  | - | self | dispenser: equips it on an entity in front, like armor (ArmorItem.dispenseArmor) (TFDispenserBehaviors); item tags: #curios:head |
| `twilightforest:quest_ram_wall_trophy` |  | `BLOCKS.register("quest_ram_wall_trophy", new TrophyWallBlock(BossVariant.QUEST_RAM, Properties.of().instabreak()));` |  | - | rolls 1: quest_ram_trophy [pool if survives_explosion] |  |
| `twilightforest:rope` | Rope | `BLOCKS.register("rope", new RopeBlock(Properties.of().forceSolidOff().noOcclusion().pushReaction(DESTROY).sound(WOOL).strength(0.3F, 3.0F)));` | multipart: y: true; x: true; z: true | - | rolls 1: rope {count 1 (add) [if state x=true]; count 1 (add) [if state y=true]; count 1 (add) [if state z=true]; count -1 (add); explosion_decay} | block tags: #c:ropes, #mc:climbable; item tags: #c:ropes |
| `twilightforest:snow_queen_boss_spawner` | Snow Queen Boss Spawner | `register("snow_queen_boss_spawner", new BossSpawnerBlock(BossVariant.SNOW_QUEEN, Properties.of().mapColor(STONE).noLootTable().sound(METAL).noOcclusion().strength(-1.0F, 3600000.8F)));` |  | BlockItem | none (noLootTable) | block tags: #c:relocation_not_supported, #mc:dragon_immune, #mc:features_cannot_replace, #mc:wither_immune, #antibuilder_ignores, #block_and_chain_never_breaks, #carminite_reactor_immune, #common_protections, #small_lakes_dont_replace, #tree_roots_skip; item tags: #randomium:blacklist |
| `twilightforest:snow_queen_trophy` | Snow Queen Trophy | `BLOCKS.register("snow_queen_trophy", new TrophyBlock(BossVariant.SNOW_QUEEN, 14, Properties.of().instabreak()));` |  | - | self | dispenser: equips it on an entity in front, like armor (ArmorItem.dispenseArmor) (TFDispenserBehaviors); block tags: #trophy_pedestal_activation_blocks; item tags: #curios:head, #trophies |
| `twilightforest:snow_queen_wall_trophy` |  | `BLOCKS.register("snow_queen_wall_trophy", new TrophyWallBlock(BossVariant.SNOW_QUEEN, Properties.of().instabreak()));` |  | - | rolls 1: snow_queen_trophy [pool if survives_explosion] |  |
| `twilightforest:spiral_bricks` | Spiral Bricks | `register("spiral_bricks", new SpiralBrickBlock(Properties.of().instrument(BASEDRUM).mapColor(STONE).noOcclusion().requiresCorrectToolForDrops().sound(STONE).strength(1.5F, 6.0F)));` | axis: x/y/z; diagonal: bottom_left/bottom_right/top_left/top_right | BlockItem | self | block tags: #mc:mineable/pickaxe, #neoforge:needs_wood_tool, #mineable_with_block_and_chain |
| `twilightforest:steeleaf_block` | Block of Steeleaf | `register("steeleaf_block", new Block(Properties.of().mapColor(PLANT).sound(NETHERITE_BLOCK).strength(5.0F, 6.0F)));` |  | BlockItem | self | block tags: #alexscaves:ferromagnetic_blocks, #c:storage_blocks, #c:storage_blocks/steeleaf, #mc:beacon_base_blocks, #mc:mineable/hoe, #mineable_with_block_and_chain, #portal/decoration; item tags: #alexscaves:ferromagnetic_items, #c:storage_blocks, #c:storage_blocks/steeleaf |
| `twilightforest:thick_castle_brick` | Thick Castle Brick | `register("thick_castle_brick", new Block(Properties.ofFullCopy(CASTLE_BRICK.get())));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #annihilation_inclusions, #block_and_chain_never_breaks, #castle_blocks, #mazebreaker_accelerated_mining, #mineable_with_block_and_chain |
| `twilightforest:towerwood` | Towerwood Planks | `register("towerwood", new Block(Properties.of().ignitedByLava().instrument(BASS).mapColor(COLOR_ORANGE).strength(40.0F, 6.0F).sound(WOOD)));` |  | BlockItem | self | flammable (encouragement 0, flammability 1); block tags: #mc:mineable/axe, #mc:planks, #druid_projectile_replaceable, #mineable_with_block_and_chain, #towerwood; item tags: #mc:planks, #towerwood |
| `twilightforest:twilight_portal_miniature_structure` | Miniature Twilight Forest Portal | `register("twilight_portal_miniature_structure", new MiniatureStructureBlock(Properties.of().noCollission().noOcclusion().requiresCorrectToolForDrops().strength(0.75F)));` | facing: east/north/south/west | BlockItem | self | block tags: #mc:mineable/pickaxe, #mineable_with_block_and_chain |
| `twilightforest:twisted_stone` | Twisted Stone | `register("twisted_stone", new RotatedPillarBlock(Properties.of().instrument(BASEDRUM).mapColor(STONE).requiresCorrectToolForDrops().sound(STONE).strength(1.5F, 6.0F)));` | axis: x/y/z | BlockItem | self | block tags: #mc:mineable/pickaxe, #neoforge:needs_wood_tool, #mineable_with_block_and_chain |
| `twilightforest:twisted_stone_pillar` | Twisted Stone Pillar | `register("twisted_stone_pillar", new WallPillarBlock(Properties.of().instrument(BASEDRUM).mapColor(STONE).noOcclusion().requiresCorrectToolForDrops().sound(STONE).strength(1.5F, 6.0F), 12, 16));` | multipart: axis: x/y/z; east: false/true; west: false/true; up: false/true; down: false/true; north: false/true; south: false/true | BlockItem | self | block tags: #mc:mineable/pickaxe, #neoforge:needs_wood_tool, #mineable_with_block_and_chain |
| `twilightforest:underbrick` | Underbrick | `register("underbrick", new Block(Properties.of().mapColor(WOOD).requiresCorrectToolForDrops().sound(DEEPSLATE_BRICKS).strength(1.5F, 6.0F)));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_stone_tool, #mineable_with_block_and_chain |
| `twilightforest:underbrick_floor` | Underbrick Floor | `register("underbrick_floor", new Block(Properties.ofFullCopy(UNDERBRICK.get())));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_stone_tool, #mineable_with_block_and_chain |
| `twilightforest:ur_ghast_boss_spawner` | Ur-Ghast Boss Spawner | `register("ur_ghast_boss_spawner", new BossSpawnerBlock(BossVariant.UR_GHAST, Properties.of().mapColor(STONE).noLootTable().sound(METAL).noOcclusion().strength(-1.0F, 3600000.8F)));` |  | BlockItem | none (noLootTable) | block tags: #c:relocation_not_supported, #mc:dragon_immune, #mc:features_cannot_replace, #mc:wither_immune, #antibuilder_ignores, #block_and_chain_never_breaks, #carminite_reactor_immune, #common_protections, #small_lakes_dont_replace, #tree_roots_skip; item tags: #randomium:blacklist |
| `twilightforest:ur_ghast_trophy` | Ur-Ghast Trophy | `BLOCKS.register("ur_ghast_trophy", new TrophyBlock(BossVariant.UR_GHAST, 13, Properties.of().instabreak()));` |  | - | self | dispenser: equips it on an entity in front, like armor (ArmorItem.dispenseArmor) (TFDispenserBehaviors); block tags: #trophy_pedestal_activation_blocks; item tags: #curios:head, #trophies |
| `twilightforest:ur_ghast_wall_trophy` |  | `BLOCKS.register("ur_ghast_wall_trophy", new TrophyWallBlock(BossVariant.UR_GHAST, Properties.of().instabreak()));` |  | - | rolls 1: ur_ghast_trophy [pool if survives_explosion] |  |
| `twilightforest:violet_castle_rune_brick` | Violet Castle Rune Brick | `register("violet_castle_rune_brick", new Block(Properties.ofFullCopy(CASTLE_BRICK.get()).mapColor(DyeColor.PURPLE)));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #annihilation_inclusions, #block_and_chain_never_breaks, #castle_blocks, #mazebreaker_accelerated_mining, #mineable_with_block_and_chain |
| `twilightforest:weathered_deadrock` | Weathered Deadrock | `register("weathered_deadrock", new Block(Properties.ofFullCopy(DEADROCK.get())));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #annihilation_inclusions, #block_and_chain_never_breaks, #deadrock, #mineable_with_block_and_chain, #supports_stalagmites |
| `twilightforest:worn_castle_brick` | Worn Castle Brick | `register("worn_castle_brick", new Block(Properties.ofFullCopy(CASTLE_BRICK.get())));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #annihilation_inclusions, #block_and_chain_never_breaks, #castle_blocks, #mazebreaker_accelerated_mining, #mineable_with_block_and_chain |
| `twilightforest:worn_castle_brick_stairs` | Worn Castle Brick Stairs | `register("worn_castle_brick_stairs", new StairBlock(WORN_CASTLE_BRICK.get().defaultBlockState(), Properties.ofFullCopy(WORN_CASTLE_BRICK.get())));` | facing: east/north/south/west; half: bottom/top; shape: inner_left/inner_right/outer_left/outer_right/straight | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #mc:stairs, #block_and_chain_never_breaks, #castle_blocks, #mazebreaker_accelerated_mining, #mineable_with_block_and_chain; item tags: #mc:stairs |
| `twilightforest:yellow_castle_rune_brick` | Yellow Castle Rune Brick | `register("yellow_castle_rune_brick", new Block(Properties.ofFullCopy(CASTLE_BRICK.get()).mapColor(DyeColor.YELLOW)));` |  | BlockItem | self | block tags: #mc:mineable/pickaxe, #mc:needs_diamond_tool, #annihilation_inclusions, #block_and_chain_never_breaks, #castle_blocks, #mazebreaker_accelerated_mining, #mineable_with_block_and_chain |

### Items (19)

| Item | English name | TF registration (`TFItems`) | Notes |
|---|---|---|---|
| `twilightforest:alpha_yeti_banner_pattern` | Banner Pattern | `ITEMS.register("alpha_yeti_banner_pattern", new BannerPatternItem(CustomTagGenerator.BannerPatternTagGenerator.ALPHA_YETI_BANNER_PATTERN, new Item.Properties().stacksTo(1).rarity(tfRarityEnumExtension.TWILIGHT)));` |  |
| `twilightforest:alpha_yeti_trophy` | Alpha Yeti Trophy | `ITEMS.register("alpha_yeti_trophy", new TrophyItem(TFBlocks.ALPHA_YETI_TROPHY.get(), TFBlocks.ALPHA_YETI_WALL_TROPHY.get(), new Item.Properties().rarity(tfRarityEnumExtension.TWILIGHT)));` | dispenser: equips it on an entity in front, like armor (ArmorItem.dispenseArmor) (TFDispenserBehaviors); item tags: #curios:head, #trophies |
| `twilightforest:hydra_banner_pattern` | Banner Pattern | `ITEMS.register("hydra_banner_pattern", new BannerPatternItem(CustomTagGenerator.BannerPatternTagGenerator.HYDRA_BANNER_PATTERN, new Item.Properties().stacksTo(1).rarity(tfRarityEnumExtension.TWILIGHT)));` |  |
| `twilightforest:hydra_trophy` | Hydra Trophy | `ITEMS.register("hydra_trophy", new TrophyItem(TFBlocks.HYDRA_TROPHY.get(), TFBlocks.HYDRA_WALL_TROPHY.get(), new Item.Properties().rarity(tfRarityEnumExtension.TWILIGHT)));` | dispenser: equips it on an entity in front, like armor (ArmorItem.dispenseArmor) (TFDispenserBehaviors); item tags: #curios:head, #trophies |
| `twilightforest:knight_phantom_banner_pattern` | Banner Pattern | `ITEMS.register("knight_phantom_banner_pattern", new BannerPatternItem(CustomTagGenerator.BannerPatternTagGenerator.KNIGHT_PHANTOM_BANNER_PATTERN, new Item.Properties().stacksTo(1).rarity(tfRarityEnumExtension.TWILIGHT)));` |  |
| `twilightforest:knight_phantom_trophy` | Knight Phantom Trophy | `ITEMS.register("knight_phantom_trophy", new TrophyItem(TFBlocks.KNIGHT_PHANTOM_TROPHY.get(), TFBlocks.KNIGHT_PHANTOM_WALL_TROPHY.get(), new Item.Properties().rarity(tfRarityEnumExtension.TWILIGHT)));` | dispenser: equips it on an entity in front, like armor (ArmorItem.dispenseArmor) (TFDispenserBehaviors); item tags: #curios:head, #trophies |
| `twilightforest:lich_banner_pattern` | Banner Pattern | `ITEMS.register("lich_banner_pattern", new BannerPatternItem(CustomTagGenerator.BannerPatternTagGenerator.LICH_BANNER_PATTERN, new Item.Properties().stacksTo(1).rarity(tfRarityEnumExtension.TWILIGHT)));` |  |
| `twilightforest:lich_trophy` | Twilight Lich Trophy | `ITEMS.register("lich_trophy", new TrophyItem(TFBlocks.LICH_TROPHY.get(), TFBlocks.LICH_WALL_TROPHY.get(), new Item.Properties().rarity(tfRarityEnumExtension.TWILIGHT)));` | dispenser: equips it on an entity in front, like armor (ArmorItem.dispenseArmor) (TFDispenserBehaviors); item tags: #curios:head, #trophies |
| `twilightforest:minoshroom_banner_pattern` | Banner Pattern | `ITEMS.register("minoshroom_banner_pattern", new BannerPatternItem(CustomTagGenerator.BannerPatternTagGenerator.MINOSHROOM_BANNER_PATTERN, new Item.Properties().stacksTo(1).rarity(tfRarityEnumExtension.TWILIGHT)));` |  |
| `twilightforest:minoshroom_trophy` | Minoshroom Trophy | `ITEMS.register("minoshroom_trophy", new TrophyItem(TFBlocks.MINOSHROOM_TROPHY.get(), TFBlocks.MINOSHROOM_WALL_TROPHY.get(), new Item.Properties().rarity(tfRarityEnumExtension.TWILIGHT)));` | dispenser: equips it on an entity in front, like armor (ArmorItem.dispenseArmor) (TFDispenserBehaviors); item tags: #curios:head, #trophies |
| `twilightforest:naga_banner_pattern` | Banner Pattern | `ITEMS.register("naga_banner_pattern", new BannerPatternItem(CustomTagGenerator.BannerPatternTagGenerator.NAGA_BANNER_PATTERN, new Item.Properties().stacksTo(1).rarity(tfRarityEnumExtension.TWILIGHT)));` |  |
| `twilightforest:naga_trophy` | Naga Trophy | `ITEMS.register("naga_trophy", new TrophyItem(TFBlocks.NAGA_TROPHY.get(), TFBlocks.NAGA_WALL_TROPHY.get(), new Item.Properties().rarity(tfRarityEnumExtension.TWILIGHT)));` | dispenser: equips it on an entity in front, like armor (ArmorItem.dispenseArmor) (TFDispenserBehaviors); item tags: #curios:head, #trophies |
| `twilightforest:quest_ram_banner_pattern` | Banner Pattern | `ITEMS.register("quest_ram_banner_pattern", new BannerPatternItem(CustomTagGenerator.BannerPatternTagGenerator.QUEST_RAM_BANNER_PATTERN, new Item.Properties().stacksTo(1).rarity(tfRarityEnumExtension.TWILIGHT)));` | item tags: #randomium:blacklist, #wip |
| `twilightforest:quest_ram_trophy` | Questing Ram Trophy | `ITEMS.register("quest_ram_trophy", new TrophyItem(TFBlocks.QUEST_RAM_TROPHY.get(), TFBlocks.QUEST_RAM_WALL_TROPHY.get(), new Item.Properties().rarity(tfRarityEnumExtension.TWILIGHT)));` | dispenser: equips it on an entity in front, like armor (ArmorItem.dispenseArmor) (TFDispenserBehaviors); item tags: #curios:head |
| `twilightforest:rope` | Rope | `ITEMS.register("rope", new RopeItem(TFBlocks.ROPE.get(), new Item.Properties()));` | item tags: #c:ropes |
| `twilightforest:snow_queen_banner_pattern` | Banner Pattern | `ITEMS.register("snow_queen_banner_pattern", new BannerPatternItem(CustomTagGenerator.BannerPatternTagGenerator.SNOW_QUEEN_BANNER_PATTERN, new Item.Properties().stacksTo(1).rarity(tfRarityEnumExtension.TWILIGHT)));` |  |
| `twilightforest:snow_queen_trophy` | Snow Queen Trophy | `ITEMS.register("snow_queen_trophy", new TrophyItem(TFBlocks.SNOW_QUEEN_TROPHY.get(), TFBlocks.SNOW_QUEEN_WALL_TROPHY.get(), new Item.Properties().rarity(tfRarityEnumExtension.TWILIGHT)));` | dispenser: equips it on an entity in front, like armor (ArmorItem.dispenseArmor) (TFDispenserBehaviors); item tags: #curios:head, #trophies |
| `twilightforest:ur_ghast_banner_pattern` | Banner Pattern | `ITEMS.register("ur_ghast_banner_pattern", new BannerPatternItem(CustomTagGenerator.BannerPatternTagGenerator.UR_GHAST_BANNER_PATTERN, new Item.Properties().stacksTo(1).rarity(tfRarityEnumExtension.TWILIGHT)));` |  |
| `twilightforest:ur_ghast_trophy` | Ur-Ghast Trophy | `ITEMS.register("ur_ghast_trophy", new TrophyItem(TFBlocks.UR_GHAST_TROPHY.get(), TFBlocks.UR_GHAST_WALL_TROPHY.get(), new Item.Properties().rarity(tfRarityEnumExtension.TWILIGHT)));` | dispenser: equips it on an entity in front, like armor (ArmorItem.dispenseArmor) (TFDispenserBehaviors); item tags: #curios:head, #trophies |

### Block entity types (1)

| Id | TF registration (`TFBlockEntities.java`) |
|---|---|
| `twilightforest:trophy` | `BLOCK_ENTITIES.register("trophy", () -> BlockEntityType.Builder.of(TrophyBlockEntity::new, TFBlocks.NAGA_TROPHY.get(), TFBlocks.LICH_TROPHY.get(), TFBlocks.MINOSHROOM_TROPHY.get(), TFBlocks.HYDRA_TROPHY.get(), TFBlocks.KNIGHT_PHANTOM_TROPHY.get(), TFBlocks.UR_GHAST_TROPHY.get(), TFBlocks.ALPHA_YETI_TROPHY.get(), TFBlocks.SNOW_QUEEN_TROPHY.get(), TFBlocks.QUEST_RAM_TROPHY.get(), TFBlocks.NAGA_WALL_TROPHY.get(), TFBlocks.LICH_WALL_TROPHY.get(), TFBlocks.MINOSHROOM_WALL_TROPHY.get(), TFBlocks.HYDRA_WALL_TROPHY.get(), TFBlocks.KNIGHT_PHANTOM_WALL_TROPHY.get(), TFBlocks.UR_GHAST_WALL_TROPHY.get(), TFBlocks.ALPHA_YETI_WALL_TROPHY.get(), TFBlocks.SNOW_QUEEN_WALL_TROPHY.get(), TFBlocks.QUEST_RAM_WALL_TROPHY.get()).build(null));` |

### Banner patterns (9)

- `twilightforest:alpha_yeti`: `{"asset_id":"twilightforest:alpha_yeti","translation_key":"block.minecraft.banner.twilightforest.alpha_yeti"}`
- `twilightforest:hydra`: `{"asset_id":"twilightforest:hydra","translation_key":"block.minecraft.banner.twilightforest.hydra"}`
- `twilightforest:knight_phantom`: `{"asset_id":"twilightforest:knight_phantom","translation_key":"block.minecraft.banner.twilightforest.knight_phantom"}`
- `twilightforest:lich`: `{"asset_id":"twilightforest:lich","translation_key":"block.minecraft.banner.twilightforest.lich"}`
- `twilightforest:minoshroom`: `{"asset_id":"twilightforest:minoshroom","translation_key":"block.minecraft.banner.twilightforest.minoshroom"}`
- `twilightforest:naga`: `{"asset_id":"twilightforest:naga","translation_key":"block.minecraft.banner.twilightforest.naga"}`
- `twilightforest:questing_ram`: `{"asset_id":"twilightforest:quest_ram","translation_key":"block.minecraft.banner.twilightforest.quest_ram"}`
- `twilightforest:snow_queen`: `{"asset_id":"twilightforest:snow_queen","translation_key":"block.minecraft.banner.twilightforest.snow_queen"}`
- `twilightforest:ur_ghast`: `{"asset_id":"twilightforest:ur_ghast","translation_key":"block.minecraft.banner.twilightforest.ur_ghast"}`

### Tags (8)

| Tag | Members (TF generated JSON) |
|---|---|
| `block/#twilightforest:castle_blocks` | castle_brick, worn_castle_brick, cracked_castle_brick, mossy_castle_brick, castle_roof_tile, thick_castle_brick, bold_castle_brick_tile, bold_castle_brick_pillar, encased_castle_brick_tile, encased_castle_brick_pillar, castle_brick_stairs, worn_castle_brick_stairs, cracked_castle_brick_stairs, mossy_castle_brick_stairs, encased_castle_brick_stairs, bold_castle_brick_stairs, pink_castle_rune_brick, yellow_castle_rune_brick, blue_castle_rune_brick, violet_castle_rune_brick, pink_castle_door, yellow_castle_door, blue_castle_door, violet_castle_door |
| `block/#twilightforest:deadrock` | deadrock, cracked_deadrock, weathered_deadrock |
| `block/#twilightforest:mazestone` | mazestone, mazestone_brick, cracked_mazestone, mossy_mazestone, cut_mazestone, decorative_mazestone, mazestone_mosaic, mazestone_border |
| `block/#twilightforest:towerwood` | towerwood, mossy_towerwood, cracked_towerwood, infested_towerwood |
| `block/#twilightforest:trophy_pedestal_activation_blocks` | naga_trophy, lich_trophy, minoshroom_trophy, hydra_trophy, knight_phantom_trophy, ur_ghast_trophy, alpha_yeti_trophy, snow_queen_trophy |
| `item/#twilightforest:towerwood` | towerwood, mossy_towerwood, cracked_towerwood, infested_towerwood |
| `item/#twilightforest:trophies` | naga_trophy, lich_trophy, minoshroom_trophy, hydra_trophy, knight_phantom_trophy, ur_ghast_trophy, alpha_yeti_trophy, snow_queen_trophy |
| `worldgen/biome/#twilightforest:valid_final_castle_biomes` | final_plateau |

### Loot tables (90)

Block loot tables summarised in the Loot column of the block table: `twilightforest:blocks/alpha_yeti_trophy`, `twilightforest:blocks/alpha_yeti_wall_trophy`, `twilightforest:blocks/arctic_fur_block`, `twilightforest:blocks/blue_castle_rune_brick`, `twilightforest:blocks/bold_castle_brick_pillar`, `twilightforest:blocks/bold_castle_brick_stairs`, `twilightforest:blocks/bold_castle_brick_tile`, `twilightforest:blocks/bold_stone_pillar`, `twilightforest:blocks/canopy_window`, `twilightforest:blocks/canopy_window_pane`, `twilightforest:blocks/carminite_block`, `twilightforest:blocks/castle_brick`, `twilightforest:blocks/castle_brick_stairs`, `twilightforest:blocks/castle_roof_tile`, `twilightforest:blocks/cracked_castle_brick`, `twilightforest:blocks/cracked_castle_brick_stairs`, `twilightforest:blocks/cracked_deadrock`, `twilightforest:blocks/cracked_etched_nagastone`, `twilightforest:blocks/cracked_mazestone`, `twilightforest:blocks/cracked_nagastone_pillar`, `twilightforest:blocks/cracked_nagastone_stairs_left`, `twilightforest:blocks/cracked_nagastone_stairs_right`, `twilightforest:blocks/cracked_towerwood`, `twilightforest:blocks/cracked_underbrick`, `twilightforest:blocks/cut_mazestone`, `twilightforest:blocks/dark_tower_miniature_structure`, `twilightforest:blocks/deadrock`, `twilightforest:blocks/decorative_mazestone`, `twilightforest:blocks/encased_castle_brick_pillar`, `twilightforest:blocks/encased_castle_brick_stairs`, `twilightforest:blocks/encased_castle_brick_tile`, `twilightforest:blocks/encased_towerwood`, `twilightforest:blocks/etched_nagastone`, `twilightforest:blocks/fiery_block`, `twilightforest:blocks/hydra_trophy`, `twilightforest:blocks/hydra_wall_trophy`, `twilightforest:blocks/iron_ladder`, `twilightforest:blocks/ironwood_block`, `twilightforest:blocks/knight_phantom_trophy`, `twilightforest:blocks/knight_phantom_wall_trophy`, `twilightforest:blocks/knightmetal_block`, `twilightforest:blocks/lich_tower_miniature_structure`, `twilightforest:blocks/lich_trophy`, `twilightforest:blocks/lich_wall_trophy`, `twilightforest:blocks/mazestone`, `twilightforest:blocks/mazestone_border`, `twilightforest:blocks/mazestone_brick`, `twilightforest:blocks/mazestone_mosaic`, `twilightforest:blocks/minoshroom_trophy`, `twilightforest:blocks/minoshroom_wall_trophy`, `twilightforest:blocks/minotaur_labyrinth_miniature_structure`, `twilightforest:blocks/mossy_castle_brick`, `twilightforest:blocks/mossy_castle_brick_stairs`, `twilightforest:blocks/mossy_etched_nagastone`, `twilightforest:blocks/mossy_mazestone`, `twilightforest:blocks/mossy_nagastone_pillar`, `twilightforest:blocks/mossy_nagastone_stairs_left`, `twilightforest:blocks/mossy_nagastone_stairs_right`, `twilightforest:blocks/mossy_towerwood`, `twilightforest:blocks/mossy_underbrick`, `twilightforest:blocks/naga_courtyard_miniature_structure`, `twilightforest:blocks/naga_trophy`, `twilightforest:blocks/naga_wall_trophy`, `twilightforest:blocks/nagastone`, `twilightforest:blocks/nagastone_head`, `twilightforest:blocks/nagastone_pillar`, `twilightforest:blocks/nagastone_stairs_left`, `twilightforest:blocks/nagastone_stairs_right`, `twilightforest:blocks/pink_castle_rune_brick`, `twilightforest:blocks/quest_ram_trophy`, `twilightforest:blocks/quest_ram_wall_trophy`, `twilightforest:blocks/rope`, `twilightforest:blocks/snow_queen_trophy`, `twilightforest:blocks/snow_queen_wall_trophy`, `twilightforest:blocks/spiral_bricks`, `twilightforest:blocks/steeleaf_block`, `twilightforest:blocks/thick_castle_brick`, `twilightforest:blocks/towerwood`, `twilightforest:blocks/twilight_portal_miniature_structure`, `twilightforest:blocks/twisted_stone`, `twilightforest:blocks/twisted_stone_pillar`, `twilightforest:blocks/underbrick`, `twilightforest:blocks/underbrick_floor`, `twilightforest:blocks/ur_ghast_trophy`, `twilightforest:blocks/ur_ghast_wall_trophy`, `twilightforest:blocks/violet_castle_rune_brick`, `twilightforest:blocks/weathered_deadrock`, `twilightforest:blocks/worn_castle_brick`, `twilightforest:blocks/worn_castle_brick_stairs`, `twilightforest:blocks/yellow_castle_rune_brick`

### Recipes (134)

Recipe-unlock advancements (generated by F1 into the recipe JSON, R21) are listed with their recipe; criteria are the `has_*` triggers besides `has_the_recipe`.

| Recipe | Type | Ingredients | Result | Unlock advancement (criteria) |
|---|---|---|---|---|
| `twilightforest:alpha_yeti_banner_pattern` | mc:crafting_shapeless | #c:paper + alpha_yeti_trophy | alpha_yeti_banner_pattern | `twilightforest:recipes/misc/alpha_yeti_banner_pattern` (has_trophy: has alpha_yeti_trophy) |
| `twilightforest:canopy_window` | mc:crafting_shaped | 'GPG' / 'PPP' / 'GPG' ; G=#c:glass_blocks, P=canopy_planks | canopy_window x4 | `twilightforest:recipes/decorations/canopy_window` (has_planks: has canopy_planks) |
| `twilightforest:canopy_window_pane` | mc:crafting_shaped | 'GGG' / 'GGG' ; G=canopy_window | canopy_window_pane x16 | `twilightforest:recipes/decorations/canopy_window_pane` (has_windows: has canopy_window) |
| `twilightforest:castleblock/bold_castle_brick_pillar` | mc:crafting_shaped | '##' / '##' ; #=thick_castle_brick | bold_castle_brick_pillar x4 | `twilightforest:recipes/building_blocks/castleblock/bold_castle_brick_pillar` (has_castle_brick: has castle_brick) |
| `twilightforest:castleblock/bold_castle_brick_stairs` | mc:crafting_shaped | '#  ' / '## ' / '###' ; #=bold_castle_brick_pillar/bold_castle_brick_tile | bold_castle_brick_stairs x8 | `twilightforest:recipes/building_blocks/castleblock/bold_castle_brick_stairs` (has_item: has bold_castle_brick_pillar) |
| `twilightforest:castleblock/bold_castle_brick_tile` | mc:crafting_shaped | '##' / '##' ; #=bold_castle_brick_pillar | bold_castle_brick_tile x4 | `twilightforest:recipes/building_blocks/castleblock/bold_castle_brick_tile` (has_castle_brick: has castle_brick) |
| `twilightforest:castleblock/bold_castle_pillar_from_tile` | mc:crafting_shaped | '##' / '##' ; #=bold_castle_brick_tile | bold_castle_brick_pillar x4 | `twilightforest:recipes/building_blocks/castleblock/bold_castle_pillar_from_tile` (has_castle_brick: has castle_brick) |
| `twilightforest:castleblock/castle_brick_stairs` | mc:crafting_shaped | '#  ' / '## ' / '###' ; #=castle_brick | castle_brick_stairs x8 | `twilightforest:recipes/building_blocks/castleblock/castle_brick_stairs` (has_item: has castle_brick) |
| `twilightforest:castleblock/cracked_castle_brick_stairs` | mc:crafting_shaped | '#  ' / '## ' / '###' ; #=cracked_castle_brick | cracked_castle_brick_stairs x8 | `twilightforest:recipes/building_blocks/castleblock/cracked_castle_brick_stairs` (has_item: has cracked_castle_brick) |
| `twilightforest:castleblock/encased_castle_brick_stairs` | mc:crafting_shaped | '#  ' / '## ' / '###' ; #=encased_castle_brick_pillar/encased_castle_brick_tile | encased_castle_brick_stairs x8 | `twilightforest:recipes/building_blocks/castleblock/encased_castle_brick_stairs` (has_item: has encased_castle_brick_pillar) |
| `twilightforest:castleblock/encased_castle_pillar` | mc:crafting_shaped | '#H#' / '#H#' ; #=castle_brick/worn_castle_brick/cracked_castle_brick/mossy_castle_brick/thick_castle_brick, H=encased_castle_brick_pillar/encased_castle_brick_tile/bold_castle_brick_pillar/bold_castle_brick_tile | encased_castle_brick_pillar x6 | `twilightforest:recipes/building_blocks/castleblock/encased_castle_pillar` (has_castle_brick: has castle_brick) |
| `twilightforest:castleblock/mossy_castle_brick` | mc:crafting_shapeless | mc:vine/mc:moss_block + castle_brick | mossy_castle_brick | `twilightforest:recipes/building_blocks/castleblock/mossy_castle_brick` (has_item: has castle_brick) |
| `twilightforest:castleblock/mossy_castle_brick_stairs` | mc:crafting_shaped | '#  ' / '## ' / '###' ; #=mossy_castle_brick | mossy_castle_brick_stairs x8 | `twilightforest:recipes/building_blocks/castleblock/mossy_castle_brick_stairs` (has_item: has mossy_castle_brick) |
| `twilightforest:castleblock/smelted_cracked_castle_brick` | mc:smelting | castle_brick (xp 0.1, 200 t) | cracked_castle_brick | `twilightforest:recipes/building_blocks/castleblock/smelted_cracked_castle_brick` (has_item: has castle_brick) |
| `twilightforest:castleblock/thick_castle_brick` | mc:crafting_shaped | '##' / '##' ; #=castle_brick/worn_castle_brick/cracked_castle_brick/mossy_castle_brick | thick_castle_brick x4 | `twilightforest:recipes/building_blocks/castleblock/thick_castle_brick` (has_castle_brick: has castle_brick) |
| `twilightforest:castleblock/worn_castle_brick_stairs` | mc:crafting_shaped | '#  ' / '## ' / '###' ; #=worn_castle_brick | worn_castle_brick_stairs x8 | `twilightforest:recipes/building_blocks/castleblock/worn_castle_brick_stairs` (has_item: has worn_castle_brick) |
| `twilightforest:compressed_blocks/arctic_fur_block` | mc:crafting_shaped | '###' / '#m#' / '###' ; #=#arctic_fur, m=arctic_fur | arctic_fur_block | `twilightforest:recipes/building_blocks/compressed_blocks/arctic_fur_block` (has_item: has #arctic_fur) |
| `twilightforest:compressed_blocks/carminite_block` | mc:crafting_shaped | '###' / '#m#' / '###' ; #=#c:gems/carminite, m=carminite | carminite_block | `twilightforest:recipes/building_blocks/compressed_blocks/carminite_block` (has_item: has #c:gems/carminite) |
| `twilightforest:compressed_blocks/fiery_block` | mc:crafting_shaped | '###' / '#m#' / '###' ; #=#c:ingots/fiery, m=fiery_ingot | fiery_block | `twilightforest:recipes/building_blocks/compressed_blocks/fiery_block` (has_item: has #c:ingots/fiery) |
| `twilightforest:compressed_blocks/ironwood_block` | mc:crafting_shaped | '###' / '#m#' / '###' ; #=#c:ingots/ironwood, m=ironwood_ingot | ironwood_block | `twilightforest:recipes/building_blocks/compressed_blocks/ironwood_block` (has_item: has #c:ingots/ironwood) |
| `twilightforest:compressed_blocks/knightmetal_block` | mc:crafting_shaped | '###' / '#m#' / '###' ; #=#c:ingots/knightmetal, m=knightmetal_ingot | knightmetal_block | `twilightforest:recipes/building_blocks/compressed_blocks/knightmetal_block` (has_item: has #c:ingots/knightmetal) |
| `twilightforest:compressed_blocks/steeleaf_block` | mc:crafting_shaped | '###' / '#m#' / '###' ; #=#c:ingots/steeleaf, m=steeleaf_ingot | steeleaf_block | `twilightforest:recipes/building_blocks/compressed_blocks/steeleaf_block` (has_item: has #c:ingots/steeleaf) |
| `twilightforest:hydra_banner_pattern` | mc:crafting_shapeless | #c:paper + hydra_trophy | hydra_banner_pattern | `twilightforest:recipes/misc/hydra_banner_pattern` (has_trophy: has hydra_trophy) |
| `twilightforest:iron_ladder` | mc:crafting_shaped | '-#-' / '-#-' ; #=mc:iron_bars, -=#c:nuggets/iron | iron_ladder x3 | `twilightforest:recipes/decorations/iron_ladder` (has_iron_bars: has mc:iron_bars) |
| `twilightforest:knight_phantom_banner_pattern` | mc:crafting_shapeless | #c:paper + knight_phantom_trophy | knight_phantom_banner_pattern | `twilightforest:recipes/misc/knight_phantom_banner_pattern` (has_trophy: has knight_phantom_trophy) |
| `twilightforest:lich_banner_pattern` | mc:crafting_shapeless | #c:paper + lich_trophy | lich_banner_pattern | `twilightforest:recipes/misc/lich_banner_pattern` (has_trophy: has lich_trophy) |
| `twilightforest:maze_stone/mossy_mazestone` | mc:crafting_shapeless | mc:vine/mc:moss_block + mazestone_brick | mossy_mazestone | `twilightforest:recipes/building_blocks/maze_stone/mossy_mazestone` (has_item: has mazestone_brick) |
| `twilightforest:maze_stone/smelted_maze_stone_cracked` | mc:smelting | mazestone_brick (xp 0.1, 200 t) | cracked_mazestone | `twilightforest:recipes/building_blocks/maze_stone/smelted_maze_stone_cracked` (has_item: has mazestone_brick) |
| `twilightforest:minoshroom_banner_pattern` | mc:crafting_shapeless | #c:paper + minoshroom_trophy | minoshroom_banner_pattern | `twilightforest:recipes/misc/minoshroom_banner_pattern` (has_trophy: has minoshroom_trophy) |
| `twilightforest:mossy_underbrick` | mc:crafting_shapeless | mc:vine/mc:moss_block + underbrick | mossy_underbrick | `twilightforest:recipes/building_blocks/mossy_underbrick` (has_item: has underbrick) |
| `twilightforest:naga_banner_pattern` | mc:crafting_shapeless | #c:paper + naga_trophy | naga_banner_pattern | `twilightforest:recipes/misc/naga_banner_pattern` (has_trophy: has naga_trophy) |
| `twilightforest:nagastone/cracked_nagastone_stairs_left` | mc:crafting_shaped | '#  ' / '## ' / '###' ; #=cracked_etched_nagastone | cracked_nagastone_stairs_left x8 | `twilightforest:recipes/building_blocks/nagastone/cracked_nagastone_stairs_left` (has_item: has cracked_etched_nagastone) |
| `twilightforest:nagastone/cracked_nagastone_stairs_right` | mc:crafting_shaped | '###' / ' ##' / '  #' ; #=cracked_etched_nagastone | cracked_nagastone_stairs_right x8 | `twilightforest:recipes/building_blocks/nagastone/cracked_nagastone_stairs_right` (has_item: has cracked_etched_nagastone) |
| `twilightforest:nagastone/mossy_etched_nagastone` | mc:crafting_shapeless | mc:vine/mc:moss_block + etched_nagastone | mossy_etched_nagastone | `twilightforest:recipes/building_blocks/nagastone/mossy_etched_nagastone` (has_item: has etched_nagastone) |
| `twilightforest:nagastone/mossy_nagastone_pillar` | mc:crafting_shapeless | mc:vine/mc:moss_block + nagastone_pillar | mossy_nagastone_pillar | `twilightforest:recipes/building_blocks/nagastone/mossy_nagastone_pillar` (has_item: has nagastone_pillar) |
| `twilightforest:nagastone/mossy_nagastone_stairs_left` | mc:crafting_shaped | '#  ' / '## ' / '###' ; #=mossy_etched_nagastone | mossy_nagastone_stairs_left x8 | `twilightforest:recipes/building_blocks/nagastone/mossy_nagastone_stairs_left` (has_item: has mossy_etched_nagastone) |
| `twilightforest:nagastone/mossy_nagastone_stairs_right` | mc:crafting_shaped | '###' / ' ##' / '  #' ; #=mossy_etched_nagastone | mossy_nagastone_stairs_right x8 | `twilightforest:recipes/building_blocks/nagastone/mossy_nagastone_stairs_right` (has_item: has mossy_etched_nagastone) |
| `twilightforest:nagastone/nagastone_spiral` | mc:crafting_shaped | 'BSS' / 'BSS' / 'BBB' ; B=mc:stone_bricks/mc:mossy_stone_bricks/mc:cracked_stone_bricks/mc:chiseled_stone_bricks, S=mc:stone_slab/mc:stone_brick_slab | spiral_bricks x8 | `twilightforest:recipes/building_blocks/nagastone/nagastone_spiral` (has_item: has spiral_bricks) |
| `twilightforest:nagastone/nagastone_stairs_left` | mc:crafting_shaped | '#  ' / '## ' / '###' ; #=etched_nagastone | nagastone_stairs_left x8 | `twilightforest:recipes/building_blocks/nagastone/nagastone_stairs_left` (has_item: has etched_nagastone) |
| `twilightforest:nagastone/nagastone_stairs_right` | mc:crafting_shaped | '###' / ' ##' / '  #' ; #=etched_nagastone | nagastone_stairs_right x8 | `twilightforest:recipes/building_blocks/nagastone/nagastone_stairs_right` (has_item: has etched_nagastone) |
| `twilightforest:nagastone/smelted_cracked_etched_nagastone` | mc:smelting | etched_nagastone (xp 0.1, 200 t) | cracked_etched_nagastone | `twilightforest:recipes/building_blocks/nagastone/smelted_cracked_etched_nagastone` (has_item: has etched_nagastone) |
| `twilightforest:nagastone/smelted_cracked_nagastone_pillar` | mc:smelting | nagastone_pillar (xp 0.1, 200 t) | cracked_nagastone_pillar | `twilightforest:recipes/building_blocks/nagastone/smelted_cracked_nagastone_pillar` (has_item: has nagastone_pillar) |
| `twilightforest:rope` | mc:crafting_shaped | '#' / '#' / '#' ; #=root_strand | rope x8 | `twilightforest:recipes/decorations/rope` (has_root_strand: has root_strand) |
| `twilightforest:smelted_cracked_underbrick` | mc:smelting | underbrick (xp 0.1, 200 t) | cracked_underbrick | `twilightforest:recipes/building_blocks/smelted_cracked_underbrick` (has_item: has underbrick) |
| `twilightforest:snow_queen_banner_pattern` | mc:crafting_shapeless | #c:paper + snow_queen_trophy | snow_queen_banner_pattern | `twilightforest:recipes/misc/snow_queen_banner_pattern` (has_trophy: has snow_queen_trophy) |
| `twilightforest:stonecutting/bold_castle_brick_pillar/bold_castle_brick_stairs` | mc:stonecutting | bold_castle_brick_pillar | bold_castle_brick_stairs | `twilightforest:recipes/building_blocks/stonecutting/bold_castle_brick_pillar/bold_castle_brick_stairs` (has_block: has bold_castle_brick_pillar) |
| `twilightforest:stonecutting/bold_castle_brick_pillar/bold_castle_brick_tile` | mc:stonecutting | bold_castle_brick_pillar | bold_castle_brick_tile | `twilightforest:recipes/building_blocks/stonecutting/bold_castle_brick_pillar/bold_castle_brick_tile` (has_block: has bold_castle_brick_pillar) |
| `twilightforest:stonecutting/bold_castle_brick_tile/bold_castle_brick_pillar` | mc:stonecutting | bold_castle_brick_tile | bold_castle_brick_pillar | `twilightforest:recipes/building_blocks/stonecutting/bold_castle_brick_tile/bold_castle_brick_pillar` (has_block: has bold_castle_brick_tile) |
| `twilightforest:stonecutting/bold_castle_brick_tile/bold_castle_brick_stairs` | mc:stonecutting | bold_castle_brick_tile | bold_castle_brick_stairs | `twilightforest:recipes/building_blocks/stonecutting/bold_castle_brick_tile/bold_castle_brick_stairs` (has_block: has bold_castle_brick_tile) |
| `twilightforest:stonecutting/castle_brick/bold_castle_brick_pillar` | mc:stonecutting | castle_brick | bold_castle_brick_pillar | `twilightforest:recipes/building_blocks/stonecutting/castle_brick/bold_castle_brick_pillar` (has_block: has castle_brick) |
| `twilightforest:stonecutting/castle_brick/bold_castle_brick_stairs` | mc:stonecutting | castle_brick | bold_castle_brick_stairs | `twilightforest:recipes/building_blocks/stonecutting/castle_brick/bold_castle_brick_stairs` (has_block: has castle_brick) |
| `twilightforest:stonecutting/castle_brick/bold_castle_brick_tile` | mc:stonecutting | castle_brick | bold_castle_brick_tile | `twilightforest:recipes/building_blocks/stonecutting/castle_brick/bold_castle_brick_tile` (has_block: has castle_brick) |
| `twilightforest:stonecutting/castle_brick/castle_brick_stairs` | mc:stonecutting | castle_brick | castle_brick_stairs | `twilightforest:recipes/building_blocks/stonecutting/castle_brick/castle_brick_stairs` (has_block: has castle_brick) |
| `twilightforest:stonecutting/castle_brick/castle_roof_tile` | mc:stonecutting | castle_brick | castle_roof_tile | `twilightforest:recipes/building_blocks/stonecutting/castle_brick/castle_roof_tile` (has_block: has castle_brick) |
| `twilightforest:stonecutting/castle_brick/encased_castle_brick_pillar` | mc:stonecutting | castle_brick | encased_castle_brick_pillar | `twilightforest:recipes/building_blocks/stonecutting/castle_brick/encased_castle_brick_pillar` (has_block: has castle_brick) |
| `twilightforest:stonecutting/castle_brick/encased_castle_brick_stairs` | mc:stonecutting | castle_brick | encased_castle_brick_stairs | `twilightforest:recipes/building_blocks/stonecutting/castle_brick/encased_castle_brick_stairs` (has_block: has castle_brick) |
| `twilightforest:stonecutting/castle_brick/encased_castle_brick_tile` | mc:stonecutting | castle_brick | encased_castle_brick_tile | `twilightforest:recipes/building_blocks/stonecutting/castle_brick/encased_castle_brick_tile` (has_block: has castle_brick) |
| `twilightforest:stonecutting/castle_brick/thick_castle_brick` | mc:stonecutting | castle_brick | thick_castle_brick | `twilightforest:recipes/building_blocks/stonecutting/castle_brick/thick_castle_brick` (has_block: has castle_brick) |
| `twilightforest:stonecutting/castle_brick/worn_castle_brick` | mc:stonecutting | castle_brick | worn_castle_brick | `twilightforest:recipes/building_blocks/stonecutting/castle_brick/worn_castle_brick` (has_block: has castle_brick) |
| `twilightforest:stonecutting/castle_brick/worn_castle_brick_stairs` | mc:stonecutting | castle_brick | worn_castle_brick_stairs | `twilightforest:recipes/building_blocks/stonecutting/castle_brick/worn_castle_brick_stairs` (has_block: has castle_brick) |
| `twilightforest:stonecutting/cracked_castle_brick/bold_castle_brick_pillar` | mc:stonecutting | cracked_castle_brick | bold_castle_brick_pillar | `twilightforest:recipes/building_blocks/stonecutting/cracked_castle_brick/bold_castle_brick_pillar` (has_block: has cracked_castle_brick) |
| `twilightforest:stonecutting/cracked_castle_brick/bold_castle_brick_stairs` | mc:stonecutting | cracked_castle_brick | bold_castle_brick_stairs | `twilightforest:recipes/building_blocks/stonecutting/cracked_castle_brick/bold_castle_brick_stairs` (has_block: has cracked_castle_brick) |
| `twilightforest:stonecutting/cracked_castle_brick/bold_castle_brick_tile` | mc:stonecutting | cracked_castle_brick | bold_castle_brick_tile | `twilightforest:recipes/building_blocks/stonecutting/cracked_castle_brick/bold_castle_brick_tile` (has_block: has cracked_castle_brick) |
| `twilightforest:stonecutting/cracked_castle_brick/cracked_castle_brick_stairs` | mc:stonecutting | cracked_castle_brick | cracked_castle_brick_stairs | `twilightforest:recipes/building_blocks/stonecutting/cracked_castle_brick/cracked_castle_brick_stairs` (has_block: has cracked_castle_brick) |
| `twilightforest:stonecutting/cracked_castle_brick/thick_castle_brick` | mc:stonecutting | cracked_castle_brick | thick_castle_brick | `twilightforest:recipes/building_blocks/stonecutting/cracked_castle_brick/thick_castle_brick` (has_block: has cracked_castle_brick) |
| `twilightforest:stonecutting/cracked_etched_nagastone/cracked_nagastone_stairs_left` | mc:stonecutting | cracked_etched_nagastone | cracked_nagastone_stairs_left | `twilightforest:recipes/building_blocks/stonecutting/cracked_etched_nagastone/cracked_nagastone_stairs_left` (has_block: has cracked_etched_nagastone) |
| `twilightforest:stonecutting/cracked_etched_nagastone/cracked_nagastone_stairs_right` | mc:stonecutting | cracked_etched_nagastone | cracked_nagastone_stairs_right | `twilightforest:recipes/building_blocks/stonecutting/cracked_etched_nagastone/cracked_nagastone_stairs_right` (has_block: has cracked_etched_nagastone) |
| `twilightforest:stonecutting/cracked_nagastone_stairs_left/cracked_nagastone_stairs_right` | mc:stonecutting | cracked_nagastone_stairs_left | cracked_nagastone_stairs_right | `twilightforest:recipes/building_blocks/stonecutting/cracked_nagastone_stairs_left/cracked_nagastone_stairs_right` (has_block: has cracked_nagastone_stairs_left) |
| `twilightforest:stonecutting/cracked_nagastone_stairs_right/cracked_nagastone_stairs_left` | mc:stonecutting | cracked_nagastone_stairs_right | cracked_nagastone_stairs_left | `twilightforest:recipes/building_blocks/stonecutting/cracked_nagastone_stairs_right/cracked_nagastone_stairs_left` (has_block: has cracked_nagastone_stairs_right) |
| `twilightforest:stonecutting/cut_mazestone/decorative_mazestone` | mc:stonecutting | cut_mazestone | decorative_mazestone | `twilightforest:recipes/building_blocks/stonecutting/cut_mazestone/decorative_mazestone` (has_block: has cut_mazestone) |
| `twilightforest:stonecutting/cut_mazestone/mazestone_border` | mc:stonecutting | cut_mazestone | mazestone_border | `twilightforest:recipes/building_blocks/stonecutting/cut_mazestone/mazestone_border` (has_block: has cut_mazestone) |
| `twilightforest:stonecutting/cut_mazestone/mazestone_brick` | mc:stonecutting | cut_mazestone | mazestone_brick | `twilightforest:recipes/building_blocks/stonecutting/cut_mazestone/mazestone_brick` (has_block: has cut_mazestone) |
| `twilightforest:stonecutting/cut_mazestone/mazestone_mosaic` | mc:stonecutting | cut_mazestone | mazestone_mosaic | `twilightforest:recipes/building_blocks/stonecutting/cut_mazestone/mazestone_mosaic` (has_block: has cut_mazestone) |
| `twilightforest:stonecutting/dark_log/encased_towerwood` | mc:stonecutting | dark_log | encased_towerwood | `twilightforest:recipes/building_blocks/stonecutting/dark_log/encased_towerwood` (has_block: has dark_log) |
| `twilightforest:stonecutting/dark_log/towerwood` | mc:stonecutting | dark_log | towerwood | `twilightforest:recipes/building_blocks/stonecutting/dark_log/towerwood` (has_block: has dark_log) |
| `twilightforest:stonecutting/dark_wood/encased_towerwood` | mc:stonecutting | dark_wood | encased_towerwood | `twilightforest:recipes/building_blocks/stonecutting/dark_wood/encased_towerwood` (has_block: has dark_wood) |
| `twilightforest:stonecutting/dark_wood/towerwood` | mc:stonecutting | dark_wood | towerwood | `twilightforest:recipes/building_blocks/stonecutting/dark_wood/towerwood` (has_block: has dark_wood) |
| `twilightforest:stonecutting/decorative_mazestone/cut_mazestone` | mc:stonecutting | decorative_mazestone | cut_mazestone | `twilightforest:recipes/building_blocks/stonecutting/decorative_mazestone/cut_mazestone` (has_block: has decorative_mazestone) |
| `twilightforest:stonecutting/decorative_mazestone/mazestone_border` | mc:stonecutting | decorative_mazestone | mazestone_border | `twilightforest:recipes/building_blocks/stonecutting/decorative_mazestone/mazestone_border` (has_block: has decorative_mazestone) |
| `twilightforest:stonecutting/decorative_mazestone/mazestone_brick` | mc:stonecutting | decorative_mazestone | mazestone_brick | `twilightforest:recipes/building_blocks/stonecutting/decorative_mazestone/mazestone_brick` (has_block: has decorative_mazestone) |
| `twilightforest:stonecutting/decorative_mazestone/mazestone_mosaic` | mc:stonecutting | decorative_mazestone | mazestone_mosaic | `twilightforest:recipes/building_blocks/stonecutting/decorative_mazestone/mazestone_mosaic` (has_block: has decorative_mazestone) |
| `twilightforest:stonecutting/encased_castle_brick_pillar/castle_roof_tile` | mc:stonecutting | encased_castle_brick_pillar | castle_roof_tile | `twilightforest:recipes/building_blocks/stonecutting/encased_castle_brick_pillar/castle_roof_tile` (has_block: has encased_castle_brick_pillar) |
| `twilightforest:stonecutting/encased_castle_brick_pillar/encased_castle_brick_stairs` | mc:stonecutting | encased_castle_brick_pillar | encased_castle_brick_stairs | `twilightforest:recipes/building_blocks/stonecutting/encased_castle_brick_pillar/encased_castle_brick_stairs` (has_block: has encased_castle_brick_pillar) |
| `twilightforest:stonecutting/encased_castle_brick_pillar/encased_castle_brick_tile` | mc:stonecutting | encased_castle_brick_pillar | encased_castle_brick_tile | `twilightforest:recipes/building_blocks/stonecutting/encased_castle_brick_pillar/encased_castle_brick_tile` (has_block: has encased_castle_brick_pillar) |
| `twilightforest:stonecutting/encased_castle_brick_tile/castle_roof_tile` | mc:stonecutting | encased_castle_brick_tile | castle_roof_tile | `twilightforest:recipes/building_blocks/stonecutting/encased_castle_brick_tile/castle_roof_tile` (has_block: has encased_castle_brick_tile) |
| `twilightforest:stonecutting/encased_castle_brick_tile/encased_castle_brick_pillar` | mc:stonecutting | encased_castle_brick_tile | encased_castle_brick_pillar | `twilightforest:recipes/building_blocks/stonecutting/encased_castle_brick_tile/encased_castle_brick_pillar` (has_block: has encased_castle_brick_tile) |
| `twilightforest:stonecutting/encased_castle_brick_tile/encased_castle_brick_stairs` | mc:stonecutting | encased_castle_brick_tile | encased_castle_brick_stairs | `twilightforest:recipes/building_blocks/stonecutting/encased_castle_brick_tile/encased_castle_brick_stairs` (has_block: has encased_castle_brick_tile) |
| `twilightforest:stonecutting/etched_nagastone/nagastone_stairs_left` | mc:stonecutting | etched_nagastone | nagastone_stairs_left | `twilightforest:recipes/building_blocks/stonecutting/etched_nagastone/nagastone_stairs_left` (has_block: has etched_nagastone) |
| `twilightforest:stonecutting/etched_nagastone/nagastone_stairs_right` | mc:stonecutting | etched_nagastone | nagastone_stairs_right | `twilightforest:recipes/building_blocks/stonecutting/etched_nagastone/nagastone_stairs_right` (has_block: has etched_nagastone) |
| `twilightforest:stonecutting/mazestone_border/cut_mazestone` | mc:stonecutting | mazestone_border | cut_mazestone | `twilightforest:recipes/building_blocks/stonecutting/mazestone_border/cut_mazestone` (has_block: has mazestone_border) |
| `twilightforest:stonecutting/mazestone_border/decorative_mazestone` | mc:stonecutting | mazestone_border | decorative_mazestone | `twilightforest:recipes/building_blocks/stonecutting/mazestone_border/decorative_mazestone` (has_block: has mazestone_border) |
| `twilightforest:stonecutting/mazestone_border/mazestone_brick` | mc:stonecutting | mazestone_border | mazestone_brick | `twilightforest:recipes/building_blocks/stonecutting/mazestone_border/mazestone_brick` (has_block: has mazestone_border) |
| `twilightforest:stonecutting/mazestone_border/mazestone_mosaic` | mc:stonecutting | mazestone_border | mazestone_mosaic | `twilightforest:recipes/building_blocks/stonecutting/mazestone_border/mazestone_mosaic` (has_block: has mazestone_border) |
| `twilightforest:stonecutting/mazestone_brick/cut_mazestone` | mc:stonecutting | mazestone_brick | cut_mazestone | `twilightforest:recipes/building_blocks/stonecutting/mazestone_brick/cut_mazestone` (has_block: has mazestone_brick) |
| `twilightforest:stonecutting/mazestone_brick/decorative_mazestone` | mc:stonecutting | mazestone_brick | decorative_mazestone | `twilightforest:recipes/building_blocks/stonecutting/mazestone_brick/decorative_mazestone` (has_block: has mazestone_brick) |
| `twilightforest:stonecutting/mazestone_brick/mazestone_border` | mc:stonecutting | mazestone_brick | mazestone_border | `twilightforest:recipes/building_blocks/stonecutting/mazestone_brick/mazestone_border` (has_block: has mazestone_brick) |
| `twilightforest:stonecutting/mazestone_brick/mazestone_mosaic` | mc:stonecutting | mazestone_brick | mazestone_mosaic | `twilightforest:recipes/building_blocks/stonecutting/mazestone_brick/mazestone_mosaic` (has_block: has mazestone_brick) |
| `twilightforest:stonecutting/mazestone_mosaic/cut_mazestone` | mc:stonecutting | mazestone_mosaic | cut_mazestone | `twilightforest:recipes/building_blocks/stonecutting/mazestone_mosaic/cut_mazestone` (has_block: has mazestone_mosaic) |
| `twilightforest:stonecutting/mazestone_mosaic/decorative_mazestone` | mc:stonecutting | mazestone_mosaic | decorative_mazestone | `twilightforest:recipes/building_blocks/stonecutting/mazestone_mosaic/decorative_mazestone` (has_block: has mazestone_mosaic) |
| `twilightforest:stonecutting/mazestone_mosaic/mazestone_border` | mc:stonecutting | mazestone_mosaic | mazestone_border | `twilightforest:recipes/building_blocks/stonecutting/mazestone_mosaic/mazestone_border` (has_block: has mazestone_mosaic) |
| `twilightforest:stonecutting/mazestone_mosaic/mazestone_brick` | mc:stonecutting | mazestone_mosaic | mazestone_brick | `twilightforest:recipes/building_blocks/stonecutting/mazestone_mosaic/mazestone_brick` (has_block: has mazestone_mosaic) |
| `twilightforest:stonecutting/mazestone/cut_mazestone` | mc:stonecutting | mazestone | cut_mazestone | `twilightforest:recipes/building_blocks/stonecutting/mazestone/cut_mazestone` (has_block: has mazestone) |
| `twilightforest:stonecutting/mazestone/decorative_mazestone` | mc:stonecutting | mazestone | decorative_mazestone | `twilightforest:recipes/building_blocks/stonecutting/mazestone/decorative_mazestone` (has_block: has mazestone) |
| `twilightforest:stonecutting/mazestone/mazestone_border` | mc:stonecutting | mazestone | mazestone_border | `twilightforest:recipes/building_blocks/stonecutting/mazestone/mazestone_border` (has_block: has mazestone) |
| `twilightforest:stonecutting/mazestone/mazestone_brick` | mc:stonecutting | mazestone | mazestone_brick | `twilightforest:recipes/building_blocks/stonecutting/mazestone/mazestone_brick` (has_block: has mazestone) |
| `twilightforest:stonecutting/mazestone/mazestone_mosaic` | mc:stonecutting | mazestone | mazestone_mosaic | `twilightforest:recipes/building_blocks/stonecutting/mazestone/mazestone_mosaic` (has_block: has mazestone) |
| `twilightforest:stonecutting/mossy_castle_brick/bold_castle_brick_pillar` | mc:stonecutting | mossy_castle_brick | bold_castle_brick_pillar | `twilightforest:recipes/building_blocks/stonecutting/mossy_castle_brick/bold_castle_brick_pillar` (has_block: has mossy_castle_brick) |
| `twilightforest:stonecutting/mossy_castle_brick/bold_castle_brick_stairs` | mc:stonecutting | mossy_castle_brick | bold_castle_brick_stairs | `twilightforest:recipes/building_blocks/stonecutting/mossy_castle_brick/bold_castle_brick_stairs` (has_block: has mossy_castle_brick) |
| `twilightforest:stonecutting/mossy_castle_brick/bold_castle_brick_tile` | mc:stonecutting | mossy_castle_brick | bold_castle_brick_tile | `twilightforest:recipes/building_blocks/stonecutting/mossy_castle_brick/bold_castle_brick_tile` (has_block: has mossy_castle_brick) |
| `twilightforest:stonecutting/mossy_castle_brick/mossy_castle_brick_stairs` | mc:stonecutting | mossy_castle_brick | mossy_castle_brick_stairs | `twilightforest:recipes/building_blocks/stonecutting/mossy_castle_brick/mossy_castle_brick_stairs` (has_block: has mossy_castle_brick) |
| `twilightforest:stonecutting/mossy_castle_brick/thick_castle_brick` | mc:stonecutting | mossy_castle_brick | thick_castle_brick | `twilightforest:recipes/building_blocks/stonecutting/mossy_castle_brick/thick_castle_brick` (has_block: has mossy_castle_brick) |
| `twilightforest:stonecutting/mossy_etched_nagastone/mossy_nagastone_stairs_left` | mc:stonecutting | mossy_etched_nagastone | mossy_nagastone_stairs_left | `twilightforest:recipes/building_blocks/stonecutting/mossy_etched_nagastone/mossy_nagastone_stairs_left` (has_block: has mossy_etched_nagastone) |
| `twilightforest:stonecutting/mossy_etched_nagastone/mossy_nagastone_stairs_right` | mc:stonecutting | mossy_etched_nagastone | mossy_nagastone_stairs_right | `twilightforest:recipes/building_blocks/stonecutting/mossy_etched_nagastone/mossy_nagastone_stairs_right` (has_block: has mossy_etched_nagastone) |
| `twilightforest:stonecutting/mossy_nagastone_stairs_left/mossy_nagastone_stairs_right` | mc:stonecutting | mossy_nagastone_stairs_left | mossy_nagastone_stairs_right | `twilightforest:recipes/building_blocks/stonecutting/mossy_nagastone_stairs_left/mossy_nagastone_stairs_right` (has_block: has mossy_nagastone_stairs_left) |
| `twilightforest:stonecutting/mossy_nagastone_stairs_right/mossy_nagastone_stairs_left` | mc:stonecutting | mossy_nagastone_stairs_right | mossy_nagastone_stairs_left | `twilightforest:recipes/building_blocks/stonecutting/mossy_nagastone_stairs_right/mossy_nagastone_stairs_left` (has_block: has mossy_nagastone_stairs_right) |
| `twilightforest:stonecutting/nagastone_stairs_left/nagastone_stairs_right` | mc:stonecutting | nagastone_stairs_left | nagastone_stairs_right | `twilightforest:recipes/building_blocks/stonecutting/nagastone_stairs_left/nagastone_stairs_right` (has_block: has nagastone_stairs_left) |
| `twilightforest:stonecutting/nagastone_stairs_right/nagastone_stairs_left` | mc:stonecutting | nagastone_stairs_right | nagastone_stairs_left | `twilightforest:recipes/building_blocks/stonecutting/nagastone_stairs_right/nagastone_stairs_left` (has_block: has nagastone_stairs_right) |
| `twilightforest:stonecutting/stone/bold_stone_pillar` | mc:stonecutting | mc:stone | bold_stone_pillar | `twilightforest:recipes/building_blocks/stonecutting/stone/bold_stone_pillar` (has_block: has mc:stone) |
| `twilightforest:stonecutting/stone/spiral_bricks` | mc:stonecutting | mc:stone | spiral_bricks | `twilightforest:recipes/building_blocks/stonecutting/stone/spiral_bricks` (has_block: has mc:stone) |
| `twilightforest:stonecutting/stone/twisted_stone` | mc:stonecutting | mc:stone | twisted_stone | `twilightforest:recipes/building_blocks/stonecutting/stone/twisted_stone` (has_block: has mc:stone) |
| `twilightforest:stonecutting/stone/twisted_stone_pillar` | mc:stonecutting | mc:stone | twisted_stone_pillar | `twilightforest:recipes/building_blocks/stonecutting/stone/twisted_stone_pillar` (has_block: has mc:stone) |
| `twilightforest:stonecutting/thick_castle_brick/bold_castle_brick_pillar` | mc:stonecutting | thick_castle_brick | bold_castle_brick_pillar | `twilightforest:recipes/building_blocks/stonecutting/thick_castle_brick/bold_castle_brick_pillar` (has_block: has thick_castle_brick) |
| `twilightforest:stonecutting/thick_castle_brick/bold_castle_brick_stairs` | mc:stonecutting | thick_castle_brick | bold_castle_brick_stairs | `twilightforest:recipes/building_blocks/stonecutting/thick_castle_brick/bold_castle_brick_stairs` (has_block: has thick_castle_brick) |
| `twilightforest:stonecutting/thick_castle_brick/bold_castle_brick_tile` | mc:stonecutting | thick_castle_brick | bold_castle_brick_tile | `twilightforest:recipes/building_blocks/stonecutting/thick_castle_brick/bold_castle_brick_tile` (has_block: has thick_castle_brick) |
| `twilightforest:stonecutting/twisted_stone/twisted_stone_pillar` | mc:stonecutting | twisted_stone | twisted_stone_pillar | `twilightforest:recipes/building_blocks/stonecutting/twisted_stone/twisted_stone_pillar` (has_block: has twisted_stone) |
| `twilightforest:stonecutting/underbrick/underbrick_floor` | mc:stonecutting | underbrick | underbrick_floor | `twilightforest:recipes/building_blocks/stonecutting/underbrick/underbrick_floor` (has_block: has underbrick) |
| `twilightforest:stonecutting/worn_castle_brick/bold_castle_brick_pillar` | mc:stonecutting | worn_castle_brick | bold_castle_brick_pillar | `twilightforest:recipes/building_blocks/stonecutting/worn_castle_brick/bold_castle_brick_pillar` (has_block: has worn_castle_brick) |
| `twilightforest:stonecutting/worn_castle_brick/bold_castle_brick_stairs` | mc:stonecutting | worn_castle_brick | bold_castle_brick_stairs | `twilightforest:recipes/building_blocks/stonecutting/worn_castle_brick/bold_castle_brick_stairs` (has_block: has worn_castle_brick) |
| `twilightforest:stonecutting/worn_castle_brick/bold_castle_brick_tile` | mc:stonecutting | worn_castle_brick | bold_castle_brick_tile | `twilightforest:recipes/building_blocks/stonecutting/worn_castle_brick/bold_castle_brick_tile` (has_block: has worn_castle_brick) |
| `twilightforest:stonecutting/worn_castle_brick/thick_castle_brick` | mc:stonecutting | worn_castle_brick | thick_castle_brick | `twilightforest:recipes/building_blocks/stonecutting/worn_castle_brick/thick_castle_brick` (has_block: has worn_castle_brick) |
| `twilightforest:stonecutting/worn_castle_brick/worn_castle_brick_stairs` | mc:stonecutting | worn_castle_brick | worn_castle_brick_stairs | `twilightforest:recipes/building_blocks/stonecutting/worn_castle_brick/worn_castle_brick_stairs` (has_block: has worn_castle_brick) |
| `twilightforest:ur_ghast_banner_pattern` | mc:crafting_shapeless | #c:paper + ur_ghast_trophy | ur_ghast_banner_pattern | `twilightforest:recipes/misc/ur_ghast_banner_pattern` (has_trophy: has ur_ghast_trophy) |
| `twilightforest:wood/mossy_towerwood` | mc:crafting_shapeless | towerwood + mc:vine/mc:moss_block | mossy_towerwood | `twilightforest:recipes/building_blocks/wood/mossy_towerwood` (has_item: has towerwood) |
| `twilightforest:wood/smoked_cracked_towerwood` | mc:smoking | towerwood (xp 0.1, 100 t) | cracked_towerwood | `twilightforest:recipes/building_blocks/wood/smoked_cracked_towerwood` (has_item: has towerwood) |

### Config keys (1)

See [config.md](config.md) for the section mapping; defaults and translation ids below are TF's.

| TF key | Default | Translation id | English (config screen) |
|---|---|---|---|
| `client.rotateTrophyHeadsGui` | `true` | `animate_trophies` | Animate Trophies — Rotate Trophy heads on item model. Has no performance impact at all. For those who don't like fun. |

## Appendix B: Strings (112 en_us keys owned)

Keys already shown above (block/item/entity names, subtitles) are not repeated. The 29 Bedrock locales are generated from TF's lang files (R9); owners use these keys with the English text as fallback.

| Key | English |
|---|---|
| `block.twilightforest.arctic_fur_block.desc` | Reduces fall damage by 90% |
| `block.twilightforest.knightmetal_block.desc` | Deals strong contact damage |
| `config.twilightforest.animate_trophies` | Animate Trophies |
| `config.twilightforest.animate_trophies.tooltip` | Rotate Trophy heads on item model. Has no performance impact at all. For those who don't like fun. |
| `item.twilightforest.alpha_yeti_banner_pattern.desc` | Alpha Yeti Face |
| `item.twilightforest.hydra_banner_pattern.desc` | Hydra Flame |
| `item.twilightforest.knight_phantom_banner_pattern.desc` | Knight Helmet |
| `item.twilightforest.lich_banner_pattern.desc` | Lich Crown |
| `item.twilightforest.minoshroom_banner_pattern.desc` | Minoshroom Axes |
| `item.twilightforest.naga_banner_pattern.desc` | Naga Scales |
| `item.twilightforest.quest_ram_banner_pattern.desc` | Questing Ram Swirls |
| `item.twilightforest.snow_queen_banner_pattern.desc` | Snowflake |
| `item.twilightforest.ur_ghast_banner_pattern.desc` | Carminite Border |

