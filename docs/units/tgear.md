# U11 `tgear`: Travellers' Gear (equipment, goggles, red thread)

Behaviour spec: [docs/spec/tgear.md](../spec/tgear.md). This page lists how the unit is built on Bedrock,
its vanilla fallbacks and its deviations from TF 4.8 (for A8's deviation list).

## Layout

| Path | What |
|---|---|
| `src/core/tgear/Logic.{h,cpp}` (+ `tests/tgear`) | Item display rules (ItemDisplayContents.Mutable), hotbar swap plan, auto repair chance, clock / moon dial texts, `${tfkeybinds/...}` interpolation |
| `src/feature/tgear/Gear.{h,cpp}` | Modifier registry (data `twilight/travellers_modifiers`), slots, broken state, `isModifierActive`, stack data: `gf:tgear.container`, `gf:tgear.display`, `gf:tgear.gloves` (modifier ids in `gf:gear`, F9's gear contract) |
| `src/feature/tgear/Recipes.cpp` | The 24 modifier recipes and the vest / gloves merge as special crafting; `add_modifier` trigger |
| `src/feature/tgear/Command.cpp` | `/twilightforest travellers_gear add_modifier|remove_modifier <modifier>` |
| `src/feature/tgear/Module.cpp` | Ticks (haste, stealth, auto repair, map ticking, arrow magnetism), perfect dodge, never-break, efficient eater, net channels, vanilla fallbacks, red thread placement |
| `src/feature/tgear/{Belt,Display,Tooltips}.cpp` | Hotbar swap, item display storage / cycle / form, lore tooltips; config section `tgear` |
| `src/feature/ext/tgear/PlayerHooks.{h,cpp}` | Contract extension: the armour damage hook (R18) |
| `src/client/tgear/TgearClient.cpp` | Keys, zoom FOV, item display HUD, red thread glow, Shift descriptions |
| `assets/pack/bp/items/tgear`, `assets/pack/bp/recipes/tgear`, `assets/pack/bp/blocks/tgear` | Wearable gear items, 35 BP recipes (one per potion variant), the red thread multiface block |
| `assets/pack/rp/{models/blocks,particles}/tgear`, `assets/pack/rp/terrain_texture.d/tgear.json` | Red thread block geometry, the perfect dodge particle (PortalParticle path) |

## Vanilla fallbacks (players without the client mod)

- **Item display**: Bedrock's inventory has no secondary click on a stack. Sneak-use the goggles in hand:
  a form stores the off-hand / hotbar map, compass, clock or moon dial (TF's insert / swap rules),
  takes the first stored item out (TF's remove) and cycles the shown map (TF's cycle). There is no HUD.
- **Swap hotbar**: sneak-use a belt or wings in hand swaps the hotbar with the worn legs piece's storage
  (the held belt itself cannot go into a container, as in TF).
- **Zoom** and **red thread vision**: client mod only (the red thread blocks render their face dots
  for everyone).
- Lore carries the modifier list ("Ability:", "- <name>", "- Empty", "Hold Shift for info") and the stored
  items of the belt / goggles.

## Deviations

- `/twilightforest travellers_gear`: the modifier name is rendered on the server with TF's default key
  names (`C`, `V`, `Z`, "Not Bound"), also in a local world (TF uses the host's current binding). TF's
  dedicated server fails with `NoClassDefFoundError` for the three names with a key placeholder;
  GloamingForest answers normally. The success message is not broadcast to other operators.
- Broken pieces keep their armour points: Bedrock's wearable protection is per item, not per stack
  (TF moves the attribute modifiers to `stored_broken_attributes`). The broken mark is a lore line, not
  part of the item name. All modifiers but auto repair are inactive, as in TF.
- Efficient eater halves the player's ExhaustionComponent walk / sprint / swim / jump / sprint jump
  coefficients while active (Bedrock's FoodExhaustionSystem; TF divides the exhaustion PlayerHooks sees).
- Auto repair, haste, stealth and the map ticking run for players only (TF ticks every LivingEntity).
  `isDay` for the auto repair boost ignores rain / thunder darkening.
- Arrow magnetism detects a vanilla arrow stuck in a block by it not moving for a tick after it flew
  (Bedrock gives no projectile-hit-block event to units); Bedrock's creative / infinity arrows count as
  CREATIVE_ONLY.
- Perfect dodge cancels the projectile's damage (combat contract); the projectile is not made to pass
  through as TF's cancelled impact does.
- All-night goggles: phantoms are kept away by holding the wearer's insomnia counter (Bedrock's
  InsomniaComponent time since rest) at 0 while the modifier is active; TF only denies the spawn event and
  lets the counter run on. No ender mask: Bedrock's enderman gaze check is data-driven (the vanilla
  enderman's carved pumpkin filter) and no contract reaches it.
- Item display: the map in the HUD is drawn from the client's map view (F12's maps::view) as coloured
  runs at TF's 0.7075 scale, without the map decorations; only vanilla filled maps tick from the goggles.
  The compass shows the player position (no lodestone line). `twentyFourHourFormat` follows the
  client's locale while `itemDisplay.followLocale` is on (TF's `use24HourTimeDefault`).
- Zoom: the mouse sensitivity slowdown, the goggles' "down" texture for other players and the
  first-person glove overlay are not drawn (no render / input contract for them).
- Red thread: the block shows TF's 2 x 2 centre piece per face; the connections between neighbouring
  threads are drawn only by the client mod's glow (red lines through walls while the vision is on or
  red thread is held). The face does not drop when its supporting block goes.
- Wing / belt visuals: the attachables of F2 show the whole legs model; the per-state wing animation is
  U12's.
- Tooltip images (TravellersBeltTooltipComponent, ItemDisplayTooltipComponent) are text lines.
- Keys are keyboard bindings of the client mod (`tgear.zoom` Z, `tgear.swap_hotbar` V,
  `tgear.item_display_map_cycle` C, `tgear.red_thread_vision` unbound), TF's defaults. Bedrock's own keyboard
  binding of Z also opens a screen over the view: remap one of the two in the controls to zoom without it.
