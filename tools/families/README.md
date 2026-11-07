# Block families (F11)

`tools/families` turns family data into Bedrock block definitions at assemble time, and
`src/feature/families` gives the blocks their Java behaviour. A unit declares its blocks in
`assets/data/families/<unit>.json` (assembled into `data/gf/families/`); F1 imports TF's own as
`assets/gen/import/<unit>/data/gf/families/<unit>.json`. The fixture families of the selftests live in
`fixtures/families.json` (gf_test ids, development builds only).

```powershell
pwsh tools/families/run.ps1             # applies the generator to bin/server and bin/client (after a build)
pwsh tools/families/run.ps1 -Check      # assembles into build/assemble-check and checks the result
pwsh tools/families/run.ps1 -Out <dir>  # an assembled or deployed mod directory
```

`tools/xmake/assemble.lua` (F0) runs the generator on every build and in `tools/assemble.ps1 -Check`
(its families hook). `run.ps1` applies the generator to a directory the build did not assemble; the
server e2e prelaunch script of F11 still runs it on the deployed copy as a safety net, a no-op on a
copy the build already generated.

## Data

```json
[
  {"id": "twilightforest:canopy_stairs", "family": "stairs", "params": {"class": "StairBlock", "base": "twilightforest:canopy_planks"}},
  {"id": "twilightforest:mason_jar", "family": "snowloggable"}
]
```

One id lists at most one shape family and any modifiers; params of later entries add to earlier
ones. An unknown family is skipped with a warning. A modifier is either an entry of its own (the
`mason_jar` line above) or, F1's form, a name in `params.traits` of the shape entry:

```json
{"id": "twilightforest:canopy_slab", "family": "slab", "params": {"class": "SlabBlock", "traits": ["waterloggable"]}}
{"id": "twilightforest:hollow_oak_log_climbable", "family": "horizontal", "params": {"class": "ClimbableHollowLogBlock", "traits": ["climbable"]}}
```

`traits` may name `waterloggable`, `snowloggable`, `climbable` and `lightable`; `connected_texture`
there only repeats the family and is ignored. The runtime (`src/feature/families/Registry.cpp`) and
the generator read both forms the same way, and `run.ps1` fails when a waterloggable block lacks
Bedrock's liquid detection.

Shape families: `cube stairs slab fence fence_gate wall door trapdoor button pressure_plate pane
pillar log wood leaves cross sapling potted sign wall_sign hanging_sign wall_hanging_sign chest
trapped_chest horizontal directional binary_rotated wall_pillar connectable_pillar
connected_texture`. Modifiers: `waterloggable snowloggable climbable lightable`.

F1's form (TF's Java class picks the variant):

| family | params.class | the kit's family |
|---|---|---|
| `sign` | `WallSignBlock` | wall sign (else standing sign) |
| `hanging_sign` | `WallHangingSignBlock` | wall hanging sign (else ceiling hanging sign) |
| `pillar` | `WallPillarBlock` | wall pillar |
| `pillar` | `ThornsBlock`, `BurntThornsBlock`, `ConnectableRotatedPillarBlock` | connectable pillar (width 10 for thorns) |

## Params

Every family:

| param | meaning |
|---|---|
| `class`, `vanillaClass`, `states`, `copyOf`, `base` | F1's import: the Java class, the vanilla class it extends, its block-state properties, the block whose properties it copies, a stairs' base block |
| `woodType` | F1's import: the TFWoodTypes field (`canopy_wood_type`, `canopy_wood_set`): the sign / chest wood (`canopy`) |
| `wood` | the wood of a sign or chest texture (default: from `woodType` or the id) |
| `textures` | a texture key, or `{key: texture key}` (`*`, `side`, `top`, `bottom`, `end`, `up`, `down`, `north`..., `front`, `back`, `lower`, `upper`, `pot`, `soil`, `plant`, `overlay`, `connected`, `single`/`left`/`right` for chests): the kit draws the block with these instead of keeping the skeleton's model |
| `render_method` | Bedrock material render method |
| `hardness`, `resistance`, `map_color`, `light`, `loot`, `tool` | base components (otherwise the skeleton's) |
| `tags` | F1's import: the block's Bedrock block tags, free of any format (see Block tags) |
| `menu_category` | when there is no skeleton |
| `sound` | the Bedrock block sound in `blocks.json` (default per family), for a block that has none yet: TF's sound types (`twilightforest.jar`, TFSoundTypes) and the blocks using them come from F1's import (`rp/blocks.d`, `rp/sounds.json.d`), which the kit leaves alone |
| `components` | raw Bedrock components merged last (their tags join the block's tags, see Block tags) |
| `waterlogged` / `waterloggable` | adds the waterloggable modifier |
| `traits` | F1's modifiers of the entry (`["waterloggable", "climbable"]`) |

Per family:

| family | params |
|---|---|
| `fence` | `wooden` (default true; Java's WOODEN_FENCES tag) |
| `door`, `trapdoor` | `block_set` (`iron`: iron sounds, not opened by hand), `open_by_hand` |
| `button` | `ticks` (default 30, TF's wooden buttons), `arrows` (wooden: true), `block_set` (`stone`, `iron`, `gold`) |
| `pressure_plate` | `sensitivity` (`everything` / `mobs`), `block_set` |
| `log`, `wood`, `pillar` | `stripped` (default: TF's STRIPPABLES, RegistrationEvents) |
| `horizontal` | `placement`: `opposite` (default; TFHorizontalBlock), `look` (BanisterBlock), `none` (ClimbableHollowLogBlock) |
| `directional` | `placement`: `nearest_opposite` (default; DirectionalBlock), `nearest` (EtchedNagastoneBlock), `nearest_y_flipped` (StrongholdShieldBlock), `none` |
| `leaves` | `tint` (Bedrock tint method by biome; default: TF's ColorHandler where Bedrock has one), `tint_color` (`#rrggbb`: a colour Java always uses, the texture pre-tinted in `terrain_texture.json`; default: TF's beanstalk and thorn leaves). TF's time, transformation, mining, sorting and rainbow oak leaves get a tint method: the client mod draws ColorHandler's colour per position through `gf::client::tint`, `log` (counts as a log for leaf distance, on any family). A tinted leaf takes Bedrock's grey image of a `_carried` vanilla texture |
| `sapling` | `grower`: Java's TreeGrower `{feature, secondary_feature, secondary_chance, mega_feature, secondary_mega_feature, flowers_feature, secondary_flowers_feature}` or TF's StructureTreeGrower `{structure}` (default: TFTreeGrowers by sapling id); `feature`: a grower with one tree. A unit's own grower: `src/feature/ext/families/Growers.h` |
| `potted` | `plant` (default: `potted_<x>` -> `<x>`, TF's thorn pots from RegistrationEvents), `special` (TF's SpecialFlowerPotBlock keeps the plant) |
| `sign`, `hanging_sign` | `wall` (default: `<x>_sign` -> `<x>_wall_sign`, `<x>_hanging_sign` -> `<x>_wall_hanging_sign`); `gui_texture`: what the native editor draws the sign with (default: `textures/twilightforest/entity/signs/<wood>`, hanging signs `textures/twilightforest/gui/hanging_signs/<wood>`) |
| `wall_sign`, `wall_hanging_sign` | `standing` (the reverse) |
| `chest`, `trapped_chest` | `layout`: `legacy` for Bedrock's pre-1.15 chest textures (fixtures) |
| `connectable_pillar` | `width` (default 16; thorns 10) |
| `connected_texture` | `connects_to` (block ids, default: itself), `faces` (default all), `render_on_disabled_faces` (default true) |
| `cross` | `shape` (selection box, block pixels) |
| `snowloggable` | `snow_texture`, `shape` (the plant's outline for the hit test). A skeleton that has Java's shapes per state and `layers` (F1's TF bushes) keeps them; otherwise the kit adds the snow's boxes per layer count |
| `lightable` | `light_levels` `{normal, dim, ominous}` |

## Sound events

The kit adds the events the game plays with a family block's sound type to that type in the
resource pack's `sounds.json`: `place_in_water` (a waterloggable block placed into water; the type's
place event, Java's BlockItem#place place sound) and, for chests, `chest.open` / `chest.closed`
(ChestBlockEntity: `random.chestopen` / `random.chestclosed`, volume 0.5, pitch 0.9..1.0). Every
family entry counts, hand-made blocks (a manual BP file) too; a waterloggable or chest family block
with no sound type in `blocks.json` fails the run. The hanging signs take Bedrock's `hanging_sign`
type (CeilingHangingSignBlock / WallHangingSignBlock: `woodType.hangingSignSoundType()`) unless
`params.sound` says otherwise; a sound the hand-made RP layer (`blocks.d`) sets always stays (a
warning names it) and is listed in `data/gf/families_sounds/<name>` (`{"manual": {id: sound}}`),
which the selftest `families.sound_events` accepts as it is.

## States

As `docs/spec/blockstates.md` (F1, rule R1): every Java property `p` is `twilightforest:p` with
Java's values (booleans `[false, true]`, integers `0..n`, enums by name); no placement traits: the
runtime sets them on placement as Java's `getStateForPlacement` does. The states the kit adds that
no Java block declares are in its own namespace, so a TF block keeps exactly its frozen
`twilightforest:*` states (F1's `import.blocks` checks that): `gloamingforest:lid` (chests: the lid's
angle in 22.5 degree steps, 0..4, moved by the runtime as Java's ChestLidController),
`gloamingforest:open_count` (trapped chests: the players inside, their signal),
`gloamingforest:<direction>` and `gloamingforest:corner_<a>_<b>` (connected textures). Java's own
SnowLoggable / LightableBlock properties `twilightforest:layers` / `twilightforest:lighting` are added
to snowloggable / lightable blocks that lack them. A lightable block keeps the light per state of F1's
skeleton (TF's `lightLevel`) unless the data gives `light_levels`.

## Chests are containers

Chests and trapped chests keep their slots, screens, sounds, opening stat and comparator output in
the inventory contract (F10, spike S3: `src/feature/inventory`). The generator declares every block
of the `chest` / `trapped_chest` families as a container in `data/gf/containers/families.json`
(fixtures: `families_test.json`), read by F10 at mod load: 27 slots, `pairs` (a left / right pair
opens as one 54-slot chest, the right half first), `chest` (no screen under a redstone conductor or a
sitting cat), the opening stat (`minecraft:open_chest`, trapped chests
`minecraft:trigger_trapped_chest`) and the comparator output. A unit that declares the block itself in
`assets/data/containers/<u>.json` wins (the generator leaves it out). The runtime
(`src/feature/families/Chests.cpp`) keeps what the chest block does: the facing and type on
placement and when a neighbour changes, the lid from F10's openers
(`feature/ext/combat/InventoryViewers.h`) and the trapped chest's signal.

The chests keep their slots in F10's native backend: the kit writes every chest family block in
format_version 1.26.50 with `minecraft:block_entity`, so each half has a data-driven block actor and
F10 gives it a game container (spike S3), saved with the chunk (27 slots per half, kept through the
pair, lid and signal state changes). Format 1.26.0 refuses `minecraft:block_entity` (`not valid
here`); 1.26.50 takes it and declares the block's tags as one `minecraft:tags` array (spike S7), so
the kit writes the tags in the form of each document's format (below). `run.ps1` (and assemble)
fails when a generated chest lacks `minecraft:block_entity` or one of its tags, or when a document
carries a tag form its format refuses.

As in Java (TF's chests are vanilla `ChestBlock` / `TrappedChestBlock`, so
`HopperBlockEntity#getContainerAt` finds their `ChestBlockEntity`), hoppers fill a chest from above
and from its sides and empty it from below, hopper minecarts empty the chest above their rail and
droppers drop into the chest they face: `src/feature/families/Hoppers.cpp` hands the game's hopper
and dropper lookups (`Hopper::_getContainerInBlock`, `Hopper::_getContainersAt`,
`DropperBlockActor::getContainerAt`) the native container, which they would not see on their own (a
data-driven block actor has no vanilla main component). A paired half gives the double chest as one
54-slot `CompoundContainer`, the right half first (a hopper on the left half fills the right half's
first free slot), the blocked rule ignored (`ChestBlock#getContainer(.., true)`), and a pending loot
table rolls on the hopper's first look (`RandomizableContainer#unpackLootTable(null)`). The lookup
reaches the native containers of the blocks hoppers may reach
(`src/feature/ext/families/Hoppers.h`): by default every block the inventory contract declares
chest-like (`Declared::chest`, so TF's chests); a unit opts another Container block entity of Java in
(the skull chest, the keepsake casket: `gf::families_ext::setHopperVisible(id, true)`) or hides one
(`false`). A block kept in block data stays invisible to hoppers whatever is set. A lookup stays
cheap for hoppers that look every tick: what it found at a position (visible or not, the halves and
their containers) is kept while the block, the neighbour its pairing depends on and the inventory
contract's declaration of the block (checked once a tick per block type, so `inventory::declare` or
`unregisterBlock` reach it in the next tick) stay the same; a half's container is handed out again
while the live block actor at its position still holds it (the cached pointers are compared with the
live ones, never dereferenced), and a half's block data is read once a tick (Java's
`RandomizableContainer` checks its pending loot table on every access; the block data contract has
no write notification that would let a flag stand in for that read).

A crafter puts what it crafts into the chest it faces (Java's `CrafterBlock#dispenseItem`:
`HopperBlockEntity.getContainerAt` then `addItem`; a double chest's right half first);
`families.chest_crafter` checks a single and a double chest.

The comparator reads a chest as Java's `ChestBlock#getAnalogOutputSignal` (F10's
`comparatorSignal`): `getRedstoneSignalFromContainer` over the single chest or the double chest's 54
slots, and 0 while a half is blocked (`getContainer(.., false)` finds none) even though hoppers still
fill it. Stacks still waiting in block data (see below) count where a move would put them (F10's
reads, `feature/ext/combat/InventoryMigrate.h`).

Every change reaches every open chest screen at the end of the tick, as Java's
`ChestMenu#broadcastChanges` does for each player looking inside: F10 notes every write of a native
container (its `setItem`: a hopper's, a dropper's or a crafter's change, another player's click, any
code's write; the hopper view's `removeItem` notes its half itself) and refreshes each client mod
chest screen showing a changed container in the context it opened the screen with
(`feature/ext/combat/InventoryChanges.h`: a double chest's clicked half, or the half code opened it
for; `families.chest_screen_context`). The game's chest screen of a vanilla client follows through
F10's proxy.

A chest whose stacks are still in block data (placed while the chests used F10's BlockData backend)
moves them into its block actor at the end of the tick its chunk loaded, whether the chunk ticks or
not (the game's block actor chunk listener, `BlockActorLevelListener::onChunkLoaded`, tells; the
sections whose palette holds no chest are skipped, and only a chest's own block data is read); a
chunk saved without the chest's block actor gets one first (the game's block actor factory makes the
one the block type declares and the chunk takes it; a state change of the block does not create it).
The chunk's first tick does the same, and moves the stacks of the chests in the loaded chunks around
it into the block actors they have (chunks loaded before the mod heard them load); a neighbour whose
chest still waits for a block actor is tried again at the next neighbour's first tick. Before all
that, a hopper's read of the chest (either half of a double chest) moves them into the block actor
the chest has (it makes none: the game may be iterating the chunk's block actors then). A player's
click moves nothing and makes no block actor inside `GameMode::useItemOn`: the chest is settled on
the next tick (`interactions::nextTick`), and the screen F10 opens for the click shows the waiting
stacks meanwhile and follows the move (`families.chest_migrate_click`). The inventory contract reads
waiting stacks itself (its screens, its comparator output) and moves them on its first write, so
nothing finds the chest empty or one half missing. The moves are F10's
(`feature/ext/combat/InventoryMigrate.h`: `hasBlockDataStacks`, `migrateBlockDataToNative`), so the
kit never reads F10's block data layout. A chest broken before its stacks moved loses none: F10 drops
the block data stacks with the block (`blockdata::onErase`) and the block actor's own
(`families.chest_migrate_removed`). `families.chest_persist_a_write` writes such stacks through F10's
own handler. No world with such chests shipped (W0 builds only), but development worlds have them.

## Block tags

The kit takes a block's Bedrock tags from F1's `params.tags` (the tool tags and, for a block a tool
mines, its tier tag; spike S7), else from the document the block starts from (the skeleton or
`params.base`, in either form), plus `params.tool`'s tag, and writes them in the form of the format
it writes the block in: one `minecraft:tags` array from 1.26.20 (the chests, 1.26.50), a
`"tag:<id>": {}` component per tag up to 1.26.10 (every other family block, 1.26.0). Whatever tag
form the skeleton copy carries is dropped first, so the skeleton's own format does not matter. A tag
an entry's `params.components` carries (a `"tag:<id>"` key or a `minecraft:tags` array) joins the
list before it is written, and only the other components are merged after it, so `components` can
never put back a tag form the block's format refuses (the fixture `gf_test:family_trapped_chest`
carries one).

## Lang strings

The kit's own strings are in `assets/lang/<locale>/families.json` (rule R9: F11 owns these keys; no
other unit may define them, as a duplicate key fails the lang merge):

| Key | Used for |
|---|---|
| `container.chest`, `container.chestDouble` | the chest family's container titles (ChestBlockEntity#getDefaultName, CompoundContainer), declared in `data/gf/containers/families.json`; F10's inventory contract falls back to the same keys without defining them |
| `sign.edit`, `hanging_sign.edit` | the sign editor's title (AbstractSignEditScreen): the client mod's native editor and the server's text for a vanilla client's form |

They are vanilla Minecraft's (not TF's) UI strings, taken verbatim from Java Edition 1.21.1's own
language files (Mojang's 1.21.1 asset index) for the 29 Bedrock locales; Bedrock's `nb_NO` uses
Java's `no_no`. Bedrock ships the same short strings under `sign.edit` / `container.chest`. Their
provenance is recorded here and in the PR that added them (#31) for the licence review.

## Audit round 1

Checked: the world lifecycle of the kit's deferred / scheduled block updates, block event
registration, and waterlogging against Java's SimpleWaterloggedBlock.

Fixed:
- The access violation in `detail::clearQueues` (src/feature/families/World.cpp) from the
  world-closed listener at server stop: the queues live on the heap for the whole process (never
  destroyed by static destruction), are guarded by a mutex, and a world close only hands the
  pending work aside; the next tick on the server thread destroys it.
- `gf::blockevents::ext::add*` / `remove` (src/feature/ext/families/BlockEventsHandles.h): every
  block event registration with a handle; a removed handler is never called again, its function is
  destroyed at the next tick (a handler may remove itself). Selftest `families.blockevents`.
- Waterloggable families: `on_liquid_touches` is `blocking` (was `no_reaction`), so flowing water
  stops at a dry block as in Java (FlowingFluid#canHoldFluid asks
  SimpleWaterloggedBlock#canPlaceLiquid, which takes only a source); a bucket or a block placed into
  a source still waterlogs it. Selftest `families.waterlogging`.

Open:
- Imported (F1, `tools/import/overrides/woodwork.json`: the hollow logs) and hand-made unit blocks
  (U5 `rope`, `iron_ladder`, U30 `blue_force_field`, ...) still declare `no_reaction`; their owners
  should switch to `blocking` the same way.
- A family block placed into flowing water keeps the flow in its water layer (Java: not
  waterlogged, getStateForPlacement asks for a source).

## Audit round 2

Fixed:
- A waterloggable family block placed into flowing water is dry (SimpleWaterloggedBlock#
  getStateForPlacement: WATERLOGGED only for a source): the placed handler of every waterloggable
  entry drops a flowing liquid layer and keeps a source (`dropFlowingWater`, Shapes.cpp). Selftest
  `families.waterlog_flowing`.

Open:
- The imported hollow logs (F1, `tools/import/overrides/woodwork.json`) and hand-made unit blocks
  (U5 `rope`, `iron_ladder`, U30 `blue_force_field`, ...) still declare `on_liquid_touches:
  no_reaction`: their owners switch to `blocking`.
