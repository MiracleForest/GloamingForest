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
once its hook is in (F0's file: the hook is a separate F0 pull request that does nothing until this
directory exists, so it can be merged first); without it `run.ps1` applies the generator, and the
e2e prelaunch scripts of F11 apply it to the deployed copy when the build has not.

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
| `menu_category` | when there is no skeleton |
| `sound` | the Bedrock block sound in `blocks.json` (default per family) |
| `sound_type` | a TF sound type (`twilightforest.jar`, TFSoundTypes; the jars get it from TFBlocks) |
| `components` | raw Bedrock components merged last |
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

The chests keep their slots in F10's BlockData backend, not its native one (a game container in the
data-driven block actor of `minecraft:block_entity`, spike S3). The kit tried both formats for that
component on the TF chests: format_version 1.26.0 refuses it (`child 'minecraft:block_entity' not
valid here`) and 1.26.50 accepts it but refuses the `tag:minecraft:is_axe_item_destructible`
components F1 gives TF's chests (`this component was found in the input, but is not present in the
Schema`), so the chests would not load. The fixture chests (no tag components) loaded with it at
1.26.50 and kept their slots through pair and lid state changes; the native backend can follow once
the import writes block tags in a form 1.26.50 takes.