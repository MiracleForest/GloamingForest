# U22 `lichdecor`: Lich tower interiors and decor

Behaviour spec translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ `9e40a757`). Checklist for the
U22 worker and the W2 audit (A3). Numbers are TF's; "Bedrock:" paragraphs mark adaptation notes.

| | |
|---|---|
| Unit | U22 `lichdecor` (W1 batch 2) |
| Selftests | `lichdecor.*`, testPos dx `2200-2299` (owners rows `selftest lichdecor.*`, `selftest_dx 2200-2299`) |
| World DB | `gf:lichdecor:*` |
| Net channels | `lichdecor.*` |
| Config | section `lichdecor` (config_section `lichdecor`): `common.disableSkullCandleCreation` (default `false`, translation `disable_skull_candles`) |
| Owned paths beyond R3 | `assets/structures/lich/rooms/**` (the lich tower room interiors; generator and template ids are U21's, see lich.md 4.3-4.4) |
| Contracts | template (F7), families + blockevents (F11), blockdata + redstone (F0), inventory (F10), entity (F8), data, client.render (F12), interactions |
| Java sources | `block/{LightableBlock,BrazierBlock,CandelabraBlock,ChiseledCanopyShelfBlock,AbstractSkullCandleBlock,SkullCandleBlock,WallSkullCandleBlock,OminousCandleBlock,OminousFireBlock,SinisterSpawnerBlock,SkullChestBlock,WroughtIronFenceBlock}.java`, `block/entity/{BrazierBlockEntity,CandelabraBlockEntity,OminousCandleBlockEntity,SkullCandleBlockEntity,SkullChestBlockEntity}.java`, `block/entity/bookshelf/*.java`, `block/entity/spawner/{SinisterSpawnerBlockEntity,SinisterSpawnerLogic}.java`, `components/item/{CandelabraData,SkullCandles}.java`, `entity/{MagicPainting,MagicPaintingVariant}.java`, `item/{MagicPaintingItem,SkullCandleItem,WroughtIronFenceItem}.java`, `enums/BrazierLight.java`, `events/EntityEvents.java` (`createSkullCandle`, `ominousFireConversion`, `zombifiedPlayerAttacks`, `attachLeadToWroughtFence`), `command/SinisterSpawnerCommand.java`, `init/{TFBlocks,TFItems,TFBlockEntities,TFDataComponents,TFDataSerializers,TFDataMaps,TFStats,TFParticleType,TFSounds}.java` |

Not owned here: exanimate essence (U9), scepters (U9), the Lich's candle-turning logic (U21 calls these blocks by
id), keepsake casket (U9, which places a skull chest at casket damage >= 2), canopy wood set (U4).

## 1. Lighting model (`LightableBlock`)

State `lighting` = `none | normal | dim | ominous` (frozen name/values, blockstates.md). Shared rules for the
candelabra and skull candles:
- Empty hand (may build) on a lit block: extinguish (`setLit(false)`, SMOKE puffs (0, 0.025, 0) at every flame offset,
  `minecraft:block.candle.extinguish`, game event BLOCK_CHANGE).
- Firestarter (flint and steel, fire charge: `FIRESTARTER_LIGHT` ability) on an unlit block: lighting NORMAL.
- Burning projectile hit: lighting NORMAL (server).
- Ambient (client, per flame per animate tick): 30 % SMOKE (only when NORMAL), 17 % `minecraft:block.candle.ambient`
  (volume 1-2, pitch 0.3-1.0); flame particle `minecraft:small_flame` (NORMAL), `twilightforest:dim_flame` (DIM),
  `twilightforest:ominous_flame` (OMINOUS).

Particles `twilightforest:dim_flame` and `twilightforest:ominous_flame`: vanilla small-flame behaviour with TF's
sprites (owned here; U21, F8 spawners and others emit them by id).

## 2. Blocks

| Block | Properties | Behaviour |
|---|---|---|
| `twilightforest:candelabra` | stone map colour, strength 1.5; states `facing` (4), `on_wall`, `lighting`, `waterlogged`, `has_candle_1..3`; block entity `twilightforest:candelabra` | 2.1 |
| `twilightforest:brazier` | wood sound, light 0/4/6/9/12 on the upper half by `light`, push DESTROY; states `half` (lower/upper), `light` (off, dim, half, bright, full); block entity `twilightforest:brazier` on the lower half | 2.2 |
| `twilightforest:chiseled_canopy_bookshelf` | chiseled-bookshelf sound, strength 2.5, flammable (30 / 20), bass; states `facing`, 6 `slot_N_occupied`, `spawner`; block entity `twilightforest:chiseled_canopy_bookshelf` | 2.3 |
| `twilightforest:coronation_carpet` | copy of red carpet; mobs may spawn on it (`isValidSpawn` always); member of `#minecraft:wool_carpets` | sinister spawners treat it as solid floor |
| `twilightforest:terrorcotta_arcs` | stone, pickaxe, 1.5 / 6.0; rotated pillar (`axis`) | |
| `twilightforest:terrorcotta_curves` | stone, pickaxe, 1.5 / 6.0; GlazedTerracottaBlock (horizontal `facing`; no push reaction set, so it moves normally with pistons) | |
| `twilightforest:terrorcotta_lines` | stone, pickaxe, 1.5 / 6.0; TF BinaryRotatedBlock (two-way `axis` x/z) | |
| `twilightforest:wrought_iron_fence` | strength 8 / 20, metal, pickaxe, no occlusion; states `post` (none, post, capped), `north_fence`..`west_fence` (none, middle, bottom, top, full), `waterlogged` | 2.4 |
| `twilightforest:sinister_spawner` | copy of `minecraft:spawner`, no loot table, xp 15-43 (`15 + nextInt(15) + nextInt(15)`); block entity (vanilla-like) | 2.5 |
| `twilightforest:skull_chest` | light 15 when lava-logged, light grey, no occlusion, push BLOCK, pickaxe, netherite sound, 3.0 / 100, explosion-proof (`Float.MAX_VALUE`); states `facing`, `multilogged` (TF BlockLoggingEnum); block entity `twilightforest:skull_chest` | 2.6 |
| `twilightforest:ominous_fire` | purple, replaceable, no collision, instabreak, light 15, wool sound, push DESTROY; fire damage 1.0 base | 2.7 |
| `twilightforest:ominous_candle` + 16 colours `twilightforest:ominous_{white,orange,magenta,light_blue,yellow,lime,pink,gray,light_gray,cyan,purple,blue,brown,green,red,black}_candle` | map colour of the colour, no occlusion, strength 0.1, candle sound, light `2 * candles`, push DESTROY; state `candles` 1-4; block entity `twilightforest:ominous_candle` | 2.8 |
| skull candles `twilightforest:{zombie,skeleton,wither_skeleton,creeper,player,piglin}_skull_candle` and the wall forms `twilightforest:{zombie,skeleton,wither_skeleton,creeper,player,piglin}_wall_skull_candle` | copies of the matching vanilla head / wall head; states `lighting`, `candles` 1-4, `rotation` 0-15 (floor) or `facing` (wall); block entity `twilightforest:skull_candle`; rendered by the block entity (invisible block model) | 2.9 |

Block ids repeated for the checklist: `twilightforest:ominous_black_candle`, `twilightforest:ominous_blue_candle`,
`twilightforest:ominous_brown_candle`, `twilightforest:ominous_cyan_candle`, `twilightforest:ominous_gray_candle`,
`twilightforest:ominous_green_candle`, `twilightforest:ominous_light_blue_candle`,
`twilightforest:ominous_light_gray_candle`, `twilightforest:ominous_lime_candle`, `twilightforest:ominous_magenta_candle`,
`twilightforest:ominous_orange_candle`, `twilightforest:ominous_pink_candle`, `twilightforest:ominous_purple_candle`,
`twilightforest:ominous_red_candle`, `twilightforest:ominous_white_candle`, `twilightforest:ominous_yellow_candle`;
`twilightforest:creeper_skull_candle`, `twilightforest:creeper_wall_skull_candle`, `twilightforest:piglin_skull_candle`,
`twilightforest:piglin_wall_skull_candle`, `twilightforest:player_skull_candle`, `twilightforest:player_wall_skull_candle`,
`twilightforest:skeleton_skull_candle`, `twilightforest:skeleton_wall_skull_candle`,
`twilightforest:wither_skeleton_skull_candle`, `twilightforest:wither_skeleton_wall_skull_candle`,
`twilightforest:zombie_skull_candle`, `twilightforest:zombie_wall_skull_candle`.

Block items: `twilightforest:brazier`, `twilightforest:candelabra`, `twilightforest:chiseled_canopy_bookshelf`,
`twilightforest:coronation_carpet`, `twilightforest:sinister_spawner`, `twilightforest:skull_chest`,
`twilightforest:terrorcotta_arcs`, `twilightforest:terrorcotta_curves`, `twilightforest:terrorcotta_lines`,
`twilightforest:wrought_iron_fence` (a `WroughtIronFenceItem`, 2.4). Ominous candles, ominous fire and wall skull
candles have no item (pick block gives the vanilla candle / exanimate essence).

### 2.1 Candelabra

- Placement: clicking a top face first tries standing (`on_wall` false, facing = player horizontal direction, needs
  a centre-supporting block below), then wall placement against each horizontal look direction (needs the block
  in front to support the centre); other faces try the wall first. Waterlogged from the fluid. Survives only while
  its support stays.
- Shapes (exact boxes in `CandelabraBlock`): wall shapes per facing, standing shapes per axis.
- Candles: holding any `#minecraft:candles` item (or sneaking) targets a slot by hit position across the block
  (`<= 0.375` slot 0, `<= 0.6875` slot 1, else slot 2; reversed for NORTH/EAST): an empty slot takes the held candle
  (block entity stores which candle block, `minecraft:block.candle.place` 1.0, 0.9-1.0, consume one); sneaking on a
  filled slot removes it into the inventory (creative: only if not already present). `has_candle_N` mirror the
  block entity; removing the last candle unlights it.
- Lighting: firestarter -> NORMAL (needs at least one candle, not waterlogged). Redstone dust on NORMAL -> DIM
  (consumes 1, `block.twilightforest.candelabra.light` 1.0, 0.9-1.0, 5 dim-flame burst particles per candle).
  A scepter (`#twilightforest:scepters`) or exanimate essence on NORMAL -> OMINOUS (`block.twilightforest.candelabra.ominous`,
  ominous flame burst). Waterlogging extinguishes.
- Light emission: NORMAL `5 * candles`; DIM and OMINOUS `2 * candles`; NONE 0.
- Redstone: only while DIM it is a power source of strength `3 * candles`, weak and strong alike
  (`getDirectSignal` returns `getSignal`), in one direction only. Vanilla's `getSignal(state, level, pos, direction)`
  convention: `direction` points from the receiver to the emitter (the receiver is at `pos.relative(direction.getOpposite())`),
  the same convention as `RedstoneTorchBlock` and floor levers.
  - Wall (`on_wall=true`): `getSignal` and `getDirectSignal` are `3 * candles` only when `direction ==
    FACING.getOpposite()`, i.e. it strongly powers only the support block at `pos.relative(FACING)` (toward its back;
    `canSurvive` checks that same block), which then powers its own neighbours as a conductor.
  - Standing (`on_wall=false`): `3 * candles` only when `direction == UP`, i.e. downward only: strong power into the
    supporting block below (`pos.below()`, both `getSignal` and `getDirectSignal` are `3 * candles` when queried from
    below); nothing above or beside it is powered directly.
  - `canConnectRedstone` is true only for a DIM wall candelabra with `FACING.getOpposite() == direction` (its back
    face, toward the support).
  - Comparator output: NONE `candles`, DIM `3 + candles`, OMINOUS `6 + candles`, NORMAL `9 + candles`.
- Drops: the base item; with silk touch the candelabra keeps its candles (data component
  `twilightforest:candelabra_data`, a list of up to 3 candle blocks), otherwise the candles drop separately. Pick
  block keeps the data. Loot `twilightforest:blocks/candelabra`: itself if it survives the explosion.
- Recipe `twilightforest:candelabra` (shaped `### / # `, `#c:ingots/wrought_iron`) -> 2 candelabras; unlock
  advancement `twilightforest:recipes/decorations/candelabra` (has `#c:ingots/wrought_iron`).
- Rendering: block model with the candle blocks drawn per slot by the block entity renderer (client mod); the
  vanilla fallback uses the `has_candle_N` states with a plain candle model.

### 2.2 Brazier

- Two-block tall: placing needs the space above (replaceable, below build limit); the upper half is added in
  `setPlacedBy`. Halves keep each other (shape updates as doors); breaking the upper half in creative or without
  the correct tool removes the lower half (no drop).
- Upper half interaction: flint and steel (durability -1) or fire charge (consumed) cycles `light`
  off -> dim -> half -> bright -> full (no further at full), `minecraft:item.flintandsteel.use`. A water container
  on a lit brazier empties 1000 mB and sets `light` off with `minecraft:block.fire.extinguish`.
- `BrazierLight`: off (light 0, size 0, smoke -), dim (4, 0.25, every 10 ticks), half (6, 0.5, 4), bright (9, 0.75,
  2), full (12, 1.0, 1). Client tick: a SMOKE particle above the upper block every `smokeRate` ticks (global counter),
  random x/z 0.3-0.7, y + 0.9, velocity (0, 0.05, 0). The fire size scales the rendered flame.
- Shapes: lower 2..14 x 0..16, upper 2..14 x 0..12. Loot `twilightforest:blocks/brazier`: itself from the lower half.
- Bedrock: two-block family ("door"-like) via F11; the renderer draws the fire by size (client mod), vanilla gets a
  per-`light` model.

### 2.3 Chiseled canopy bookshelf

Vanilla chiseled-bookshelf behaviour (6 book slots, `#minecraft:bookshelf_books`, comparator by last slot) plus:
- `spawner` state true: interaction fails (cannot insert/take); the block entity runs a Death Tome spawner
  (`BookshelfSpawner`): max nearby 4, spawn range 4, spawn check range 12, initial delay 20, delay 200-400, player
  range 8 (creative players count). When it may spawn, it shuffles the filled slots and tries
  `attemptSpawnTome(slot)`: spawn at `pos + facing + (r1 - r2) * 2` horizontally, `y + (r1 - r2)`, needs the block in
  front replaceable, no collision, not peaceful for hostiles, custom spawn rules if any; the spawned mob plays the
  spawn animation and the slot's book is removed. With no filled slots left the shelf turns `spawner = false`.
- Catching fire while a spawner: every filled slot spawns a burning (200 ticks) tome targeting the igniter (5 tries
  each), then the shelf is destroyed.
- Spawn egg on a non-empty shelf: plays `block.twilightforest.chiseled_bookshelf.convert` (0.35, 0.6-1.0) and lets
  the egg set the spawned entity (vanilla spawner egg behaviour).
- Broken by a player while a spawner: `entity.twilightforest.death_tome.death` (U21's event, 1.0, 1.0) and 20 POOF
  particles to players within 32.
- Loot `twilightforest:blocks/chiseled_canopy_bookshelf`: itself if it survives the explosion.
- Lich tower rooms place it through the `chiseled_canopy_shelf` marker (lich.md 4.4).

### 2.4 Wrought iron fence and bar

- Connections: to `#minecraft:walls`, iron bars / panes and any sturdy face that is not a connection exception.
  Each side is `none`, or `full` / `top` / `bottom` / `middle` depending on whether the fence above / below also
  connects that way (stacked fences form continuous panels).
- `post`: `capped` stays while the block above is replaceable; else `post` when this or any fence directly
  above/below in the column "should be a post" (no sides, an odd number of sides on an axis, or a corner); else
  `none`.
- Item: sneak-use on the top fence of a column toggles the finial (`capped` <-> computed post state) and plays
  `block.twilightforest.wrought_iron_fence.extend` (0.35, 0.75-0.85). Tooltip
  `block.twilightforest.wrought_iron_fence.cap` "Shift + right-click the same block to place finial".
- A lead used on a fence with a post leashes the player's leashed mobs to it (`LeadItem.bindPlayerMobs`).
- Not pathfindable; skylight passes unless waterlogged.
- Recipe `twilightforest:wrought_iron_fence` (shaped `###/###` of `#c:ingots/wrought_iron`) -> 3 fences; unlock
  `twilightforest:recipes/decorations/wrought_iron_fence`. Loot `twilightforest:blocks/wrought_iron_fence`: itself.
- `twilightforest:wrought_iron_bar` ("Wrought Iron Bar"): plain item, the only TF member of `#c:ingots/wrought_iron`;
  creative/commands only: TF 4.8 has no recipe and no loot table that yields it (it appears only in `TFItems`, the
  item model, the `c:ingots/wrought_iron` tag, the lang files and `TFCreativeTabs`). The candelabra and wrought iron
  fence recipes are therefore effectively creative-only in TF 4.8; keep it that way (no invented recipe or loot).
- Bedrock: post (3) x four sides of 5 values (625) x waterlogged (2) = 3750 permutations is far over budget: store the side states as BlockData or
  compute the geometry client-side (F11 connected family); keep the state names for parity.

### 2.5 Sinister spawner

- A vanilla-like monster spawner (`SinisterSpawnerLogic extends BaseSpawner`) with: a set of particle options
  (`ParticleOptions` list, saved), an entity scan range (`EntityScanRange`, default = spawn range), and an optional
  loot table (`LootTable`) rolled into the drops when broken.
- Spawning: when a player is near, choose `countNextToSpawn = nextInt(1 + spawnCount)`; positions are found by a
  walking scan from below the spawner (`scanSpawnPositions`: walk down through air, add the position 50 % when the
  floor is solid or coronation carpet and the spot is air, wander horizontally, restart when blocked or out of
  range); spawn into the buffered positions (vanilla custom spawn rules / spawn placement checks; max nearby
  entities counted in the scan range box); each spawn emits 10 of every configured particle around the mob, and a
  successful batch 10 around the spawner.
- Client: particles at the spawner while a player is near; the mob preview spins at `1000 / (delay + 200)`.
- Creative use with a boss trophy or boss spawner item sets its particles: naga -> smoke + crit; lich -> smoke +
  ominous flame; minoshroom -> smoke + crimson spore; hydra -> smoke + flame; knight phantom -> smoke + ominous
  flame; ur-ghast -> smoke + redstone dust; alpha yeti -> TF snow + falling water; snow queen -> snow + snow warning;
  final boss spawner -> annihilate; quest ram trophy -> transformation particle.
- Lich tower rooms place it through `sinister_spawner[:mobs[:range[:scan]]]` with ominous flame particles and loot
  `twilightforest:blocks/ominous_spawner_drops` (= one `twilightforest:exanimate_essence`).
- Subcommand: TF literal **`/twilightforest sinister_spawner`**, permission 2. Children: `add_particle <particle>
  <pos>`, `remove_particle <particle> <pos>`, `set_loot <loot_table> <pos>` (suggests loot tables) and `clear_loot
  <pos>`. Each returns 1 on a change, 0 otherwise.
- Ownership conflict: owners.tsv (row `subcommand spawner U22`) and AGENTS.md R14 name the subcommand `spawner`, but
  TF registers `sinister_spawner` (`SinisterSpawnerCommand.java:31`). For parity the command players type is
  `/twilightforest sinister_spawner`. The owners id is raised with the coordinator in
  [issue #5](https://github.com/MiracleForest/GloamingForest/issues/5), so that F0/A8 can rename the owners.tsv row
  to `sinister_spawner`. Until then the row `spawner` is the ownership record for this one subcommand. U22 registers
  only the `sinister_spawner` literal with `GF_REGISTER_SUBCOMMAND` and adds no `spawner` alias, because TF has none.

### 2.6 Skull chest

- 45-slot container (9 x 5, `GENERIC_9x5` menu), lootable (`RandomizableContainer`), title = block name or the
  custom name. Lid animation like a chest; open/close sounds `block.twilightforest.skull_chest.open` /
  `block.twilightforest.skull_chest.close` (0.5, 0.9-1.0).
- Optional owner profile (`owner`): only the owner or permission level 3 may open it; others hear
  `block.twilightforest.skull_chest.locked` (notify, 0.5, 0.5) and see the action-bar message
  `block.twilightforest.skull_chest.locked` with the owner's name in red.
- Multi-logging (`multilogged`): water / lava / obsidian / blocks as the keepsake casket (lava-logged gives light
  15 and turns to obsidian next to water); when filled by a solid block it cannot be opened and uses the solid shape.
- Breaking (survival, doTileDrops): drops itself (named after the owner's display name if set) plus the logged
  block; contents spill. Explosion resistance infinite. Loot `twilightforest:blocks/skull_chest` has an empty entry
  (the drop is spawned manually).
- Placed by U9's keepsake casket when the casket would break (damage >= 2 on its 15 % damage roll).

### 2.7 Ominous fire

- A fire block (`BaseFireBlock`, damage 1.0) that survives only on a sturdy top face without fluid; any shape update
  that fails the check removes it. Pick block: `twilightforest:exanimate_essence` (U9 places it with the essence and
  plays `block.twilightforest.ominous_fire.light`).
- Entities inside that are not `#minecraft:undead` take 1.0 `twilightforest:ominous_fire` damage per tick contact.
- Damage type `twilightforest:ominous_fire`: message id `twilightforest.ominous`, exhaustion 0.1, effects BURNING,
  scaling when caused by living non-player; tags `c:is_magic`, bypasses armor, wither immune to, no knockback, panic
  environmental causes.
- Conversion on death by this damage (`ominousFireConversion`): a server player becomes a zombie at its position
  carrying the player's profile (attachment `ZOMBIFIED_PLAYER`), custom name = player name, can pick up loot, adult,
  finalised as CONVERSION; other entities convert through the data map `twilightforest:ominous_fire`
  (`minecraft:horse` -> `minecraft:zombie_horse`, `minecraft:piglin` -> `minecraft:zombified_piglin`,
  `minecraft:villager` -> `minecraft:zombie_villager`). Zombified-player zombies deal their damage as ominous fire
  damage (`zombifiedPlayerAttacks`). Loot `twilightforest:blocks/ominous_fire`: empty.
- Block tag: TF adds it to `#minecraft:fire` (U22 contributes the member through the tag fragment).

### 2.8 Ominous candles

- Map from the 17 vanilla candles to their ominous twins (`CANDLE_MAP`); the Lich converts lit candles into them and
  back (lich.md 1.12). Shapes as vanilla candles (1-4).
- Flames bob: each flame's y = `0.5 + 0.25 * ((sin(t*7 + x*i) + sin(t*4.2 + y*i) + sin(t*3 + z*i)) / 4 * 0.5 + 0.5)`
  with `t = gameTime * 0.014`; particles per flame: 30 % SMOKE, 17 % candle ambient, always an ominous flame. The block
  entity only stores the renderer's per-flame heights.
- Use with an empty hand (may build): becomes the lit-off vanilla candle with the same count (extinguish sound and
  smoke). Using the matching vanilla candle item adds a candle (max 4, place sound `(volume + 1) / 2`, pitch * 0.8).
- `eruptFlameParticles`: 5 ominous flames per candle with small random velocity (horizontal +-0.025, up 0.015), shown
  when U9's exanimate essence converts a lit vanilla candle into its ominous twin (sound
  `block.twilightforest.ominous_fire.light` 1.5, `(r1 - r2) * 0.2 + 0.75`; the essence is consumed).
- Loot: the vanilla candle of the same colour, count = candles (2-4 via block-state conditions), explosion decay
  (`twilightforest:blocks/ominous_candle` and `twilightforest:blocks/ominous_<colour>_candle`).

### 2.9 Skull candles

- Creation (`createSkullCandle`, unless `disableSkullCandleCreation`): right-clicking a **vanilla** skull or head
  block (skeleton, wither skeleton, player, zombie, creeper, piglin; floor or wall) with a **vanilla**
  `#minecraft:candles` item while not sneaking replaces it with the matching skull candle (same rotation/facing,
  lighting NONE, candle colour from the item, player profile kept), consumes one candle, swings, awards the stat
  `twilightforest:skull_candles_made` and cancels the use. Sound `minecraft:block.candle.place`. Dragon heads are not
  convertible.
- Adding candles: the same-colour candle item (not sneaking) adds one up to 4 (candle place sound); sneaking with an
  empty hand (secondary use) removes one candle into the inventory (`minecraft:block.candle.break`); removing the
  last one restores the vanilla skull (with its owner profile).
- Light: NORMAL `3 * candles`, OMINOUS `2 * candles`, DIM `candles`, NONE 0. Lightable rules of section 1.
- Block entity `twilightforest:skull_candle` (a SkullBlockEntity): saved `CandleColor` (0 plain, 1..16 dye colours,
  `CandleColors` order), owner profile; when powered by redstone it animates (dragon/piglin style) like vanilla heads.
- Drops: the vanilla head (`twilightforest:blocks/<type>_skull_candle` / `<type>_wall_skull_candle` drop the vanilla
  skull; player heads copy profile, note block sound and custom name) plus the candles; with silk touch the skull
  candle item itself with data component `twilightforest:skull_candles` (`{color, count}`) and the profile.
- Items (`SkullCandleItem`, standing-and-wall, equipable on the head): `twilightforest:zombie_skull_candle`,
  `twilightforest:skeleton_skull_candle`, `twilightforest:wither_skeleton_skull_candle`,
  `twilightforest:creeper_skull_candle`, `twilightforest:player_skull_candle` (name `%s's Head with Candles` when it
  has a profile), `twilightforest:piglin_skull_candle`; tooltip `item.twilightforest.skull_candle.desc` "Has: %s %s
  Candle" / `.desc.multiple` "Has: %s %s Candles" (count, colour).
- Rendering: block entity renderer draws the vanilla head model plus the candle block on top (client mod); vanilla
  clients see a static skull + candle geometry.

## 3. Magic paintings

- Entity `twilightforest:magic_painting` (MISC 0.5 x 0.5, tracking 10, update interval MAX: never re-sent), a hanging
  entity with synced variant (data serializer `twilightforest:magic_painting_variant`); saved variant id;
  bounding box from the variant's pixel size (16 px per block). Breaking drops the item (not in creative, honours
  `doEntityDrops`) with `minecraft:entity.painting.break`; placing plays `minecraft:entity.painting.place`.
- Item `twilightforest:magic_painting`: places like a painting; with data component
  `twilightforest:magic_painting_variant` it places that variant, otherwise the largest variants that fit are chosen
  at random (`MagicPainting.create`).
- Registry `magic_paintings` (data-driven `twilight/magic_paintings`), five entries:

| Entry | Size (px) | Title / author keys | Layers (parallax, opacity modifier) |
|---|---|---|---|
| `magic_paintings/twilightforest:castaway_paradise` | 32 x 64 | "Castaway Paradise" / "HexaBlu" | sunset (view-angle 0.01, 64x64), sun, faraway palm, ocean, deckchair, palm tree, frame |
| `magic_paintings/twilightforest:darkness` | 64 x 32 | "Darkness" / "???" | background, sky (view-angle 0.01, 128x32; sine-time opacity 0.03), terrain, gems, gems (day-time opacity 1-23999 inverted x2), lightning (lightning opacity), frame |
| `magic_paintings/twilightforest:lucid_lands` | 48 x 48 | "Lucid Lands" / "Androsa" | background, clouds (linear-time 0.00075, 122x48), mesa, jungle and plains parallax, gold layers fading in while holding a gold ingot (x0.01), corrupt layers fading in under a harmful effect, frame |
| `magic_paintings/twilightforest:music_in_the_mire` | 64 x 48 | "Music in the Mire" / "TripleHeadedSheep" | 10 view-angle (0.015) layers with lightning overlays and a day-time fog (22331-2000), frame |
| `magic_paintings/twilightforest:the_hostile_paradise` | 48 x 32 | "The Hostile Paradise" / "Oz" | 13 layers: view-angle (0.02) parallax islands, dungeons, portal, field; distance-based fog opacities (4-12, 5-15, 6-18, 12-24), frame |

  Layer fields: `path`, optional `parallax {type: view_angle|linear_time|sine_time, multiplier, width, height}`,
  optional `opacity_modifier {type, multiplier, invert, min, max, from, to, item, effect_category}` (types include
  sine_time, day_time, lightning, holding_item, mob_effect_category, distance), `fullbright`, `local_lighting`;
  `back_texture` `twilightforest:magic_paintings/back`.
- The lich tower gallery rooms hang the painting named after the gallery (lich.md 4.4).
- Bedrock: the client mod renders the layered parallax (F12 render); vanilla clients get a flat composite texture
  of the base layers on a painting-like entity.

## 4. Data components, data map, stat

- `twilightforest:candelabra_data`: list (max 3) of candle block ids, persistent and network-synced.
- `twilightforest:skull_candles`: `{color: int, count: int}`, default `{0, 1}`.
- `twilightforest:magic_painting_variant`: holder of a `magic_paintings` entry.
- Data serializer `twilightforest:magic_painting_variant` (entity data of the painting).
- Data map `twilightforest:ominous_fire` (entity type -> `{transform_to}`), 3 entries above (consumer U22, R7).
- Stat `twilightforest:skull_candles_made` ("Skull Candles Created"), custom stat with default formatter.

## 5. Recipes and unlock advancements (owned)

| Recipe | Pattern | Result | Unlock advancement |
|---|---|---|---|
| `twilightforest:candelabra` | `###` / ` # ` (`#c:ingots/wrought_iron`) | 2 candelabras | `twilightforest:recipes/decorations/candelabra` (has wrought iron) |
| `twilightforest:wrought_iron_fence` | `###` / `###` | 3 fences | `twilightforest:recipes/decorations/wrought_iron_fence` |
| `twilightforest:stonecutting/stone/terrorcotta_arcs` | stone | arcs | `twilightforest:recipes/building_blocks/stonecutting/stone/terrorcotta_arcs` (has stone) |
| `twilightforest:stonecutting/stone/terrorcotta_curves` | stone | curves | `twilightforest:recipes/building_blocks/stonecutting/stone/terrorcotta_curves` |
| `twilightforest:stonecutting/stone/terrorcotta_lines` | stone | lines | `twilightforest:recipes/building_blocks/stonecutting/stone/terrorcotta_lines` |
| `twilightforest:stonecutting/terrorcotta_arcs/terrorcotta_curves` | arcs | curves | `twilightforest:recipes/building_blocks/stonecutting/terrorcotta_arcs/terrorcotta_curves` (has arcs) |
| `twilightforest:stonecutting/terrorcotta_arcs/terrorcotta_lines` | arcs | lines | `twilightforest:recipes/building_blocks/stonecutting/terrorcotta_arcs/terrorcotta_lines` |
| `twilightforest:stonecutting/terrorcotta_curves/terrorcotta_arcs` | curves | arcs | `twilightforest:recipes/building_blocks/stonecutting/terrorcotta_curves/terrorcotta_arcs` |
| `twilightforest:stonecutting/terrorcotta_curves/terrorcotta_lines` | curves | lines | `twilightforest:recipes/building_blocks/stonecutting/terrorcotta_curves/terrorcotta_lines` |
| `twilightforest:stonecutting/terrorcotta_lines/terrorcotta_arcs` | lines | arcs | `twilightforest:recipes/building_blocks/stonecutting/terrorcotta_lines/terrorcotta_arcs` |
| `twilightforest:stonecutting/terrorcotta_lines/terrorcotta_curves` | lines | curves | `twilightforest:recipes/building_blocks/stonecutting/terrorcotta_lines/terrorcotta_curves` |

## 6. Loot tables (owned)

Self-drops (if the block survives the explosion): `twilightforest:blocks/brazier` (lower half only),
`twilightforest:blocks/candelabra`, `twilightforest:blocks/chiseled_canopy_bookshelf`,
`twilightforest:blocks/coronation_carpet`, `twilightforest:blocks/terrorcotta_arcs`,
`twilightforest:blocks/terrorcotta_curves`, `twilightforest:blocks/terrorcotta_lines`,
`twilightforest:blocks/wrought_iron_fence`. Special: `twilightforest:blocks/skull_chest` (manual drop),
`twilightforest:blocks/ominous_fire` (none), `twilightforest:blocks/ominous_spawner_drops` (1 exanimate essence,
used by sinister spawners), ominous candles (`twilightforest:blocks/ominous_candle`,
`twilightforest:blocks/ominous_black_candle`, `twilightforest:blocks/ominous_blue_candle`,
`twilightforest:blocks/ominous_brown_candle`, `twilightforest:blocks/ominous_cyan_candle`,
`twilightforest:blocks/ominous_gray_candle`, `twilightforest:blocks/ominous_green_candle`,
`twilightforest:blocks/ominous_light_blue_candle`, `twilightforest:blocks/ominous_light_gray_candle`,
`twilightforest:blocks/ominous_lime_candle`, `twilightforest:blocks/ominous_magenta_candle`,
`twilightforest:blocks/ominous_orange_candle`, `twilightforest:blocks/ominous_pink_candle`,
`twilightforest:blocks/ominous_purple_candle`, `twilightforest:blocks/ominous_red_candle`,
`twilightforest:blocks/ominous_white_candle`, `twilightforest:blocks/ominous_yellow_candle`: the vanilla candle x
candles, explosion decay). Skull candles drop the vanilla head: `twilightforest:blocks/creeper_skull_candle`,
`twilightforest:blocks/creeper_wall_skull_candle` (creeper head), `twilightforest:blocks/piglin_skull_candle`,
`twilightforest:blocks/piglin_wall_skull_candle` (piglin head), `twilightforest:blocks/player_skull_candle`,
`twilightforest:blocks/player_wall_skull_candle` (player head, copying profile, note block sound, custom name),
`twilightforest:blocks/skeleton_skull_candle`, `twilightforest:blocks/skeleton_wall_skull_candle`,
`twilightforest:blocks/wither_skeleton_skull_candle`, `twilightforest:blocks/wither_skeleton_wall_skull_candle`,
`twilightforest:blocks/zombie_skull_candle`, `twilightforest:blocks/zombie_wall_skull_candle`; the code adds the
candles or swaps in the skull candle item under silk touch (2.9).

## 7. Sounds (owned, re-synthesised through `tools/sounds/recipes/lichdecor.json`)

| Event | Subtitle | Use |
|---|---|---|
| `block.twilightforest.candelabra.light` | "Candelabra dims" | redstone -> DIM |
| `block.twilightforest.candelabra.ominous` | "Candelabra sparks" | scepter / essence -> OMINOUS; Lich candle sweep |
| `block.twilightforest.chiseled_bookshelf.convert` | "Chiseled Bookshelf converts" | spawn egg on the shelf |
| `block.twilightforest.ominous_fire.light` | "Flame Whooshes Ominously" | Lich candle sweep; exanimate essence |
| `block.twilightforest.skull_chest.open` | "Skull Chest opens" | open |
| `block.twilightforest.skull_chest.close` | "Skull Chest closes" | close |
| `block.twilightforest.skull_chest.locked` | "Skull Chest clicks" | locked attempt |
| `block.twilightforest.wrought_iron_fence.extend` | "Iron clanks" | finial toggle |

## 8. Lich tower room interiors (`assets/structures/lich/rooms/**`)

U22 re-designs the room templates of the pools `twilightforest:lich_tower/3x3`, `5x5`, `7x7`, `9x9`, their
`ladder_N` variants, `9x9/special` (keepsake casket room), `center_decor` and `room_decor`, using the template ids and
pool memberships in lich.md section 8 and the marker vocabulary in lich.md 4.4. Each room keeps: the
`twilightforest:lich_tower/room` entry jigsaw, `bridge` exits, `roof` and `beard` connectors, the
`twilightforest:ladder_below/N` slots its ladder pools require, `decor` slots, and spawner / chest / lectern /
shelf / jar markers that reproduce TF's room themes (library, potion, altar, grave, study, nursery, ...). Room
spawners also get U21's room spawner processor.

## 9. Bedrock adaptation notes

- Custom blocks cannot hold block entities: candelabra candles, skull candle colour/profile, ominous candle heights,
  shelf books + spawner state, skull chest inventory/owner and sinister spawner settings live in BlockData and
  container entities per spike S3 (F10 inventory, F11 families).
- Redstone output of the candelabra and comparator values go through `redstone::registerEmitter` /
  `registerAnalog`. The contract's `EmitterFn` gets `towards`, the side from the candelabra to the neighbour, which
  is the opposite of Java's `direction`: return `3 * candles` (DIM only) for `towards == DOWN` when standing and for
  `towards == FACING` when on a wall, 0 otherwise (2.1). The neighbour then counts as strongly powered (Java's
  `getDirectSignal` is the same value).
- The wrought iron fence state space must be reduced for Bedrock's permutation budget (see 2.4).
- Interactions use `interactions::onUseOn` (vanilla skull + vanilla candle -> skull candle; lead on fence).

## 10. Selftests (`lichdecor.*`, dx 2200-2299)

`lichdecor.candelabra_slots`, `lichdecor.candelabra_signal` (DIM: 3 per candle into the block below a standing
candelabra and into the support behind a wall one, 0 into the block above and the other sides; comparator table),
`lichdecor.brazier_cycle`, `lichdecor.skull_candle_create` (stat + config off), `lichdecor.ominous_candle_revert`,
`lichdecor.ominous_fire_conversion` (villager -> zombie villager), `lichdecor.skull_chest_lock`,
`lichdecor.fence_finial`, `lichdecor.sinister_spawner_command`, `lichdecor.recipes`.
