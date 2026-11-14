# Travellers' Gear: equipment, goggles and red thread (U11 `tgear`)

Behaviour spec of unit **U11** (wave W1), translated from the upstream 4.8 (`9e40a757`, MC 1.21.1) by F16. It is
the checklist the U11 worker implements and the A6 audit compares against. Sections 1-5 are the behaviour; Appendix A
lists every id U11 owns with upstream's registration and data (all 24 modifier recipes, the 12 non-movement modifier
registry entries); Appendix B the strings. The movement modifiers are U12's (`tgmove.md`); the modifier framework,
items, recipes and tooltips are U11's.

## 1. Scope

- Items `travellers_goggles` (helmet), `travellers_vest` (chest), `travellers_gloves` (chest, merge-only),
  `travellers_wings` (legs), `travellers_belt` (legs), `travellers_boots` (feet); armor material `travellers`.
- The **modifier framework**: registry `gloamingforest:travellers_modifiers` (data-driven), modifier types
  (`gloamingforest:component`, `builtin`, `attribute`, `transferable_component` — `TravellersModifierTypes`), recipe serializers
  `travellers_gear_modifier_shaped_recipe`, `travellers_gear_modifier_shapeless_recipe`,
  `travellers_vest_gloves_merge_recipe`, 33 recipes, trigger `gloamingforest:add_modifier`, subcommands
  `/gloamingforest travellers_gear add_modifier <modifier>` and `/gloamingforest travellers_gear remove_modifier
  <modifier>` (owners.tsv `subcommand` rows `travellers_gear`, `travellers_gear add_modifier`,
  `travellers_gear remove_modifier`; 3.1).
- U11 modifiers: `all_night_goggles`, `arrow_magnetism`, `auto_repair`, `efficient_eater`, `haste`, `item_display`,
  `perfect_dodge`, `red_thread_vision`, `stealth`, `swap_hotbar`, `swap_hotbar_ability`, `zoom` (+ their data
  components) and the gear-state components `has_travellers_belt/boots/chestplate/gloves/wings`, `travellers_armor`,
  `stored_broken_attributes`.
- Goggles features: zoom (key `zoom`, default Z), red thread vision (key `red_thread_vision`, unbound), item display
  HUD (key `item_display_map_cycle`, default C), the `red_thread` block + block entity; belt hotbar swap (key
  `swap_hotbar`, default V); attachments `is_using_goggles_zoom_modifier`, `travellers_goggles_red_thread_vision`;
  particle `perfect_dodge`; 9 sound events; tag `#travellers_agile_ranger_whitelisted` (read by U12); client config
  keys `client.firstPersonGloveOverlay` and `client.Item Display Modifier Settings.{screenOffsetX,screenOffsetY,
  screenScale,twentyFourHourFormat}` (section `tgear`); net channels `tgear.goggles_zoom`, `tgear.swap_hotbar`,
  `tgear.cycle_map_slot`.

Contracts: item, gear (data), recipes, combat, entity, projectile, client.ui, client.render, client.input+movement,
client.world.

## 2. Upstream sources

`item/travellers_gear/{TravellersArmorItem,TravellersArmorBeltItem,TravellersGogglesItem,TravellersGearLogic}.java`,
`item/travellers_gear/modifiers/*.java`, `init/custom/TravellersModifiersManager.java`,
`init/custom/TravellersModifierTypes.java`, `init/custom/ItemDisplays.java`, `components/item/ItemDisplayContents.java`,
`item/recipe/travellers/*.java`, `events/TravellersGearEvents.java`, `client/event/TravellersClientEvents.java`,
`client/overlay/ItemDisplayOverlay.java`, `client/overlay/display/*.java`, `client/renderer/tooltip/TravellersBeltTooltipComponent.java`,
`client/model/armor/{TravellersGearModels,TravellersWingsModel}.java`, `client/model/item/TravellersGearItemModel.java`,
`block/RedThreadBlock.java`, `block/entity/RedThreadBlockEntity.java`, `client/renderer/block/RedThreadRenderer.java`,
`client/model/entity/RedThreadModel.java`, `network/{GogglesZoom,SwapHotbar,CycleMapSlot}Packet.java`,
`command/TravellersGearCommand.java`, `init/TFKeyBinds.java`, `init/TFKeyBindsCategories.java`,
`init/TFArmorMaterials.java`, `init/TFAttributeModifiers.java`, `asmhooks/PlayerHooks.java` (efficient eater),
`advancements/AddModifierTrigger.java`, `config/TFClientConfig.java`.

## 3. Behaviour

### 3.1 The gear

Material `travellers`: defense boots 2, leggings 3, chestplate 4, helmet 2 (body 4), enchantability 0, toughness 0,
repair ingredient `gloamingforest:tanned_leather` (but `isRepairable` is false: no anvil repair; auto repair only),
layer `gloamingforest:travellers`. All pieces carry `gloamingforest:travellers_armor` (`IS_TRAVELLERS_GEAR`).

| Item | Slot | Modifier slots | Durability | Built-in components |
|---|---|---|---|---|
| `travellers_goggles` (`TravellersGogglesItem`) | head | 3 | helmet 11 x 12 = 132 | `zoom_ability_modifier` 0.3 (builtin ability `zoom`) |
| `travellers_vest` | chest | 3 | chest 16 x 12 = 192 | `has_travellers_chestplate`, `swift_swim` (+1 water movement efficiency) |
| `travellers_gloves` | chest | 0 | - (stack 1) | `has_travellers_gloves`; tooltip `item.gloamingforest.travellers_gloves.desc` |
| `travellers_wings` (`TravellersArmorBeltItem`) | legs | 3 | legs 15 x 12 = 180 | `has_travellers_wings`, `high_jump_amplifier` 1 (builtin ability `high_jump`) |
| `travellers_belt` (`TravellersArmorBeltItem`) | legs | 0 | - (stack 1) | `swap_hotbar_ability`, `minecraft:container` (9 empty), `has_travellers_belt` |
| `travellers_boots` | feet | 3 | boots 13 x 12 = 156 | `has_travellers_boots`, `high_step` (+0.5 step height) |

- Never break: armor damage that would break a piece is clamped to leave 1 durability; reaching the last point plays
  `entity.item.break` to the wearer. At max damage - 1 the piece is **broken**: name gets `travellers_gear.broken`,
  its attribute modifiers are moved to `stored_broken_attributes` and cleared (restored when repaired), and every
  modifier except `auto_repair` is inactive.
- Not enchantable at a table (`isEnchantable` false) and, in survival, not on an anvil either: `isBookEnchantable`
  true lets a book pass the first check, but `supportsEnchantment` false for every enchantment makes the anvil drop
  each book enchantment (only creative players, for whom every enchantment counts as supported, can apply books);
  `isPrimaryItemFor` false; two travellers' pieces cannot be combined on an anvil. A grindstone with one piece
  that has insertable modifiers removes all of them (output = the piece without them; contents of `swap_hotbar` /
  `item_display` are given back to the player on take); two pieces or no modifiers -> refused.
- Goggles and wings make piglins neutral; boots walk on powder snow.
- Tooltip: each ability modifier as `travellers_gear.ability` (gold) with the modifier name; each insertable modifier
  as "- <name>" (gray), with Shift its description lines indented by `travellers_gear.info_indent`; empty slots
  "- `travellers_gear.modifier.empty`" (dark gray) up to the slot count; without Shift and if any description exists:
  `travellers_gear.shift_info` with "Shift" in yellow. Modifier names are `travellers_gear.modifier.gloamingforest.<id>`
  rendered through `TooltipStringInterpolator` (placeholders like keybind names).
- Armor model (`TravellersGearModels`): helmet goggles (outer layer texture switches to
  `textures/models/armor/travellers_layer_1_down.png` while zooming), chest model draws the body only with
  `has_travellers_chestplate` and the arms only with `has_travellers_gloves` (slim variant for slim skins), legs model
  `TravellersWingsModel` shows wings with `has_travellers_wings` and the belt with `has_travellers_belt` or the
  `swap_hotbar` modifier and animates per wing state (U12), boots model. First-person: with
  `firstPersonGloveOverlay` (default true) gloves are drawn on the first-person arm (`travellers_layer_1.png`) unless the
  chest has the emperor's cloth.
- Recipes (Appendix A): shaped crafting of the six items from tanned leather and other materials, `travellers_vest_gloves_merge_recipe` (exactly a vest without gloves + gloves -> the vest with
  `has_travellers_gloves`), and the modifier recipes `gloamingforest:add_modifier_to_travellers_gear/<item>/<modifier>_modifier`
  (shaped or shapeless JSON with `modifier_key`, `pattern` / `ingredients`, `is_rotated`; `auto_repair` has a rotated
  variant per item). A modifier recipe matches when the gear has a free modifier slot, lacks that modifier, and at most
  one ingredient carries the transferable component; the result is a copy of the gear with the modifier added (or
  transferred, e.g. the belt's container for `swap_hotbar`). Crafting a piece with new modifiers fires
  `gloamingforest:add_modifier` per new modifier (`ItemCraftedEvent`).
- Command (`TravellersGearCommand`), upstream's literals below `/gloamingforest`:
  `/gloamingforest travellers_gear add_modifier <modifier>` and `/gloamingforest travellers_gear remove_modifier
  <modifier>`. Permission 2 (`LEVEL_GAMEMASTERS`, on the `travellers_gear` literal); `travellers_gear` alone or a
  child without `<modifier>` is incomplete (no executor). `<modifier>` is a resource location naming a key of the
  `gloamingforest:travellers_modifiers` registry (`ResourceKeyArgument`, suggestions = the registry's keys; a
  malformed id is a parse error). Both act on the executing player's main-hand stack; checks in this order, the
  first failure is the command error:
  - both: the key is not in the registry -> `commands.tffeature.invalid_modifier` (the id) — checked first, by
    `ResourceKeyArgument.resolveKey` in the `executes` lambda before `validate()`, so the console naming an unknown id
    gets `invalid_modifier`, not `not_player`; the source is not a player, or is a fake player ->
    `commands.tffeature.not_player`; the main-hand item is not `TravellersModifiable` (the six items of 3.1) ->
    `not_travellers_gear`; the modifier is an ability (`builtin` type, or `attribute` with `builtin_modifier`:
    `zoom`, `swift_swim`, `swap_hotbar_ability`, `high_jump`, `step_up`) -> `ability_modifier`;
  - `add_modifier`: insertable modifiers on the stack >= the item's modifier slots (always for gloves and belt, 0
    slots) -> `too_many_modifiers`; the stack already has it -> `has_modifier` (modifier name); the item's equipment
    slot (`getEquipmentSlotForItem`) is not in the modifier's slot group -> `wrong_modifier_slot` (modifier name);
    then the modifier is added with its default value (`component`: the registry default; `transferable_component`
    `swap_hotbar`: the marker + an empty 9-slot `minecraft:container`; `attribute` `aquatic_agility`: the marker +
    its attribute entries) and `commands.tffeature.added_modifier` (modifier name, stack hover name) is sent to the
    source and broadcast to operators;
  - `remove_modifier`: the stack lacks it -> `no_modifier` (modifier name); otherwise its component(s) are removed
    (`aquatic_agility`: the marker and its attribute entries; `swap_hotbar`: the marker and the `minecraft:container`;
    the items stored by `swap_hotbar` / `item_display` are deleted, not given back as by the grindstone) and
    `commands.tffeature.removed_modifier` (modifier name, stack hover name) is sent and broadcast.
  The modifier name (`TravellersModifiersManager.getModifierTooltipComponent`, built only after the four common
  checks) is **a literal rendered on the server**, not a translatable component: `TooltipStringInterpolator.render`
  translates `travellers_gear.modifier.gloamingforest.<id>` with the server's language (`Component.translatable(key)
  .getString()`: the host's selected language on an integrated server; en_us on a dedicated server, where NeoForge's
  `LanguageHook` loads the mods' en_us), replaces every `${tfkeybinds/<key name>}` with that key mapping's current
  binding name (`KeyModifier.getCombinedName`, e.g. `C`; an unbound key gives `key.keyboard.unknown`, "Not Bound"; a
  key name upstream does not register gives "Nonexistent key") and wraps the result in `Component.literal`. The same
  literal goes to the source and into the operator broadcast; the stack name stays a translatable hover name. Three
  insertable modifiers have a placeholder in their name: `item_display` (`key.gloamingforest.item_display_map_cycle`),
  `red_thread_vision` and `swap_hotbar` (`zoom` and `swap_hotbar_ability` have one too but are rejected as abilities
  before the name is built). On a dedicated server upstream's placeholder lookup loads `TFKeyBinds`, whose
  `net.minecraft.client.KeyMapping` does not exist there (not in the 1.21.1 server jar), so for these three upstream ends
  in a `NoClassDefFoundError`, an `Error` that `Commands.performCommand` (it catches `Exception`) does not turn into a
  command failure. Neither path checks the broken state or fires `gloamingforest:add_modifier` (only crafting does).
  The ten `commands.tffeature.*` keys are F6's in owners.tsv (the partition of upstream's `commands.tffeature.*` keys), so
  U11 uses them with `tr(key, fallback)` and these English texts: `not_player` "This command must be run by a valid
  player!", `not_travellers_gear` "You are not holding a piece of Traveller's Gear", `ability_modifier` "Abilities
  cannot be added or removed from Traveller's Gear", `too_many_modifiers` "This piece of Traveller's Gear already
  has the maximum amount of modifiers", `has_modifier` "This piece of Traveller's Gear already has %s",
  `wrong_modifier_slot` "%s is not allowed on this piece of Traveller's Gear", `no_modifier` "%s is not applied to
  this piece of Traveller's Gear", `invalid_modifier` "%s is not a valid Traveller's Gear Modifier", `added_modifier`
  "Added %s to %s!", `removed_modifier` "Removed %s from %s!".

### 3.2 Modifier framework

Modifier JSON types: `component` (insertable: sets one data component with a default value; active when the
component is present), `builtin` (ability: present from the item's own components, cannot be added/removed),
`attribute` (`TravellersEntryModifier`: a component plus attribute modifier entries applied while worn),
`transferable_component` (marker component plus a component copied from one ingredient). A modifier is **active** when its slot group's single armor slot holds a stack
with the modifier, the wearer is not a spectator, and the stack is not broken (except `auto_repair`, always active).
Missing registry entries are warned once and treated as absent.

| Modifier (U11) | Slot | Type / component (default) | Effect |
|---|---|---|---|
| `auto_repair` | any armor | `auto_repair_probability` 0.001 | server tick, only when **more than** 200 ticks have passed since the last armor damage (`gameTime - last_damage_armor_time <= 200` returns; U12 attachment): each piece with the modifier repairs 1 durability when `chance > nextFloat()`. Chance (`getAutoRepairChance`): not under open sky (`canSeeSky` at the block position) -> the base probability; under open sky: in the Gloaming dimension (dimension type) **always** at-least-one-success in 1.5 tries (`1 - (1 - p)^1.5`, any time of day); in other dimensions at-least-one-success in 3 tries only while `isDay()`, otherwise the base probability |
| `zoom` | head | builtin `zoom_ability_modifier` (0.3) | while the zoom key is held (not scoping): FOV x 0.3, mouse sensitivity x (0.5 - 1/(6s)) / (0.3 + 0.05); sounds `item.gloamingforest.travellers_goggles.zoom_in` / `zoom_out`; state synced with `GogglesZoomPacket` to the server and trackers (texture flip) |
| `red_thread_vision` | head | `red_thread_vision` (unit) | the key toggles `travellers_goggles_red_thread_vision` (kept on death); red threads render full bright through the glow render type while active or while holding red thread |
| `all_night_goggles` | head | `all_night_goggles` (unit) | no phantom spawns for the wearer; endermen do not aggro on gaze (ender mask) |
| `item_display` | head | `item_display` ({chosen_map_slot 0, slots []}) | HUD of stored map / compass / clock / moon dial (3.3) |
| `aquatic_agility` | head | attribute | U12 (`tgmove.md`) |
| `stealth` | chest | `stealth_crouching` (unit) | while crouching: Invisibility (2 ticks, hidden) refreshed every tick; client hides the player immediately |
| `arrow_magnetism` | chest | `arrow_magnetism` (unit) | the wearer's arrows that hit a block within 200 ticks are returned to the inventory (if pickup allowed and not creative) and removed; non-player owners just lose the arrow |
| `efficient_eater` | chest | `efficient_eater` 2.0 | movement / jump food exhaustion divided by 2 |
| `perfect_dodge` | chest | `perfect_dodge_probability` 0.3 | a projectile hitting the wearer is cancelled with probability 0.3 (client always cancels the visual hit): sound `item.gloamingforest.travellers_gear.perfect_dodge` (1.5, voice pitch) at the hit point and 20 `gloamingforest:perfect_dodge` particles |
| `haste` | chest | `haste_amplifier` 1 | Haste II (amplifier 1, 2 ticks, hidden) every tick |
| `swap_hotbar_ability` | legs | builtin (belt) | enables the swap key |
| `swap_hotbar` | legs | transferable: marker `swap_hotbar_modifier` + `minecraft:container` from a belt | adds the belt's 9-slot storage to the wings |

### 3.3 Item display (goggles)

- Storage: 6 slots laid out MAP, MAP, MAP, COMPASS, CLOCK, MOON_DIAL plus `chosen_map_slot`. Bundle-like inventory
  interaction (secondary click, stack size 1): putting an item in fills the first empty matching slot, else swaps the
  first matching slot (not identical stacks), the rest goes back; clicking with an empty cursor/slot removes the
  first stored item. Sounds `item.bundle.insert` / `remove_one` (0.8, 0.8 + rand*0.4). Tooltip image shows the slots.
  Valid: any map item, compass, clock, `gloamingforest:moon_dial`. Tooltip image (`ItemDisplayTooltipComponent`):
  vanilla bundle background sprite 6 x 18 + 2 = 110 wide, 22 high, slots (bundle slot sprite 18x20) at x + 18i + 1,
  y + 1; empty slots show the type's placeholder 16x16 texture (`textures/item/map_display.png`,
  `compass_display.png`, `clock_display.png`, `moon_dial_display.png`); height + 4.
- Server: while worn on the head with the modifier active, the active map ticks as if held and its update packet is
  sent to the wearer (maps keep updating).
- Cycle key (default C) sends `CycleMapSlotPacket`; the server moves `chosen_map_slot` to the next filled map slot
  (or -1 = none) and plays `item.gloamingforest.travellers_gear.cycle_maps` / `cycle_maps_empty`.
- HUD (`ItemDisplayOverlay`, not in F3 or hidden GUI): translate by (`screenOffsetX`, `screenOffsetY`) = (4, 4),
  scale `screenScale` 1.0; widgets stacked vertically, sorted by display position (map TOP first), centred on the
  widest widget:
  - map: 100x102, map background checkerboard quad 100x100 at x = max(widest/2 - 50, 0), map rendered at scale
    0.7075 with offset -4.75;
  - compass: text = player position (short string), or with a lodestone in this dimension
    `travellers_gear.modifier.gloamingforest.item_display.compass.lodestone` (pos, Manhattan distance);
  - clock: in natural dimensions an 8x8 icon from `textures/gui/time.png` (frames sunrise 0-500/22000+, day
    501-12499, sunset 12500-13999, night 14000-21999) + the time HH:mm (24 h if `twentyFourHourFormat`, default from
    the locale) or hh:mm a, computed from (dayTime + 6000) % 24000 as a real 24 h day; else
    `...item_display.clock.unknown`;
  - moon dial: 8x8 icon from `textures/gui/moon.png` (phase k: column k%4, row k/4) + the moon phase text.

### 3.4 Belt hotbar swap

`travellers_belt` (and wings with `swap_hotbar`) store 9 stacks in `minecraft:container`. The swap key (default V)
sends `SwapHotbarPacket` when the legs piece has the ability/modifier and a container: for each hotbar slot whose item
can go in containers and is not in `#gloamingforest:travellers_belt_blacklisted` (no tag file in upstream 4.8: empty unless a datapack adds one), the hotbar stack and the stored
stack are exchanged (when the swap is inactive, e.g. broken, only into empty hotbar slots); sound
`item.gloamingforest.travellers_gear.swap_hotbar` if anything changed. Belts cannot go inside containers. Tooltip image
shows the 9 stored items (`TravellersBeltTooltipComponent`: bundle background 9 x 18 + 2 = 164 x 22, bundle slot
sprites 18x20 at x + 18i + 1, items at +1/+1 with decorations; height + 4).

### 3.5 Red thread (`RedThreadBlock`, `RedThreadBlockEntity`)

A `MultifaceBlock` (faces `north/east/south/west/up/down`, waterloggable like glow lichen), instabreak, map FIRE,
no collision / occlusion / terrain particles, push DESTROY, no spawning on it; replaceable only by red thread items
(adds a face), spreads with `MultifaceSpreader`. Rendered by `RedThreadRenderer` (model `RedThreadModel`): a centre
piece per face plus connections to faces of the same block, neighbouring threads on the same face and threads around
corners; full-bright glow render type while the viewer holds red thread or has red thread vision active, otherwise
`entityCutout` with world light.

## 4. Bedrock adaptation notes

- Modifiers live in item user data (`gear::add/remove`, `gear::modifiers`, `gear::level`), loaded from the imported
  `travellers_modifiers` JSON; the modifier recipes run as special crafting (`recipes::registerSpecialCrafting`) in the
  crafting table; vanilla clients see results via lore.
- Never-break and broken-state attribute removal: F9 `item` hooks (`armorTick`, durability change) and attribute
  components rebuilt server side.
- Keybinds (`client::keys::registerKey`) exist only with the client mod (deviation: keyboard defaults only); vanilla
  fallbacks: zoom and red thread vision toggled by sneaking + using the goggles item, swap hotbar by sneak-using the
  belt, map cycle by sneak-using the goggles (document in `docs/units/tgear.md`).
- Item display HUD, zoom FOV, glove overlay, red thread glow: client mod (`client::hud`, `camera::addFovModifier`,
  `render`); vanilla clients get a form-based view of the stored items and no zoom.
- Perfect dodge / arrow magnetism: `combat::onIncoming` + projectile hit handlers (`projectile` contract hooks for
  vanilla arrows).
- `/gloamingforest travellers_gear` messages: render the modifier name once on the server, as upstream does, in the
  server's language (`tr("travellers_gear.modifier.gloamingforest.<id>", English name)`; not the source player's
  language: upstream's integrated server uses the host's language for every source, its dedicated server en_us), expand
  each `${tfkeybinds/<key name>}` the way `TooltipStringInterpolator` does, and send that same text to the source and
  in the operator broadcast (the stack name is the item's own name). The client build in a local world (upstream's
  integrated server) uses the host's language and puts in the host's current binding of that key (`client::keys`,
  "Not Bound" when unbound). The server build (BDS) has no player key bindings and must not fail as upstream's dedicated
  server does: it puts in upstream's
  default binding (`C` for `item_display_map_cycle`, `V` for `swap_hotbar`, "Not Bound" for `red_thread_vision`), a
  deviation from an upstream failure that U11 records in `docs/units/tgear.md` for A8's deviation list.
- Stealth: hidden Invisibility effect works on vanilla clients.
- Red thread: multiface block family; the glow is client-mod rendering, vanilla shows the normal texture.

## 5. Acceptance checklist (selftests `tgear.*`, dx 1100-1199)

- Six items with upstream defense, slots and durability; never-break clamp; broken state disables modifiers except auto
  repair; anvil/grindstone rules.
- Every modifier recipe adds its modifier (respecting free slots and duplicates) and fires `add_modifier`;
  vest + gloves merge; `/gloamingforest travellers_gear add_modifier` / `remove_modifier` errors (in upstream's check
  order) and success messages.
- Auto repair probability with sky boost (Gloaming: 1.5 tries day and night; elsewhere 3 tries by day only), none
  within 200 ticks of armor damage; haste, stealth, perfect dodge (seeded 0.3), arrow magnetism, efficient
  eater halving exhaustion, all-night goggles blocking phantoms.
- Item display insert/swap/remove rules, map cycling sounds, HUD layout screenshot (client mod).
- Belt swap exchanges hotbar and storage, respects the blacklist.
- Red thread placement/faces; glow while holding thread (screenshot).

## Appendix A: Owned ids (generated inventory)

Every id this unit owns in `docs/spec/owners.tsv`, with the upstream 4.8 data the W1 worker must reproduce. Registration statements are upstream's own Java (`init/upstream*.java`), condensed (namespaces such as `MapColor.` dropped); loot, recipe, tag, advancement and data rows are summarised from upstream's generated JSON (`src/generated/resources`). Loot notation: `self` = drops the block itself when it survives the explosion; `alternatives(a → b)` = first matching child; `[if ...]` = conditions; `{...}` = functions. In summaries `mc:` is `minecraft:` and ids without a namespace are `gloamingforest:`.

| Kind | Count |
|---|---|
| `advancement` | 6 |
| `armor_material` | 1 |
| `attachment` | 2 |
| `block` | 1 |
| `block_entity` | 1 |
| `block_item` | 1 |
| `config_key` | 5 |
| `config_section` | 1 |
| `data_component` | 19 |
| `e2e` | 1 |
| `item` | 6 |
| `keybind` | 4 |
| `lang` | 84 |
| `loot_table` | 1 |
| `net` | 4 |
| `particle` | 1 |
| `path` | 20 |
| `recipe` | 33 |
| `recipe_serializer` | 3 |
| `registry_entry` | 12 |
| `selftest` | 1 |
| `selftest_dx` | 1 |
| `sound_event` | 9 |
| `subcommand` | 3 |
| `tag` | 1 |
| `trigger` | 1 |
| `worlddb` | 1 |

### Unit facts

| Kind | Ids |
|---|---|
| Selftest glob (`selftest`) | `tgear.*` |
| Selftest dx range (`selftest_dx`) | `1100-1199` |
| World DB keys (`worlddb`) | `gf:tgear:*` |
| E2E scenario (`e2e`) | `tgear` |
| Net channels (`net`) | `tgear.*`, `tgear.cycle_map_slot`, `tgear.goggles_zoom`, `tgear.swap_hotbar` |
| Config section (`config_section`) | `tgear` |
| Owned paths (`path`) | `**/*.d/tgear.json`, `assets/data/*/tgear.json`, `assets/gen/audio/tgear/**`, `assets/gen/geo/tgear/**`, `assets/gen/import/tgear/**`, `assets/lang/*/tgear.json`, `assets/pack/{bp,rp}/**/tgear/**`, `assets/pack/rp/texts.d/*/tgear.lang`, `assets/structures/tgear/**`, `assets/tags/**/*.d/tgear.json`, `docs/units/tgear.md`, `src/client/tgear/**`, `src/core/tgear/**`, `src/feature/ext/tgear/**`, `src/feature/tgear/**`, `tests/tgear/**`, `tools/e2e/*/scenarios/tgear{,.*}.ps1`, `tools/import/overrides/tgear.json`, `tools/sounds/recipes/tgear.json`, `tools/structures/tgear/**` |

### Blocks (1)

Item column: `BlockItem` = upstream registers a plain block item with the block (`TFBlocks.register`), the id is also a `block_item` row; `-` = no item of the same id (the block is placed by another item, or has none).

Block states come from upstream's blockstate JSON (`multipart:` lists only the property values the model parts test). Notes collect `RegistrationEvents` (fire, axe stripping, flower pots, jar lids), dispenser behaviours and the `neoforge:compostables` / `neoforge:furnace_fuels` data maps.

| Block | English name | upstream registration (`TFBlocks`) | Block states | Item | Loot (`blocks/<id>`) | Notes |
|---|---|---|---|---|---|---|
| `gloamingforest:red_thread` | Red Thread | `register("red_thread", new RedThreadBlock(Properties.of().instabreak().mapColor(FIRE).isValidSpawn(TFBlocks::noSpawning).noCollission().noOcclusion().noTerrainParticles().pushReaction(DESTROY)));` |  | BlockItem | rolls 1: red_thread {count 1 (add) [if state east=true]; count 1 (add) [if state west=true]; count 1 (add) [if state north=true]; count 1 (add) [if state south=true]; count 1 (add) [if state up=true]; count 1 (add) [if state down=true]; count -1 (add); explosion_decay} |  |

### Items (6)

| Item | English name | upstream registration (`TFItems`) | Notes |
|---|---|---|---|
| `gloamingforest:travellers_belt` | Traveller's Belt | `ITEMS.register("travellers_belt", new TravellersArmorBeltItem(ArmorItem.Type.LEGGINGS, TravellersArmorBeltItem.beltProperties(new Item.Properties().stacksTo(1)), 0));` | item tags: #mc:leg_armor |
| `gloamingforest:travellers_boots` | Traveller's Boots | `ITEMS.register("travellers_boots", new TravellersArmorItem(ArmorItem.Type.BOOTS, TravellersArmorItem.bootsProperties(new Item.Properties()), 3, 12));` | item tags: #mc:foot_armor, #mc:freeze_immune_wearables |
| `gloamingforest:travellers_gloves` | Traveller's Gloves | `ITEMS.register("travellers_gloves", new TravellersArmorItem(ArmorItem.Type.CHESTPLATE, TravellersArmorItem.glovesProperties(new Item.Properties().stacksTo(1)), 0));` | item tags: #mc:chest_armor |
| `gloamingforest:travellers_goggles` | Traveller's Goggles | `ITEMS.register("travellers_goggles", new TravellersGogglesItem(TravellersArmorItem.gogglesProperties(new Item.Properties())));` | item tags: #mc:head_armor |
| `gloamingforest:travellers_vest` | Traveller's Vest | `ITEMS.register("travellers_vest", new TravellersArmorItem(ArmorItem.Type.CHESTPLATE, TravellersArmorItem.chestProperties(new Item.Properties()), 3, 12));` | item tags: #mc:chest_armor, #mc:freeze_immune_wearables |
| `gloamingforest:travellers_wings` | Traveller's Wings | `ITEMS.register("travellers_wings", new TravellersArmorBeltItem(ArmorItem.Type.LEGGINGS, TravellersArmorItem.wingsProperties(new Item.Properties()), 3, 12));` | item tags: #mc:leg_armor |

### Block entity types (1)

| Id | upstream registration (`TFBlockEntities.java`) |
|---|---|
| `gloamingforest:red_thread` | `BLOCK_ENTITIES.register("red_thread", () -> BlockEntityType.Builder.of(RedThreadBlockEntity::new, TFBlocks.RED_THREAD.get()).build(null));` |

### Particle types (1)

| Id | upstream registration (`TFParticleType.java`) |
|---|---|
| `gloamingforest:perfect_dodge` | `PARTICLE_TYPES.register("perfect_dodge", new SimpleParticleType(false));` |

### Data components (19)

| Id | upstream registration (`TFDataComponents.java`) |
|---|---|
| `gloamingforest:all_night_goggles` | `COMPONENTS.register("all_night_goggles", () -> DataComponentType.<Unit>builder().persistent(Codec.unit(Unit.INSTANCE)).networkSynchronized(StreamCodec.unit(Unit.INSTANCE)).cacheEncoding().build());` |
| `gloamingforest:arrow_magnetism` | `COMPONENTS.register("arrow_magnetism", () -> DataComponentType.<Unit>builder().persistent(Codec.unit(Unit.INSTANCE)).networkSynchronized(StreamCodec.unit(Unit.INSTANCE)).cacheEncoding().build());` |
| `gloamingforest:auto_repair_probability` | `COMPONENTS.register("auto_repair_probability", () -> DataComponentType.<Float>builder().persistent(ExtraCodecs.POSITIVE_FLOAT).networkSynchronized(ByteBufCodecs.FLOAT).cacheEncoding().build());` |
| `gloamingforest:efficient_eater` | `COMPONENTS.register("efficient_eater", () -> DataComponentType.<Float>builder().persistent(Codec.FLOAT).networkSynchronized(ByteBufCodecs.FLOAT).cacheEncoding().build());` |
| `gloamingforest:has_travellers_belt` | `COMPONENTS.register("has_travellers_belt", () -> DataComponentType.<Unit>builder().persistent(Codec.unit(Unit.INSTANCE)).networkSynchronized(StreamCodec.unit(Unit.INSTANCE)).cacheEncoding().build());` |
| `gloamingforest:has_travellers_boots` | `COMPONENTS.register("has_travellers_boots", () -> DataComponentType.<Unit>builder().persistent(Codec.unit(Unit.INSTANCE)).networkSynchronized(StreamCodec.unit(Unit.INSTANCE)).cacheEncoding().build());` |
| `gloamingforest:has_travellers_chestplate` | `COMPONENTS.register("has_travellers_chestplate", () -> DataComponentType.<Unit>builder().persistent(Codec.unit(Unit.INSTANCE)).networkSynchronized(StreamCodec.unit(Unit.INSTANCE)).cacheEncoding().build());` |
| `gloamingforest:has_travellers_gloves` | `COMPONENTS.register("has_travellers_gloves", () -> DataComponentType.<Unit>builder().persistent(Codec.unit(Unit.INSTANCE)).networkSynchronized(StreamCodec.unit(Unit.INSTANCE)).cacheEncoding().build());` |
| `gloamingforest:has_travellers_wings` | `COMPONENTS.register("has_travellers_wings", () -> DataComponentType.<Unit>builder().persistent(Codec.unit(Unit.INSTANCE)).networkSynchronized(StreamCodec.unit(Unit.INSTANCE)).cacheEncoding().build());` |
| `gloamingforest:haste_amplifier` | `COMPONENTS.register("haste_amplifier", () -> DataComponentType.<Integer>builder().persistent(ExtraCodecs.UNSIGNED_BYTE).networkSynchronized(ByteBufCodecs.INT).cacheEncoding().build());` |
| `gloamingforest:item_display` | `COMPONENTS.register("item_display", () -> DataComponentType.<ItemDisplayContents>builder().persistent(ItemDisplayContents.CODEC).networkSynchronized(ItemDisplayContents.STREAM_CODEC).cacheEncoding().build());` |
| `gloamingforest:perfect_dodge_probability` | `COMPONENTS.register("perfect_dodge_probability", () -> DataComponentType.<Float>builder().persistent(ExtraCodecs.POSITIVE_FLOAT).networkSynchronized(ByteBufCodecs.FLOAT).cacheEncoding().build());` |
| `gloamingforest:red_thread_vision` | `COMPONENTS.register("red_thread_vision", () -> DataComponentType.<Unit>builder().persistent(Codec.unit(Unit.INSTANCE)).networkSynchronized(StreamCodec.unit(Unit.INSTANCE)).cacheEncoding().build());` |
| `gloamingforest:stealth_crouching` | `COMPONENTS.register("stealth_crouching", () -> DataComponentType.<Unit>builder().persistent(Codec.unit(Unit.INSTANCE)).networkSynchronized(StreamCodec.unit(Unit.INSTANCE)).cacheEncoding().build());` |
| `gloamingforest:stored_broken_attributes` | `COMPONENTS.register("stored_broken_attributes", () -> DataComponentType.<ItemAttributeModifiers>builder().persistent(ItemAttributeModifiers.CODEC).networkSynchronized(ItemAttributeModifiers.STREAM_CODEC).cacheEncoding().build());` |
| `gloamingforest:swap_hotbar_ability` | `COMPONENTS.register("swap_hotbar_ability", () -> DataComponentType.<Unit>builder().persistent(Codec.unit(Unit.INSTANCE)).networkSynchronized(StreamCodec.unit(Unit.INSTANCE)).cacheEncoding().build());` |
| `gloamingforest:swap_hotbar_modifier` | `COMPONENTS.register("swap_hotbar_modifier", () -> DataComponentType.<Unit>builder().persistent(Codec.unit(Unit.INSTANCE)).networkSynchronized(StreamCodec.unit(Unit.INSTANCE)).cacheEncoding().build());` |
| `gloamingforest:travellers_armor` | `COMPONENTS.register("travellers_armor", () -> DataComponentType.<Unit>builder().persistent(Codec.unit(Unit.INSTANCE)).networkSynchronized(StreamCodec.unit(Unit.INSTANCE)).cacheEncoding().build());` |
| `gloamingforest:zoom_ability_modifier` | `COMPONENTS.register("zoom_ability_modifier", () -> DataComponentType.<Float>builder().persistent(ExtraCodecs.POSITIVE_FLOAT).networkSynchronized(ByteBufCodecs.FLOAT).cacheEncoding().build());` |

### Data attachments (2)

| Id | upstream registration (`TFDataAttachments.java`) |
|---|---|
| `gloamingforest:is_using_goggles_zoom_modifier` | `ATTACHMENT_TYPES.register("is_using_goggles_zoom_modifier", () -> AttachmentType.builder(() -> false).serialize(Codec.BOOL).sync(ByteBufCodecs.BOOL).build());` |
| `gloamingforest:travellers_goggles_red_thread_vision` | `ATTACHMENT_TYPES.register("travellers_goggles_red_thread_vision", () -> AttachmentType.builder(() -> true).serialize(Codec.BOOL).build());` |

### Armor materials (1)

| Id | upstream registration (`TFArmorMaterials.java`) |
|---|---|
| `gloamingforest:travellers` | `ARMOR_MATERIALS.register("travellers", new ArmorMaterial(Util.make(new EnumMap<>(ArmorItem.Type.class), map -> { map.put(ArmorItem.Type.BOOTS, 2); map.put(ArmorItem.Type.LEGGINGS, 3); map.put(ArmorItem.Type.CHESTPLATE, 4); map.put(ArmorItem.Type.HELMET, 2); map.put(ArmorItem.Type.BODY, 4); }), 0, SoundEvents.ARMOR_EQUIP_GENERIC, () -> Ingredient.of(TFItems.TANNED_LEATHER), List.of(new ArmorMaterial.Layer(GloamingForestMod.prefix("travellers"))), 0F, 0.0F)); ;; }), 0, SoundEvents.ARMOR_EQUIP_GENERIC, () -> Ingredient.of(TFItems.TANNED_LEATHER), List.of(new ArmorMaterial.Layer(GloamingForestMod.prefix("travellers"))), 0F, 0.0F));` |

### Recipe serializers / types (3)

| Id | upstream registration (`TFRecipes.java`) |
|---|---|
| `gloamingforest:travellers_gear_modifier_shaped_recipe` | `RECIPE_SERIALIZERS.register("travellers_gear_modifier_shaped_recipe", TravellersGearModifierShapedRecipe.Serializer::new);` |
| `gloamingforest:travellers_gear_modifier_shapeless_recipe` | `RECIPE_SERIALIZERS.register("travellers_gear_modifier_shapeless_recipe", TravellersGearModifierShapelessRecipe.Serializer::new);` |
| `gloamingforest:travellers_vest_gloves_merge_recipe` | `RECIPE_SERIALIZERS.register("travellers_vest_gloves_merge_recipe", new SimpleCraftingRecipeSerializer<>(TravellersVestGlovesMergeRecipe::new));` |

### Advancement triggers (1)

| Id | upstream registration (`TFAdvancements.java`) |
|---|---|
| `gloamingforest:add_modifier` | `TRIGGERS.register("add_modifier", AddModifierTrigger::new);` |

### Key bindings (4)

| Id | upstream registration (`TFKeyBinds.java`) |
|---|---|
| `gloamingforest:item_display_map_cycle` | `register("item_display_map_cycle", InputConstants.Type.KEYSYM, GLFW.GLFW_KEY_C, TFKeyBindsCategories.TRAVELLERS_GEAR);` |
| `gloamingforest:red_thread_vision` | `register("red_thread_vision", InputConstants.Type.KEYSYM, GLFW.GLFW_KEY_UNKNOWN, TFKeyBindsCategories.TRAVELLERS_GEAR);` |
| `gloamingforest:swap_hotbar` | `register("swap_hotbar", InputConstants.Type.KEYSYM, GLFW.GLFW_KEY_V, TFKeyBindsCategories.TRAVELLERS_GEAR);` |
| `gloamingforest:zoom` | `register("zoom", InputConstants.Type.KEYSYM, GLFW.GLFW_KEY_Z, TFKeyBindsCategories.TRAVELLERS_GEAR);` |

### Registry entries (12)

- `travellers_modifiers/gloamingforest:all_night_goggles`: `{"type":"gloamingforest:component","component":{"gloamingforest:all_night_goggles":{}},"description":[{"translate":"travellers_gear.modifier.gloamingforest.all_night_goggles.description"}],"equipment_slots":"head"}`
- `travellers_modifiers/gloamingforest:arrow_magnetism`: `{"type":"gloamingforest:component","component":{"gloamingforest:arrow_magnetism":{}},"description":[{"translate":"travellers_gear.modifier.gloamingforest.arrow_magnetism.description"}],"equipment_slots":"chest"}`
- `travellers_modifiers/gloamingforest:auto_repair`: `{"type":"gloamingforest:component","component":{"gloamingforest:auto_repair_probability":0.001},"description":[{"translate":"travellers_gear.modifier.gloamingforest.auto_repair.description"}],"equipment_slots":"armor"}`
- `travellers_modifiers/gloamingforest:efficient_eater`: `{"type":"gloamingforest:component","component":{"gloamingforest:efficient_eater":2.0},"description":[{"translate":"travellers_gear.modifier.gloamingforest.efficient_eater.description"}],"equipment_slots":"chest"}`
- `travellers_modifiers/gloamingforest:haste`: `{"type":"gloamingforest:component","component":{"gloamingforest:haste_amplifier":1},"description":[{"translate":"travellers_gear.modifier.gloamingforest.haste.description"}],"equipment_slots":"chest"}`
- `travellers_modifiers/gloamingforest:item_display`: `{"type":"gloamingforest:component","component":{"gloamingforest:item_display":{"chosen_map_slot":0,"slots":[]}},"description":[{"translate":"travellers_gear.modifier.gloamingforest.item_display.description"}],"equipment_slots":"head"}`
- `travellers_modifiers/gloamingforest:perfect_dodge`: `{"type":"gloamingforest:component","component":{"gloamingforest:perfect_dodge_probability":0.3},"description":[{"translate":"travellers_gear.modifier.gloamingforest.perfect_dodge.description"}],"equipment_slots":"chest"}`
- `travellers_modifiers/gloamingforest:red_thread_vision`: `{"type":"gloamingforest:component","component":{"gloamingforest:red_thread_vision":{}},"description":[{"translate":"travellers_gear.modifier.gloamingforest.red_thread_vision.description"}],"equipment_slots":"head"}`
- `travellers_modifiers/gloamingforest:stealth`: `{"type":"gloamingforest:component","component":{"gloamingforest:stealth_crouching":{}},"description":[{"translate":"travellers_gear.modifier.gloamingforest.stealth.description"}],"equipment_slots":"chest"}`
- `travellers_modifiers/gloamingforest:swap_hotbar`: `{"type":"gloamingforest:transferable_component","component":"gloamingforest:swap_hotbar_modifier","description":[{"translate":"travellers_gear.modifier.gloamingforest.swap_hotbar.description"}],"equipment_slots":"legs","transferable_components":{"minecraft:container":[]}}`
- `travellers_modifiers/gloamingforest:swap_hotbar_ability`: `{"type":"gloamingforest:builtin","component":"gloamingforest:swap_hotbar_ability","equipment_slots":"legs"}`
- `travellers_modifiers/gloamingforest:zoom`: `{"type":"gloamingforest:builtin","component":"gloamingforest:zoom_ability_modifier","equipment_slots":"head"}`

### Sound events (9)

`sounds.json` content (vanilla `event` references can be played with the matching Bedrock vanilla sound; upstream file names are listed only as names: the audio itself is All Rights Reserved and is re-synthesised by F3/the owner).

| Event | Subtitle | sounds.json entries |
|---|---|---|
| `item.gloamingforest.travellers_gear.cycle_maps` | subtitles.gloamingforest.item.travellers_gear.cycle_maps = Map swaps | item/book/open_flip1 |
| `item.gloamingforest.travellers_gear.cycle_maps_empty` | subtitles.gloamingforest.item.travellers_gear.cycle_maps_empty = Map swaps | item/book/open_flip2 |
| `item.gloamingforest.travellers_gear.double_jump` | subtitles.gloamingforest.item.travellers_gear.double_jump = Double Jump performed | gloamingforest:random/travellers/double_jump |
| `item.gloamingforest.travellers_gear.perfect_dodge` | subtitles.gloamingforest.item.travellers_gear.perfect_dodge = Attack dodged | gloamingforest:random/travellers/perfect_dodge |
| `item.gloamingforest.travellers_gear.side_step` | subtitles.gloamingforest.item.travellers_gear.side_step = Side Step performed | gloamingforest:random/travellers/side_step |
| `item.gloamingforest.travellers_gear.side_step_ready` | subtitles.gloamingforest.item.travellers_gear.side_step_ready = Side Step recharged | gloamingforest:random/travellers/side_step_ready |
| `item.gloamingforest.travellers_gear.swap_hotbar` | subtitles.gloamingforest.item.travellers_gear.swap_hotbar = Belt rustles | event item.armor.equip_leather |
| `item.gloamingforest.travellers_goggles.zoom_in` | subtitles.gloamingforest.item.travellers_goggles.zoom_in = Traveller's Goggles zoom in | event item.spyglass.use |
| `item.gloamingforest.travellers_goggles.zoom_out` | subtitles.gloamingforest.item.travellers_goggles.zoom_out = Traveller's Goggles zoom out | event item.spyglass.stop_using |

### Tags (1)

| Tag | Members (upstream generated JSON) |
|---|---|
| `item/#gloamingforest:travellers_agile_ranger_whitelisted` | moonworm_queen |

### Loot tables (1)

Block loot tables summarised in the Loot column of the block table: `gloamingforest:blocks/red_thread`

### Recipes (33)

Recipe-unlock advancements (generated by F1 into the recipe JSON, R21) are listed with their recipe; criteria are the `has_*` triggers besides `has_the_recipe`.

| Recipe | Type | Ingredients | Result | Unlock advancement (criteria) |
|---|---|---|---|---|
| `gloamingforest:add_modifier_to_travellers_gear/travellers_boots/auto_repair_modifier` | travellers_gear_modifier_shaped_recipe | {"is_rotated":false,"modifier_key":"gloamingforest:auto_repair","pattern":{"key":{"E":{"item":"minecraft:experience_bottle"},"M":{"item":"gloamingforest:moss_patch"},"T":{"item":"gloamingforest:travellers_boots"}},"pattern":[" E ","MTM"," E "]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_boots/auto_repair_modifier_rotated` | travellers_gear_modifier_shaped_recipe | {"is_rotated":true,"modifier_key":"gloamingforest:auto_repair","pattern":{"key":{"E":{"item":"minecraft:experience_bottle"},"M":{"item":"gloamingforest:moss_patch"},"T":{"item":"gloamingforest:travellers_boots"}},"pattern":[" M ","ETE"," M "]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_boots/slimy_soles_modifier` | travellers_gear_modifier_shaped_recipe | {"is_rotated":false,"modifier_key":"gloamingforest:slimy_soles","pattern":{"key":{"B":{"item":"gloamingforest:travellers_boots"},"S":{"item":"minecraft:slime_block"},"W":{"tag":"c:strings"}},"pattern":["WBW","S S"]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_boots/straight_ahead_modifier` | travellers_gear_modifier_shaped_recipe | {"is_rotated":false,"modifier_key":"gloamingforest:straight_ahead","pattern":{"key":{"B":{"item":"gloamingforest:travellers_boots"},"H":{"item":"minecraft:rabbit_hide"},"M":{"item":"gloamingforest:raw_meef"},"P":[{"type":"neoforge:components","components":{"minecraft:potion_contents":{"potion":"minecraft:swiftness"}},"items":"minecraft:potion"},{"type":"neoforge:components","components":{"minecraft:potion_contents":{"potion":"minecraft:long_swiftness"}},"items":"minecraft:potion"},{"type":"neoforge:components","components":{"minecraft:potion_contents":{"potion":"minecraft:strong_swiftness"}},"items":"minecraft:potion"}]},"pattern":["MPM","HBH"]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_boots/unrestrained_modifier` | travellers_gear_modifier_shaped_recipe | {"is_rotated":false,"modifier_key":"gloamingforest:unrestrained","pattern":{"key":{"b":{"item":"gloamingforest:travellers_boots"},"i":{"tag":"c:ingots/iron"},"k":{"tag":"c:ingots/knightmetal"},"n":{"tag":"c:nuggets/iron"}},"pattern":["n n","kbk","i i"]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_boots/water_walk_modifier` | travellers_gear_modifier_shaped_recipe | {"is_rotated":false,"modifier_key":"gloamingforest:water_walk","pattern":{"key":{"B":{"item":"gloamingforest:travellers_boots"},"L":{"item":"minecraft:lily_pad"},"S":{"item":"gloamingforest:maze_slime_ball"}},"pattern":["SBS","L L"]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_goggles/all_night_goggles_modifier` | travellers_gear_modifier_shaped_recipe | {"is_rotated":false,"modifier_key":"gloamingforest:all_night_goggles","pattern":{"key":{"G":{"item":"gloamingforest:travellers_goggles"},"M":{"item":"minecraft:phantom_membrane"},"Y":{"item":"minecraft:yellow_stained_glass_pane"}},"pattern":["M M","YGY","M M"]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_goggles/aquatic_agility_modifier` | travellers_gear_modifier_shaped_recipe | {"is_rotated":false,"modifier_key":"gloamingforest:aquatic_agility","pattern":{"key":{"b":{"item":"minecraft:bamboo"},"g":{"item":"gloamingforest:travellers_goggles"},"s":{"tag":"c:slime_balls"},"w":[{"type":"neoforge:components","components":{"minecraft:potion_contents":{"potion":"minecraft:water_breathing"}},"items":"minecraft:potion"},{"type":"neoforge:components","components":{"minecraft:potion_contents":{"potion":"minecraft:long_water_breathing"}},"items":"minecraft:potion"}]},"pattern":[" b","gb","sw"]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_goggles/auto_repair_modifier` | travellers_gear_modifier_shaped_recipe | {"is_rotated":false,"modifier_key":"gloamingforest:auto_repair","pattern":{"key":{"E":{"item":"minecraft:experience_bottle"},"M":{"item":"gloamingforest:moss_patch"},"T":{"item":"gloamingforest:travellers_goggles"}},"pattern":[" E ","MTM"," E "]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_goggles/auto_repair_modifier_rotated` | travellers_gear_modifier_shaped_recipe | {"is_rotated":true,"modifier_key":"gloamingforest:auto_repair","pattern":{"key":{"E":{"item":"minecraft:experience_bottle"},"M":{"item":"gloamingforest:moss_patch"},"T":{"item":"gloamingforest:travellers_goggles"}},"pattern":[" M ","ETE"," M "]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_goggles/item_display_modifier` | travellers_gear_modifier_shaped_recipe | {"is_rotated":false,"modifier_key":"gloamingforest:item_display","pattern":{"key":{"g":{"item":"gloamingforest:travellers_goggles"},"i":{"item":"minecraft:item_frame"},"s":{"tag":"c:rods/wooden"}},"pattern":["sss","igi","sss"]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_goggles/red_thread_vision_modifier` | travellers_gear_modifier_shaped_recipe | {"is_rotated":false,"modifier_key":"gloamingforest:red_thread_vision","pattern":{"key":{"G":{"item":"gloamingforest:travellers_goggles"},"R":{"item":"gloamingforest:red_thread"}},"pattern":[" R ","RGR"]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_vest/arrow_magnetism_modifier` | travellers_gear_modifier_shapeless_recipe | {"ingredients":[{"item":"gloamingforest:exanimate_essence"},{"item":"gloamingforest:ore_magnet"},{"item":"gloamingforest:liveroot"},{"item":"minecraft:chicken"},{"item":"gloamingforest:travellers_vest"}],"modifier_key":"gloamingforest:arrow_magnetism"} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_vest/auto_repair_modifier` | travellers_gear_modifier_shaped_recipe | {"is_rotated":false,"modifier_key":"gloamingforest:auto_repair","pattern":{"key":{"E":{"item":"minecraft:experience_bottle"},"M":{"item":"gloamingforest:moss_patch"},"T":{"item":"gloamingforest:travellers_vest"}},"pattern":[" E ","MTM"," E "]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_vest/auto_repair_modifier_rotated` | travellers_gear_modifier_shaped_recipe | {"is_rotated":true,"modifier_key":"gloamingforest:auto_repair","pattern":{"key":{"E":{"item":"minecraft:experience_bottle"},"M":{"item":"gloamingforest:moss_patch"},"T":{"item":"gloamingforest:travellers_vest"}},"pattern":[" M ","ETE"," M "]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_vest/efficient_eater_modifier` | travellers_gear_modifier_shapeless_recipe | {"ingredients":[{"item":"gloamingforest:hydra_chop"},{"item":"gloamingforest:hydra_chop"},{"item":"gloamingforest:hydra_chop"},{"item":"minecraft:bundle"},{"item":"gloamingforest:travellers_vest"}],"modifier_key":"gloamingforest:efficient_eater"} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_vest/haste_modifier` | travellers_gear_modifier_shaped_recipe | {"is_rotated":false,"modifier_key":"gloamingforest:haste","pattern":{"key":{"O":{"item":"minecraft:obsidian"},"R":{"item":"minecraft:redstone_block"},"V":{"item":"gloamingforest:travellers_vest"}},"pattern":["RRR","RVR","OOO"]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_vest/perfect_dodge_modifier` | travellers_gear_modifier_shaped_recipe | {"is_rotated":false,"modifier_key":"gloamingforest:perfect_dodge","pattern":{"key":{"E":{"item":"minecraft:ender_eye"},"P":{"item":"minecraft:ender_pearl"},"S":{"item":"minecraft:sugar"},"V":{"item":"gloamingforest:travellers_vest"}},"pattern":[" E ","PVP"," S "]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_vest/stealth_modifier` | travellers_gear_modifier_shapeless_recipe | {"ingredients":[[{"type":"neoforge:components","components":{"minecraft:potion_contents":{"potion":"minecraft:invisibility"}},"items":"minecraft:potion"},{"type":"neoforge:components","components":{"minecraft:potion_contents":{"potion":"minecraft:long_invisibility"}},"items":"minecraft:potion"}],{"item":"minecraft:ender_eye"},{"item":"minecraft:spider_eye"},{"item":"minecraft:golden_carrot"},{"item":"gloamingforest:travellers_vest"}],"modifier_key":"gloamingforest:stealth"} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_wings/agile_ranger_modifier` | travellers_gear_modifier_shaped_recipe | {"is_rotated":false,"modifier_key":"gloamingforest:agile_ranger","pattern":{"key":{"F":{"item":"gloamingforest:raven_feather"},"H":{"item":"minecraft:rabbit_hide"},"P":[{"type":"neoforge:components","components":{"minecraft:potion_contents":{"potion":"minecraft:swiftness"}},"items":"minecraft:potion"},{"type":"neoforge:components","components":{"minecraft:potion_contents":{"potion":"minecraft:long_swiftness"}},"items":"minecraft:potion"},{"type":"neoforge:components","components":{"minecraft:potion_contents":{"potion":"minecraft:strong_swiftness"}},"items":"minecraft:potion"}],"W":{"item":"gloamingforest:travellers_wings"}},"pattern":["FPF","HWH"]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_wings/auto_repair_modifier` | travellers_gear_modifier_shaped_recipe | {"is_rotated":false,"modifier_key":"gloamingforest:auto_repair","pattern":{"key":{"E":{"item":"minecraft:experience_bottle"},"M":{"item":"gloamingforest:moss_patch"},"T":{"item":"gloamingforest:travellers_wings"}},"pattern":[" E ","MTM"," E "]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_wings/auto_repair_modifier_rotated` | travellers_gear_modifier_shaped_recipe | {"is_rotated":true,"modifier_key":"gloamingforest:auto_repair","pattern":{"key":{"E":{"item":"minecraft:experience_bottle"},"M":{"item":"gloamingforest:moss_patch"},"T":{"item":"gloamingforest:travellers_wings"}},"pattern":[" M ","ETE"," M "]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_wings/double_jump_modifier` | travellers_gear_modifier_shaped_recipe | {"is_rotated":false,"modifier_key":"gloamingforest:double_jump","pattern":{"key":{"L":[{"type":"neoforge:components","components":{"minecraft:potion_contents":{"potion":"minecraft:leaping"}},"items":"minecraft:potion"},{"type":"neoforge:components","components":{"minecraft:potion_contents":{"potion":"minecraft:long_leaping"}},"items":"minecraft:potion"},{"type":"neoforge:components","components":{"minecraft:potion_contents":{"potion":"minecraft:strong_leaping"}},"items":"minecraft:potion"}],"P":{"item":"minecraft:piston"},"S":{"tag":"c:strings"},"W":{"item":"gloamingforest:travellers_wings"}},"pattern":[" L ","SWS","P P"]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_wings/gradual_glide_modifier` | travellers_gear_modifier_shaped_recipe | {"is_rotated":false,"modifier_key":"gloamingforest:gradual_glide","pattern":{"key":{"C":{"item":"minecraft:cobweb"},"E":{"item":"gloamingforest:borer_essence"},"F":{"item":"minecraft:feather"},"W":{"item":"gloamingforest:travellers_wings"}},"pattern":["FCF","FWF","FEF"]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_wings/side_step_modifier` | travellers_gear_modifier_shaped_recipe | {"is_rotated":false,"modifier_key":"gloamingforest:side_step","pattern":{"key":{"B":{"tag":"c:bones"},"P":{"item":"minecraft:piston"},"S":{"tag":"c:strings"},"W":{"item":"gloamingforest:travellers_wings"}},"pattern":["SBS","PWP","SBS"]}} | - | - |
| `gloamingforest:add_modifier_to_travellers_gear/travellers_wings/swap_hotbar_modifier` | travellers_gear_modifier_shapeless_recipe | {"ingredients":[{"item":"gloamingforest:travellers_wings"},{"item":"gloamingforest:travellers_belt"}],"modifier_key":"gloamingforest:swap_hotbar"} | - | - |
| `gloamingforest:equipment/travellers_belt` | mc:crafting_shaped | 'lll' / 'ixi' / 'lll' ; i=#c:nuggets/iron, l=tanned_leather, x={"type":"neoforge:difference","base":{"tag":"c:chests/wooden"},"subtracted":{"tag":"c:chests/trapped"}} | travellers_belt | `gloamingforest:recipes/transportation/equipment/travellers_belt` (has_leather: has tanned_leather) |
| `gloamingforest:equipment/travellers_boots` | mc:crafting_shaped | 's s' / 'l l' / 'l l' ; l=tanned_leather, s=#c:strings | travellers_boots | `gloamingforest:recipes/transportation/equipment/travellers_boots` (has_leather: has tanned_leather) |
| `gloamingforest:equipment/travellers_gloves` | mc:crafting_shaped | 's s' / 'l l' ; l=tanned_leather, s=#c:strings | travellers_gloves | `gloamingforest:recipes/transportation/equipment/travellers_gloves` (has_leather: has tanned_leather) |
| `gloamingforest:equipment/travellers_goggles` | mc:crafting_shaped | 'l l' / 'plp' / 'g g' ; g=#c:ingots/gold, l=tanned_leather, p=#c:glass_panes/colorless | travellers_goggles | `gloamingforest:recipes/transportation/equipment/travellers_goggles` (has_leather: has tanned_leather) |
| `gloamingforest:equipment/travellers_vest` | mc:crafting_shaped | 'l l' / 'lwl' / 'lwl' ; l=tanned_leather, w=#mc:wool | travellers_vest | `gloamingforest:recipes/transportation/equipment/travellers_vest` (has_leather: has tanned_leather) |
| `gloamingforest:equipment/travellers_wings` | mc:crafting_shaped | 'g g' / 'ili' / 'ici' ; c=#c:gems/carminite, g=#c:ingots/gold, i=#c:ingots/copper, l=tanned_leather | travellers_wings | `gloamingforest:recipes/transportation/equipment/travellers_wings` (has_leather: has tanned_leather) |
| `gloamingforest:travellers_vest_gloves_merge_recipe` | travellers_vest_gloves_merge_recipe | {} | - | - |

### Config keys (5)

See [config.md](config.md) for the section mapping; defaults and translation ids below are upstream's.

| Upstream key | Default | Translation id | English (config screen) |
|---|---|---|---|
| `client.firstPersonGloveOverlay` | `true` | `first_person_glove_overlay` | First-Person Glove Overlay — Allows Traveller's Gloves to render on your hand when in 1st person view. |
| `client.Item Display Modifier Settings.screenOffsetX` | `4` | `screen_offset_x` | Display X Offset — Defines the starting Y offset for all display modifiers on the screen. |
| `client.Item Display Modifier Settings.screenOffsetY` | `4` | `screen_offset_y` | Display Y Offset — Defines the starting Y offset for all display modifiers on the screen. |
| `client.Item Display Modifier Settings.screenScale` | `1.0D` | `screen_scale` | Display Scale — Defines the scale of all display modifiers on the screen. |
| `client.Item Display Modifier Settings.twentyFourHourFormat` | `TFConfig.use24HourTimeDefault()` | `twenty_four_hour_format` | 24-Hour Format — If ON, the clock upgrade displays time in 24-hour format instead of 12-hour format. |

### subcommand (3)

`travellers_gear`, `travellers_gear add_modifier`, `travellers_gear remove_modifier`

## Appendix B: Strings (84 en_us keys owned)

Keys already shown above (block/item/entity names, subtitles) are not repeated. The 29 Bedrock locales are generated from upstream's lang files (R9); owners use these keys with the English text as fallback.

| Key | English |
|---|---|
| `config.gloamingforest.first_person_glove_overlay` | First-Person Glove Overlay |
| `config.gloamingforest.first_person_glove_overlay.tooltip` | Allows Traveller's Gloves to render on your hand when in 1st person view. |
| `config.gloamingforest.item_display` | Item Display Modifier Settings |
| `config.gloamingforest.item_display.tooltip` | Controls where various elements render when using the Item Display Modifier on Traveller's Gear. |
| `config.gloamingforest.screen_offset_x` | Display X Offset |
| `config.gloamingforest.screen_offset_x.tooltip` | Defines the starting Y offset for all display modifiers on the screen. |
| `config.gloamingforest.screen_offset_y` | Display Y Offset |
| `config.gloamingforest.screen_offset_y.tooltip` | Defines the starting Y offset for all display modifiers on the screen. |
| `config.gloamingforest.screen_scale` | Display Scale |
| `config.gloamingforest.screen_scale.tooltip` | Defines the scale of all display modifiers on the screen. |
| `config.gloamingforest.twenty_four_hour_format` | 24-Hour Format |
| `config.gloamingforest.twenty_four_hour_format.tooltip` | If ON, the clock upgrade displays time in 24-hour format instead of 12-hour format. |
| `item.gloamingforest.travellers_gloves.desc` | Cosmetic |
| `key.gloamingforest.categories.travellers_gear` | Gloaming Forest (Traveller's Gear) |
| `key.gloamingforest.item_display_map_cycle` | Cycle Item Display's Stored Maps |
| `key.gloamingforest.red_thread_vision` | See Red Thread with Goggles |
| `key.gloamingforest.swap_hotbar` | Swap Hotbar |
| `key.gloamingforest.zoom` | Zoom With Goggles |
| `travellers_gear.ability` | Ability: %s |
| `travellers_gear.broken` |  (Broken) |
| `travellers_gear.info_indent` |   ⤷  |
| `travellers_gear.modifier.empty` | Empty |
| `travellers_gear.modifier.gloamingforest.agile_ranger` | Agile Ranger |
| `travellers_gear.modifier.gloamingforest.agile_ranger.description` | Allows normal movement when using bow-like items |
| `travellers_gear.modifier.gloamingforest.all_night_goggles` | All-Night Goggles |
| `travellers_gear.modifier.gloamingforest.all_night_goggles.description` | Prevents Insomnia and Enderman aggression |
| `travellers_gear.modifier.gloamingforest.aquatic_agility` | Aquatic Agility |
| `travellers_gear.modifier.gloamingforest.aquatic_agility.description` | Respiration and Aqua Affinity in one |
| `travellers_gear.modifier.gloamingforest.arrow_magnetism` | Arrow Magnetism |
| `travellers_gear.modifier.gloamingforest.arrow_magnetism.description` | Retrieves missed arrows |
| `travellers_gear.modifier.gloamingforest.auto_repair` | Auto-Repair |
| `travellers_gear.modifier.gloamingforest.auto_repair.description` | Repairs durability over time |
| `travellers_gear.modifier.gloamingforest.double_jump` | Double Jump |
| `travellers_gear.modifier.gloamingforest.double_jump.description` | Lets you do a second jump in mid-air |
| `travellers_gear.modifier.gloamingforest.efficient_eater` | Efficient Eater |
| `travellers_gear.modifier.gloamingforest.efficient_eater.description` | Reduces movement-based hunger drain |
| `travellers_gear.modifier.gloamingforest.gradual_glide` | Gradual Glide (sneak to activate) |
| `travellers_gear.modifier.gloamingforest.gradual_glide.description` | Allows for gliding through the air |
| `travellers_gear.modifier.gloamingforest.haste` | Haste |
| `travellers_gear.modifier.gloamingforest.haste.description` | Grants Haste II |
| `travellers_gear.modifier.gloamingforest.high_jump` | High Jump |
| `travellers_gear.modifier.gloamingforest.item_display` | Item Display (keybind: ${tfkeybinds/key.gloamingforest.item_display_map_cycle}) |
| `travellers_gear.modifier.gloamingforest.item_display.clock.unknown` | Time Unknown |
| `travellers_gear.modifier.gloamingforest.item_display.compass.lodestone` | %s (%s blocks away) |
| `travellers_gear.modifier.gloamingforest.item_display.description` | Right-click items on the Goggles to add displays |
| `travellers_gear.modifier.gloamingforest.perfect_dodge` | Perfect Dodge |
| `travellers_gear.modifier.gloamingforest.perfect_dodge.description` | 30% chance to dodge projectiles |
| `travellers_gear.modifier.gloamingforest.red_thread_vision` | Red Thread Vision (keybind: ${tfkeybinds/key.gloamingforest.red_thread_vision}) |
| `travellers_gear.modifier.gloamingforest.red_thread_vision.description` | Allows you to see placed Red Thread |
| `travellers_gear.modifier.gloamingforest.side_step` | Sidestep |
| `travellers_gear.modifier.gloamingforest.side_step.description` | Double-tap %s or %s to do a dash |
| `travellers_gear.modifier.gloamingforest.slimy_soles` | Slimy Soles |
| `travellers_gear.modifier.gloamingforest.slimy_soles.description` | Prevents fall damage by making you bounce |
| `travellers_gear.modifier.gloamingforest.stealth` | Stealth (sneak to activate) |
| `travellers_gear.modifier.gloamingforest.stealth.description` | Sneak to turn invisible |
| `travellers_gear.modifier.gloamingforest.step_up` | Step-Up |
| `travellers_gear.modifier.gloamingforest.straight_ahead` | Straight Ahead |
| `travellers_gear.modifier.gloamingforest.straight_ahead.description` | Increases forward movement speed |
| `travellers_gear.modifier.gloamingforest.swap_hotbar` | Swap Hotbar (keybind: ${tfkeybinds/key.gloamingforest.swap_hotbar}) |
| `travellers_gear.modifier.gloamingforest.swap_hotbar_ability` | Swap Hotbar (keybind: ${tfkeybinds/key.gloamingforest.swap_hotbar}) |
| `travellers_gear.modifier.gloamingforest.swap_hotbar.description` | Allows you to store and withdraw your hotbar |
| `travellers_gear.modifier.gloamingforest.swift_swim` | Swift Swim |
| `travellers_gear.modifier.gloamingforest.unrestrained` | Unrestrained |
| `travellers_gear.modifier.gloamingforest.unrestrained.description` | Prevents blocks from slowing you down |
| `travellers_gear.modifier.gloamingforest.water_walk` | Water Walk |
| `travellers_gear.modifier.gloamingforest.water_walk.description` | Allows you to walk on Water |
| `travellers_gear.modifier.gloamingforest.zoom` | Zoom (keybind: ${tfkeybinds/key.gloamingforest.zoom}) |
| `travellers_gear.shift_info` | Hold %s for info |

