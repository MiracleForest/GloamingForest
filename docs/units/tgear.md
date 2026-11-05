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
| `src/feature/ext/tgear/ClientHooks.{h,cpp}` | Contract extension (client): the world FOV (`LevelRendererPlayer::getFov`), the local turn (`LocalPlayer::localPlayerTurn`) and secondary clicks in container screens (`ContainerScreenController::_handlePlaceOne / _handleTakeHalf`) (R18) |
| `src/client/tgear/TgearClient.cpp` | Keys, zoom FOV and turn, item display HUD, red thread (RedThreadRenderer: connections, corners, glow), worn model state, item icon overlays, TF's tooltip with Shift descriptions |
| `assets/pack/bp/items/tgear`, `assets/pack/bp/recipes/tgear`, `assets/pack/bp/blocks/tgear` | Wearable gear items, 35 BP recipes (one per potion variant), the red thread multiface block (thin per-face selection boxes, waterloggable) |
| `assets/pack/rp/{models/blocks,particles,attachables,render_controllers}/tgear`, `assets/pack/rp/terrain_texture.d/tgear.json` | Red thread block geometry (RedThreadModel's centre piece), the perfect dodge particle (PortalParticle path), the vest / wings / goggles attachables and their render controllers |

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
- **Worn models**: the attachables draw the gloves, the belt on the wings and the goggles' "down" texture
  from molang variables the client mod sets; a vanilla client draws the vest, wings and goggles alone.

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
- Perfect dodge: the combat contract cancels the damage; a tick later the projectile gets the motion it hit
  with back, one step further along (TF cancels the impact, so the projectile flies on).
- All-night goggles: a natural phantom spawn (LeviLamina's SpawningMobEvent) whose nearest player wears
  them is cancelled; the time since rest runs on, as in TF. The ender mask and the piglins' neutrality
  are target filters (F8's `addTargetFilter`): an enderman never targets the wearer unless the wearer hurt
  it (Bedrock's gaze check is the vanilla enderman's data-driven filter); a piglin never targets a player
  in the goggles or the wings unless hurt by them (Java piglins also anger at chest opening and gold
  mining; Bedrock's piglin anger is data driven).
- Powder snow and freezing: the boots give the wearer Bedrock's CanStandOnPowderSnowComponent and the
  boots / vest its FreezingImmuneComponent (the entity flags Bedrock reads; its leather armour sets their
  `FromEquipment` twins from a hard-coded item check).
- Item display: the HUD map background is Bedrock's `textures/map/map_background` (Java's
  `map_background_checkerboard` is a Java resource Bedrock does not ship); the map decorations are drawn
  upright (F12's canvas has no rotated blit). TF's own maps (magic, maze, ore) tick from the goggles through
  F9's item behaviour registry (`gf::item::internal::behaviourOf`, the owner's inventory tick as the
  selected item). `twentyFourHourFormat` follows the client's locale while `itemDisplay.followLocale` is on
  (TF's `use24HourTimeDefault`).
- Tooltip images (TravellersBeltTooltipComponent, ItemDisplayTooltipComponent): Bedrock's hover text is a
  string (`ItemStackBase::getFormattedHovertext`, drawn by the JSON UI's hover text label); there is no
  per-item tooltip image to hook. The stored items are listed as lines.
- Item icons: Bedrock item icons are static per item (`minecraft:icon`). With the client mod the slot
  renderer (F12's `registerItemRenderer`, Gui) draws TravellersGearItemModel's layers over the icon: the
  `*_broken` texture, then one overlay per modifier and the gloves overlay (from `broken/` while
  broken). Without the mod the base icon shows.
- Zoom: the world FOV is multiplied in `LevelRendererPlayer::getFov` (ext hook): F12's
  `camera::addFovModifier` hooks `LocalPlayer::getFieldOfViewModifier`, which the game does not use for
  the world camera (round 1: identical screenshots). The turn is scaled by TF's slowZoomSensitivity at
  Java's default sensitivity (Bedrock's sensitivity setting has another scale).
- Red thread: the block model draws TF's 2 x 2 centre piece per face for everyone; the client mod draws the
  connections (same block, next thread, round the corner unless a full face is in the way) with the
  cutout material at the block's light, and while red thread is held or the vision is on, everything full
  bright through walls (the UI textured material: no depth test). MultifaceSpreader: TF's red thread is not
  bonemealable, so the spreader is never used. A face whose support goes is removed without a drop (Java's
  updateShape); breaking the block drops one thread per face (the imported loot table). Selection boxes are
  one box per state (63 permutations; the union of the 1 px face slabs: Bedrock has a single selection
  box per block).
- Wing visuals: the per-state wing animation is U12's (`tgmove`, TravellersWingsModel's animation).
- Keys are keyboard bindings of the client mod (`tgear.zoom` Z, `tgear.swap_hotbar` V,
  `tgear.item_display_map_cycle` C, `tgear.red_thread_vision` unbound), TF's defaults. Bedrock's own keyboard
  binding of Z also opens a screen over the view: remap one of the two in the controls to zoom without it.
