# Models: Java model converter and geometry import (F2)

Contract **geo** (owner F2): `tools/models/convert.ps1` turns The Twilight Forest 4.8's Java entity models
(the `LayerDefinition` factories, LGPL code) into Bedrock geometry, bone maps, base animations and attachables;
`tools/models/attachable.ps1` writes the attachable of one item. W1 units only add animations and render
controllers on top of this output.

## Workflow

```powershell
pwsh tools/models/convert.ps1 -Unit hydra            # a W1 unit regenerates its partition and commits it
pwsh tools/models/convert.ps1 -Java HydraModel -Unit hydra
pwsh tools/models/attachable.ps1 -Item twilightforest:fiery_helmet
pwsh tools/models/convert.ps1 -Check                 # CI / review: committed files match the converter
pwsh tools/models/convert.ps1 -All -Out build/geo    # every partition, outside the repository
pwsh tools/models/test.ps1                           # the converter's tests (see Tests)
```

- The TF source comes from the shared checkout (`tools/tf/fetch.ps1`, rule R24); the converter runs it and
  never reads `.ogg` / `.nbt` files.
- **Partitions.** `assets/gen/geo/<unit>/**` belongs to `<unit>` (owners.tsv, rule R3): each W1 unit runs
  `convert.ps1 -Unit <unit>` in its own pull request and commits the result; nobody edits these files by hand
  (a manual file with the same id under `assets/pack/rp/**` overrides a generated one). F2 commits the
  partitions no unit claims: `models` (the index and the shared render controllers) and `landmark`
  (`protection_box`, F6). Without `-Unit` the converter writes only F2's partitions; `-All` writes all.
- `-Check` regenerates in memory and fails when a committed file differs, is missing or is stale: F2's
  partitions and every unit partition that is committed (or only the `-Unit` partition).
- The output is deterministic (fixed number formatting, catalog order, a fixed seed for TF's unseeded
  `RandomSource.create()`).

## Output

Per unit `<u>` (the owner of the entity / block / item the layer serves, from `tools/models/catalog.json`):

| File | Content |
|---|---|
| `assets/gen/geo/<u>/rp/models/entity/<name>.geo.json` | `geometry.twilightforest.<name>`, format 1.12.0. `<name>` is the TF model layer (`TFModelLayers`, `<path>` or `<path>_<layer>` when the layer is not `main`), an armor piece (`<material>.<slot>`) or an extract. |
| `assets/gen/geo/<u>/rp/animations/<name>.base.json` | `animation.twilightforest.<name>.base` (format 1.8.0): the setupAnim statements translated to Molang (only when at least one is). |
| `assets/gen/geo/<u>/bones/<name>.json` | The bone map: Java part path -> bone, parent, Bedrock pivot / rotation, Java offset / rotation, cube count, mirror; the layer and its supplier; the textures (Bedrock path and TF source); the setupAnim report (`translated`, `todo` with line, Java source and reason); notes. Not part of the pack. |
| `assets/gen/geo/<u>/rp/attachables/<item>.json` | Attachables of armor, Travellers' Gear, worn trophies, the mystic crown and the knightmetal shield. |
| `assets/gen/geo/models/rp/render_controllers/twilightforest.models.render_controllers.json` | `controller.render.twilightforest.models.part<N>`: the extra geometries of multi-part attachables (`Geometry.part1`, ...). |
| `assets/gen/geo/models/data/gf/models/index.json` | Every geometry, base animation and attachable with unit, ids, textures, bone / cube counts and visible bounds. Deployed as `<mod>/data/gf/models/index.json` (the `models.*` selftests, the e2e preview). |

Texture paths are `textures/twilightforest/<path below TF's assets/twilightforest/textures/>` without the
extension (`texturePrefix` in the catalog; `minecraft:<path>` names a vanilla texture). **Integration point
with F1 (import):** the imported textures must land at these paths (or the prefix changes in one place and
every partition is regenerated).

## Coordinates and UV

Java model space is in pixels with y pointing down; a part's `PartPose` offset is its pivot relative to the
parent's pivot and its rotation is applied as Rz · Ry · Rx (radians). The living-entity renderer draws model
point p at (p.x, 24 − p.y, −p.z) / 16 for an entity facing south; Bedrock entity geometry draws b at
(b.x, b.y, −b.z) / 16. So:

- pivot `b = (P.x, 24 − P.y, P.z)` with `P` the sum of the offsets up the chain (Bedrock pivots are absolute,
  in the unrotated bind pose, which composes exactly like Java's chain of translate-then-rotate);
- cube `origin = (P.x + x, 24 − (P.y + y + h), P.z + z)`, `size = (w, h, d)`, `inflate` = the uniform
  `CubeDeformation` (a non-uniform one is baked into the box with per-face UV);
- rotation `[deg(xRot), deg(yRot), deg(zRot)]`: Bedrock's z-y-x order and sign conventions cancel the y flip
  (checked against Bedrock's own pig geometry, body `[90, 0, 0]`, and `animation.common.look_at_target`,
  which uses `+query.target_x/y_rotation` like Java's `head.xRot = headPitch`, `head.yRot = netHeadYaw`).

UV: Java's `CubeListBuilder` uses the box layout with float sizes; Bedrock's box UV floors the sizes. Whole-pixel
cubes keep box UV (`"uv": [u, v]`, `"mirror": true`); a cube with a fractional size (the death tome's 0.005-deep
pages), a `texScale`, visible-face sets or a scaled extract gets per-face UV with Java's exact regions. Bedrock's
`east` face is the −x face (Java's WEST): north `[u+d, v+d] w×h`, east `[u, v+d] d×h`, south `[u+2d+w, v+d]`,
west `[u+d+w, v+d]`, up `[u+d, v] w×d`, down `[u+d+w, v+d] w×−d`; `mirror()` swaps east / west and flips every
face horizontally.

Names: Java part names stay, except the vanilla skeleton names at the root (`right_arm` → `rightArm`, ...,
`right_hind_leg` → `leg0`, `left_hind_leg` → `leg1`, `right_front_leg` → `leg2`, `left_front_leg` → `leg3`) so
the vanilla humanoid / quadruped animations apply. A name used twice (Snow Queen's `spike_4` under each crown
piece) becomes `<parent bone>_<name>`. A root `hat` with the head's pivot and rotation is parented to `head`
(Java copies the head pose to the hat every frame). `addOrReplaceChild` keeps the replaced part's children,
as Minecraft 1.21.1 does. Worn and held extracts prefix every bone with their wrapper's name
(`naga_trophy_worn_head`): Bedrock binds an attachable bone that has a wearer's bone name (`head`, `hat`, ...)
to that bone.

## The interpreter

`tools/models/lib` holds a Java lexer and recursive-descent parser for the subset TF's model classes use
(classes, fields, methods, generics, casts, lambdas, method references, switch expressions, enhanced for) and an
interpreter with Java's numeric semantics (int / long / float / double promotion, float arithmetic, integer
division, `Mth`'s 65536-entry sine table, `LegacyRandomSource` = `java.util.Random`'s LCG). Builtins model
Minecraft's builder API (`MeshDefinition`, `PartDefinition`, `CubeListBuilder` with every `addBox` overload,
`PartPose`, `CubeDeformation`, `LayerDefinition`, `LayerDefinitions`) and the vanilla meshes TF extends
(`HumanoidModel.createMesh`, `QuadrupedModel.createBodyMesh`, inherited as `PigModel` / `SheepModel` /
`SkeletonModel`). The layer list is not hand-written: the converter evaluates
`ClientRegistrationEvents.registerLayerDefinitions` (101 layers) and fails when one is missing from the catalog.
`JappaPackReloadListener.isJappaPackLoaded()` is false: the Jappa models belong to an optional resource pack
that TF does not ship (not ported, docs/spec/README.md).

## Renderers with their own transforms

Every layer geometry uses the living-entity mapping above (`LivingEntityRenderer`: yaw, `scale(-1, -1, 1)`,
`translate(0, -1.501, 0)`). Layers drawn by other renderers keep that mapping, and the owning unit reproduces
the renderer's own PoseStack transforms (or adds an extract): `hydra_mortar` (HydraMortarRenderer: no flip, the
model at the entity position, TNT-style swell), `cube_of_annihilation` / `roving_cube` (spin about y, scale 2 for
the roving cube), `chain` / `chain_block` (BlockChainRenderer: yaw − 90, pitch, links along the chain),
`protection_box`, `moonworm` (also MoonwormShotRenderer), the block entities `cicada` / `firefly` / `moonworm`
(facing, a random spin), `brazier`, `red_thread` (per face), `skull_chest` / `keepsake_casket` (facing) and the
`knightmetal_shield` item (ISTER `scale(1, -1, -1)`; see the held extract). In the e2e preview these models float
or sit upside down, as expected. `lich_minion`, `loyal_zombie` and `rising_zombie` declare a 64×32 texture
because Bedrock's vanilla zombie texture is 64×32; TF draws Java's 64×64 one with the same UVs.

## setupAnim → Molang

Per entity layer, `prepareMobModel` then `setupAnim` of the model class (catalog `animClass`, default the
supplier's class) are read statement by statement. Assignments to a part's `xRot` / `yRot` / `zRot` / `x` /
`y` / `z` whose right side uses only the parameters, constants, earlier locals, earlier assignments and
`Mth` / `Math` functions become Molang:

| Java | Molang |
|---|---|
| `limbSwing`, `limbSwingAmount` | `query.modified_distance_moved`, `query.modified_move_speed` |
| `ageInTicks`, `entity.tickCount (+ partialTick)` | `query.life_time * 20` |
| `netHeadYaw`, `headPitch` | `query.target_y_rotation`, `query.target_x_rotation` |
| `Mth.cos(x)` / `Mth.sin(x)` (radians) | `math.cos(x * 57.2958)` / `math.sin(...)` (degrees) |
| `part.xRot = e` (absolute radians) | `rotation[0] = e * 57.2958 − default` (Molang adds to the geometry) |
| `part.y = e` (pixels, y down) | `position[1] = −(e − default)` |
| `this.attackTime`, `this.young`, `this.riding` | `variable.attack_time`, `query.is_baby`, `query.is_riding` |

Locals are followed through reassignments (a local changed inside a condition or loop is dropped), divisions by
a constant zero stay untranslated, and the re-parented `hat` gets only its difference to the head (nothing when
Java copies the head's pose). The sign of the position x channel follows the geometry's x and is not yet checked
in game.

The quadruped walk comes out as Bedrock's own `animation.quadruped.walk` (`math.cos(query.modified_distance_moved
* 38.1704) * query.modified_move_speed * 80.2141`) and the head look as `look_at_target`. Everything else
(entity state, loops, conditions, visibility, other models' parts) is listed in the bone map's `setupAnim.todo`
with its line, Java source and reason; models without their own setupAnim name the vanilla animations of their
base class. 28 layers get a base animation; the owning units translate the rest by hand.

## Armor and Travellers' Gear

`HumanoidArmorLayer` draws the outer layer for helmet, chestplate and boots and the inner layer for leggings,
with the parts of the slot visible (helmet: head + hat; chestplate: body + arms; leggings: body + legs; boots:
legs). The converter writes one geometry per piece (`geometry.twilightforest.<material>.<slot>`) whose bones
carry the player's bone names, so the attachable follows the wearer, and an attachable with
`controller.render.armor`, the `armor` / `armor_enchanted` materials and the `*_layer_visible` parent setup.
Arctic, fiery, knightmetal, phantom and yeti armor have TF models; ironwood, steeleaf and naga armor use the
vanilla model (`geometry.humanoid.armor.*`). Travellers' Gear pieces follow `TravellersArmorItem.ArmorRender`:
goggles, vest (body), gloves (arms; a slim variant), belt and wings (children of the leggings' body, which TF
does not draw), boots. Not covered: the arctic armor's dye tint and overlay, the travellers' merged items
(vest + gloves, wings + belt by data component), the wings' animation (`setupModelAnimations`) and the goggles'
`_down` texture (unit tgear / armory).

## Extracts: trophies, worn and held items

Parts of a model drawn through a renderer's `PoseStack` (catalog `extracts`, op chains `opChains`): every
transform (translate, uniform scale, rotations) of the chain is multiplied out; the scale is baked into the
cubes (per-face UV keeps the texture regions), and the rotation and offset go to a wrapper bone (Bedrock euler
angles of the composed rotation) that hangs from the frame:

| Frame | Placement | Used for |
|---|---|---|
| `world` | entity geometry facing south at yaw 0, origin = block centre on the floor | `TrophyRenderer` + `renderTrophy` (floor: `<boss>_trophy`; wall, facing north: `<boss>_wall_trophy`) |
| `head` | a bone parented to the wearer's `head` | trophies on the head (`CustomHeadLayer` + the `template_trophy` "head" display + ISTER + `TrophyRenderer.render(null, 180)`), the mystic crown (the Lich trophy's hat) |
| `rightArm` | a bone parented to the wearer's `rightArm` | the knightmetal shield in the right hand (third person) |

The nine trophies (`BossVariant` NAGA … QUEST_RAM; FINAL_BOSS has no trophy model) use the pose
`setupRotationsForTrophy` leaves at rest (the Ur-Ghast's `waveTentacles(0, 0)`, the hydra's jaw at mouth angle 0
on the floor and 0.5 on the wall). Extra textures become extra geometries: `knight_phantom_trophy_armor`
(phantom armor helmet, drawn at alpha 0.0625), `quest_ram_trophy_lines` (translucent, full bright). Worn and held
attachables render only in their slot (`c.item_slot == 'head'` / `'main_hand'`). U5 places the floor geometry
on its 16 rotations and the wall geometry on its 4 facings.

## Layers

| Layer | Unit | Serves | Output |
|---|---|---|---|
| `adherent` | finalcastle | twilightforest:adherent | `geometry.twilightforest.adherent` (9 bones, 8 cubes) + base animation |
| `alpha_yeti` | caves | twilightforest:alpha_yeti | `geometry.twilightforest.alpha_yeti` (22 bones, 22 cubes) + base animation |
| `alpha_yeti_trophy` | building |  | trophy extracts |
| `arctic_armor_inner` / `_outer` | armory |  | per-slot armor geometry |
| `armored_giant` | giants | twilightforest:armored_giant | vanilla `geometry.humanoid.custom` (TFGiantRenderer draws the player model with the viewer's skin) |
| `bighorn_sheep` | fauna | twilightforest:bighorn_sheep | `geometry.twilightforest.bighorn_sheep` (8 bones, 16 cubes) |
| `blockchain_goblin` | stronghold | twilightforest:blockchain_goblin | `geometry.twilightforest.blockchain_goblin` (8 bones, 7 cubes) + base animation |
| `boar` | fauna | twilightforest:boar | `geometry.twilightforest.boar` (6 bones, 9 cubes) |
| `brazier` | lichdecor | twilightforest:brazier | `geometry.twilightforest.brazier` (7 bones, 7 cubes) |
| `bunny` | fauna | twilightforest:dwarf_rabbit | `geometry.twilightforest.bunny` (6 bones, 9 cubes) + base animation |
| `carminite_broodling` | darktower | twilightforest:carminite_broodling | vanilla `geometry.spider.v1.8` |
| `carminite_ghastguard` | darktower | twilightforest:carminite_ghastguard | `geometry.twilightforest.carminite_ghastguard` (10 bones, 10 cubes) |
| `carminite_ghastling` | darktower | twilightforest:carminite_ghastling | `geometry.twilightforest.carminite_ghastling` (10 bones, 10 cubes) |
| `carminite_golem` | darktower | twilightforest:carminite_golem | `geometry.twilightforest.carminite_golem` (9 bones, 22 cubes) |
| `chain` | weapons | twilightforest:chain_block | `geometry.twilightforest.chain` (1 bone, 1 cube) |
| `chain_block` | weapons | twilightforest:chain_block | `geometry.twilightforest.chain_block` (28 bones, 28 cubes) |
| `cicada` | flora | twilightforest:cicada | `geometry.twilightforest.cicada` (6 bones, 6 cubes) |
| `cube_of_annihilation` | weapons | twilightforest:cube_of_annihilation | `geometry.twilightforest.cube_of_annihilation` (4 bones, 4 cubes) + base animation |
| `death_tome` | lich | twilightforest:death_tome | `geometry.twilightforest.death_tome` (13 bones, 11 cubes) |
| `deer` | fauna | twilightforest:deer | `geometry.twilightforest.deer` (9 bones, 31 cubes) |
| `fiery_armor_inner` / `_outer` | armory |  | per-slot armor geometry |
| `fire_beetle` | hostiles | twilightforest:fire_beetle | `geometry.twilightforest.fire_beetle` (19 bones, 19 cubes) + base animation |
| `firefly` | flora | twilightforest:firefly | `geometry.twilightforest.firefly` (4 bones, 4 cubes) |
| `giant_miner` | giants | twilightforest:giant_miner | vanilla `geometry.humanoid.custom` |
| `harbinger_cube` | finalcastle | twilightforest:harbinger_cube | `geometry.twilightforest.harbinger_cube` (6 bones, 5 cubes) |
| `hedge_spider` | hostiles | twilightforest:hedge_spider | vanilla `geometry.spider.v1.8` |
| `helmet_crab` | stronghold | twilightforest:helmet_crab | `geometry.twilightforest.helmet_crab` (19 bones, 18 cubes) + base animation |
| `hostile_wolf` | hostiles | twilightforest:hostile_wolf, twilightforest:mist_wolf | vanilla `geometry.wolf` |
| `hydra` | hydra | twilightforest:hydra | `geometry.twilightforest.hydra` (25 bones, 50 cubes) + base animation |
| `hydra_head` | hydra | twilightforest:hydra | `geometry.twilightforest.hydra_head` (3 bones, 16 cubes) |
| `hydra_mortar` | hydra | twilightforest:hydra_mortar | `geometry.twilightforest.hydra_mortar` (1 bone, 1 cube) |
| `hydra_neck` | hydra | twilightforest:hydra | `geometry.twilightforest.hydra_neck` (1 bone, 2 cubes) + base animation |
| `hydra_trophy` | building |  | trophy extracts |
| `ice_crystal` | aurora | twilightforest:ice_crystal | `geometry.twilightforest.ice_crystal` (32 bones, 32 cubes) |
| `keepsake_casket` | artifacts | twilightforest:keepsake_casket | `geometry.twilightforest.keepsake_casket` (3 bones, 8 cubes) |
| `king_spider` | hostiles | twilightforest:king_spider | vanilla `geometry.spider.v1.8` |
| `knight_phantom` | stronghold | twilightforest:knight_phantom | `geometry.twilightforest.knight_phantom` (7 bones, 6 cubes) + base animation |
| `knight_phantom_trophy` | building |  | trophy extracts |
| `knightmetal_armor_inner` / `_outer` | armory |  | per-slot armor geometry |
| `knightmetal_shield` | weapons | twilightforest:knightmetal_shield | `geometry.twilightforest.knightmetal_shield` (2 bones, 2 cubes) + held extract |
| `kobold` | fauna | twilightforest:kobold | `geometry.twilightforest.kobold` (11 bones, 10 cubes) + base animation |
| `lich` | lich | twilightforest:lich | `geometry.twilightforest.lich` (9 bones, 9 cubes) |
| `lich_minion` | lich | twilightforest:lich_minion | `geometry.twilightforest.lich_minion` (7 bones, 7 cubes) |
| `lich_trophy` | building |  | trophy extracts, mystic crown |
| `lower_goblin_knight` | stronghold | twilightforest:lower_goblin_knight | `geometry.twilightforest.lower_goblin_knight` (8 bones, 7 cubes) + base animation |
| `loyal_zombie` | artifacts | twilightforest:loyal_zombie | `geometry.twilightforest.loyal_zombie` (7 bones, 7 cubes) + base animation |
| `maze_slime` / `maze_slime_outer` | labyrinth | twilightforest:maze_slime | vanilla `geometry.slime` / `geometry.slime.armor` |
| `minoshroom` | labyrinth | twilightforest:minoshroom | `geometry.twilightforest.minoshroom` (18 bones, 17 cubes) + base animation |
| `minoshroom_trophy` | building |  | trophy extracts |
| `minotaur` | labyrinth | twilightforest:minotaur | `geometry.twilightforest.minotaur` (12 bones, 11 cubes) |
| `mist_wolf` | hostiles | twilightforest:mist_wolf | vanilla `geometry.wolf` (registered, unused: MistWolfRenderer bakes `hostile_wolf`) |
| `moonworm` | flora | twilightforest:moonworm | `geometry.twilightforest.moonworm` (4 bones, 4 cubes) |
| `mosquito_swarm` | fauna | twilightforest:mosquito_swarm | `geometry.twilightforest.mosquito_swarm` (103 bones, 103 cubes; TF's unseeded layout, fixed seed here) |
| `naga` / `naga_body` | naga | twilightforest:naga | `geometry.twilightforest.naga` / `naga_body` (1 bone, 1 cube) |
| `naga_trophy` | building |  | trophy extracts |
| `noop` | aurora |  | not drawn (NoopModel, 0×0 texture: the snow guardian shows only armor) |
| `penguin` | fauna | twilightforest:penguin | `geometry.twilightforest.penguin` (8 bones, 7 cubes) + base animation |
| `phantom_armor_inner` / `_outer` | armory |  | per-slot armor geometry |
| `pinch_beetle` | hostiles | twilightforest:pinch_beetle | `geometry.twilightforest.pinch_beetle` (25 bones, 25 cubes) + base animation |
| `protection_box` | landmark | twilightforest:protection_box | `geometry.twilightforest.protection_box` (1 bone, 1 cube) |
| `quest_ram` | questcamps | twilightforest:quest_ram | `geometry.twilightforest.quest_ram` (28 bones, 43 cubes) + base animation |
| `quest_ram_trophy` | building |  | trophy extracts |
| `raven` | fauna | twilightforest:raven | `geometry.twilightforest.raven` (11 bones, 11 cubes) + base animation |
| `red_thread` | tgear | twilightforest:red_thread | `geometry.twilightforest.red_thread` (5 bones, 5 cubes) |
| `redcap` | hostiles | twilightforest:redcap, twilightforest:redcap_sapper | `geometry.twilightforest.redcap` (7 bones, 11 cubes) + base animation |
| `redcap_inner` / `redcap_outer` | hostiles | twilightforest:redcap | `geometry.twilightforest.redcap_inner` / `redcap_outer` (armor layers, 7 bones) |
| `rising_zombie` | lich | twilightforest:rising_zombie | `geometry.twilightforest.rising_zombie` (7 bones, 7 cubes) |
| `roving_cube` | finalcastle | twilightforest:roving_cube | `geometry.twilightforest.roving_cube` (4 bones, 4 cubes) + base animation |
| `skeleton_druid` | hostiles | twilightforest:skeleton_druid | `geometry.twilightforest.skeleton_druid` (8 bones, 8 cubes) |
| `skull_chest` | lichdecor | twilightforest:skull_chest | `geometry.twilightforest.skull_chest` (2 bones, 5 cubes) |
| `slime_beetle` / `slime_beetle_tail` | hostiles | twilightforest:slime_beetle | `geometry.twilightforest.slime_beetle` / `slime_beetle_tail` (18 bones) + base animations |
| `snow_queen` | aurora | twilightforest:snow_queen | `geometry.twilightforest.snow_queen` (24 bones, 22 cubes) |
| `snow_queen_trophy` | building |  | trophy extracts |
| `squirrel` | fauna | twilightforest:squirrel | `geometry.twilightforest.squirrel` (9 bones, 12 cubes) + base animation |
| `stable_ice_core` | aurora | twilightforest:stable_ice_core | `geometry.twilightforest.stable_ice_core` (33 bones, 33 cubes) |
| `swarm_spider` | hostiles | twilightforest:swarm_spider | vanilla `geometry.spider.v1.8` |
| `tiny_bird` | fauna | twilightforest:tiny_bird | `geometry.twilightforest.tiny_bird` (8 bones, 8 cubes) + base animation |
| `towerwood_borer` | darktower | twilightforest:towerwood_borer | vanilla `geometry.silverfish` |
| `travellers_armor_*` (5) | tgear |  | Travellers' Gear piece geometry |
| `troll` | caves | twilightforest:troll | `geometry.twilightforest.troll` (8 bones, 7 cubes) + base animation |
| `unstable_ice_core` | aurora | twilightforest:unstable_ice_core | `geometry.twilightforest.unstable_ice_core` (33 bones, 33 cubes) |
| `upper_goblin_knight` | stronghold | twilightforest:upper_goblin_knight | `geometry.twilightforest.upper_goblin_knight` (15 bones, 15 cubes) + base animation |
| `ur_ghast` | darktower | twilightforest:ur_ghast | `geometry.twilightforest.ur_ghast` (37 bones, 37 cubes) |
| `ur_ghast_trophy` | building |  | trophy extracts |
| `winter_wolf` | hostiles | twilightforest:winter_wolf | vanilla `geometry.wolf` |
| `wraith` | hostiles | twilightforest:wraith | `geometry.twilightforest.wraith` (8 bones, 8 cubes) + base animation |
| `yeti` | caves | twilightforest:yeti | `geometry.twilightforest.yeti` (12 bones, 12 cubes) + base animation |
| `yeti_armor_inner` / `_outer` | armory |  | per-slot armor geometry |

Totals: 101 layers (every model class of `client/model/entity` but the base / interface classes
`FixedHumanoidModel`, `TrophyBlockModel`, `HostileWolfModel`, `MistWolfModel` (vanilla wolf mesh), the 9 armor
model classes, the block-entity and item models), 124 generated geometries (64 layer geometries, 25 armor and
gear pieces, 18 floor and wall trophies plus 4 overlay geometries, 12 worn extracts (9 trophies, 2 overlays, the
mystic crown) and 1 held extract), 12 vanilla-model layers, 28 base animations, 45 attachables. The Curios charm
of life necklace (`CuriosCompat`) is not ported (docs/spec/README.md).

## Tests

- `pwsh tools/models/test.ps1`: the lexer and parser, the interpreter's Java semantics (integer division,
  float arithmetic, string concatenation, `Mth`'s sine table, `java.util.Random` values for seed 42), the
  conversion against Bedrock's vanilla pig geometry, per-face UV and mirror, unique bone names, euler
  round trips, setupAnim → Molang, the three hard models (Hydra, Naga segment, Snow Queen) against values
  worked out by hand from TF's code, and every generated file (structure, unique bones, parents, UVs inside
  the texture, animation bones, attachable geometries, determinism, `-Check`). F2 owns no `tests/` path, so
  these run in PowerShell rather than gtest.
- Selftests `models.*` (`src/feature/ext/models/ModelsSelfTest.cpp`, rule R18: F2 owns no source directory):
  the deployed index and every generated file present in the deployed pack; a unit partition not regenerated
  yet is reported, F2's own partitions must be complete.
- e2e (`pwsh -NoProfile -File tools/models/e2e/run.ps1 server` / `client -Mode local`): the scenario files live
  in `tools/models/e2e/{server,client}/` (F2 owns no `tools/e2e/**` path) and are copied into the harness for
  one run. The prelaunch step puts every partition into the deployed mod (as after W1) and, on the client,
  builds a test-only preview pack: one actor per geometry with its TF texture and base animation, armor stands
  with every attachable on test items, 31 screenshots of four models each. Every content-log error fails the run.
