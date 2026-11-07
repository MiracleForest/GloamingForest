# U11 `tgear`: Travellers' Gear (equipment, goggles, red thread)

Behaviour spec: [docs/spec/tgear.md](../spec/tgear.md). This page lists how the unit is built on Bedrock,
its vanilla fallbacks and its deviations from TF 4.8 (for A8's deviation list).

## Layout

| Path | What |
|---|---|
| `src/core/tgear/Logic.{h,cpp}` (+ `tests/tgear`) | Item display rules (ItemDisplayContents.Mutable), hotbar swap plan, auto repair chance, clock / moon dial texts, `${tfkeybinds/...}` interpolation |
| `src/feature/tgear/Gear.{h,cpp}` | Modifier registry (data `twilight/travellers_modifiers`), slots, broken state, `isModifierActive` for any wearer, stack data: `gf:tgear.container`, `gf:tgear.display`, `gf:tgear.gloves` (modifier ids in `gf:gear`, F9's gear contract) |
| `src/feature/tgear/Recipes.cpp` | The 24 modifier recipes and the vest / gloves merge as special crafting; `add_modifier` trigger for the crafter |
| `src/feature/tgear/Command.cpp` | `/twilightforest travellers_gear add_modifier|remove_modifier <modifier>` (soft enum of the registry keys plus an any-word overload for TF's "not a valid Traveller's Gear Modifier", operators see the result) |
| `src/feature/tgear/WornModel.cpp` | The worn models' state of a player (gloves, slim arms, belt, goggles zoom, first-person glove overlay) as bits of its synced mark variant, for every viewer |
| `src/feature/tgear/Module.cpp` | Ticks of every wearer (haste, auto repair, powder snow / freezing; players: stealth, the item display's map, efficient eater, broken names), arrow magnetism, perfect dodge, never-break, the broken pieces' armour, all-night goggles (phantoms, ender mask), piglins, the stealth target filter, net channels, vanilla fallbacks, the red thread's MultifaceBlock rules, red thread vision (`gf:tgear:red_thread_vision`) |
| `src/feature/tgear/{Belt,Display,Tooltips}.cpp` | Hotbar swap, item display storage / cycle / secondary clicks / lodestone / TF map ticking / form, lore tooltips for vanilla players; config section `tgear` |
| `src/feature/ext/tgear/PlayerHooks.{h,cpp}` | Contract extension: the armour damage hook of mobs and players, before and after the game (R18) |
| `src/feature/ext/tgear/ClientHooks.{h,cpp}` | Contract extension (client): the local turn (`LocalPlayer::localPlayerTurn`, an R8 exception for A8 to fold into F12's movement / camera contract) and secondary clicks in container screens (`ContainerScreenController::_handlePlaceOne / _handleTakeHalf`) (R18) |
| `src/feature/ext/tgear/CraftHooks.{h,cpp}` | Contract extension: the grindstone result and take (`CraftHandlerGrindstone::getResultItem / _handleCraftAction`), the anvil refusal (`AnvilContainerScreenValidator::getCraftResult`) and the storage refusal of shulker boxes / bundles (`ShulkerBoxContainerValidation / DynamicContainerValidation::isItemAllowedInSlot`, `ShulkerBoxBlockActor::canPushInItem`) (R18) |
| `src/feature/tgear/Machines.cpp` | TF's grindstone, anvil and canFitInsideContainerItems rules |
| `src/client/tgear/TgearClient.cpp` | Keys, zoom FOV, turn and sound, item display HUD, red thread (RedThreadRenderer: next-thread and corner connections, the glow), the first-person glove overlay setting, the gear's icon layers and the red thread's size icons, TF's tooltip with Shift descriptions |
| `src/feature/ext/tgear/IconHooks.{h,cpp}` | Contract extension (client): GUI icon layers (`ItemRenderer::renderGuiItemInChunk`: the base icon replaced by an item atlas entry, overlay entries drawn over it in the same batch) (R18) |
| `assets/pack/bp/items/tgear`, `assets/pack/bp/recipes/tgear`, `assets/pack/bp/blocks/tgear` | Wearable gear items, 35 BP recipes (one per potion variant), the red thread multiface block (thin per-face selection boxes, waterloggable) |
| `assets/pack/rp/{models/blocks,particles}/tgear`, `assets/pack/rp/terrain_texture.d/tgear.json` | Red thread block geometry (RedThreadModel's centre piece per face and the 24 same-block connections, shown by the face states), the perfect dodge particle (PortalParticle path) |
| `assets/pack/rp/textures/twilightforest/entity/tgear/tf-derived/` | `red_thread.png` (TF's texture with the transposed copies of the parts the east / west faces need: block geometry cannot rotate a face's UVs) and `red_thread_texels.json` (TF's texels, which the client draws the connections with) |
| `assets/pack/rp/item_texture.d/tgear.json` | Item atlas entries of the `*_broken` icons, every modifier overlay (normal and broken) and the red thread bundle / spool icons |

## Vanilla fallbacks (players without the client mod)

- **Item display**: Bedrock's inventory gives a vanilla client no secondary click on a stack that a server
  can take. Sneak-use the goggles in hand: a form stores the off-hand / hotbar map, compass, clock or moon
  dial (TF's insert / swap rules), takes the first stored item out (TF's remove) and cycles the shown map
  (TF's cycle). There is no HUD.
- **Swap hotbar**: sneak-use a belt or wings in hand swaps the hotbar with the worn legs piece's storage
  (the held belt itself cannot go into a container, as in TF).
- **Zoom** and **red thread vision**: client mod only. The red thread blocks show their centre pieces and
  the connections between the faces of one block for everyone (block geometry); the connections to the
  next thread and round corners are the client mod's.
- **Tooltips**: lore carries the modifier list ("Ability:", "- <name>" with its description lines under
  it, "- Empty") and the stored items of the belt / goggles. Lore cannot follow Shift, so a vanilla viewer
  always sees the descriptions (TF's tooltip with Shift held) and no "Hold Shift for info"; players with
  the client mod get TF's tooltip from the client instead.
- **Item icons**: the base icon (Bedrock icons are static per item: no broken texture, overlays or spool
  sizes without the client mod).
- **Worn models**: the attachables of F2 show the vest, wings, goggles and boots (see Deviations: the
  gloves arms, the belt and the "down" texture wait for F2).

## Deviations (each with why Bedrock / LeviLamina cannot do it TF's way)

- `/twilightforest travellers_gear`: the modifier name is rendered on the server with TF's default key
  names (`C`, `V`, `Z`, "Not Bound"; Java's defaults for vanilla keybind components such as side_step's
  `key.left` / `key.right`: `A` / `D`), also in a local world. TF uses the host's current binding; F12's key
  contract (`client/input/Input.h`) exposes `registerKey` and `down` only, not the current binding. A
  non-operator gets "You need operator permission." instead of the literal being hidden: `/twilightforest`
  is one LeviLamina command (its other subcommands are open to everyone) and LeviLamina overloads carry no
  permission of their own. A key that is not in the registry reaches the command through a second
  (any-word) overload and gets TF's "%s is not a valid Traveller's Gear Modifier".
- Broken pieces: the name is "<name> (Broken)" through a custom name the server sets while the piece is
  broken (Bedrock items have no per-stack name function); a player's own anvil name is never replaced.
  Their armour points are taken out of the damage (combat contract: the incoming damage is raised so the
  game's 4 % per point reduction counts only the pieces that are not broken); the armour bar still shows
  the item's protection (Bedrock's protection is per item, `minecraft:wearable`).
- Efficient eater halves the player's ExhaustionComponent walk / sprint / swim / jump / sprint jump
  coefficients while active (Bedrock's FoodExhaustionSystem; TF divides the exhaustion PlayerHooks sees).
- Stealth, the item display's map and efficient eater run for players (TF: PlayerTickEvent / the
  player's goggles); haste, auto repair and arrow magnetism run for every wearer. `isDay` for the auto repair
  boost ignores rain / thunder darkening.
- Arrow magnetism: Bedrock gives units no projectile-impact event (F11's `onProjectileHit` is per block
  id). An arrow or trident of a wearer is tracked from the first tick it is seen flying; the tick its
  ProjectileComponent is in the ground is its impact (within 200 ticks of the first sighting). Arrows
  stuck before (shot earlier, loaded from disk) never count. Bedrock's creative / infinity arrows count as
  CREATIVE_ONLY.
- Perfect dodge: only a projectile's own impact (arrow, trident, thrown, mob projectile, fireball, wither
  skull, wind charge, spit damage) is dodged, never the explosion, fire or magic damage a projectile causes;
  the explosion of the dodged impact itself (same projectile, same victim, same tick) is cancelled with it,
  as TF cancels the whole impact. The combat contract cancels the damage; a tick later the projectile gets
  the motion it hit with back, one step further along (TF cancels the impact, so the projectile flies on;
  Bedrock removes an arrow that hit an entity before units see it, so an arrow may still vanish).
- All-night goggles: a natural phantom spawn (LeviLamina's SpawningMobEvent) is cancelled when every player
  who could own it wears active goggles: the insomniac players of the dimension within 64 blocks
  (InsomniaComponent), else the nearest player (Bedrock does not tell which player a phantom spawn is
  for; TF denies per insomniac player). The time since rest runs on, as in TF. The ender mask and the piglins' neutrality
  are target filters (F8's `addTargetFilter`): an enderman never targets the wearer unless the wearer hurt
  it (Bedrock's gaze check is the vanilla enderman's data-driven filter); a piglin never targets a player
  in the goggles or the wings unless hurt by them (Java piglins also anger at chest opening and gold
  mining; Bedrock's piglin anger is data driven).
- Powder snow and freezing: the boots give the wearer Bedrock's CanStandOnPowderSnowComponent and the
  boots / vest its FreezingImmuneComponent (the entity flags Bedrock reads; its leather armour sets their
  `FromEquipment` twins from a hard-coded item check).
- Item display: the map decorations are rotated about their centre by rotation x 360 / 16 degrees
  (MapRenderer), through the UI's world matrix around F12's blit. TF's own maps (magic, maze, ore) tick from the goggles through
  F9's item behaviour registry (`gf::item::internal::behaviourOf`, the owner's inventory tick as the
  selected item). `twentyFourHourFormat` follows the client's locale while `itemDisplay.followLocale` is on
  (TF's `use24HourTimeDefault`). The moon dial follows the level's day time in every dimension
  (DimensionType#moonPhase(getDayTime()): the Twilight Forest's fixed_time only changes the clock), and
  outside a natural dimension shows `phase_unknown_fools` on April 1st (local date), as MoonDialItem.
  The slot of a secondary click with the goggles on the cursor only receives a stored item the slot may
  hold (removeFirstFree(slot): the game's container validation of that slot, e.g. armour slots take only
  their wearable, a furnace result nothing).
- Item icons (TravellersGearItemModel, the red thread's `twilightforest:size` property): with the client
  mod every GUI slot (inventory, hotbar, containers) draws the icon through `ItemRenderer::renderGuiItemInChunk`
  with layers (ext/tgear IconHooks, because F12's Gui item renderer, `InventoryItemRenderer::render`, does
  not fire for these slots on 1.26.51): the `*_broken` icon replaces the base while broken, then one
  overlay per insertable modifier and the gloves overlay of a vest with gloves (from `broken/` while
  broken), drawn in the same batch; red thread stacks of 4 / 16 / 32 and more show red_thread_bundle_0 /
  _1 / _2. The layers are item atlas entries (`item_texture.d/tgear.json`). In the hand, on the ground and
  in frames the base icon stays (Bedrock extrudes the item's own atlas icon there); vanilla clients see the
  base icon everywhere (Bedrock icons are static per item).
- Zoom FOV: the client mod sends the zoom state with its FOV times the zoom (`LevelRendererPlayer::getFov`
  without the variable modifiers, x 0.3); the server narrows that player's camera with the camera command
  (`camera "<player>" fov_set <fov>` / `fov_clear`, run as the server: the vanilla camera's custom FOV).
  Round-3 probes: the command changes 49 % of the pixels, a hand-built `CameraInstructionPacket` with the
  same FOV instruction is accepted without any effect. Java's ComputeFovModifierEvent multiplies the FOV every frame; on Bedrock the world projection
  comes from the vanilla camera's FOV component, which neither F12's `camera::addFovModifier`
  (`LocalPlayer::getFieldOfViewModifier`) nor `LevelRendererPlayer::getFov` / `CameraAPI::tryGetFOV`
  reach (rounds 1-2). The vanilla camera takes custom FOVs within [30, 110] only (content log of round 3:
  "Custom FOV value must be within [30.0, 110.0]"), so TF's 70 x 0.3 = 21 degrees is drawn at 30 degrees
  (x 0.43 at the default FOV; any client FOV of 100 or more gets TF's exact x 0.3). The turn slowdown (`LocalPlayer::localPlayerTurn`, TF's slowZoomSensitivity at
  Java's default sensitivity) stays a client hook. Speed / sprint FOV changes during a zoom keep the
  zoomed FOV (the instruction fixes the camera FOV). The zoom sound plays on the zooming client only, at
  once (`ClientInstance::playUI`, TF's LocalPlayer#playSound); the zoom state goes to the players in the
  same dimension within their view distance (TF's sendToPlayersTrackingEntity).
- Worn models (gloves on the vest and their slim variant, the belt on swap-hotbar wings, the goggles'
  `travellers_layer_1_down` while zooming, the first-person glove overlay): the server keeps each player's
  state in its synced mark variant (`WornModel.cpp`: bit 1 gloves, 2 slim arms, 4 belt, 8 zooming, 16 the
  client's firstPersonGloveOverlay, sent on `tgear.glove_overlay`; selftest `tgear.worn_model`), which
  every client receives, and an attachable reads it as `q.mark_variant` of its wearer. PENDING F2: the
  attachables are F2's generated files (`assets/gen/geo/tgear/rp/attachables`, from
  `tools/models/catalog.json`, checked by `tools/models/convert.ps1 -Check` in CI), and a manual
  attachable with the same identifier (the owner's override, `assets/pack/rp/attachables/tgear/`) makes
  assemble drop the generated file, which F2's selftest `models.attachables` reads by its generated path
  (`src/feature/ext/models/ModelsSelfTest.cpp`, `readJson(rpDir() / file)`: "missing") and so fails in the
  full run. The hookup F2 (or A8) has to generate, with F2's own part render controllers (b(n) =
  `math.mod(math.floor(q.mark_variant / n), 2) == 1`):
  vest: geometry part1 `travellers_armor.gloves`, part2 `travellers_armor.gloves_slim` (texture
  travellers_layer_1), render controllers `controller.render.armor`, part1 if `b(1) && !b(2) &&
  (!c.is_first_person || b(16))`, part2 if `b(1) && b(2) && (!c.is_first_person || b(16))`; gloves: armor
  if `!b(2) && (...)`, part1 = gloves_slim if `b(2) && (...)`; wings: armor if `!c.is_first_person`, part1 =
  `travellers_armor.belt` (travellers_layer_2) if `b(4) && !c.is_first_person`; goggles: armor if `!b(8)`,
  part1 = the goggles with `travellers_layer_1_down` if `b(8)`. Mobs wearing the gear keep the plain
  attachables (their mark variant is their own).
- Tooltip images (TravellersBeltTooltipComponent, ItemDisplayTooltipComponent: the bundle background, the
  slot sprites, the *_display.png placeholders): Bedrock's hover text is one string
  (`ItemStackBase::getFormattedHovertext`, drawn by the JSON UI's hover text label); F12's `tooltip::add`
  adds lines and there is no hover-panel image hook in the client contracts (drawing one means hooking the
  hover text renderer, F12's). The stored items are listed as lines (with the client mod and as lore).
  Pending F12 (a hover-panel image hook).
- Item display HUD: the overlay draws at (screenOffsetX, screenOffsetY) x screenScale in GUI pixels with
  the UI's world matrix minus any translation another control left (the chat panel did in round 2) and the
  full clipping rectangle. The map background is Bedrock's `textures/map/map_background`: Java's
  `map_background_checkerboard` is a Mojang Java Edition resource that Bedrock does not ship and that
  GloamingForest cannot redistribute.
- Grindstone and anvil (TravellersGearEvents): one piece with insertable modifiers comes out of the
  grindstone without them (Bedrock's grindstone result is replaced on the server); two pieces or a piece
  without modifiers get no result; the swap_hotbar container and the item display's contents are given
  to the player once the result is taken (the next tick, after the item stack request). The anvil never
  combines two pieces, and a survival player gets no result for a piece and an enchanted book (Java drops
  every unsupported book enchantment, leaving no result); a vanilla client may still preview a result,
  which the server refuses when it is taken.
- Belts and storage (canFitInsideContainerItems): a belt or wings carrying the swap_hotbar container
  (TravellersArmorBeltItem, the class of both) are refused by shulker box slots, a shulker box's hopper
  input and bundles (server-side validation).
- Red thread: the block geometry draws TF's 2 x 2 centre piece per face and the 24 connections between the
  faces of the same block (bone visibility by the face states), for everyone; the east / west faces use
  transposed copies of the part textures (the tf-derived texture), since Bedrock's block geometry cannot
  rotate a face's UVs. The client mod draws the connections to the same face of the next thread and round
  the corner (unless a full face is in the way) at the block's light (the dimension's brightness ramp,
  Java's packedLight) and, while red thread is held or the vision is on, everything full bright through
  walls (the GLOW render type). It draws them texel by texel from TF's texture (`red_thread_texels.json`)
  as vertex-coloured quads with the materials F12's Lines use (`wireframe` depth tested,
  `ui_fill_gradient` without a depth test): the textured immediate path sampled grey on 1.26.51 (round 3).
  The thread is placed with the stone sound (`dig.stone` at pitch 0.8: SoundType.STONE). MultifaceSpreader:
  TF's red thread is not bonemealable, so the spreader is never used. A face whose support goes is removed
  without a drop (Java's updateShape); breaking the block drops one thread per face (the imported loot
  table). Selection boxes are one box per state (63 permutations; the union of the 1 px face slabs:
  Bedrock has a single selection box per block).
- Wing visuals: the per-state wing animation is U12's (`tgmove`, TravellersWingsModel's animation); its
  hookup on the wings' attachable (U12's `animation.twilightforest.travellers_wings`) waits for F2 as the
  worn models do (F2's selftest also refuses an animation outside its index).
- Item display HUD map: the client e2e makes a real map (an empty map used in the hand) that
  `tgear.real_arm` stores, so the map widget is screenshotted with its colours.
- Keys are keyboard bindings of the client mod (`tgear.zoom` Z, `tgear.swap_hotbar` V,
  `tgear.item_display_map_cycle` C, `tgear.red_thread_vision` unbound), TF's defaults. Bedrock's own keyboard
  binding of Z opens the mob effects screen, which takes the key while it is open: remap one of the two in
  the controls to zoom (the client e2e unbinds the effects key for its run, tgear.prelaunch.ps1).

## Audit round 1

- Server scenario `tgear`: PASSED (23 / 23 selftests, 2026-11-07).
- Client run (`-Scenario tgear -Mode local`): FAILS at "no slot icon was drawn with its layers". It never passed with
  that check: no earlier client log (worktrees of 2026-11-05 and 11-06) has a `tgear: icon of ... drawn with` line.
  The HUD item display (map with its frame and decorations, the position line, the clock with time.png's sun, the moon
  dial line) and the worn gear in the inventory (gloves, belt, wings) look as TF's in the screenshots.
- Found (diagnostics added to `src/feature/ext/tgear/IconHooks.cpp`: one line per GUI pass and TF item): on 1.26.51
  the hotbar and the inventory slots bind their items through `ItemRendererDataHelpers::setItemRendererBindings`
  (OreUI data: `ItemRendererData` holds the item id + aux, the count and a few strings, no user data), and the slot
  icons are then drawn from a fresh stack of that id: `renderGuiItemInChunk` / `ComponentItem::getIconInfo` see the
  gear and the red thread only as count-1 stacks without modifiers (the recipe book's and the atlas capture's), so
  `gearIcon` / `threadIcon` find no layers there. The layered stacks reach `setItemRendererBindings` only, and the
  `GeometryAtlas::ItemHandleImpl` constructor hook never runs. Open for round 2: carry the layers through the binding
  (a key the renderer keeps, e.g. one of `ItemRendererData`'s strings, mapped back to the layers when the tile is
  drawn) or draw the overlays over the bound slot; then the broken texture, the modifier overlays and the spool
  sizes show.
- Open: the hidden effects of the gear (Haste of `haste`, Jump Boost of the wings) show their icons in Bedrock's HUD
  effect list (see U12's audit).
- Not this unit's (seen in every client run): the content-log warnings of U9's / U7's emperor's cloth recipes
  ("has the same ingredients as minecraft:iron_chestplate's recipe ... Adding duplicate crafting_table recipe").
