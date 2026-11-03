# Models: Java model converter and geometry import (F2)

Contract **geo** (owner F2): `tools/models/convert.ps1` turns The Twilight Forest 4.8's Java entity models
(the `LayerDefinition` factories, LGPL code) into Bedrock geometry, bone maps, base animations and attachables;
`tools/models/attachable.ps1` writes the attachable of one item. W1 units only add animations and render
controllers on top of this output.

## Workflow

```powershell
pwsh tools/models/convert.ps1                        # every partition
pwsh tools/models/convert.ps1 -Unit hydra            # one unit's partition (a W1 unit after a catalog change)
pwsh tools/models/convert.ps1 -Java HydraModel -Unit hydra  # one model class's files in one partition
pwsh tools/models/attachable.ps1 -Item twilightforest:fiery_helmet
pwsh tools/models/convert.ps1 -Check                 # CI / review: committed files match the converter
pwsh tools/models/convert.ps1 -Out build/geo         # outside the repository
pwsh tools/models/test.ps1                           # the converter's tests (see Tests)
```

- The TF source comes from the shared checkout (`tools/tf/fetch.ps1`, rule R24); the converter runs it and
  never reads `.ogg` / `.nbt` files.
- **Partitions.** `assets/gen/geo/<unit>/**` holds the files of the ids `<unit>` owns (owners.tsv rows of mode
  `gen`). Only the tool writes them (rule R3): F2, the tool unit of the geo layer, commits every partition, and a
  W1 unit regenerates its own with `-Unit <unit>`. Nobody edits these files by hand: a manual file with the same
  id under `assets/pack/rp/**` overrides a generated one, and the converter's input is
  `tools/models/catalog.json`. `models` holds the index and the shared render controllers.
- Scope: every partition by default (stale files and partitions are removed); `-Unit` one partition; `-Java` /
  `-Item` only the files written for that model class (layer supplier or `animClass`) or item, taken from the same
  full run (`Generator.Select`); with `-Unit` too, only those files of that partition. `attachable.ps1 -Item`
  writes the item's attachable with its geometries, bone maps and animations (`-Unit` keeps to one partition).
  `-Check` regenerates in memory and fails when a file of the scope differs, is missing or is stale.
- The output is deterministic (fixed number formatting, catalog order, a fixed seed for TF's unseeded
  `RandomSource.create()`).

## Output

Per unit `<u>` (the owner of the entity / block / item the layer serves, from `tools/models/catalog.json`):

| File | Content |
|---|---|
| `assets/gen/geo/<u>/rp/models/entity/<name>.geo.json` | `geometry.twilightforest.<name>`, format 1.12.0. `<name>` is the TF model layer (`TFModelLayers`, `<path>` or `<path>_<layer>` when the layer is not `main`), an armor piece (`<material>.<slot>`) or an extract. |
| `assets/gen/geo/<u>/rp/animations/<name>.base.json` | `animation.twilightforest.<name>.base` (format 1.8.0): one per entity layer, with the setupAnim statements translated to Molang (a stub with a zero rotation on the first bone when none translates: the owning unit adds the rest by hand); also the Ur-Ghast trophy extracts' tentacle wave. |
| `assets/gen/geo/<u>/bones/<name>.json` | The bone map: Java part path -> bone, parent, Bedrock pivot / rotation, Java offset / rotation, cube count, mirror; the layer and its supplier; the textures (Bedrock path and TF source); the setupAnim report (`translated`: what the base animation does; `todo`: line (`end_line` for a branch), `class` when another class's source holds it, Java source, reason, and for a case entry `affects`, the channels that differ when it holds; `overrides_vanilla`: bone axes Java sets with `=` after the vanilla `super.setupAnim`); notes. Not part of the pack. |
| `assets/gen/geo/<u>/rp/attachables/<item>.json` | Attachables of armor and Travellers' Gear; of the trophies and the mystic crown (worn on the head, held in the right and left hand) and of the knightmetal shield (right and left hand): one geometry per slot, drawn only in that slot. |
| `assets/gen/geo/models/rp/render_controllers/twilightforest.models.render_controllers.json` | `controller.render.twilightforest.models.part<N>`: the extra geometries of multi-part attachables (`Geometry.part1`, ...), with the enchantment glint (`variable.is_enchanted`, `Material.enchanted`, `Texture.enchanted`) like `controller.render.item_default`. |
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

Names: Java part names stay, except the vanilla skeleton names at the root of a model of that vanilla family
(the layer's model class, catalog `animClass` or the supplier's class): `right_arm` → `rightArm`, ... in a
`HumanoidModel` (and its vanilla subclasses `ZombieModel`, `SkeletonModel`, ...), `right_hind_leg` → `leg0`,
`left_hind_leg` → `leg1`, `right_front_leg` → `leg2`, `left_front_leg` → `leg3` in a `QuadrupedModel` (`PigModel`,
`SheepModel`, ...), so the vanilla humanoid / quadruped animations apply. Other models keep TF's names, which need
not follow the vanilla sides: the hydra's (a `HierarchicalModel`) `left_leg` sits at −x where Bedrock's `leftLeg`
is at +x, and the Minoshroom (a `HumanoidModel`: `rightArm` ...) keeps `left_front_leg` / `right_front_leg`
(at −x / +x, mirrored against `leg3` / `leg2`), as do the quest ram and the carminite golem. A name used twice (Snow Queen's `spike_4` under each crown
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
(facing, a random spin), `brazier`, `red_thread` (per face), `skull_chest` / `keepsake_casket` (facing), the
`knightmetal_shield` item (ISTER `scale(1, -1, -1)`; see the held extracts), `naga` (NagaRenderer.scale: after
the living-entity transforms `scale(2.01)` and `translate(0, 0.75, 0)`, dazed `translate(0, 1.075, 0.175)`; the
body does not keel over while alive) and `naga_body` (NagaSegmentRenderer, a TFPartRenderer: the segment's
interpolated yaw and pitch, `scale(2)`, `translate(0, -1.25, 0)`, then TFPartRenderer's `scale(-1, -1, 1)`,
`translate(0, -1.501, 0)`; no body yaw). In the e2e preview these models float or sit upside down, as expected
(the naga head and segment float well above the ground). `lich_minion`, `loyal_zombie` and `rising_zombie` declare a 64×32 texture
because Bedrock's vanilla zombie texture is 64×32; TF draws Java's 64×64 one with the same UVs.

## setupAnim → Molang

Per entity layer, `prepareMobModel` then `setupAnim` of the model class (catalog `animClass`, default the
supplier's class) are run statement by statement, together with the TF methods they call (a TF
`super.setupAnim`, `this.helper(...)`, inlined with their own lines in the report). Assignments to a part's
`xRot` / `yRot` / `zRot` / `x` / `y` / `z` (also `setPos`, `setRotation`, `copyFrom`, `resetPose`,
`getAllParts().forEach(ModelPart::resetPose)` and the vanilla `AnimationUtils.bobArms` / `bobModelPart`) whose
value uses only the parameters, constants, earlier locals, earlier values and `Mth` / `Math` functions
(`triangleWave`, `frac`, `square` included) become Molang:

| Java | Molang |
|---|---|
| `limbSwing`, `limbSwingAmount` | `query.modified_distance_moved`, `query.modified_move_speed` |
| `ageInTicks`, `entity.tickCount (+ partialTick)` | `query.life_time * 20` |
| `netHeadYaw`, `headPitch` | `query.target_y_rotation`, `query.target_x_rotation` |
| `Mth.cos(x)` / `Mth.sin(x)` (radians) | `math.cos(x * 57.2958)` / `math.sin(...)` (degrees) |
| `part.xRot = e` (absolute radians) | `rotation[0] = e * 57.2958 − default` (Molang adds to the geometry) |
| `part.y = e` (pixels, y down) | `position[1] = −(e − default)` |
| `this.attackTime`, `this.young`, `this.riding` | `variable.attack_time`, `query.is_baby`, `query.is_riding` |

**The base path.** Every value (a local or a bone property) is followed with the statements it carries and
the cases it depends on:

- A condition that translates (`this.young`, `this.riding`, `limbSwingAmount > 0.2`, `this.attackTime > 0`)
  runs both branches and merges what they set with Molang `?:`.
- A condition that does not translate (entity state: `entity.isTired()`) counts as **false**; for `a != b` the
  `a == b` counts as false (so the `!=` holds), and for `!x` the `x` counts as false. The base animation runs the
  branch that follows and is correct for that case. The condition becomes a **case entry** in `todo` ("the base
  animation counts X as false, so it runs ... and leaves out lines a-b") whose `affects` lists every channel that
  differs when X holds: what either branch changes, and every later value computed from it. A boolean local of
  entity state (`boolean flag = e.isAggressive()`) and a ternary on one work the same way (LoyalZombieModel's
  arms: `-PI / 2.25`, `-80` degrees, the not aggressive case).
- **Base-case values** (catalog `baseCase`). Where "false" is not the state TF's mobs are in, the catalog gives
  the value: `{scope, expr, value, fixed?, why}` with `scope` a class of the model's chain (`HumanoidModel` /
  `QuadrupedModel` cover their vanilla families, `*` every model), `expr` the Java expression (`entity` for the
  entity parameter, `this.` for the model's fields and methods, `(*)` for any arguments), `value` a number,
  boolean, enum constant (`HumanoidArm.RIGHT`) or `null`. Enum constants and `null` only take part in `==` / `!=`
  (folded against a base-case value of the same type), never in Molang. A **fixed** value is one TF never
  changes: `this.leftArmPose` / `this.rightArmPose` are `ArmPose.EMPTY` (TF's humanoid mobs never get an arm
  pose: no TF model or renderer sets one, `HumanoidMobRenderer` and `HumanoidModel.prepareMobModel` of 1.21.1
  do not either) and `isTwoHanded()` of them false, `JappaPackReloadListener.isJappaPackLoaded()` false; a
  comparison with it folds to a constant, leaves no case entry, and the bone map notes the value and the
  lines. So TrollModel's `if (this.leftArmPose != ArmPose.EMPTY) rightArm.xRot += PI` never runs (the troll's
  arms swing at its sides) and UpperGoblinKnightModel's `leftArm.xRot * 0.5 - PI / 10` neither; the vanilla
  `poseRightArm` / `poseLeftArm` set `yRot = 0`. Any other value is the common state and its statements are
  case entries ("counts entity.hasShield() as true (the UpperGoblinKnight constructor ...)"): the main and
  attack arm `HumanoidArm.RIGHT` (95% of mobs; `getArm(arm)` follows it), the troll's target `null`, the lower
  goblin knight ridden and armored, the upper one with its shield, the birds landed, the Minoshroom's charge
  scale 0. Without an entry, `getArm(arm)` is the right arm and `poseRightArm` / `poseLeftArm` assume EMPTY, as
  cases. The `models` test fails on an entry no model uses.
- A branch that **returns early** is never the base path's (whatever the condition counted as false): `if
  (!entity.isAlive()) return;` runs on, as "counts !entity.isAlive() as false (the if branch returns early)";
  the case's `affects` also lists everything the rest of the method sets. A branch that may return in a nested
  condition is a case the same way.
- An **assignment inside an expression** (a condition, an initializer, a value, an argument: `if ((head.yRot =
  ageInTicks) > 2)`, `x = y++`) that always runs and whose target the expression does not read elsewhere runs
  first, like a statement, and the expression reads its result; one behind `&&` / `||` / `?:` or in a lambda
  makes what it changes unknown. `a = b = v` assigns both. A call inside an expression that changes parts (a
  TF method, a part method) makes what it changes unknown.
- A value that does not translate (`float f2 = e.getArmAngle()`, a field, a call) makes the property
  **unknown** from that line on: a later compound assignment (`+=` ...) or read of it stays in `todo` ("previous
  value at line N not translated") and gets no channel, and the statements whose effect it replaced leave
  `translated` ("its effect is not in the base animation: ..."). A loop, a switch (`for (a = ...; ...; b += ...)`
  included), a call on a part or one the translator does not know makes everything it may change unknown the
  same way (a part it cannot resolve counts as one of its candidates: an array element or an unresolved
  `this.<field>` any part no field names, `getArm(...)` either arm, an unknown local or lambda parameter any
  part).
- After a vanilla `super.setupAnim` (HumanoidModel and its subclasses, QuadrupedModel and its subclasses) the
  vanilla parts hold "the vanilla pose plus a delta": `+=` / `-=` add to the delta (emitted on top of the vanilla
  Bedrock animations), `=` replaces the vanilla pose and is listed in `overrides_vanilla` (bone, property, lines:
  the vanilla animations must not drive those bone axes, in the cases those lines belong to), and `*=` or a
  read of the vanilla value stays untranslated, as does a copy of it onto a bone no vanilla animation drives.
  Where one branch of a translated condition sets an axis and the other keeps the vanilla pose (which a Molang
  `?:` cannot pick), the base animation takes the branch that sets it and the condition becomes a case for the
  other: LichModel's `if (entity.tickCount > 0 && ...)` holds on every frame after the first, so its raised left
  arm keeps `leftArm.zRot = 0.5 - (cos(age * 0.26) * 0.15 + 0.05)` (in `overrides_vanilla`).
  Arguments the model changed before the call (LichModel: `limbSwingAmount * 0.75`) are named in the call's
  todo entry, since the vanilla Bedrock animations read the unchanged queries. A model whose setupAnim does not call `super.setupAnim` gets a
  note that no vanilla animation may play on these bones; one without its own setupAnim gets the vanilla
  animations of its base class.

`translated` therefore lists exactly what the base animation reproduces, and `todo` everything the owning unit
has to add (the cases, the unknown values, visibility, the vanilla parts). Divisions by a constant zero stay
untranslated. `copyFrom` copies every property of the source, its initial pose where nothing set it (Java
copies the whole pose). The re-parented `hat` gets only its difference to the head, on the rotation and the
position channels (nothing when Java copies the head's pose; a hat with no cube or only empty ones is left out).
The position channel moves along the geometry's own axes: x as Java's x (the
geometry keeps Java's x), y negated, z as is. The e2e preview checks the x sign in game: its first actor
(`gfpreview:xsign`, shot_01) is a test model whose setupAnim sets `moved.x = 10`, converted by this translator;
the blue cube lands on the red one at x 8..12 (it would sit on the other side of the white origin cube if
Bedrock mirrored the channel). Checked on the 1.26.51 client: blue sits on red.

The quadruped walk comes out as Bedrock's own `animation.quadruped.walk` (`math.cos(query.modified_distance_moved
* 38.1704) * query.modified_move_speed * 80.2141`) and the head look as `look_at_target`. Everything else
(entity state, loops, conditions, visibility, other models' parts) is listed in the bone map's `setupAnim.todo`
with its line, Java source and reason; models without their own setupAnim name the vanilla animations of their
base class. Every converted entity layer (56) gets `animation.twilightforest.<layer>.base`: 35 with translated
statements, 21 stubs (no translatable statement, or `noAnim`: posed by its renderer) that hold a zero rotation on
the first bone, because the client rejects an animation with an empty `bones` object (`Required child ... not
found`, found by the content-log check); the owning units
translate the rest by hand on top. Vanilla-model layers (spiders, wolves, slimes, silverfish, the giants' player
model) use the vanilla Bedrock animations of that geometry.

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
| `world` | world space of the block (origin = block centre on the floor, x east, z south) written as entity geometry: on an actor at yaw 0 the world axes coincide, so TF's rotation 0 trophy, which faces north, shows its face to the actor's back | `TrophyRenderer` + `renderTrophy` (floor: `<boss>_trophy`; wall, facing north: `<boss>_wall_trophy`) |
| `head` | a bone parented to the wearer's `head` | `<item>_worn`: trophies and the mystic crown on the head (`CustomHeadLayer.translateToHead` + the item's `head` display + ISTER) |
| `rightArm` | a bone parented to the wearer's `rightArm` | `<item>_held`: trophies, the mystic crown and the knightmetal shield in the right hand (`ItemInHandLayer` + `thirdperson_righthand`) |
| `leftArm` | a bone parented to the wearer's `leftArm` | `<item>_held_left`: the same in the left hand (`ItemInHandLayer` + `thirdperson_lefthand`, `leftHand`) |

**Item forms.** An extract with `item` and `forms` is written once per form of the catalog's `itemForms`
(`trophy`: worn, held, held_left; `builtin_item`: the same without `TrophyRenderer`; `held_item`: held, held_left)
and gives the item one attachable with the geometries of every form, each drawn only in its slot
(`c.item_slot == 'head'` / `'main_hand'` / `'off_hand'`; `controller.render.item_default` for the first,
`controller.render.twilightforest.models.part<N>` for the others, all with the enchantment glint).

**Display transforms come from TF's item models**, not from the catalog: the op `["display", <context>]` reads
`models/item/<item>.json` from the TF checkout (`src/generated/resources` first, then `src/main/resources`) and
follows its `twilightforest:item/...` parents the way `BlockModel.getTransforms()` does (a model's own entry for
the context wins, else the parent's; a model without `thirdperson_lefthand` / `firstperson_lefthand` uses its own
`*_righthand`, as `ItemTransforms.Deserializer` does), then applies `ItemTransform.apply(leftHand)`: translation
/ 16 clamped to ±5 blocks, x mirrored for the left hand; `rotationXYZ` with y and z negated for the left hand;
scale clamped to ±4. So the Alpha Yeti trophy is worn with its own `head` display (translation 0 2 0, scale 1.5),
the Ur-Ghast trophy with translation 0 18 0 and held with its `thirdperson_righthand` (rotation 45 135 0,
translation 0 4 3.6, scale 0.5), the other trophies and the mystic crown with `template_trophy`'s (head 0 6 0
×1.6; thirdperson_righthand 45 135 0, 0 3 0, ×0.5) and the shield with `knightmetal_shield.json`'s
(thirdperson_righthand 0 90 0, 10.51 6 -4; thirdperson_lefthand 0 90 0, 10.51 6 12). Each bone map lists the
transform it used (`item_display`: model, file, context, the entry it came from, values).

The nine trophies (`BossVariant` NAGA … QUEST_RAM; FINAL_BOSS has no trophy model) use the pose
`setupRotationsForTrophy` leaves (the hydra's jaw at mouth angle 0 on the floor and in item form, 0.5 on the
wall; `renderTrophy`'s item form where TF has one: the hydra's 0.9 scale and z −1, the quest ram's z 0.5, no y
offset for the Ur-Ghast). Extra textures become extra geometries: `knight_phantom_trophy*_armor` (phantom armor
helmet, drawn at alpha 0.0625), `quest_ram_trophy*_lines` (translucent, full bright). U5 places the floor
geometry on its 16 rotations and the wall geometry on its 4 facings.

**The Ur-Ghast trophies animate.** `TrophyBlockEntity` counts `animatedTicks` for the Ur-Ghast trophies (and only
them), the ISTER passes `ClientGameEvents.time`; `setupRotationsForTrophy(ticks × 4.5)` calls
`waveTentacles(x, x × 0.2)`, so the tentacles wave with `ageInTicks = 0.9 × ticks = query.life_time × 18`. The
catalog's pose expressions use `ageInTicks` (0 in the geometry) and the extract's `animation`; the five Ur-Ghast
extracts get `animation.twilightforest.<extract>.base` (36 tentacle bones: the difference to the pose at tick 0),
which the attachable plays in the slot of its geometry and the preview actors play on the floor and wall ones.

Not covered (the owning units add animations on top, as the record says):

- First person. Bedrock draws an attachable in first person from the first-person arm bones
  (`animation.player.first_person.*`), so the third-person geometry shows there. TF's first-person displays are
  `firstperson_righthand` rotation 0 180 0, translation 0 2 0, scale 1 (`template_trophy`; Ur-Ghast 0 11 0) and
  for the shield 0 180 5, −10 2 −10, ×1.25 (left: 10 0 −10): an animation on `c.is_first_person` (units building,
  artifacts, weapons).
- The knightmetal shield's blocking pose (`models/item/knightmetal_shield_blocking.json`, the `blocking`
  override: thirdperson_righthand 45 135 0, 3.51 11 −2; thirdperson_lefthand 45 135 0, 13.51 3 5;
  firstperson_righthand 0 180 −5, −15 5 −11; firstperson_lefthand 5 5 −11): an animation on `query.blocking`
  (unit weapons).
- An item with `minecraft:wearable` is drawn in the hands as its icon, not through its attachable (seen on the
  1.26.51 client: the preview's wearable test trophies show their icon in the hands; non-wearable copies show the
  held geometry in both hands). The attachables are complete for every slot; how the trophies (unit building) and
  the mystic crown (unit artifacts) go on the head without `minecraft:wearable` (or whether they accept the icon in
  the hands) is the owning unit's item definition.
- Slim arms: `PlayerModel.translateToHand` moves a slim arm 0.5 px toward the body before the item.
- `gui`, `ground` and `fixed` displays (inventory icons, dropped items, item frames) are not attachables.

## Item models

TF has three item models. `KnightmetalShieldModel` (layer `knightmetal_shield`) has `LayerDefinition` geometry and
is converted (`geometry.twilightforest.knightmetal_shield`, the held extracts above). The other two, in
`client/model/item`, are `BakedModel` / `IUnbakedGeometry` wrappers around flat item sprites and produce no
geometry, so there is nothing to convert; their owners reproduce the behaviour with item textures:

- `TrollsteinnModel` (unit caves, the `twilightforest:trollsteinn` block item): shows
  `models/item/trollsteinn_light` (cube_all `block/trollsteinn_light`) while the raw brightness at the holder (or
  the item entity) is above `TrollsteinnBlock.LIGHT_THRESHOLD` (7, with `calculateServerSkyDarken` on the client),
  else the normal `trollsteinn` model.
- `TravellersGearItemModel` (unit tgear, loader `twilightforest:travellers_gear` of the Travellers' Gear items):
  the item's `base` sprite (`broken` when the gear is broken), one more sprite layer per inserted Travellers'
  modifier (`item/<modifier_directory><modifier>`, the broken directory when broken; each layer scaled
  1 + 0.001 × n so it sits in front of the previous one), and the gloves sprite when the item has
  `TRAVELLERS_HAS_GLOVES`; drawn flat (not GUI-3D, no block light).
## Layers

Every converted entity layer has a base animation; `+ base animation` marks the 35 with translated statements (`slime_beetle` / `slime_beetle_tail` share a row).

| Layer | Unit | Serves | Output |
|---|---|---|---|
| `adherent` | finalcastle | twilightforest:adherent | `geometry.twilightforest.adherent` (9 bones, 8 cubes) + base animation |
| `alpha_yeti` | caves | twilightforest:alpha_yeti | `geometry.twilightforest.alpha_yeti` (22 bones, 22 cubes) + base animation |
| `alpha_yeti_trophy` | building |  | trophy extracts (floor, wall, worn, held) |
| `arctic_armor_inner` / `_outer` | armory |  | per-slot armor geometry |
| `armored_giant` | giants | twilightforest:armored_giant | vanilla `geometry.humanoid.custom` (TFGiantRenderer draws the player model with the viewer's skin) |
| `bighorn_sheep` | fauna | twilightforest:bighorn_sheep | `geometry.twilightforest.bighorn_sheep` (8 bones, 16 cubes) |
| `blockchain_goblin` | stronghold | twilightforest:blockchain_goblin | `geometry.twilightforest.blockchain_goblin` (8 bones, 7 cubes) + base animation |
| `boar` | fauna | twilightforest:boar | `geometry.twilightforest.boar` (6 bones, 9 cubes) |
| `brazier` | lichdecor | twilightforest:brazier | `geometry.twilightforest.brazier` (7 bones, 7 cubes) |
| `bunny` | fauna | twilightforest:dwarf_rabbit | `geometry.twilightforest.bunny` (6 bones, 9 cubes) + base animation |
| `carminite_broodling` | darktower | twilightforest:carminite_broodling | vanilla `geometry.spider.v1.8` |
| `carminite_ghastguard` | darktower | twilightforest:carminite_ghastguard | `geometry.twilightforest.carminite_ghastguard` (10 bones, 10 cubes) + base animation |
| `carminite_ghastling` | darktower | twilightforest:carminite_ghastling | `geometry.twilightforest.carminite_ghastling` (10 bones, 10 cubes) + base animation |
| `carminite_golem` | darktower | twilightforest:carminite_golem | `geometry.twilightforest.carminite_golem` (9 bones, 22 cubes) + base animation |
| `chain` | weapons | twilightforest:chain_block | `geometry.twilightforest.chain` (1 bone, 1 cube) |
| `chain_block` | weapons | twilightforest:chain_block | `geometry.twilightforest.chain_block` (28 bones, 28 cubes) |
| `cicada` | flora | twilightforest:cicada | `geometry.twilightforest.cicada` (6 bones, 6 cubes) |
| `cube_of_annihilation` | weapons | twilightforest:cube_of_annihilation | `geometry.twilightforest.cube_of_annihilation` (4 bones, 4 cubes) + base animation |
| `death_tome` | lich | twilightforest:death_tome | `geometry.twilightforest.death_tome` (13 bones, 11 cubes) + base animation |
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
| `hydra_trophy` | building |  | trophy extracts (floor, wall, worn, held) |
| `ice_crystal` | aurora | twilightforest:ice_crystal | `geometry.twilightforest.ice_crystal` (32 bones, 32 cubes) |
| `keepsake_casket` | artifacts | twilightforest:keepsake_casket | `geometry.twilightforest.keepsake_casket` (3 bones, 8 cubes) |
| `king_spider` | hostiles | twilightforest:king_spider | vanilla `geometry.spider.v1.8` |
| `knight_phantom` | stronghold | twilightforest:knight_phantom | `geometry.twilightforest.knight_phantom` (7 bones, 6 cubes) + base animation |
| `knight_phantom_trophy` | building |  | trophy extracts (floor, wall, worn, held) |
| `knightmetal_armor_inner` / `_outer` | armory |  | per-slot armor geometry |
| `knightmetal_shield` | weapons | twilightforest:knightmetal_shield | `geometry.twilightforest.knightmetal_shield` (2 bones, 2 cubes) + held extracts (right and left hand) |
| `kobold` | fauna | twilightforest:kobold | `geometry.twilightforest.kobold` (11 bones, 10 cubes) + base animation |
| `lich` | lich | twilightforest:lich | `geometry.twilightforest.lich` (9 bones, 9 cubes) + base animation |
| `lich_minion` | lich | twilightforest:lich_minion | `geometry.twilightforest.lich_minion` (7 bones, 7 cubes) |
| `lich_trophy` | building |  | trophy extracts (floor, wall, worn, held), mystic crown (worn, held; unit artifacts) |
| `lower_goblin_knight` | stronghold | twilightforest:lower_goblin_knight | `geometry.twilightforest.lower_goblin_knight` (8 bones, 7 cubes) + base animation |
| `loyal_zombie` | artifacts | twilightforest:loyal_zombie | `geometry.twilightforest.loyal_zombie` (7 bones, 7 cubes) + base animation |
| `maze_slime` / `maze_slime_outer` | labyrinth | twilightforest:maze_slime | vanilla `geometry.slime` / `geometry.slime.armor` |
| `minoshroom` | labyrinth | twilightforest:minoshroom | `geometry.twilightforest.minoshroom` (18 bones, 17 cubes) + base animation |
| `minoshroom_trophy` | building |  | trophy extracts (floor, wall, worn, held) |
| `minotaur` | labyrinth | twilightforest:minotaur | `geometry.twilightforest.minotaur` (12 bones, 11 cubes) |
| `mist_wolf` | hostiles | twilightforest:mist_wolf | vanilla `geometry.wolf` (registered, unused: MistWolfRenderer bakes `hostile_wolf`) |
| `moonworm` | flora | twilightforest:moonworm | `geometry.twilightforest.moonworm` (4 bones, 4 cubes) |
| `mosquito_swarm` | fauna | twilightforest:mosquito_swarm | `geometry.twilightforest.mosquito_swarm` (103 bones, 103 cubes; TF's unseeded layout, fixed seed here) + base animation |
| `naga` / `naga_body` | naga | twilightforest:naga | `geometry.twilightforest.naga` / `naga_body` (1 bone, 1 cube) |
| `naga_trophy` | building |  | trophy extracts (floor, wall, worn, held) |
| `noop` | aurora |  | not drawn (NoopModel, 0×0 texture: the snow guardian shows only armor) |
| `penguin` | fauna | twilightforest:penguin | `geometry.twilightforest.penguin` (8 bones, 7 cubes) + base animation |
| `phantom_armor_inner` / `_outer` | armory |  | per-slot armor geometry |
| `pinch_beetle` | hostiles | twilightforest:pinch_beetle | `geometry.twilightforest.pinch_beetle` (25 bones, 25 cubes) + base animation |
| `protection_box` | landmark | twilightforest:protection_box | `geometry.twilightforest.protection_box` (1 bone, 1 cube) |
| `quest_ram` | questcamps | twilightforest:quest_ram | `geometry.twilightforest.quest_ram` (28 bones, 43 cubes) + base animation |
| `quest_ram_trophy` | building |  | trophy extracts (floor, wall, worn, held) |
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
| `snow_queen_trophy` | building |  | trophy extracts (floor, wall, worn, held) |
| `squirrel` | fauna | twilightforest:squirrel | `geometry.twilightforest.squirrel` (9 bones, 12 cubes) + base animation |
| `stable_ice_core` | aurora | twilightforest:stable_ice_core | `geometry.twilightforest.stable_ice_core` (33 bones, 33 cubes) |
| `swarm_spider` | hostiles | twilightforest:swarm_spider | vanilla `geometry.spider.v1.8` |
| `tiny_bird` | fauna | twilightforest:tiny_bird | `geometry.twilightforest.tiny_bird` (8 bones, 8 cubes) + base animation |
| `towerwood_borer` | darktower | twilightforest:towerwood_borer | vanilla `geometry.silverfish` |
| `travellers_armor_*` (5) | tgear |  | Travellers' Gear piece geometry |
| `troll` | caves | twilightforest:troll | `geometry.twilightforest.troll` (8 bones, 7 cubes) + base animation |
| `unstable_ice_core` | aurora | twilightforest:unstable_ice_core | `geometry.twilightforest.unstable_ice_core` (33 bones, 33 cubes) |
| `upper_goblin_knight` | stronghold | twilightforest:upper_goblin_knight | `geometry.twilightforest.upper_goblin_knight` (15 bones, 15 cubes) + base animation |
| `ur_ghast` | darktower | twilightforest:ur_ghast | `geometry.twilightforest.ur_ghast` (37 bones, 37 cubes) + base animation |
| `ur_ghast_trophy` | building |  | trophy extracts (floor, wall, worn, held) + tentacle animations |
| `winter_wolf` | hostiles | twilightforest:winter_wolf | vanilla `geometry.wolf` |
| `wraith` | hostiles | twilightforest:wraith | `geometry.twilightforest.wraith` (8 bones, 8 cubes) + base animation |
| `yeti` | caves | twilightforest:yeti | `geometry.twilightforest.yeti` (12 bones, 12 cubes) + base animation |
| `yeti_armor_inner` / `_outer` | armory |  | per-slot armor geometry |

Totals: 101 layers (every model class of `client/model/entity` but the base / interface classes
`FixedHumanoidModel`, `TrophyBlockModel`, `HostileWolfModel`, `MistWolfModel` (vanilla wolf mesh), the 9 armor
model classes, the block-entity and item models), 149 generated geometries (64 layer geometries, 25 armor and
gear pieces, 18 floor and wall trophies plus 4 overlay geometries, 12 worn extracts (9 trophies, 2 overlays, the
mystic crown) and 26 held extracts (the same 12 plus the knightmetal shield, in each hand)), 12 vanilla-model
layers, 61 base animations (56 entity layers, 35 of them with translated statements, and 5 Ur-Ghast trophy
extracts), 45 attachables. The Curios charm of life necklace (`CuriosCompat`) is not ported (docs/spec/README.md).

## Tests

- `pwsh tools/models/test.ps1`: the lexer and parser, the interpreter's Java semantics (integer division,
  float arithmetic, string concatenation, `Mth`'s sine table, `java.util.Random` values for seed 42), the
  conversion against Bedrock's vanilla pig geometry, per-face UV and mirror, unique bone names, euler
  round trips, the vanilla skeleton names only in their family (Minoshroom, quest ram, golem, hydra keep TF's),
  setupAnim → Molang (the position channel signs; the base path: unknown values and the compound
  assignments after them, statements that leave `translated`, cases and their `affects`, loops and nested
  assignments, assignments inside conditions, early returns, base-case values (the TrollModel pattern) and
  every catalog entry in use, vanilla deltas and `overrides_vanilla`, a branch that keeps the vanilla pose (the
  LichModel pattern), `copyFrom` of an unset source, the hat's position; the real Loyal Zombie, Alpha Yeti,
  Yeti, Troll, goblin knight and Lich outputs), the catalog's LGPL attribution, `-Java` matching whole class
  names (YetiModel is not AlphaYetiModel), the three hard models (Hydra, Naga
  segment, Snow Queen) against values worked out by hand from TF's code, the item display transforms (the Alpha
  Yeti's and Ur-Ghast's own head displays, the left-hand fallback and mirror), the per-slot attachables, the
  Ur-Ghast tentacle animation, a base animation per entity layer, `-Java` / `-Item` selection and `-Unit`
  intersection, and every generated file (structure, unique bones, parents, UVs inside the texture, animation
  bones, attachable geometries, determinism, `-Check`). F2 owns no `tests/` path and the converter is C# run by
  PowerShell, so these are not gtests: the server scenario `models` runs them first and fails on any failure
  (CI does not run e2e; wiring `test.ps1` into `.github/` is F0's).
- Selftests `models.*` (`src/feature/ext/models/ModelsSelfTest.cpp`, rule R18: F2 owns no source directory):
  the deployed index, and every geometry, base animation and attachable it lists is in the deployed pack and
  well-formed (missing, unparsable or inconsistent files fail a check; every JSON access is checked). The
  client build adds `models.client_hud` (`src/feature/ext/models/ModelsClientHud.cpp`), an info-only probe
  for the client scenario: the game's hide-GUI options as the client holds them (`hud hidden|shown, hand
  hidden|shown`, read on the client thread).
- e2e (`tools/e2e/{server,client}/scenarios/models*.ps1`):
  - server `models`: `tools/models/test.ps1`, then the selftests. BDS loads behavior packs only, so it cannot
    see resource-pack errors.
  - client `models` (BeDD, local world): the prelaunch step builds a test-only preview pack
    (`tools/models/e2e/preview.ps1`): one actor per geometry with its TF texture and base animation, the x-sign
    actor, armor stands (facing the camera) with every attachable on test items in every slot it is drawn in
    (head, main hand, off hand), and two broken canary files; it also sets the instance's options for the run:
    the game's content log file on (`options.txt content_log_file:1`) and the HUD and hand shown
    (`gfx_hidehud:0`, `gfx_hidehand:0`, whatever an earlier run left). The instance's own values are saved in the
    e2e home (`models-options.<instance>.json`, outside `out/`) and put back by `models.restore.ps1` however the
    run ends (a backup that a killed run left behind is kept by the next models run and restored at its end; a
    hidden HUD or hand found before the run is restored as shown, since it can only be a lost F1's leftover).
    The scenario runs the selftests, turns off command feedback, hides the HUD with F1, takes 31 screenshots of
    four models each, shows the HUD again with F1 (`hud_back.png`, in creative mode: the hotbar), then reads the
    client's content log (`<game data>/logs/ContentLog*.txt`): the canary geometry must be reported (the log is
    captured), and any other error or warning fails the run (`out/models/content-log.txt`). Every F1 press is
    checked with `models.client_hud` and repeated when the game missed it (at most 3 presses each way; then the
    run fails). Commands naming the preview items run after each shot's function (functions are parsed before
    data-driven items exist).
  - server `models.preview`: the preview behavior pack on BDS (actors, items, functions) loads without a
    content-log error and every function and command runs.
