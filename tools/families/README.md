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

`tools/xmake/assemble.lua` (F0) runs the generator itself once its hook is in (the call is in the
PR of F11); until then `run.ps1` applies it, and the e2e prelaunch scripts of F11 apply it to the
deployed copy.

## Data

```json
[
  {"id": "twilightforest:canopy_stairs", "family": "stairs", "params": {"class": "StairBlock", "base": "twilightforest:canopy_planks"}},
  {"id": "twilightforest:mason_jar", "family": "snowloggable"}
]
```

One id lists at most one shape family and any modifiers; params of later entries add to earlier
ones. An unknown family is skipped with a warning.

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
| `leaves` | `tint` (Bedrock tint method; default: TF's ColorHandler where Bedrock has one), `log` (counts as a log for leaf distance, on any family) |
| `sapling` | `grower`: Java's TreeGrower `{feature, secondary_feature, secondary_chance, mega_feature, secondary_mega_feature, flowers_feature, secondary_flowers_feature}` or TF's StructureTreeGrower `{structure}` (default: TFTreeGrowers by sapling id); `feature`: a grower with one tree. A unit's own grower: `src/feature/ext/families/Growers.h` |
| `potted` | `plant` (default: `potted_<x>` -> `<x>`, TF's thorn pots from RegistrationEvents), `special` (TF's SpecialFlowerPotBlock keeps the plant) |
| `sign`, `hanging_sign` | `wall` (default: `<x>_sign` -> `<x>_wall_sign`, `<x>_hanging_sign` -> `<x>_wall_hanging_sign`) |
| `wall_sign`, `wall_hanging_sign` | `standing` (the reverse) |
| `chest`, `trapped_chest` | `layout`: `legacy` for Bedrock's pre-1.15 chest textures (fixtures) |
| `connectable_pillar` | `width` (default 16; thorns 10) |
| `connected_texture` | `connects_to` (block ids, default: itself), `faces` (default all), `render_on_disabled_faces` (default true) |
| `cross` | `shape` (selection box, block pixels) |
| `snowloggable` | `snow_texture`, `shape` (the plant's outline for the hit test) |
| `lightable` | `light_levels` `{normal, dim, ominous}` |

## States

As `docs/spec/blockstates.md` (F1, rule R1): every Java property `p` is `twilightforest:p` with
Java's values (booleans `[false, true]`, integers `0..n`, enums by name); no placement traits: the
runtime sets them on placement as Java's `getStateForPlacement` does. The kit adds
`twilightforest:open_count` (chests: the players inside, the lid, the trapped chest's signal),
`twilightforest:<direction>` and `twilightforest:corner_<a>_<b>` (connected textures), and
`twilightforest:layers` / `twilightforest:lighting` to snowloggable / lightable blocks that lack them.
