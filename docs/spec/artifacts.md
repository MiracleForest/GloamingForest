# Scepters, charms, artifacts and death keeping (U9 `artifacts`)

Behaviour spec of unit **U9** (wave W1), translated from The Twilight Forest 4.8 (`9e40a757`, MC 1.21.1) by F16. It is
the checklist the U9 worker implements and the A2 audit compares against. Sections 1-5 are the behaviour; Appendix A
lists every id U9 owns with TF's registration and data; Appendix B the strings.

## 1. Scope

- **Scepters**: `twilight_scepter`, `lifedrain_scepter`, `zombie_scepter`, `fortification_scepter`; repair recipe
  types `scepter_repair`, `essence_repair_recipe`; `exanimate_essence`; entities `wand_bolt`, `loyal_zombie`;
  fortification shields (attachment `fortification_shields`, attribute `max_shield_strength`, HUD overlay,
  subcommand `/twilightforest shield`, function `twilightforest:give_3_shields`, stat `tf_shields_broken`).
- **Charms and death keeping**: `charm_of_life_1/2`, `charm_of_keeping_1/2/3`, the `keepsake_casket` block, item and
  block entity (casket repair recipe type `casket_repair_recipe`, component `casket_damage`, config
  `common.casketUUIDLocking`), `#twilightforest:kept_on_death`, entity `charm_effect`, stats
  `life_charms_activated` / `keeping_charms_activated`, net channel `artifacts.spawn_charm`.
- **Potion flasks**: `brittle_potion_flask`, `greater_potion_flask`, component `flask_contents`, attachment
  `flask_doses`, trigger `drink_from_flask`.
- **Other artifacts**: `emperors_cloth` (+ recipe type `emperors_cloth_recipe`, component `emperors_cloth`),
  `four_leaf_clover`, `crown_splinter`, `mystic_crown`, `moon_dial`, `pocket_watch`, `ore_meter` (components
  `ore_data`, `ore_filter`, `ore_loading`, `ore_range`, `ore_scanner`; net `artifacts.wipe_ore_meter`), attachment
  `zombified_player`, damage types `expired`, `lifedrain`, `twilight_scepter`, particles `magic_effect`,
  `shield_break`, `twilight_orb`, net `artifacts.lifedrain_particle`, 25 sound events, client config keys
  `client.fortificationShieldIndicator`, `client.fortificationShieldIndicatorInCreative`,
  `client.prettifyOreMeterGui`, `client.totemCharmAnimation` (section `artifacts`).

Not U9: the skull chest (U22, the casket's parent class: shared logic described here), ominous fire / candles (U22),
the `renewal` enchantment (U10; scepters call its recharge), the no-template smithing type used by the emperor's cloth
smithing recipe (U7), item display modifiers showing the moon dial / pocket watch on the HUD (U11).

The recipes `twilightforest:jeed/{fire_resistance,regeneration,resistance,slowness}` in Appendix A are JEED-compat
data (`jeed:effect_provider`, condition `neoforge:mod_loaded jeed`): inert, see not-ported.md.

Contracts: item, projectile, entity, death, inventory, recipes, data, client.ui, client.render.

## 2. TF sources

`item/TwilightWandItem.java`, `entity/projectile/TwilightWandBolt.java`, `item/LifedrainScepterItem.java`,
`network/LifedrainParticlePacket.java`, `item/ZombieWandItem.java`, `entity/monster/LoyalZombie.java`,
`item/FortificationWandItem.java`, `components/entity/FortificationShieldAttachment.java`,
`events/CapabilityEvents.java` (shield tick/absorb), `client/overlay/ShieldOverlay.java`, `command/ShieldCommand.java`,
`init/TFAttributes.java`, `item/ExanimateEssenceItem.java`, `item/recipe/{ScepterRepair,EssenceRepair,CasketRepair,EmperorsCloth}Recipe.java`,
`enchantment/RechargeScepterEffect.java`, `events/CharmEvents.java`, `entity/CharmEffect.java`,
`network/SpawnCharmPacket.java`, `block/KeepsakeCasketBlock.java`, `block/SkullChestBlock.java`,
`block/entity/{KeepsakeCasket,SkullChest}BlockEntity.java`, `item/KeepsakeCasketItem.java`,
`client/renderer/block/{KeepsakeCasket,SkullChest}Renderer.java`, `enums/BlockLoggingEnum.java`,
`util/TFItemStackUtils.java`, `item/BrittleFlaskItem.java`, `item/GreaterFlaskItem.java`,
`components/item/PotionFlaskComponent.java`, `components/entity/PotionFlaskTrackingAttachment.java`,
`client/renderer/tooltip/PotionFlaskTooltipComponent.java`, `item/MysticCrownItem.java`, `item/MoonDialItem.java`,
`item/PocketWatchItem.java`, `events/ToolEvents.java` (pocket watch fatigue), `item/OreMeterItem.java`,
`components/item/{OreScannerComponent,OreScannerData}.java`, `client/event/OverlayHandler.java` (ore meter stats),
`network/WipeOreMeterPacket.java`, `events/EntityEvents.java` (ore meter wipe, ominous fire zombification,
zombified attacks, casket break), `events/MiscEvents.java` (cloth washing), `asmhooks/ArmorHooks.java` (cloth hides
armor), `client/event/ClientGameEvents.java` (cloth tooltip), `data/function/give_3_shields.mcfunction`.

## 3. Behaviour

### 3.1 Scepters (common rules)

Durability: twilight and lifedrain **99**, zombie and fortification **9**; UNCOMMON; members of `#twilightforest:scepters`.
A scepter at 0 remaining uses refuses to work (fail) unless in creative; uses never break it (`hurtButDontBreak`).
Tooltip `item.twilightforest.scepter.desc` with the remaining charges. **Renewal** (U10 enchantment): every 20 ticks,
when not selected, a scepter at 0 charges recharges from the inventory (one exanimate essence -> full; otherwise
consumes the scepter's repair ingredients from its `scepter_repair` recipe and restores `durability` charges). Wearing
the **mystic crown** gives twilight and lifedrain scepters a 5% chance not to use a charge and zombie scepters a 10%
chance to summon a baby zombie.

- **Twilight scepter**: use plays `item.twilightforest.twilight_scepter.use` (1, 1 +- 0.2) and fires a `wand_bolt`
  (speed 1.5, inaccuracy 1.0; gravity 0.003): trail of 5 `twilightforest:magic_effect` particles per tick with colour
  (0.17, 0.80, 0.69) x (0.5..1); hitting a living entity deals **6** `twilightforest:twilight_scepter` damage and
  plays `...twilight_scepter.pearl_hit`; any hit removes it with 8 `twilight_orb` particles. Hitting the bolt
  reflects it along the attacker's look (speed 1.5, inaccuracy 0.1, new owner). It ignores lich bolts/bombs.
  1 charge per shot. Dispenser fires a bolt (1 charge, inaccuracy 6.0).
- **Lifedrain scepter**: hold-use (BOW animation, 72000). Every 5 use ticks: target = the closest pickable entity within
  20 blocks along the look (ray against boxes inflated by their pick radius). Living, non armor-stand target: server
  sends `LifedrainParticlePacket(user id, target eye)` (red `magic_effect` trail from the hand, client) and plays
  `item.twilightforest.life_scepter.drain`; deals **1** `twilightforest:lifedrain` damage. On success: if the target's
  health <= 1 and it is not a boss: (players attacking non-`#lifedrain_drops_no_flesh` targets) roll the
  `twilightforest:items/lifedrain_scepter_kill_bonus` loot table (F10's shared table) at the target and shatter it into
  50 + (int)width x 25 (1/100: 50 + (int)width x 75, power 1.0 instead of 0.3) rotten flesh item particles (the int cast
  truncates the width before the multiply: width 0.6 gives 50, width 1.4 gives 75); spawn animation, its death sound,
  then kill it (players: max damage; others: `die` + discard). Otherwise Slowness III (amplifier 2) for 20 ticks; every
  10 ticks the user heals 1 and (player) gains food 1 / saturation 0.1. 1 charge per successful tick. A target with
  health <= the user's is lifted (motion y 0.15).
- **Zombie scepter**: ray (no fluids) to a block; spawns a `loyal_zombie` at the hit point if it fits: spawn animation,
  tamed by the player, Strength II (amplifier 1) for 1200 ticks, sound `item.twilightforest.zombie_scepter.use`, game
  event entity_place, 1 charge.
  - `loyal_zombie` (TamableAnimal, 0.6x1.95, MONSTER category, no XP): HP **40**, speed 0.3, armor 3; attack deals 7
    and pushes the target up 0.2. Goals: float(1), melee(4, 1.0, follow), follow owner(5, 1.0, 10 -> 2), wander(7),
    look at player(9, 8), random look(9); targets: owner hurt by(1), owner hurt(2), hurt by(3), nearest monster(4).
    Wolf-like target filter (no creepers, ghasts, own team's tamed animals, tamed horses, protected players).
    Without Strength it takes 2 `twilightforest:expired` damage every 20 ticks (dies after the summon wears off). The
    owner feeding rotten flesh renews Strength II 1200 t, heals 1, plays `entity.zombie.infect`. Baby: half size, eye
    0.93, speed +50%. Sounds `entity.twilightforest.loyal_zombie.{ambient,hurt,death,step}` (step volume 0.15).
    Despawns only when untamed.
- **Fortification scepter**: use sets the user's **temporary** shields to **5** (resets the timer), 1 charge, sound
  `entity.twilightforest.fortification_shield.add` (1, 1 +- 0.2), cooldown **1200** ticks (not in creative).
- **Fortification shields** (`FortificationShieldAttachment`, also used by the Lich): temporary + permanent counts
  (each clamped 0..115). Every tick (server, not invulnerable players): when temporary shields exist the timer counts
  down from **240** (with the mystic crown it skips every third tick) and at 0 one temporary shield expires (sound
  `...fortification_shield.expire`). Incoming damage that does not bypass armor is cancelled while shields remain;
  if the entity is not in invulnerability frames one shield breaks (temporary first; sound
  `...fortification_shield.break`, pitch 1 +- 0.3; stat `twilightforest:tf_shields_broken` for players unless
  expired), 16 `twilightforest:shield_break` particles are sent on the side facing the attacker, and invulnerability
  frames restart. HUD (`ShieldOverlay`, client): when shields > 0, the player can be hurt (or
  `fortificationShieldIndicatorInCreative`) and `fortificationShieldIndicator` is on, draws up to 10
  `twilightforest:fortification_shield` sprites (9x9) at x = width/2 - 91 + 8i, y = height - leftHeight, then raises
  leftHeight by 10.
- `/twilightforest shield <target> set|add <amount> [temp=true]` (permission 2) sets / adds shields;
  `function twilightforest:give_3_shields` (Appendix A). Attribute `twilightforest:max_shield_strength`
  (`attribute.name.lich.shield_strength`, default 6, 0..1024, synced).
- **Exanimate essence** (stack 16, UNCOMMON): use on a lit vanilla candle with an ominous counterpart turns it into
  the ominous candle (same count, flame burst); otherwise places `twilightforest:ominous_fire` on the clicked face if
  it can survive; sound `block.twilightforest.ominous_fire` (1.5, 0.75 +- 0.2), consumes one.
- **Repair recipes** (special crafting, Appendix A JSON): `scepter_repair` `{scepter, repair_ingredients[], durability,
  category}` — with one ingredient type: one damaged scepter + n copies, restores n x durability (refused if it would
  overshoot past full by a whole ingredient); with several ingredients: exactly those plus the damaged scepter,
  restores `durability`. `essence_repair_recipe`: one damaged `#scepters` + one exanimate essence -> fully repaired.

### 3.2 Charms and the keepsake casket (`CharmEvents`)

Only for real, non-creative, non-spectator players; server side.

- **Charm of life** (highest priority on death): charm II found (inventory, consumed) -> cancel death, health = max,
  Regeneration IV 600 t, Resistance I 600 t, Fire Resistance 600 t; else charm I -> cancel death, health 8,
  Regeneration I 100 t. Sends `SpawnCharmPacket(charm stack, item.twilightforest.charm.life)`, stat
  `life_charms_activated` +1.
- **Charm of keeping** (high priority, only without keepInventory; `handleCharmOfKeeping` / `applyCharm`): the tiers
  are tried III, then II, then I, and the first that applies wins. Scopes, exactly as TF 4.8 computes them:
  - **III**: the whole main inventory (slots 0-35), with **every** keepsake casket kept (`skipCasketCheck`).
  - **II**: the hotbar (slots 0-8).
  - **I**: **no** main-inventory slot. TF passes `NonNullList.of(items.get(selected))`, and
    `NonNullList.of(default, elements...)` with one argument builds an *empty* list (the argument is only the
    default value), so the selected slot is not kept. **TF quirk, keep it** (like the casket-break `!op || !owner`
    test below): charm I keeps only armor and offhand, and only when the selected slot is a hotbar slot.
  - Every tier also keeps the 4 armor slots and the offhand.
  - A tier applies only when its slots (scope + armor + offhand), ignoring every stack of that charm item, hold at
    least one non-empty stack; otherwise it returns false without consuming anything and the next tier is tried (so
    charm I is not consumed when armor and offhand are empty). Then one charm is consumed (searched armor, then main
    inventory, then offhand; one item of the first matching stack) and remembered (`CharmStack`); without a charm the
    tier does not apply.
  - Caskets (`keepWholeListAndCheckCasket`, tiers II and I): walking the kept slots in order (main slots, then armor,
    then offhand), the **first** keepsake casket stack found leaves exactly one casket in its slot (the rest of that
    stack, count - 1, is kept; a stack of 1 is not kept at all) so `stockKeepsakeCasket` can place it; every later
    casket stack is kept whole. Once a casket was left in the main slots, armor and offhand are kept whole. Caskets
    outside the scope stay in the inventory as any other unkept item.
  - With or without a charm: items in `#twilightforest:kept_on_death` (phantom helmet / chestplate, tower key) are
    always kept (checked after the charm, in main, armor and offhand slots). Kept items are stored in the player's
    persisted NBT (`TFCharmInventory`, only when not empty) and returned on respawn (not after the End credits); the
    consumed charm (`CharmStack`) shows the charm animation with `item.twilightforest.charm.keep` on respawn, stat
    `keeping_charms_activated` +1.
- **Keepsake casket**: if the player still has any non-casket item and a casket in the inventory, one casket is
  consumed and placed at the death position (y clamped to [minY + 2, logicalHeight - 1], first replaceable block
  upward), multilogged with the fluid there, `damage` from the last casket (`CasketDamage`), facing = random of the
  first three 2D directions. With chance **0.15** the casket is damaged: at damage >= 2 a `twilightforest:skull_chest`
  is placed instead, else damage + 1. Owner = the player when `casketUUIDLocking` (default **false**), else none. Name
  = first 12 characters of the player name + "'s " + (1/1000 "Costco Casket" else the block name). Contents (45
  slots): armor reversed (head first), 4 empty, offhand, main inventory rows 9-35 then the hotbar. Without items to
  store, held caskets are moved to the kept inventory instead.
- Casket block (`KeepsakeCasketBlock` < `SkullChestBlock`): `strength(5.0, 1200.0)`, NETHERITE_BLOCK, push BLOCK,
  requires tool, light 15 when lava-logged; states `facing` (opposite of the player), `multilogged` (air, water, lava,
  obsidian, stone, basalt; lava + water neighbour -> obsidian, lava on soul soil + blue ice -> basalt, water + lava ->
  stone, with fizz), `damage` 0..2; explosion resistance infinite; entities cannot destroy it; 5-row chest menu
  (`GENERIC_9x5`), comparator = container fill. Using a charm of keeping III on a damaged casket repairs one level
  (sound `block.twilightforest.casket.repair` 0.5, 0.9 + rand*0.1, consumes the charm); otherwise opens the menu (not
  when solidly logged). Open / close sounds `...casket.open` / `...casket.close` (0.5, 0.9 + rand*0.1). Locked
  caskets (owner set, `SkullChestBlockEntity.canOpen`, empty or not) open only for the owner or permission level
  3; others get `block.twilightforest.casket.locked` (red, action bar) and `...casket.locked` (0.5, 0.5).
  Breaking an owned casket that is not empty is cancelled (`EntityEvents.onCasketBreak`, no message) unless the
  player has permission level 3 **and** is the owner (TF's `!op || !owner` test; keep it); an empty one breaks
  normally. Breaking (survival, block
  drops on) drops the casket item with its damage (`casket_damage`) and custom name, plus the logged block; contents
  spill. Item names: `chipped_keepsake_casket` (1), `damaged_keepsake_casket` (2). Placing restores the damage.
  Renderer (`KeepsakeCasketRenderer` < `SkullChestRenderer`, layer `TFModelLayers.KEEPSAKE_CASKET`): textures
  `textures/model/casket/keepsake_casket_{0,1,2}.png` by damage; chest-like lid opening with eased angle
  1 - (1 - t)^3 times -90 degrees; the animated model is used only when not solidly logged (otherwise the block
  model shows).
  Repair recipe `casket_repair_recipe`: damaged casket + charm of keeping III -> damage - 1.
- Charm animation (client, `SpawnCharmPacket`): with `totemCharmAnimation` (default false) the totem item activation
  plus 20 item particles; otherwise a `charm_effect` entity orbits the player's eyes (radius 0.75, angle tick/10 + pi,
  item particles, removed after 200 ticks or when the player dies / is invisible); the sound plays at the camera
  (1.5, 1.0).

### 3.3 Potion flasks

- `brittle_potion_flask` (stack 64 when empty, 1 when filled; default `flask_contents` EMPTY breakable) and
  `greater_potion_flask` (stack 1, UNCOMMON, fire resistant, unbreakable contents). Max doses **3** (brittle:
  3 - breakage). Filling: right-click (secondary click) a potion onto the flask in an inventory, or the flask onto a
  potion, when the flask is empty or holds the same potion and has room: the potion becomes a glass bottle, one dose
  is added, sound `item.twilightforest.flask.fill` (volume (doses+1)*0.25, pitch 0.9 + rand*0.1); stacked empty
  flasks split off one.
- Drinking (32 ticks, DRINK): applies each effect (instant ones instantly); an amplified Instant Damage (or Instant
  Health for undead) instead deals `6 << amplifier` damage of `twilightforest:failed_challenge`. Tracks
  `flask_doses` (consecutive doses of the same potion and start time) and fires `twilightforest:drink_from_flask`
  (doses, seconds since the first dose, potion). Removes a dose (empty at 0); brittle flasks add breakage per dose and
  break at breakage 3 (sound `...flask.break` 1.5, 0.7) else crack (`...flask.crack` 1.5, 2.0). Bar: width
  round(13 - |doses - 3| * 13/3), colour = potion colour; item tint layer 0 = potion colour; brittle flask model
  predicate `twilightforest:breakage` = breakage. Earning an advancement with a flask criterion resets the dose
  tracking (`EntityEvents.resetFlaskLogic`).
- Tooltip image (`PotionFlaskTooltipComponent`, width **115**): description lines word-wrapped at 115 px (empty
  flask: `item.twilightforest.flask.empty_description`; filled: the potion name + vanilla potion effect lines, colour
  0xAAAAAA unless styled), then a 13-px bar 4 px below centred at offset (113 - 115)/2: the liquid is the water still
  texture tinted with the potion colour, width doses x (115/3) - 1, drawn upward from y + 13; broken segments covered
  by 0xAA434343 from x + seg*(3 - breakage); 1-px separators (`twilightforest:flask_dose_bar` sprite) every 115/3 px;
  border sprite `twilightforest:flask_bar_border` 115x13; with no doses `item.twilightforest.flask.empty` centred in
  white. Height = description height + 21.

### 3.4 Other artifacts

- **Mystic crown** (stack 1, UNCOMMON, head slot, swap-equip on use; attributes per Appendix A: +2 armor head ...):
  scepter bonuses above, slows shield expiry.
- **Moon dial**: tooltip `item.twilightforest.moon_dial.phase_<0..7>` in natural dimensions, else `phase_unknown`
  (`phase_unknown_fools` on April 1st).
- **Pocket watch** (stack 1): in the hotbar or offhand gives hidden Speed I and Jump Boost I (5 ticks, refreshed);
  held gives Haste I; while held, Mining Fatigue cannot be applied (`ToolEvents.preventFatigueWithPocketWatch`).
  Tooltip `item.twilightforest.pocket_watch.desc`.
- **Emperor's cloth**: `emperors_cloth_recipe` (cloth + one `#twilightforest:emperors_cloth_applicable` item without
  crafting remainder and without the component) sets `twilightforest:emperors_cloth`; such armor (and elytra/cape) is
  **not rendered** on the wearer (client), tooltip `item.twilightforest.emperors_cloth.desc` ("Shrouded") at line 1;
  using the item on a water cauldron with water removes it, lowers the level, stat `minecraft:clean_armor`.
- **Ore meter** (stack 1, UNCOMMON; only real server players): use (not sneaking, not loading) starts a scan of the
  chunks within `ore_range` (0..2, default 1) of the player's chunk over 50 + 25*range ticks, sound
  `item.twilightforest.ore_meter.crackle` (0.5, 0.9 + rand*0.1); the scan walks the whole column volume of those
  chunks in equal per-tick steps, counting blocks; when done stores `ore_data` (counts of `#c:ores` blocks, or only the
  filter block) with the centre chunk, volume and range. Sneak-use in the air cycles the range 0 -> 1 -> 2 -> 0
  (message `misc.twilightforest.ore_meter_new_range`, `ui.button.click` 0.25, 0.75 + 0.1*range); sneak-use on a block
  in `#twilightforest:ore_meter_targetable` sets the filter (`misc.twilightforest.ore_meter_set_block`,
  `...ore_meter.target_block`). Left-click in the air with data or filter wipes both (client sends
  `WipeOreMeterPacket(hand)`; sound `...ore_meter.clear` 1.25, 0.6 + rand*0.2). HUD while held: loading box (0,0)-
  (56,16) 0x9B000000 with "Loading" + 1..3 dots ((progress/5)%3); results box at the top-left: headers
  `ore_meter_range` (range, chunk x, z) and `ore_meter_total`, then rows sorted by count: prettified (config default
  true) as columns name / dash / count / ratio (`%.3f` %) with headers, else one column "name - count ratio". Padding 4,
  line height + 1, background 0x9B000000. Tooltip shows the filter (`misc.twilightforest.ore_meter_targeted_block`).
- **Zombified players**: a player killed by ominous fire becomes a zombie named after them carrying
  `zombified_player` (their profile; can pick up loot, adult); its attacks are converted to ominous fire damage with
  the death message `death.attack.twilightforest.ominousFire.zombified_player`.
- `four_leaf_clover`, `crown_splinter`: plain items (crafting / loot); charms and the crown are crafted per Appendix A.

## 4. Bedrock adaptation notes

- Death keeping goes through F10's `death::onLethal` (charm of life: survive) and `death::registerKeeper` (charms of
  keeping, kept-on-death tag), `death::onDropsRemaining` (place the casket with the remaining items) and
  `death::onRespawn` (return items, charm animation). Kept items persist in `gf:artifacts:` world DB keys per player
  UUID (R16), not in player NBT.
- Casket: custom block without block entity -> S3 backend via `inventory::registerBlock("twilightforest:keepsake_casket",
  {.slots = 45, .open = ...})`; owner / name / damage in BlockData; multilogging states as integer states (permutation
  budget: 4 facing x 6 logged x 3 damage). The client mod renders the casket model; vanilla clients see a static
  geometry and a 45-slot container UI (best effort).
- Shields: per-entity data through `entity::data`; damage cancel via `combat::onIncoming` (priority above armor);
  HUD via `client::hud::addLayer` (client mod); vanilla clients get an action-bar shield count.
- Flasks: Bedrock has no right-click-in-inventory transfer; fill in the world (use a potion while holding the flask in
  the other hand) and with the client mod's inventory screen hook; document in not-ported.md.
- Emperor's cloth hiding armor needs the client mod (render hook); vanilla clients still see the armor.
- Ore meter HUD: client mod (`client::hud`); vanilla: send the result lines as a chat/form message.
- Charm animation: client mod (`artifacts.spawn_charm`), vanilla: totem particle + title.

## 5. Acceptance checklist (selftests `artifacts.*`, dx 900-999)

- Scepter durabilities, no-break rule, crown bonuses (seeded), renewal recharge from essence / ingredients.
- Twilight bolt 6 damage; lifedrain 1 damage per 5 ticks, heal per 10, execution at <= 1 HP with loot; loyal zombie
  stats, expiry damage, rotten-flesh renewal; fortification 5 temp shields, 240-tick expiry, absorption and stat.
- Charm of life I/II outcomes; keeping scopes: III whole inventory with every casket kept, II hotbar, I armor and
  offhand only (the selected slot is dropped: TF quirk) and not consumed when armor and offhand are empty; one casket
  of the first casket stack left behind for placing (II/I); kept-on-death items; casket placement, contents layout, 15%
  damage / skull chest at damage 2, UUID locking, repair by charm III (block and recipe).
- Flask fill / drink / crack / break, harm-dose damage, `drink_from_flask` data.
- Pocket watch effects and fatigue immunity; moon dial phase keys; emperor's cloth recipe and washing.
- Ore meter scan duration and counts on a fixed chunk, range cycling, filter, wipe.

## Appendix A: Owned ids (generated inventory)

Every id this unit owns in `docs/spec/owners.tsv`, with the TF 4.8 data the W1 worker must reproduce. Registration statements are TF's own Java (`init/TF*.java`), condensed (namespaces such as `MapColor.` dropped); loot, recipe, tag, advancement and data rows are summarised from TF's generated JSON (`src/generated/resources`). Loot notation: `self` = drops the block itself when it survives the explosion; `alternatives(a → b)` = first matching child; `[if ...]` = conditions; `{...}` = functions. In summaries `mc:` is `minecraft:` and ids without a namespace are `twilightforest:`.

| Kind | Count |
|---|---|
| `advancement` | 4 |
| `attachment` | 3 |
| `attribute` | 1 |
| `block` | 1 |
| `block_entity` | 1 |
| `config_key` | 5 |
| `config_section` | 1 |
| `damage_type` | 3 |
| `data_component` | 8 |
| `e2e` | 1 |
| `entity` | 3 |
| `function` | 1 |
| `item` | 20 |
| `lang` | 91 |
| `loot_table` | 3 |
| `net` | 4 |
| `particle` | 3 |
| `path` | 20 |
| `recipe` | 15 |
| `recipe_serializer` | 4 |
| `selftest` | 1 |
| `selftest_dx` | 1 |
| `sound_event` | 25 |
| `stat` | 3 |
| `subcommand` | 1 |
| `tag` | 3 |
| `trigger` | 1 |
| `worlddb` | 1 |

### Unit facts

| Kind | Ids |
|---|---|
| Selftest glob (`selftest`) | `artifacts.*` |
| Selftest dx range (`selftest_dx`) | `900-999` |
| World DB keys (`worlddb`) | `gf:artifacts:*` |
| E2E scenario (`e2e`) | `artifacts` |
| Net channels (`net`) | `artifacts.*`, `artifacts.lifedrain_particle`, `artifacts.spawn_charm`, `artifacts.wipe_ore_meter` |
| Config section (`config_section`) | `artifacts` |
| Owned paths (`path`) | `**/*.d/artifacts.json`, `assets/data/*/artifacts.json`, `assets/gen/audio/artifacts/**`, `assets/gen/geo/artifacts/**`, `assets/gen/import/artifacts/**`, `assets/lang/*/artifacts.json`, `assets/pack/{bp,rp}/**/artifacts/**`, `assets/pack/rp/texts.d/*/artifacts.lang`, `assets/structures/artifacts/**`, `assets/tags/**/*.d/artifacts.json`, `docs/units/artifacts.md`, `src/client/artifacts/**`, `src/core/artifacts/**`, `src/feature/artifacts/**`, `src/feature/ext/artifacts/**`, `tests/artifacts/**`, `tools/e2e/*/scenarios/artifacts{,.*}.ps1`, `tools/import/overrides/artifacts.json`, `tools/sounds/recipes/artifacts.json`, `tools/structures/artifacts/**` |

### Blocks (1)

Item column: `BlockItem` = TF registers a plain block item with the block (`TFBlocks.register`), the id is also a `block_item` row; `-` = no item of the same id (the block is placed by another item, or has none).

Block states come from TF's blockstate JSON (`multipart:` lists only the property values the model parts test). Notes collect `RegistrationEvents` (fire, axe stripping, flower pots, jar lids), dispenser behaviours and the `neoforge:compostables` / `neoforge:furnace_fuels` data maps.

| Block | English name | TF registration (`TFBlocks`) | Block states | Item | Loot (`blocks/<id>`) | Notes |
|---|---|---|---|---|---|---|
| `twilightforest:keepsake_casket` | Keepsake Casket | `BLOCKS.register("keepsake_casket", new KeepsakeCasketBlock(Properties.of().lightLevel(state -> state.getValue(BlockLoggingEnum.MULTILOGGED) == BlockLoggingEnum.LAVA ? 15 : 0).mapColor(COLOR_BLACK).noOcclusion().pushReaction(BLOCK).requiresCorrectToolForDrops().sound(NETHERITE_BLOCK).strength(5.0F, 1200.0F)));` | multilogged: air/basalt/lava/obsidian/stone/water | - | rolls 1:  {mc:copy_state{"block":"twilightforest:keepsake_casket","properties":["damage"]}} | block tags: #mc:dragon_immune, #mc:features_cannot_replace, #mc:mineable/pickaxe, #mc:wither_immune, #antibuilder_ignores, #block_and_chain_never_breaks, #carminite_reactor_immune, #common_protections, #mineable_with_block_and_chain, #progression_allow_breaking, #small_lakes_dont_replace, #tree_roots_skip |

### Items (20)

| Item | English name | TF registration (`TFItems`) | Notes |
|---|---|---|---|
| `twilightforest:brittle_potion_flask` | Brittle Potion Flask | `ITEMS.register("brittle_potion_flask", new BrittleFlaskItem(new Item.Properties().component(TFDataComponents.POTION_FLASK_CONTENTS, PotionFlaskComponent.EMPTY)));` |  |
| `twilightforest:charm_of_keeping_1` | Charm of Keeping I | `ITEMS.register("charm_of_keeping_1", new Item(new Item.Properties().rarity(UNCOMMON)));` | item tags: #curios:charm |
| `twilightforest:charm_of_keeping_2` | Charm of Keeping II | `ITEMS.register("charm_of_keeping_2", new Item(new Item.Properties().rarity(UNCOMMON)));` | item tags: #alexscaves:ferromagnetic_items, #curios:charm |
| `twilightforest:charm_of_keeping_3` | Charm of Keeping III | `ITEMS.register("charm_of_keeping_3", new Item(new Item.Properties().rarity(UNCOMMON)));` | item tags: #curios:charm, #mc:piglin_loved |
| `twilightforest:charm_of_life_1` | Charm of Life I | `ITEMS.register("charm_of_life_1", new Item(new Item.Properties().rarity(UNCOMMON)));` | item tags: #curios:charm |
| `twilightforest:charm_of_life_2` | Charm of Life II | `ITEMS.register("charm_of_life_2", new Item(new Item.Properties().rarity(UNCOMMON)));` | item tags: #curios:charm, #mc:piglin_loved |
| `twilightforest:crown_splinter` | Crown Splinter | `ITEMS.register("crown_splinter", new Item(new Item.Properties().rarity(UNCOMMON)));` |  |
| `twilightforest:emperors_cloth` | Emperor's Cloth | `ITEMS.register("emperors_cloth", new Item(new Item.Properties().rarity(UNCOMMON)));` |  |
| `twilightforest:exanimate_essence` | Exanimate Essence | `ITEMS.register("exanimate_essence", new ExanimateEssenceItem(new Item.Properties().rarity(UNCOMMON).stacksTo(16)));` |  |
| `twilightforest:fortification_scepter` | Scepter of Fortification | `ITEMS.register("fortification_scepter", new FortificationWandItem(new Item.Properties().durability(9).rarity(UNCOMMON)));` | item tags: #scepters |
| `twilightforest:four_leaf_clover` | Four-Leaf Clover | `ITEMS.register("four_leaf_clover", new Item(new Item.Properties()));` |  |
| `twilightforest:greater_potion_flask` | Greater Potion Flask | `ITEMS.register("greater_potion_flask", new GreaterFlaskItem(new Item.Properties().stacksTo(1).rarity(UNCOMMON).fireResistant().component(TFDataComponents.POTION_FLASK_CONTENTS, PotionFlaskComponent.EMPTY_UNBREAKABLE)));` |  |
| `twilightforest:keepsake_casket` | Keepsake Casket | `ITEMS.register("keepsake_casket", new KeepsakeCasketItem(TFBlocks.KEEPSAKE_CASKET.get(), new Item.Properties().rarity(UNCOMMON).component(TFDataComponents.CASKET_DAMAGE, 0)));` |  |
| `twilightforest:lifedrain_scepter` | Scepter of Life Draining | `ITEMS.register("lifedrain_scepter", new LifedrainScepterItem(new Item.Properties().durability(99).rarity(UNCOMMON)));` | item tags: #scepters |
| `twilightforest:moon_dial` | Moon Dial | `ITEMS.register("moon_dial", new MoonDialItem(new Item.Properties().rarity(UNCOMMON)));` |  |
| `twilightforest:mystic_crown` | Mystic Crown | `ITEMS.register("mystic_crown", new MysticCrownItem(new Item.Properties().rarity(UNCOMMON).stacksTo(1).attributes(ItemAttributeModifiers.builder().add(Attributes.ARMOR, new AttributeModifier(ResourceLocation.withDefaultNamespace("armor.head"), 2.0F, AttributeModifier.Operation.ADD_VALUE), EquipmentSlotGroup.HEAD).build())));` |  |
| `twilightforest:ore_meter` | Ore Meter | `ITEMS.register("ore_meter", new OreMeterItem(new Item.Properties().stacksTo(1).rarity(UNCOMMON)));` |  |
| `twilightforest:pocket_watch` | Rabbit's Pocket Watch | `ITEMS.register("pocket_watch", new PocketWatchItem(new Item.Properties().stacksTo(1).rarity(UNCOMMON)));` |  |
| `twilightforest:twilight_scepter` | Scepter of Twilight | `ITEMS.register("twilight_scepter", new TwilightWandItem(new Item.Properties().durability(99).rarity(UNCOMMON)));` | dispenser: fires its projectile and damages the stack (TFDispenserBehaviors); item tags: #scepters |
| `twilightforest:zombie_scepter` | Zombie Scepter | `ITEMS.register("zombie_scepter", new ZombieWandItem(new Item.Properties().durability(9).rarity(UNCOMMON)));` | item tags: #scepters |

### Block entity types (1)

| Id | TF registration (`TFBlockEntities.java`) |
|---|---|
| `twilightforest:keepsake_casket` | `BLOCK_ENTITIES.register("keepsake_casket", () -> BlockEntityType.Builder.of(KeepsakeCasketBlockEntity::new, TFBlocks.KEEPSAKE_CASKET.get()).build(null));` |

### Entity types (3)

| Id | TF registration (`TFEntities.java`) |
|---|---|
| `twilightforest:charm_effect` (Charm Effect) | `buildNoEgg(TFEntityNames.CHARM_EFFECT, makeCastedBuilder(CharmEffect.class, CharmEffect::new, 0.0F, 0.0F, 16, Integer.MAX_VALUE).noSave().noSummon(), true);` |
| `twilightforest:loyal_zombie` (Loyal Zombie) | `make(TFEntityNames.LOYAL_ZOMBIE, LoyalZombie::new, MobCategory.MONSTER, 0.6F, 1.95F, 0, 0, -0.7F);` |
| `twilightforest:wand_bolt` (Twilight Scepter Bolt) | `buildNoEgg(TFEntityNames.WAND_BOLT, makeCastedBuilder(TwilightWandBolt.class, TwilightWandBolt::new, 0.25F, 0.25F, 150, 5), false);` |

### Particle types (3)

| Id | TF registration (`TFParticleType.java`) |
|---|---|
| `twilightforest:magic_effect` | `PARTICLE_TYPES.register("magic_effect", new ParticleType<>(false) { @Override public MapCodec<ColorParticleOption> codec() { return ColorParticleOption.codec(MAGIC_EFFECT.get()); } @Override public StreamCodec<? super RegistryFriendlyByteBuf, ColorParticleOption> streamCodec() { return ColorParticleOption.streamCodec(MAGIC_EFFECT.get()); } });` |
| `twilightforest:shield_break` | `PARTICLE_TYPES.register("shield_break", new SimpleParticleType(false));` |
| `twilightforest:twilight_orb` | `PARTICLE_TYPES.register("twilight_orb", new SimpleParticleType(false));` |

### Data components (8)

| Id | TF registration (`TFDataComponents.java`) |
|---|---|
| `twilightforest:casket_damage` | `COMPONENTS.register("casket_damage", () -> DataComponentType.<Integer>builder().persistent(Codec.INT).networkSynchronized(ByteBufCodecs.INT).build());` |
| `twilightforest:emperors_cloth` | `COMPONENTS.register("emperors_cloth", () -> DataComponentType.<Unit>builder().persistent(Codec.unit(Unit.INSTANCE)).networkSynchronized(StreamCodec.unit(Unit.INSTANCE)).build());` |
| `twilightforest:flask_contents` | `COMPONENTS.register("flask_contents", () -> DataComponentType.<PotionFlaskComponent>builder().persistent(PotionFlaskComponent.CODEC).networkSynchronized(PotionFlaskComponent.STREAM_CODEC).build());` |
| `twilightforest:ore_data` | `register("ore_data", OreScannerData.CODEC, OreScannerData.STREAM_CODEC);` |
| `twilightforest:ore_filter` | `COMPONENTS.register("ore_filter", () -> DataComponentType.<Block>builder().persistent(BuiltInRegistries.BLOCK.byNameCodec().orElse(Blocks.AIR)).networkSynchronized(ByteBufCodecs.registry(Registries.BLOCK)).cacheEncoding().build());` |
| `twilightforest:ore_loading` | `COMPONENTS.register("ore_loading", () -> DataComponentType.<Integer>builder().persistent(ExtraCodecs.NON_NEGATIVE_INT.orElse(0)).networkSynchronized(ByteBufCodecs.VAR_INT).cacheEncoding().build());` |
| `twilightforest:ore_range` | `COMPONENTS.register("ore_range", () -> DataComponentType.<Integer>builder().persistent(ExtraCodecs.NON_NEGATIVE_INT.orElse(1)).networkSynchronized(ByteBufCodecs.VAR_INT).cacheEncoding().build());` |
| `twilightforest:ore_scanner` | `register("ore_scanner", OreScannerComponent.CODEC);` |

### Data attachments (3)

| Id | TF registration (`TFDataAttachments.java`) |
|---|---|
| `twilightforest:flask_doses` | `ATTACHMENT_TYPES.register("flask_doses", () -> AttachmentType.builder(PotionFlaskTrackingAttachment::new).serialize(PotionFlaskTrackingAttachment.CODEC).build());` |
| `twilightforest:fortification_shields` | `ATTACHMENT_TYPES.register("fortification_shields", () -> AttachmentType.builder(FortificationShieldAttachment::new).serialize(FortificationShieldAttachment.CODEC).sync(FortificationShieldAttachment.STREAM_CODEC).build());` |
| `twilightforest:zombified_player` | `ATTACHMENT_TYPES.register("zombified_player", () -> AttachmentType.builder(() -> UUIDUtil.createOfflineProfile("GizmoTheMoonPig")).serialize(Codecs.SIMPLE_GAME_PROFILE).build());` |

### Attributes (1)

| Id | TF registration (`TFAttributes.java`) |
|---|---|
| `twilightforest:max_shield_strength` | `ATTRIBUTES.register("max_shield_strength", new RangedAttribute("attribute.name.lich.shield_strength", 6, 0, 1024).setSyncable(true));` |

### Recipe serializers / types (4)

| Id | TF registration (`TFRecipes.java`) |
|---|---|
| `twilightforest:casket_repair_recipe` | `RECIPE_SERIALIZERS.register("casket_repair_recipe", new SimpleCraftingRecipeSerializer<>(CasketRepairRecipe::new));` |
| `twilightforest:emperors_cloth_recipe` | `RECIPE_SERIALIZERS.register("emperors_cloth_recipe", new SimpleCraftingRecipeSerializer<>(EmperorsClothRecipe::new));` |
| `twilightforest:essence_repair_recipe` | `RECIPE_SERIALIZERS.register("essence_repair_recipe", new SimpleCraftingRecipeSerializer<>(EssenceRepairRecipe::new));` |
| `twilightforest:scepter_repair` | `RECIPE_SERIALIZERS.register("scepter_repair", ScepterRepairRecipe.Serializer::new);` |

### Stats (3)

| Id | TF registration (`TFStats.java`) |
|---|---|
| `twilightforest:keeping_charms_activated` (Charms of Keeping Used) | `makeTFStat("keeping_charms_activated");` |
| `twilightforest:life_charms_activated` (Charms of Life Used) | `makeTFStat("life_charms_activated");` |
| `twilightforest:tf_shields_broken` (Fortification Shields Broken) | `makeTFStat("tf_shields_broken");` |

### Advancement triggers (1)

| Id | TF registration (`TFAdvancements.java`) |
|---|---|
| `twilightforest:drink_from_flask` | `TRIGGERS.register("drink_from_flask", DrinkFromFlaskTrigger::new);` |

### Damage types (3)

- `twilightforest:expired`: `{"exhaustion":0.0,"message_id":"twilightforest.expired","scaling":"when_caused_by_living_non_player"}` death.attack.twilightforest.expired = "%1$s's life expired"; death.attack.twilightforest.expired.player = "%1$s's life expired";
- `twilightforest:lifedrain`: `{"exhaustion":0.0,"message_id":"twilightforest.lifedrain","scaling":"when_caused_by_living_non_player"}` death.attack.twilightforest.lifedrain = "%1$s's life was drained by %2$s"; death.attack.twilightforest.lifedrain.item = "%1$s's life was drained by %2$s using %3$s";
- `twilightforest:twilight_scepter`: `{"exhaustion":0.0,"message_id":"indirectMagic","scaling":"when_caused_by_living_non_player"}`

### Sound events (25)

`sounds.json` content (vanilla `event` references can be played with the matching Bedrock vanilla sound; TF file names are listed only as names: the audio itself is All Rights Reserved and is re-synthesised by F3/the owner).

| Event | Subtitle | sounds.json entries |
|---|---|---|
| `block.twilightforest.casket.close` | subtitles.twilightforest.block.casket.close = Keepsake Casket closes | twilightforest:random/casket/close |
| `block.twilightforest.casket.locked` | subtitles.twilightforest.block.casket.locked = Keepsake Casket clicks | event block.chest.locked |
| `block.twilightforest.casket.open` | subtitles.twilightforest.block.casket.open = Keepsake Casket opens | twilightforest:random/casket/open |
| `block.twilightforest.casket.repair` | subtitles.twilightforest.block.casket.repair = Keepsake Casket repaired | twilightforest:random/casket/repair |
| `entity.twilightforest.fortification_shield.add` | subtitles.twilightforest.entity.fortification_shield.add = Fortification Shield spawns | twilightforest:random/scepter/shield_use |
| `entity.twilightforest.fortification_shield.block` | subtitles.twilightforest.entity.fortification_shield.block = Fortification Shield deflects | event entity.zombie.attack_iron_door volume=0.5 |
| `entity.twilightforest.fortification_shield.break` | subtitles.twilightforest.entity.fortification_shield.break = Fortification Shield breaks | twilightforest:random/scepter/shield_break |
| `entity.twilightforest.fortification_shield.expire` | subtitles.twilightforest.entity.fortification_shield.expire = Fortification Shield expires | twilightforest:random/scepter/shield_stop |
| `entity.twilightforest.loyal_zombie.ambient` | subtitles.twilightforest.entity.loyal_zombie.ambient = Loyal Zombie groans | event entity.zombie.ambient |
| `entity.twilightforest.loyal_zombie.death` | subtitles.twilightforest.entity.loyal_zombie.death = Loyal Zombie dies | event entity.zombie.death |
| `entity.twilightforest.loyal_zombie.hurt` | subtitles.twilightforest.entity.loyal_zombie.hurt = Loyal Zombie hurts | event entity.zombie.hurt |
| `entity.twilightforest.loyal_zombie.step` | subtitles.block.generic.footsteps | event entity.zombie.step |
| `item.twilightforest.charm.keep` | subtitles.twilightforest.item.charm.keep = Charm of Keeping returns items | event entity.zombie_villager.converted |
| `item.twilightforest.charm.life` | subtitles.twilightforest.item.charm.life = Charm of Keeping regenerates | event item.totem.use |
| `item.twilightforest.flask.break` | subtitles.twilightforest.item.flask.break = Brittle Flask shatters | event block.glass.break |
| `item.twilightforest.flask.crack` | subtitles.twilightforest.item.flask.crack = Brittle Flask cracks | event block.glass.break |
| `item.twilightforest.flask.fill` | subtitles.twilightforest.item.flask.fill = Potion Flask fills | event block.brewing_stand.brew |
| `item.twilightforest.life_scepter.drain` | subtitles.twilightforest.item.life_scepter.drain = Life Scepter drains | twilightforest:random/scepter/drain1, twilightforest:random/scepter/drain2, twilightforest:random/scepter/drain3 |
| `item.twilightforest.ore_meter.clear` | subtitles.twilightforest.item.ore_meter.clear = Ore Meter wipes information | block/crafter/craft |
| `item.twilightforest.ore_meter.crackle` | subtitles.twilightforest.item.ore_meter.crackle = Ore Meter crackles | ambient/nether/basalt_deltas/click1, ambient/nether/basalt_deltas/click2, ambient/nether/basalt_deltas/click3, ambient/nether/basalt_deltas/click4, ambient/nether/basalt_deltas/click5 |
| `item.twilightforest.ore_meter.target_block` | subtitles.twilightforest.item.ore_meter.target_block = Ore Meter locks onto block | event item.lodestone_compass.lock |
| `item.twilightforest.shield.shatter` | subtitles.twilightforest.item.shield.shatter = Wooden Shield shatters | event entity.zombie.break_wooden_door |
| `item.twilightforest.twilight_scepter.pearl_hit` | subtitles.twilightforest.item.twilight_scepter.pearl_hit = Twilight Pearl hits mob | twilightforest:random/scepter/twilight_hit1, twilightforest:random/scepter/twilight_hit2, twilightforest:random/scepter/twilight_hit3 |
| `item.twilightforest.twilight_scepter.use` | subtitles.twilightforest.item.twilight_scepter.use = Twilight Scepter throws pearl | twilightforest:random/scepter/twilight_use1, twilightforest:random/scepter/twilight_use2, twilightforest:random/scepter/twilight_use3 |
| `item.twilightforest.zombie_scepter.use` | subtitles.twilightforest.item.zombie_scepter.use = Loyal Zombie summoned | twilightforest:random/scepter/zombie |

### Tags (3)

| Tag | Members (TF generated JSON) |
|---|---|
| `block/#twilightforest:ore_meter_targetable` | #c:ores, #mc:base_stone_overworld, #mc:base_stone_nether, #mc:dirt, #c:sands, #c:sandstone/blocks, #mc:terracotta, #c:gravels, #mc:nylium, #c:ores_in_ground/root, mc:budding_amethyst, mc:calcite, mc:soul_sand, mc:soul_soil |
| `item/#twilightforest:kept_on_death` | tower_key, phantom_helmet, phantom_chestplate |
| `item/#twilightforest:scepters` | twilight_scepter, lifedrain_scepter, zombie_scepter, fortification_scepter |

### Loot tables (3)

Block loot tables summarised in the Loot column of the block table: `twilightforest:blocks/keepsake_casket`

| Table | Summary |
|---|---|
| `twilightforest:chests/casket_loot` | rolls uniform(8-16): mc:rotten_flesh \| mc:bone w3 |
| `twilightforest:entities/loyal_zombie` | no pools (drops nothing) |

### Recipes (15)

Recipe-unlock advancements (generated by F1 into the recipe JSON, R21) are listed with their recipe; criteria are the `has_*` triggers besides `has_the_recipe`.

| Recipe | Type | Ingredients | Result | Unlock advancement (criteria) |
|---|---|---|---|---|
| `twilightforest:casket_repair_recipe` | casket_repair_recipe | {} | - | - |
| `twilightforest:charm_of_keeping_2` | mc:crafting_shapeless | charm_of_keeping_1 + charm_of_keeping_1 + charm_of_keeping_1 + charm_of_keeping_1 | charm_of_keeping_2 | `twilightforest:recipes/tools/charm_of_keeping_2` (has_item: has charm_of_keeping_1) |
| `twilightforest:charm_of_keeping_3` | mc:crafting_shapeless | charm_of_keeping_2 + charm_of_keeping_2 + charm_of_keeping_2 + charm_of_keeping_2 | charm_of_keeping_3 | `twilightforest:recipes/tools/charm_of_keeping_3` (has_item: has charm_of_keeping_2) |
| `twilightforest:charm_of_life_2` | mc:crafting_shapeless | charm_of_life_1 + charm_of_life_1 + charm_of_life_1 + charm_of_life_1 | charm_of_life_2 | `twilightforest:recipes/tools/charm_of_life_2` (has_item: has charm_of_life_1) |
| `twilightforest:emperors_cloth_recipe` | emperors_cloth_recipe | {} | - | - |
| `twilightforest:equipment/fortification_scepter` | scepter_repair | {"durability":9,"repair_ingredients":[{"item":"minecraft:golden_apple"}],"scepter":"twilightforest:fortification_scepter"} | - | - |
| `twilightforest:equipment/lifedrain_scepter` | scepter_repair | {"durability":9,"repair_ingredients":[{"item":"minecraft:fermented_spider_eye"}],"scepter":"twilightforest:lifedrain_scepter"} | - | - |
| `twilightforest:equipment/twilight_scepter` | scepter_repair | {"durability":9,"repair_ingredients":[{"tag":"c:ender_pearls"}],"scepter":"twilightforest:twilight_scepter"} | - | - |
| `twilightforest:equipment/zombie_scepter` | scepter_repair | {"durability":9,"repair_ingredients":[[{"type":"neoforge:components","components":{"minecraft:potion_contents":{"potion":"minecraft:strength"}},"items":"minecraft:potion"},{"type":"neoforge:components","components":{"minecraft:potion_contents":{"potion":"minecraft:long_strength"}},"items":"minecraft:potion"},{"type":"neoforge:components","components":{"minecraft:potion_contents":{"potion":"minecraft:strong_strength"}},"items":"minecraft:potion"}],{"item":"minecraft:rotten_flesh"}],"scepter":"twilightforest:zombie_scepter"} | - | - |
| `twilightforest:essence_repair_recipe` | essence_repair_recipe | {} | - | - |
| `twilightforest:jeed/fire_resistance` | jeed:effect_provider | {"effect":"minecraft:fire_resistance","providers":[{"item":"twilightforest:charm_of_life_2"}]} [conditions [{"type":"neoforge:mod_loaded","modid":"jeed"}]] | - | - |
| `twilightforest:jeed/regeneration` | jeed:effect_provider | {"effect":"minecraft:regeneration","providers":[{"item":"twilightforest:charm_of_life_1"},{"item":"twilightforest:charm_of_life_2"},{"item":"twilightforest:blightberry"}]} [conditions [{"type":"neoforge:mod_loaded","modid":"jeed"}]] | - | - |
| `twilightforest:jeed/resistance` | jeed:effect_provider | {"effect":"minecraft:resistance","providers":[{"item":"twilightforest:charm_of_life_2"}]} [conditions [{"type":"neoforge:mod_loaded","modid":"jeed"}]] | - | - |
| `twilightforest:jeed/slowness` | jeed:effect_provider | {"effect":"minecraft:slowness","providers":[{"item":"twilightforest:lifedrain_scepter"},{"item":"twilightforest:death_tome_spawn_egg"},{"item":"twilightforest:skyberry"}]} [conditions [{"type":"neoforge:mod_loaded","modid":"jeed"}]] | - | - |
| `twilightforest:mystic_crown` | mc:crafting_shaped | 'ttt' / 't t' / 'ttt' ; t=crown_splinter | mystic_crown | `twilightforest:recipes/combat/mystic_crown` (has_item: has crown_splinter) |

### Config keys (5)

See [config.md](config.md) for the section mapping; defaults and translation ids below are TF's.

| TF key | Default | Translation id | English (config screen) |
|---|---|---|---|
| `client.fortificationShieldIndicator` | `true` | `shield_indicator` | Fortification Shield Indicator — Renders how many fortification shields are currently active on your player above your armor bar. "Turn this off if other mods render over/under it. |
| `client.fortificationShieldIndicatorInCreative` | `false` | `shield_indicator_creative` | Fortification Shield Indicator (creative) — Enables the fortification shield indicator in creative for debugging. |
| `client.prettifyOreMeterGui` | `true` | `prettify_ore_meter_gui` | Prettify Ore Meter GUI — Lines up the dashes and percentages in the Ore Meter GUI. |
| `client.totemCharmAnimation` | `false` | `totem_charm_animation` | Totem of Undying Charm Animation — Makes Charms when triggered display like the Totem of Undying instead of our own effects. |
| `common.casketUUIDLocking` | `false` | `casket_uuid_locking` | Casket UUID Locking — If true, Keepsake Caskets that are spawned when a player dies will not be accessible by other players. Use this if you dont want people taking from other people's death caskets. NOTE: server operators will still be able to open locked caskets. |

### Datapack functions (1)

- `twilightforest:give_3_shields`:
```mcfunction
twilightforest shield @p add 3 false
```

### subcommand (1)

`shield`

## Appendix B: Strings (91 en_us keys owned)

Keys already shown above (block/item/entity names, subtitles) are not repeated. The 29 Bedrock locales are generated from TF's lang files (R9); owners use these keys with the English text as fallback.

| Key | English |
|---|---|
| `block.twilightforest.casket.locked` | This Casket can only be opened by %s! |
| `block.twilightforest.chipped_keepsake_casket` | Chipped Keepsake Casket |
| `block.twilightforest.damaged_keepsake_casket` | Damaged Keepsake Casket |
| `config.twilightforest.casket_uuid_locking` | Casket UUID Locking |
| `config.twilightforest.casket_uuid_locking.tooltip` | If true, Keepsake Caskets that are spawned when a player dies will not be accessible by other players. Use this if you dont want people taking from other people's death caskets. NOTE: server operators will still be able to open locked caskets. |
| `config.twilightforest.prettify_ore_meter_gui` | Prettify Ore Meter GUI |
| `config.twilightforest.prettify_ore_meter_gui.tooltip` | Lines up the dashes and percentages in the Ore Meter GUI. |
| `config.twilightforest.shield_indicator` | Fortification Shield Indicator |
| `config.twilightforest.shield_indicator_creative` | Fortification Shield Indicator (creative) |
| `config.twilightforest.shield_indicator_creative.tooltip` | Enables the fortification shield indicator in creative for debugging. |
| `config.twilightforest.shield_indicator.tooltip` | Renders how many fortification shields are currently active on your player above your armor bar. "Turn this off if other mods render over/under it. |
| `config.twilightforest.totem_charm_animation` | Totem of Undying Charm Animation |
| `config.twilightforest.totem_charm_animation.tooltip` | Makes Charms when triggered display like the Totem of Undying instead of our own effects. |
| `item.twilightforest.emperors_cloth.desc` | Shrouded |
| `item.twilightforest.flask.empty` | Empty |
| `item.twilightforest.flask.empty_description` | Can hold multiple potion doses |
| `item.twilightforest.flask.no_refill` | Cannot be refilled |
| `item.twilightforest.moon_dial.phase_0` | Full Moon |
| `item.twilightforest.moon_dial.phase_1` | Waning Gibbous |
| `item.twilightforest.moon_dial.phase_2` | Third Quarter |
| `item.twilightforest.moon_dial.phase_3` | Waning Crescent |
| `item.twilightforest.moon_dial.phase_4` | New Moon |
| `item.twilightforest.moon_dial.phase_5` | Waxing Crescent |
| `item.twilightforest.moon_dial.phase_6` | First Quarter |
| `item.twilightforest.moon_dial.phase_7` | Waxing Gibbous |
| `item.twilightforest.moon_dial.phase_unknown` | Moon phase indeterminate |
| `item.twilightforest.moon_dial.phase_unknown_fools` | 404 moon not found |
| `item.twilightforest.pocket_watch.desc` | It seems to always be running late |
| `item.twilightforest.scepter.desc` | %s charges left |
| `misc.twilightforest.ore_meter_header_block` | Block |
| `misc.twilightforest.ore_meter_header_count` | Count |
| `misc.twilightforest.ore_meter_header_ratio` | Ratio |
| `misc.twilightforest.ore_meter_loading` | Loading |
| `misc.twilightforest.ore_meter_new_range` | Range set to %s chunks |
| `misc.twilightforest.ore_meter_no_blocks` | No blocks found nearby |
| `misc.twilightforest.ore_meter_range` | Radius: %s, Origin: [%s, %s] |
| `misc.twilightforest.ore_meter_ratio` | (%s%%) |
| `misc.twilightforest.ore_meter_separator` | - |
| `misc.twilightforest.ore_meter_set_block` | Targeted block set to %s |
| `misc.twilightforest.ore_meter_targeted_block` | Targeted block: %s |
| `misc.twilightforest.ore_meter_total` | Total blocks scanned: %s |

