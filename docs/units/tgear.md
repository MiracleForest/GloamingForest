# U11 `tgear`: Travellers' Gear (equipment, goggles, red thread)

Behaviour spec: [docs/spec/tgear.md](../spec/tgear.md). This page lists how the unit is built on Bedrock,
its vanilla fallbacks and its deviations from TF 4.8 (for A8's deviation list).

## Layout

| Path | What |
|---|---|
| `src/core/tgear/Logic.{h,cpp}` (+ `tests/tgear`) | Item display rules (ItemDisplayContents.Mutable), hotbar swap plan, auto repair chance, clock / moon dial texts, `${tfkeybinds/...}` interpolation |
| `src/feature/tgear/Gear.{h,cpp}` | Modifier registry (data `twilight/travellers_modifiers`), slots, broken state, `isModifierActive` for any wearer, stack data: `gf:tgear.container`, `gf:tgear.display`, `gf:tgear.gloves` (modifier ids in `gf:gear`, F9's gear contract) |
| `src/feature/tgear/Recipes.cpp` | The 24 modifier recipes and the vest / gloves merge as special crafting; `add_modifier` trigger for the crafter |
| `src/feature/tgear/Command.cpp` | `/twilightforest travellers_gear add_modifier|remove_modifier <modifier>` (soft enum of the registry keys, operators see the result) |
| `src/feature/tgear/Module.cpp` | Ticks of every wearer (haste, auto repair, powder snow / freezing; players: stealth, the item display's map, efficient eater, broken names), arrow magnetism, perfect dodge, never-break, the broken pieces' armour, all-night goggles (phantoms, ender mask), piglins, the stealth target filter, net channels, vanilla fallbacks, the red thread's MultifaceBlock rules, red thread vision (`gf:tgear:red_thread_vision`) |
| `src/feature/tgear/{Belt,Display,Tooltips}.cpp` | Hotbar swap, item display storage / cycle / secondary clicks / lodestone / TF map ticking / form, lore tooltips for vanilla players; config section `tgear` |
| `src/feature/ext/tgear/PlayerHooks.{h,cpp}` | Contract extension: the armour damage hook of mobs and players, before and after the game (R18) |
| `src/feature/ext/tgear/ClientHooks.{h,cpp}` | Contract extension (client): the local turn (`LocalPlayer::localPlayerTurn`, an R8 exception for A8 to fold into F12's movement / camera contract) and secondary clicks in container screens (`ContainerScreenController::_handlePlaceOne / _handleTakeHalf`) (R18) |
| `src/feature/ext/tgear/CraftHooks.{h,cpp}` | Contract extension: the grindstone result and take (`CraftHandlerGrindstone::getResultItem / _handleCraftAction`), the anvil refusal (`AnvilContainerScreenValidator::getCraftResult`) and the storage refusal of shulker boxes / bundles (`ShulkerBoxContainerValidation / DynamicContainerValidation::isItemAllowedInSlot`, `ShulkerBoxBlockActor::canPushInItem`) (R18) |
| `src/feature/tgear/Machines.cpp` | TF's grindstone, anvil and canFitInsideContainerItems rules |
| `src/client/tgear/TgearClient.cpp` | Keys, zoom FOV and turn, item display HUD, red thread (RedThreadRenderer: connections, corners, glow), worn model state, item icon overlays, TF's tooltip with Shift descriptions |
| `assets/pack/bp/items/tgear`, `assets/pack/bp/recipes/tgear`, `assets/pack/bp/blocks/tgear` | Wearable gear items, 35 BP recipes (one per potion variant), the red thread multiface block (thin per-face selection boxes, waterloggable) |
| `assets/pack/rp/{models/blocks,particles}/tgear`, `assets/pack/rp/terrain_texture.d/tgear.json` | Red thread block geometry (RedThreadModel's centre piece), the perfect dodge particle (PortalParticle path) |

## Vanilla fallbacks (players without the client mod)

- **Item display**: Bedrock's inventory gives a vanilla client no secondary click on a stack that a server
  can take. Sneak-use the goggles in hand: a form stores the off-hand / hotbar map, compass, clock or moon
  dial (TF's insert / swap rules), takes the first stored item out (TF's remove) and cycles the shown map
  (TF's cycle). There is no HUD.
- **Swap hotbar**: sneak-use a belt or wings in hand swaps the hotbar with the worn legs piece's storage
  (the held belt itself cannot go into a container, as in TF).
- **Zoom** and **red thread vision**: client mod only (the red thread blocks render their centre pieces
  for everyone).
- **Tooltips**: lore carries the modifier list ("Ability:", "- <name>", "- Empty", "Hold Shift for info")
  and the stored items of the belt / goggles (players with the client mod get TF's tooltip from the
  client instead, with the Shift descriptions).
- **Worn models**: not drawn (see Deviations); the attachables of F2 show the vest, wings and goggles.

## Deviations (each with why Bedrock / LeviLamina cannot do it TF's way)

- `/twilightforest travellers_gear`: the modifier name is rendered on the server with TF's default key
  names (`C`, `V`, `Z`, "Not Bound"), also in a local world. TF uses the host's current binding; F12's key
  contract (`client/input/Input.h`) exposes `registerKey` and `down` only, not the current binding. A
  non-operator gets "You need operator permission." instead of the literal being hidden: `/twilightforest`
  is one LeviLamina command (its other subcommands are open to everyone) and LeviLamina overloads carry no
  permission of their own. A malformed or unknown key fails Bedrock's soft-enum parse (TF: a
  ResourceLocation parse error or "not a valid Traveller's Gear Modifier").
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
- Item display: the map decorations are drawn
  upright (F12's canvas has no rotated blit). TF's own maps (magic, maze, ore) tick from the goggles through
  F9's item behaviour registry (`gf::item::internal::behaviourOf`, the owner's inventory tick as the
  selected item). `twentyFourHourFormat` follows the client's locale while `itemDisplay.followLocale` is on
  (TF's `use24HourTimeDefault`).
- Item icons: Bedrock item icons are static per item (`minecraft:icon`). With the client mod the slot
  renderer (F12's `registerItemRenderer`, Gui) draws TravellersGearItemModel's layers over the icon: the
  `*_broken` texture, then one overlay per modifier and the gloves overlay (from `broken/` while
  broken). Without the mod the base icon shows.
- Zoom FOV: the client mod sends the zoom state with its FOV times the zoom (`LevelRendererPlayer::getFov`
  without the variable modifiers, x 0.3); the server narrows that player's camera with a camera FOV
  instruction (`CameraInstructionPacket`, the `camera fov_set` / `fov_clear` of the vanilla camera, no
  easing). Java's ComputeFovModifierEvent multiplies the FOV every frame; on Bedrock the world projection
  comes from the vanilla camera's FOV component, which neither F12's `camera::addFovModifier`
  (`LocalPlayer::getFieldOfViewModifier`) nor `LevelRendererPlayer::getFov` / `CameraAPI::tryGetFOV`
  reach (rounds 1-2). The turn slowdown (`LocalPlayer::localPlayerTurn`, TF's slowZoomSensitivity at
  Java's default sensitivity) stays a client hook. Speed / sprint FOV changes during a zoom keep the
  zoomed FOV (the instruction fixes the camera FOV).
- Worn models (gloves on the vest, slim gloves, the belt on swap-hotbar wings, the goggles' "down"
  texture while zooming, the first-person glove overlay): NOT ACHIEVED. Bedrock attachables cannot read
  per-stack item data, and attachable molang cannot read the wearer's variables: `c.owning_entity->v.x`
  fails ("unknown variable context.owning_entity") and `v.x` fails ("unknown variable", even with the
  wearer's parent_setup defining it) - content log of round 2. The client mod could draw the parts only
  posed on the player's arm / leg / head bones, and the bone poses of a rendered player are not offered by
  F12's entity renderer contract (`registerEntityRenderer` gets the actor and the camera, no bone
  matrices); hooking the actor renderer for them is F12's (R8). Closest workaround in place: the F2
  attachables draw the vest, wings, goggles and boots themselves. Routed to F12 / A8: an entity layer with
  the posed bone matrices (Java's RenderLayer) would let U11 draw TravellersGearModels /
  TravellersWingsModel's belt / the goggles' down texture with drawGeo. The client already tracks the
  other players' zoom (`tgear.goggles_zoom`).
- Item icon overlays: drawn through F12's Gui item renderer (InventoryItemRenderer::render, after the slot
  batch) with the slot's absolute position and no translation of another control; the client logs the
  first draws ("tgear: gear icon ... drawn at").
- Tooltip images (TravellersBeltTooltipComponent, ItemDisplayTooltipComponent: the bundle background, the
  slot sprites, the *_display.png placeholders): Bedrock's hover text is one string
  (`ItemStackBase::getFormattedHovertext`, drawn by the JSON UI's hover text label); F12's `tooltip::add`
  adds lines and there is no hover-panel image hook in the client contracts (drawing one means hooking the
  hover text renderer, F12's). The stored items are listed as lines (with the client mod and as lore).
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
- Belts and storage (canFitInsideContainerItems): a belt with its container is refused by shulker box
  slots, a shulker box's hopper input and bundles (server-side validation).- Red thread: the block model draws TF's 2 x 2 centre piece per face for everyone; the client mod draws the
  connections (same block, next thread, round the corner unless a full face is in the way) with the
  cutout material at the block's light, and while red thread is held or the vision is on, everything full
  bright through walls (the UI textured material: no depth test). MultifaceSpreader: TF's red thread is not
  bonemealable, so the spreader is never used. A face whose support goes is removed without a drop (Java's
  updateShape); breaking the block drops one thread per face (the imported loot table). Selection boxes are
  one box per state (63 permutations; the union of the 1 px face slabs: Bedrock has a single selection
  box per block).
- Wing visuals: the per-state wing animation is U12's (`tgmove`, TravellersWingsModel's animation).
- Item display HUD map: the client e2e makes a real map (an empty map used in the hand) that
  `tgear.real_arm` stores, so the map widget is screenshotted with its colours.
- Keys are keyboard bindings of the client mod (`tgear.zoom` Z, `tgear.swap_hotbar` V,
  `tgear.item_display_map_cycle` C, `tgear.red_thread_vision` unbound), TF's defaults. Bedrock's own keyboard
  binding of Z also opens a screen over the view: remap one of the two in the controls to zoom without it.
