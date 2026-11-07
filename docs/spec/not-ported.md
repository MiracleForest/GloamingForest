# Not ported (F16 `spec-items`)

What of The Twilight Forest 4.8 (`9e40a757`, MC 1.21.1) GloamingForest does **not** reproduce, or reproduces only in a
degraded form, with the reason and the workaround. Owned by F16 (W0b); W1 units add their own entries through their
`docs/units/<unit>.md` and A7 (assets, licence and vanilla-client matrix audit) and A8 fold them here in W2 (the units
merged by W2 round 1 are folded in sections 4 and 6). Every row names the owner that decides or implements the
workaround. "Client mod" = the GloamingForest client build installed; "vanilla" = an unmodified Bedrock client joining
a BDS (best effort, AGENTS.md). Unfinished work that is not a platform, TF or licence limit is listed separately in
section 7, so that "every content item implemented or listed with a reason" stays checkable.

The ids F16 owns (the OptiFine config key, the 127 compat / tips strings and the four unimplemented structure keys)
are listed in the appendices; none of them has runtime behaviour in GloamingForest.

## 1. Mod compatibility layers (`src/main/java/twilightforest/compat/**`, compat data)

| TF feature | Size in TF 4.8 | Status | Workaround |
|---|---|---|---|
| JEI plugin (`compat/jei`) | 28 classes | not ported (no JEI on Bedrock) | Bedrock recipe book shows vanilla-type recipes (F1 import); custom recipe types (drying, uncrafting, travellers' gear modifiers, repairs) are explained by item tooltips (F9 `item::addTooltip`) and the uncrafting table itself (U13) |
| REI plugin (`compat/rei`) | 26 classes | not ported | same as JEI |
| EMI plugin (`compat/emi`) | 21 classes | not ported | same as JEI |
| Recipe viewer constants (`compat/RecipeViewerConstants`) | 1 class | not needed | - |
| Jade plugin (`compat/jade`) | 5 classes; keys `config.jade.plugin_twilightforest.chiseled_bookshelf_spawner`, `config.jade.plugin_twilightforest.drying_rack`, `config.jade.plugin_twilightforest.quest_ram_wool`, `jade.drying_rack.remaining` | not ported | optional: the client mod may show the same three facts (drying time left, quest ram wool, bookshelf spawner) in a look-at HUD later (F12 `hud::addLayer`); not required for parity |
| The One Probe plugin (`compat/top`) | 4 classes | not ported (commented out in TF's IMC) | - |
| Curios (`compat/curios`, `data/twilightforest/curios/entities`) | 8 classes + slot data | not ported | charms work from the inventory (U9), cicada-on-head sound only from the helmet slot (U6) |
| Cosmetic Armor (`compat/CosmeticArmorCompat`) | 1 class | not ported | emperor's cloth (U9) hides armor with the client mod |
| Default modified travellers' gear getter (`compat/common`) | 1 class | not ported (recipe-viewer helper) | - |
| Museum Curator exhibits (`data/twilightforest/museumexhibits/**`) | 67 JSON files, 22 `museumcurator.*` keys | not ported | none |
| Tips mod tips (`assets/twilightforest/tips/*.json`) | 99 tips, 99 `twilightforest.tips.*` keys | not ported | candidate for loading-screen tips or an in-game tips page later (F12; TF has no tips subcommand, so a command for it would be a GloamingForest addition with its own owners.tsv `subcommand` row, R14); the strings stay in the lang files |
| JEED effect providers (`data/twilightforest/recipe/jeed/*.json`, type `jeed:effect_provider`, condition `neoforge:mod_loaded jeed`) | 13 files (`twilightforest:jeed/{blindness,fire_resistance,frosted,hunger,jump_boost,mining_fatigue,night_vision,poison,regeneration,resistance,slowness,strength,wither}`, owned by U6, U9, U16, U17, U27 per owners.tsv) | inert | the condition is never true; owners keep the ids (R1) but implement nothing; importers may skip the type |
| Other mods' tags (`data/{aether,alexscaves,ars_nouveau,artifacts,create,createaddition,farmersdelight,forbidden_arcanus,immersiveengineering,randomium}/tags/**`) | tag files | imported as data only (F1) | harmless without those mods |
| Shield Parry mod detection (`EntityEvents.SHIELD_PARRY_MOD_LOADED`) | - | always "not loaded" | TF's own parrying applies (U8) |

## 2. Client and loader features without a Bedrock counterpart

| TF feature | Status | Workaround / owner |
|---|---|---|
| `OptifineWarningScreen` + config `client.disableOptifineNagScreen` (translation id `optifine`, keys `config.twilightforest.optifine`, `config.twilightforest.optifine.tooltip`, `gui.twilightforest.optifine.*`) | not ported: there is no OptiFine on Bedrock | the key is not part of any config section (config.md: "(not ported)") |
| `JappaPackReloadListener` (switches entity models when the "Jappa" programmer-art pack is loaded) | not ported | the imported default textures/models only; a Bedrock resource pack can override them |
| `ModUpdateURLInterceptor` (NeoForge update checker URL) | not ported | releases are announced through the GloamingForest repository |
| NeoForge config files and config screen (`TFCommonConfig`, `TFClientConfig`) | replaced | per-unit JSON sections + `/twilightforest config` (F0, config.md) |
| Skylight world preset | replaced | a `terrain` config option (U2), Bedrock has no world-preset choice for custom dimensions |
| Data pack overrides of TF registries (wood palettes, biome stacks, restrictions, magic paintings, travellers modifiers, ...) | baked | GloamingForest reads the imported JSON from its own data folder; server owners may edit those files |
| Creative tabs (`TFCreativeTabs`) | replaced | Bedrock creative catalog groups (F9 `catalog`), no top-level tabs |
| `mod_exists` loot condition | reduced | recognises only `twilightforest` and `minecraft` (F10) |
| Item durability / flask dose bar colours, title splash, Halloween visuals | client mod only | F9 `item::setBar`, F12 |
| Key bindings (`TFKeyBinds`: zoom Z, item display map cycle C, swap hotbar V, red thread vision unbound) | keyboard defaults only | touch / controller players use item-use fallbacks (U11) |
| ASM transformers (`asmhooks/**`: water walking, unrestrained movement, efficient eater exhaustion, straight-ahead FOV, emperor's cloth armor/elytra/cape hiding, leash pathfinding override, giant renderers, ...) | re-implemented per unit | exact with the client mod; approximations or none for vanilla (rows below) |

## 3. Content that TF 4.8 registers but does not implement

| TF id / feature | Status |
|---|---|
| Structure `twilightforest:quest_island` | key only, no structure / set / pieces in TF 4.8 (the set registration is commented out in `TFStructureSets`): not ported. `LegacyLandmarkPlacements` still maps the `twilightforest:lake` biome centre to `quest_island`; with no registered structure TF places nothing there (no landmark's `valid_*_biomes` tag contains the lake: neither the variety landmarks picked by `pickVarietyLandmark` nor the biome-locked ones placed by `forceStructureForCenters`), and the magic map shows no icon (`structureRegistry.getHolder` is empty). F6 and U13 must keep the key in `pickForChunk` and treat it as absent without warnings: no generator, no map decoration, no hint, no log line. `/tf center` (F6) at a lake centre names it as TF does, `structure.twilightforest.quest_island` (no lang key in TF, so the raw key is shown) |
| Structure `twilightforest:druid_grove` | key only: not ported |
| Structure `twilightforest:floating_ruins` | key only: not ported |
| Structure `twilightforest:world_tree` | key only: not ported |
| Boggard entity | commented out in `TFEntities` / attributes: not registered, not ported |
| Ten `*_miniature_structure` blocks (hedge maze, hollow hill, quest grove, mushroom tower, hydra lair, goblin stronghold, yeti cave, aurora palace, troll cave cottage, final castle) | commented out in `TFBlocks`: not registered (owners.tsv counts 523 blocks) |
| Sound event `GHAST_TRAP_ACTIVE` | commented out: 328 sound events |
| PlateauBoss (`twilightforest:plateau_boss`) and its spawner (`final_boss_boss_spawner` never spawns) | kept as TF's placeholder (U30 / U5) |
| Final castle "work in progress" message on `progression_end` | ported as is (U14) |
| Uncraftable quest ram banner pattern (`quest_ram_banner_pattern`, no recipe or loot) | ported as is: creative / commands only (U5) |

## 4. Platform deviations for vanilla Bedrock clients (best effort)

The client mod restores full parity for everything in this table unless the row says otherwise.

| Area | Vanilla client behaviour | Owner |
|---|---|---|
| Sounds and music (229 `.ogg`, 328 events, 9 discs) | All Rights Reserved in TF: every event and track is re-synthesised (F3 baseline, unit recipes, U10 music); never identical to TF, for any client | F3, owners, U10, A7 |
| Structure templates (274 `.nbt`) | All Rights Reserved: re-designed as `.gft` templates with TF's footprint, slots, spawners, chests and loot | F7, U19-U22, U30 |
| Locales | 99 TF lang files -> the 29 Bedrock locales; `en_ud` and others dropped | F1 |
| TF enchantments (`fire_react`, `chill_aura`, `destruction`, `renewal`) and the `frosted` effect | stored in item user data with server-written lore and glint; frosted uses a hidden Slowness carrier; exact tooltips and HUD icon only with the client mod | F9, U10 |
| Dimension height -32..256 and fixed time | spike S1 answered: a native Bedrock custom dimension (F4's BP), so every 1.26.50+ client knows its id and height; the fixed time is a hook plus per-player clock packets for vanilla clients. Remaining limits for every client: a non-player entity crossing a portal arrives with a new unique id; vanilla logic that reads the level time (not the dimension's time of day) follows the overworld clock. Vanilla only: the portal overlay is a camera fade that can only grow to opaque ([S1](../spikes/S1.md)) | F4 |
| Sky (no sun/moon, bright stars), aurora, locked-biome weather (blizzard, mosquitoes, ashes, dark streams, big rain), structure sparkles, spooky fog smoothing | no GLSL on Bedrock: client mod renders CPU meshes; vanilla gets client-biome sky/fog colours and `PlayerFogPacket` only | U15, F12 |
| Client-authoritative movement: double jump, sidestep, gradual glide, agile ranger, straight ahead, step-up, water walk, unrestrained, slimy soles bounce, feather fan launch, yeti throw, custom knockback | exact only with the client mod; vanilla gets motion packets on input edges (double jump), hidden Slow Falling for glide (sinks about 0.06 blocks a tick against TF's 0.36), no sidestep, no step-up, no water walk, unrestrained only as the cobweb / berry bush / powder snow slowdown immunity (no friction, slime, honey or soul sand change); high jump, slimy soles, straight ahead, swift swim, agile ranger and aquatic agility come from the server's simulation ([tgmove.md](../units/tgmove.md)) | U12, U8, F12 |
| Climbable hollow logs, rope climbing / tightrope walking | climbing only with the client mod (Bedrock has no data-driven climbable block); vanilla cannot climb them | U4, U5 |
| Custom screens: uncrafting table, advancement tab, item display HUD, ore meter HUD, belt / goggles bundle-like slots, flask filling by inventory click | DDUI / forms / chest-like container skins on vanilla; flasks fill by using a potion with the flask in the other hand | U13, U14, U11, U9, F12 |
| Advancement frame textures | Mojang's (ARR): redrawn for the client-mod tab | U14, A7 |
| Boss bars | exact RGB with the client mod, nearest of 8 BossBarColor values on vanilla | F8 |
| Map decorations (15 TF types), magic/maze/ore map rendering | mapped to the 30 vanilla map decoration types; colours from the vanilla palette | U13, F6 |
| Giants' skins (`giantSkinUUIDs`) | **not restored by the client mod either**: Bedrock cannot bind another player's skin to a mob, so giants always render with the default skin; the config key is kept but unused ([giants.md](../units/giants.md)) | U29 |
| Banner patterns | spike S6 answered: both builds append TF's nine patterns to the pattern registry (loom works); vanilla clients keep such a banner but draw it as a plain field with an untranslated line | U5, F9 |
| Trim materials | spike S6: clients draw armour trimmed with a non-vanilla material as an empty icon (client mod and vanilla alike; no exported icon generator), so U7 does not register TF's six materials for the smithing table; drawing them is client-mod work not done (section 7) | U7, F9 |
| Music disc items | spike S6: BP `minecraft:record` without `sound_event` (the BP binds it to the LevelSoundEvent enum); the jukebox plays TF's song by name through a hook + PlaySoundPacket, heard by every client | U10, F9 |
| Block entities (signs, chests, casket, drying racks, jars, candles, critters, trophies, red thread, skull chests) | spike S3 answered with a fallback: storage in `minecraft:block_entity` containers (TF chests) or BlockData; the client mod opens TF's chest screen; vanilla gets the game's chest screen through an invisible chest-minecart proxy for 27 / 54 slots and a form for other sizes (casket and skull chest: 45), with no TF title skin; vanilla sees static geometry where no data-driven actor exists (jar contents, critter animation, trophy GUI spin); drying rack items are a display entity for vanilla ([S3](../spikes/S3.md)) | F10, F11, owners |
| Emperor's cloth hiding armor | **not yet with the client mod either** (no armour render hook in F12; section 7); the recipe, tooltip and washing work | U9, F12 |
| Glove first-person overlay, goggles zoom texture, gloves / belt worn models | the state is synced to every client (mark variant); drawing it waits for F2's attachables (section 7) | U11, F2 |
| Travellers' wings animation | client mod only (it patches the animation into the attachable on its side); vanilla sees the wings in the rest pose | U12 |
| Lich attributes `twilightforest:clone_count`, `twilightforest:minion_count`, U9's `twilightforest:max_shield_strength` | **not restored by the client mod**: Bedrock has no custom attributes and no `/attribute`; actor data with TF's defaults (2, 9, 6) that cannot be changed in game ([lich.md](../units/lich.md)) | U21, U9 |
| Mason jar dynamic light from its contents | light 0 on vanilla unless the permutation budget allows a light state | U6 |
| No-template smithing (emperor's cloth smithing) | Bedrock smithing needs a template: special crafting fallback | U7, F9 |
| Crafted items with default enchantments (24 recipes) | enchanted after crafting by a server hook; recipe book preview shows no enchantments | U7, F9 |
| Arctic armor dye colour | `minecraft:dyeable`: dyed and washed in cauldrons (Bedrock's leather rules; Java's crafting-grid dyeing has no Bedrock equivalent for non-leather items); the inventory icon shows the dye for every client; the **worn model is undyed for every client** (F2's generated attachable, section 7) ([armory.md](../units/armory.md)) | U7, F2 |
| Keybind-only actions (zoom, red thread vision, map cycle, hotbar swap) | sneak-use fallbacks for vanilla: the goggles open a form (store / take / cycle the map; no item display HUD), a belt or the wings swap the hotbar; zoom and red thread vision are client mod only. Bedrock's own Z binding (mob effects screen) competes with the zoom key ([tgear.md](../units/tgear.md)) | U11 |

## 5. How to add an entry

A unit that cannot reproduce a TF behaviour exactly records it in `docs/units/<unit>.md` (section "Not ported") with
the TF class, the reason (platform, licence, spike result) and the workaround; A7/A8 copy it here in W2 with the owner.
Nothing in TF 4.8 may be dropped silently: the W2 "registered but unimplemented" report (R17) must match this list.

W2 round 1 folded the 20 unit docs present on `main` then (`docs/units/*.md`, sections "Not ported", "Deviations",
"Bedrock limits", "Bedrock adaptation(s)", "Vanilla-client deviations", "Vanilla fallbacks", "Vanilla clients (best
effort)", "Deviations from TF", and deviations stated in their running text): rows that refine an area of section 4
updated it there, the rest are section 6; unfinished work went to section 7. Folding rules for later rounds:

- Only a real limit goes into sections 1-4 / 6: the Bedrock platform or LeviLamina cannot do it, a vanilla client cannot
  be told it, TF 4.8 itself does not implement it, or a licence forbids the asset. A row that reproduces TF's result
  by another path ("different path, same result") is not a deviation and is not folded.
- Work a unit or a contract owner simply has not done yet goes into section 7 with its owner, and leaves it when done.
- The unit doc stays the detailed record; each row here links it and stays one line. Update a row instead of adding a
  second one for the same TF feature.

## 6. Unit deviations (W1, folded in W2 round 1)

Deviations the W1 units recorded that are not already a section 4 row. Why: **P** = the Bedrock platform or LeviLamina
cannot do it TF's way (every client); **V** = a vanilla client cannot be told it (client mod restores parity); **L** =
licence. "Client mod": *parity* = exact with the client mod, *same* = the deviation holds with the client mod too.
Details, measurements and the TF classes in full are in the linked unit doc.

| Owner | TF feature / class | What GloamingForest does instead | Why | Client mod |
|---|---|---|---|---|
| U1 [biomes](../units/biomes.md) | Position-dependent grass / foliage (spooky forest, dark forest centre, enchanted forest bands) | vanilla: static biome colours (enchanted forest `#00FF80`, dark forest centre `#E94E14` / `#554114`) | V: Bedrock blends static biome colours | parity |
| U1 [biomes](../units/biomes.md) | Leaves in cold biomes (Java has no seasons) | vanilla: Bedrock's seasons-map leaf colour | V: the foliage colour is read as a seasons-map place | parity |
| U1 [biomes](../units/biomes.md) | Biome water colours (`water_color`) | TF's colour is in every data path, but 1.26.51 draws the Twilight Forest's water in one colour (measured `#436168`) | P: no water tint path exposed in the 26.51.7 headers | same |
| U1 [biomes](../units/biomes.md) | Underwater fog eye adaptation (Java's 5-30 s ramp from 24 blocks) | fixed fog end (96 blocks, 81.6 in `has_closer_water_fog` biomes) | P: static fog definitions | same |
| U3 [trees](../units/trees.md) | Leaf runes (`ClientLevel#animateTick`, uncapped) | vanilla: server samples, at most 16 runes per player a tick | V | parity |
| U3 [trees](../units/trees.md) | Magic leaves' position colours (ColorHandler) | vanilla: the biome foliage colour | V: Bedrock's tint methods are all biome colours; a pre-tinted texture would be tinted again | parity |
| U3 [trees](../units/trees.md) | `StructureTemplate.updateShapeAtEdge` | none: the game connects shapes, F11 recomputes leaf distances on neighbour changes | P: no Bedrock counterpart | same |
| U4 [woodwork](../units/woodwork.md) | Banister extension posts, hollow log selection shapes | boxes stay inside the block (the extension's posts below have none); one selection box (hollow logs select as a full block); collisions are Java's | P: one in-block selection box per block | same |
| U4 [woodwork](../units/woodwork.md) | Hollow log crawling (swimming pose on a sneak-click) | the server moves the player in; the client mod sets the pose; vanilla crawls only where the game itself does | V: the pose is the client's (PlayerAuthInput) | parity |
| U4 [woodwork](../units/woodwork.md) | TF boat models (BoatModel / ChestBoatModel with TF textures) | vanilla: drawn as the game's boats (runtime id `minecraft:boat`) | V | parity |
| U4 [woodwork](../units/woodwork.md) | Drying rack item (DryingRackRenderer) | vanilla: a display entity holding the item in the game's third-person hand pose | V: no block entity renderer | parity |
| U7 [armory](../units/armory.md) | Attack speed | none | P: Bedrock has no attack cooldown | same |
| U7 [armory](../units/armory.md) | Fractional attack damage (shovels 4.5 / 5.5), armour toughness | integer item damage plus `attackBonus` for the half point; toughness added by `ext/armory`, rounded to Bedrock's integer total | P: integer item damage, no toughness on data items | same |
| U9 [artifacts](../units/artifacts.md) | `SpawnCharmPacket` (client-side charm entity) | the server spawns `twilightforest:charm_effect`, so other players see it too | V: a vanilla client cannot be told to spawn a client entity | same |
| U9 [artifacts](../units/artifacts.md) | Lifedrain beam (client packet, coloured particles) | server-drawn `magic_effect` particles, colour from the sign of their velocity | V: fx gives TF particles no colour variable on vanilla | same |
| U9 [artifacts](../units/artifacts.md) | Lifedrain killing blow's death sound | none | P: death sounds are per entity in the resource pack | same |
| U9 [artifacts](../units/artifacts.md) | Keepsake casket placement height (logical height) | capped by the dimension's top | P: no logical height | same |
| U9 [artifacts](../units/artifacts.md) | Damaged casket item names (chipped / damaged) | the first lore line | P: no per-stack name function | same |
| U9 [artifacts](../units/artifacts.md) | Potion flask tooltip image and potion-coloured icon | lore (potion name, dose meter); icons without the potion colour | P: static item icons, one-string hover text | same |
| U9 [artifacts](../units/artifacts.md) | Mystic crown's 3D inventory model | the crown splinter's icon | P: no 3D item model in inventory slots | same |
| U9 [artifacts](../units/artifacts.md) | Pocket watch Mining Fatigue immunity | the effect instance is removed on the next tick | P: no "effect applicable" event | same |
| U9 [artifacts](../units/artifacts.md) | Fortification shield HUD | vanilla: the count on the action bar while it changes | V | parity |
| U11 [tgear](../units/tgear.md) | `/twilightforest travellers_gear` hidden from non-operators | "You need operator permission." | P: LeviLamina overloads carry no permission of their own | same |
| U11 [tgear](../units/tgear.md) | Broken pieces (name function, no armour) | custom name "<name> (Broken)"; armour taken out of the damage, the armour bar still shows the item's protection | P: no per-stack name function; protection is per item | same |
| U11 [tgear](../units/tgear.md) | Efficient eater (exhaustion divided in PlayerHooks) | halves ExhaustionComponent's movement coefficients | P: Bedrock's FoodExhaustionSystem | same |
| U11 [tgear](../units/tgear.md) | Arrow magnetism (projectile impact) | arrows tracked from first sighting; arrows stuck earlier (or loaded from disk) never count | P: no projectile-impact event | same |
| U11 [tgear](../units/tgear.md) | Perfect dodge (impact cancelled, projectile flies on) | damage cancelled, the projectile's motion given back a tick later; an arrow may still vanish | P: Bedrock removes an arrow that hit an entity before units see it | same |
| U11 [tgear](../units/tgear.md) | All-night goggles: phantom spawn denied per insomniac player | cancelled when every candidate player within 64 blocks wears active goggles | P: Bedrock does not tell whose phantom spawn it is | same |
| U11 [tgear](../units/tgear.md) | Piglin neutrality (incl. chest opening / gold mining anger) | target filter only; Bedrock's data-driven anger is untouched | P | same |
| U11 [tgear](../units/tgear.md) | Goggles zoom FOV x 0.3 | camera `fov_set`, clamped to [30, 110]: TF's 21 degrees is drawn at 30 (exact from a client FOV of 100) | P: the vanilla camera's FOV range; the projection is unreachable otherwise | same |
| U11 [tgear](../units/tgear.md) | Tooltip images (belt, item display) | listed as lines / lore | P: one-string hover text (an F12 hover-panel hook is section 7) | same (not yet) |
| U11 [tgear](../units/tgear.md) | Item display map background (`map_background_checkerboard`) | Bedrock's `map_background` | L: a Mojang Java Edition resource | same |
| U11 [tgear](../units/tgear.md) | Grindstone / anvil refusal (TravellersGearEvents) | vanilla may preview a result the server refuses when taken | V | parity |
| U11 [tgear](../units/tgear.md) | Red thread (RedThreadRenderer, multi-box shape) | vanilla: the centre pieces and same-block connections only; one selection box per state (union of the face slabs) | V (connections), P (selection box) | parity (drawing) |
| U11 [tgear](../units/tgear.md) | Gear icons (broken, modifier overlays, spool sizes), Shift tooltips | vanilla: the base icon and lore that always shows the descriptions; client mod: layered icons in GUI slots, base icon in hand / ground / frames | P: static item icons; V: lore cannot follow Shift | partial |
| U12 [tgmove](../units/tgmove.md) | `leashed_pathfinder_override` (no close-range leash pathing) | leash soft distance at least 6 and paths ending near the holder dropped | P: Bedrock's leash presets | same |
| U14 [progression](../units/progression.md) | Java's `announceAdvancements` gamerule | GloamingForest bool gamerule through `/twilightforest gamerule` | P: Bedrock's `/gamerule` takes no new rules | same |
| U14 [progression](../units/progression.md) | `progression_end` message link | the URL follows the text | P: Bedrock chat has no links | same |
| U14 [progression](../units/progression.md) | `ui.toast.challenge_complete` | original synthesized `gf.progression.challenge_complete` | L: Mojang (All Rights Reserved) | same |
| U14 [progression](../units/progression.md) | AdvancementToast / LockedBiomeToast | vanilla: the game's text toast (yellow / pink first line), countdown run by the server | V | parity |
| U15 [atmosphere](../units/atmosphere.md) | Sky shaders (stars, aurora, void plate) under Vibrant Visuals | drawn in the world pass and lit by the scene light (aurora as darker ribbons, stars as dark points); VV's own dusk band shows | P: no shader in VV's deferred pipeline | same |
| U15 [atmosphere](../units/atmosphere.md) | FogHandler's terrain and sky fog per rendered frame | one distance fog with FogHandler's planes, stepped in 1/60 s frames; planes x3 under VV | P: Bedrock has one distance fog | same |
| U16 [fauna](../units/fauna.md) | One goal selector with shared flags | BP behaviours and F8 native goals in two selectors that share no flags | P | same |
| U16 [fauna](../units/fauna.md) | Second random stroll goal (squirrel 1.25, dwarf rabbit 1.0) | not ported | P: one `behavior.random_stroll` per entity | same |
| U16 [fauna](../units/fauna.md) | Walk target values (stroll spot choice) | used for spawning only; Bedrock's stroll picks its own spots | P | same |
| U16 [fauna](../units/fauna.md) | Mosquito swarm without fire overlay | the fire overlay shows when burning | P: no render hook for it | same |
| U16 [fauna](../units/fauna.md) | Baby models with bigger heads | half-scale babies | P: `minecraft:scale` only | same |
| U17 [hostiles](../units/hostiles.md) | Skeleton druid loot pickup (`setCanPickUpLoot`) | druids never pick up items | P: Bedrock's equip behaviour does not follow Java's chance | same |
| U17 [hostiles](../units/hostiles.md) | `Mob#doHurtTarget` replacements (wraith, pinch beetle, fire beetle) | vanilla hit cancelled, TF damage applied on the next tick | P: one hurt per victim in the engine's melee | same |
| U17 [hostiles](../units/hostiles.md) | Druid riding a vanilla skeleton on a king spider | the druid rides the spider | P: Bedrock skeletons cannot carry riders | same |
| U17 [hostiles](../units/hostiles.md) | Wraith home for `MobSpawnType.STRUCTURE` | home from a spawner spawn or set by the placing structure | P: Bedrock keeps no spawn reason | same |
| U18 [minorlandmarks](../units/minorlandmarks.md) | Hedge punching re-check (`player.swinging`, ray trace) | the server's swing flag or still mining that hedge; a stepped 5-block eye ray | P | same |
| U21 [lich](../units/lich.md) | Rising zombie block-break pitch 0.7..0.85 | the engine's pitch | P: `LevelSoundEvent::BreakBlock` has no pitch | same |
| U21 [lich](../units/lich.md) | `LichMinionModel` colour multiply (red / blue x 0.5) | overlay blend: red and blue exact, green pulled toward 0.5 | P: no colour multiplier for opaque entity materials | same |
| U21 [lich](../units/lich.md) | Lich bolt / bomb pick radius 1.0 | `minecraft:custom_hit_test` of 2.25 blocks | P: box hit tests only | same |
| U22 [lichdecor](../units/lichdecor.md) | `set_loot` / `clear_loot` not flagged for saving | always saved | P: no BlockData write that skips saving | same |
| U22 [lichdecor](../units/lichdecor.md) | `set_loot minecraft:...` (Java vanilla tables) | reported as unknown | L: no Java vanilla loot tables shipped (Mojang data) | same |
| U22 [lichdecor](../units/lichdecor.md) | Particle option codecs (DataFixerUpper) | required fields checked only; the error lists the missing keys without codec context | P: no DFU codecs | same |
| U22 [lichdecor](../units/lichdecor.md) | `<loot>` resource-location argument | a file-path token | P: Bedrock string arguments stop at `/` | same |
| U22 [lichdecor](../units/lichdecor.md) | Candelabra `waterlogged` state | water in the liquid layer counts as waterlogged | P | same |
| U22 [lichdecor](../units/lichdecor.md) | Player skull candle owner profile | none kept | P: Bedrock heads carry no player profile | same |
| U23 [labyrinth](../units/labyrinth.md) | `MazeSlimeBlock` sticky for pistons | not sticky | P: custom blocks cannot be sticky | same |
| U23 [labyrinth](../units/labyrinth.md) | Slime block bounce / slowdown | players bounce but are not slowed, mobs are slowed but do not bounce, items and projectiles neither | P: player movement is client-side; a per-tick sweep of all actors | same |
| U23 [labyrinth](../units/labyrinth.md) | MinoshroomMushroomLayer | not drawn | P: a render controller draws one geometry with one texture (block render: section 7) | same (not yet) |
| U23 [labyrinth](../units/labyrinth.md) | Minotaur charge walk-animation speed-up | limbs at a fixed 1.2 blocks a tick while charging | P: the walk animation state is the client's | same |
| U23 [labyrinth](../units/labyrinth.md) | Slime squish (client fields) | Molang approximation in the client entity | P | same |
| U26 [darktower](../units/darktower.md) | `InfestedBlock#spawnAfterBreak` only for world breaks | infested towerwood replaced by another unit or `/setblock` / `/fill` also spawns a borer | P: removals come without their cause | same |
| U26 [darktower](../units/darktower.md) | ReactorDebrisRenderer face crops | faces scaled over the sub-box, not cropped; inner faces not drawn | P: fixed geometry UVs | degraded |
| U26 [darktower](../units/darktower.md) | Reactor debris on vanilla clients | the blank block model for its 3 seconds | V: no block renderer | degraded |
| U26 [darktower](../units/darktower.md) | Ur-Ghast tantrum rain halved in fast graphics | not reproduced | P: no fast-graphics rain density switch | same |
| U27 [caves](../units/caves.md) | Troll's held rock (a ThrownBlock riding the troll) | drawn on the troll by a render controller | P: no actor for a riding block | same |
| U27 [caves](../units/caves.md) | Thrown blocks / falling ice block models | a cube with the block's vanilla texture chosen by a property (fixed block set) | P: entities cannot draw a block model | same |
| U27 [caves](../units/caves.md) | Alpha yeti ground pound (`causeFallDamage`) | on every landing while rampaging | P: no fall event below 3 blocks | same |
| U28 [aurora](../units/aurora.md) | Aurora blocks' per-position colours | vanilla: baked textures | V | parity |
| U28 [aurora](../units/aurora.md) | Auroralized glass beacon colour | vanilla: the engine's white beam | V: no beacon colour block component; each client computes the beam | parity |
| U28 [aurora](../units/aurora.md) | Aurora block 10-tick break with `progress_glacier` | vanilla clients breaking client-authoritatively mine at the engine's speed | V | parity |
| U28 [aurora](../units/aurora.md) | Snow guardian gear enchant chance (chunk inhabited time) | without inhabited time | P: Bedrock has none | same |
| U28 [aurora](../units/aurora.md) | Snow particle motion (per-tick drag) | the continuous form (time constant 2.8 against 3.3 ticks) | P: particle engine motion | same |
| U28 [aurora](../units/aurora.md) | Unstable ice core map colours (MapColor) | Java's colours for the common blocks, Bedrock's map colour otherwise | P: no Java MapColor | same |
| U29 [giants](../units/giants.md) | Cloud rain / snow sheets (CloudEvents rendering) | falling particles; vanilla gets them from the server | P | same |
| U29 [giants](../units/giants.md) | Entity wetness under rainy clouds (enderman / blaze damage) | not reproduced | P: no `isRainingAt` hook | same |
| U29 [giants](../units/giants.md) | Giant pickaxe / sword +2.5 interaction range | not reproduced | P: no interaction range attribute | same |
| U29 [giants](../units/giants.md) | Giant block outline | drawn besides Bedrock's own highlight | P: the highlight cannot be replaced | same |
| U29 [giants](../units/giants.md) | Beanstalk rumble camera shake | client mod only (`giants.rumble`); dust for everyone | V | parity |

## 7. Open implementation gaps (not platform limits)

Work that is possible on Bedrock but not done on `main` at W2 round 1; each leaves this list when it lands.

| Owner | Gap (reported by) |
|---|---|
| F2 | Travellers' gear worn models: gloves / slim gloves, belt on the wings, goggles `travellers_layer_1_down` from the synced mark variant (U11, [tgear.md](../units/tgear.md)) |
| F2 | Arctic armour worn model ignores the dye; fiery armour worn model not full bright (U7, [armory.md](../units/armory.md)) |
| F5 | Spawn costs: `SpecialSpawnFn` cannot tell a pack's type pick from a member check, so another type may be picked (U1, [biomes.md](../units/biomes.md)) |
| F5 | The natural spawner does not check `BlockState#isValidSpawn` (U3 works around it with special spawn lists) (U3, [trees.md](../units/trees.md)) |
| F5 / A8 | `minecraft:tree` writes no leaf distances; U3's temporary `gf_trees:update_leaves` data patch to drop once it does (U3) |
| F11 | Leaf distances recomputed a tick after tree placement (Java's UPDATE_KNOWN_SHAPE keeps TF's), logged as `PARITY GAP (F11)` (U3) |
| F12 / U9 | Armour render hook for emperor's cloth hiding (U9, [artifacts.md](../units/artifacts.md)) |
| F12 / U11 | Hover-panel image hook for TF's tooltip images; current key binding query for `/twilightforest travellers_gear` key names (U11) |
| F12 / U14 | Shared toast slot table between F12's and U14's toasts (U14, [progression.md](../units/progression.md)) |
| F12 / U23 | Minoshroom mushroom layer (block render on the torso and head bones) (U23, [labyrinth.md](../units/labyrinth.md)) |
| F8 / U9 | A dispensed twilight scepter bolt needs a player within 64 blocks as shooter (U9) |
| F0 / F1 | Assemble's dangling-reference check knows no jigsaw junction or template pool ids, so U19 uses `gloamingforest:camp/...` and U21 `gf:lich_tower/...` instead of TF's names (U19, U21) |
| F0 | owners.tsv rows for `trigger twilightforest:flask_doses_reset`, `gamerule announceAdvancements`, `sound_event gf.progression.challenge_complete` (U14) |
| U7 | TF's six trim materials not registered; the client mod must draw their item icons (spike S6); ironwood tools mine at the skeleton's 7 instead of 6.5 (U7) |
| U9 | Keepsake casket lid animation (client renderer); a mob's temporary shields do not count down after a reload until it is hit (U9) |
| U21 / A8 | Commit the lich tower layout / volume golden hashes with U22's room templates present (U21, [lich.md](../units/lich.md)) |
| U22 | DIM candelabra's circuit producer does not light vanilla mechanisms beside the support; client renderers (flames and smoke, brazier fire, ominous candle bobbing, skull chest lid, magic painting parallax, candle colours); skull chest multilogging of arbitrary blocks; the chiseled bookshelf's real igniter (U22, [lichdecor.md](../units/lichdecor.md)) |
| U29 | Up / down faces of the tiled giant block geometry to verify on client screenshots (U29, [giants.md](../units/giants.md)) |

## Appendix A: Owned ids (generated inventory)

Every id this unit owns in `docs/spec/owners.tsv`, with the TF 4.8 data the W1 worker must reproduce. Registration statements are TF's own Java (`init/TF*.java`), condensed (namespaces such as `MapColor.` dropped); loot, recipe, tag, advancement and data rows are summarised from TF's generated JSON (`src/generated/resources`). Loot notation: `self` = drops the block itself when it survives the explosion; `alternatives(a → b)` = first matching child; `[if ...]` = conditions; `{...}` = functions. In summaries `mc:` is `minecraft:` and ids without a namespace are `twilightforest:`.

| Kind | Count |
|---|---|
| `config_key` | 1 |
| `lang` | 127 |
| `net` | 1 |
| `path` | 5 |
| `selftest` | 1 |
| `selftest_dx` | 1 |
| `structure` | 4 |
| `worlddb` | 1 |

### Unit facts

| Kind | Ids |
|---|---|
| Selftest glob (`selftest`) | `spec-items.*` |
| Selftest dx range (`selftest_dx`) | `6600-6699` |
| World DB keys (`worlddb`) | `gf:spec-items:*` |
| Net channels (`net`) | `spec-items.*` |
| Owned paths (`path`) | `assets/gen/audio/spec-items/**`, `assets/gen/geo/spec-items/**`, `assets/gen/import/spec-items/**`, `docs/spec/{woodwork,building,flora,armory,weapons,artifacts,provisions,tgear,tgmove,uncraftmaps,progression,atmosphere}.md`, `docs/spec/not-ported.md` |

### Config keys (1)

See [config.md](config.md) for the section mapping; defaults and translation ids below are TF's.

| TF key | Default | Translation id | English (config screen) |
|---|---|---|---|
| `client.disableOptifineNagScreen` | `false` | `optifine` | Optifine Screen — Disable the nag screen when OptiFine is installed. |

### structure (4)

`twilightforest:druid_grove`, `twilightforest:floating_ruins`, `twilightforest:quest_island`, `twilightforest:world_tree`

## Appendix B: Strings (127 en_us keys owned)

Keys already shown above (block/item/entity names, subtitles) are not repeated. The 29 Bedrock locales are generated from TF's lang files (R9); owners use these keys with the English text as fallback.

| Key | English |
|---|---|
| `config.jade.plugin_twilightforest.chiseled_bookshelf_spawner` | Chiseled Canopy Bookshelf Spawns |
| `config.jade.plugin_twilightforest.drying_rack` | Drying Rack Time |
| `config.jade.plugin_twilightforest.quest_ram_wool` | Questing Ram Wool |
| `config.twilightforest.optifine` | Optifine Screen |
| `config.twilightforest.optifine.tooltip` | Disable the nag screen when OptiFine is installed. |
| `jade.drying_rack.remaining` | %s remaining |
| `museumcurator.animalhusbandry.twilightforest.bugs` | Bugs |
| `museumcurator.architecture.twilightforest.aurorablocks` | Aurora Blocks |
| `museumcurator.architecture.twilightforest.banisters` | Banisters |
| `museumcurator.architecture.twilightforest.castlebrick` | Castle Brick |
| `museumcurator.architecture.twilightforest.giantblocks` | Giant Blocks |
| `museumcurator.botany.twilightforest.beanstalk` | Beanstalk |
| `museumcurator.botany.twilightforest.canopytree` | Canopy Tree |
| `museumcurator.botany.twilightforest.darkwoodtree` | Darkwood Tree |
| `museumcurator.botany.twilightforest.mangrovetree` | Mangrove Tree |
| `museumcurator.botany.twilightforest.miningtree` | Minewood Tree |
| `museumcurator.botany.twilightforest.sortingtree` | Sortingwood Tree |
| `museumcurator.botany.twilightforest.timetree` | Timewood Tree |
| `museumcurator.botany.twilightforest.transformationtree` | Transwood Tree |
| `museumcurator.botany.twilightforest.twilightoaktree` | Twilight Oak Tree |
| `museumcurator.equipment.twilightforest.scepters` | Scepters of Power |
| `museumcurator.lithology.twilightforest.deadrock` | Deadrock |
| `museumcurator.lithology.twilightforest.mazestone` | Mazestone |
| `museumcurator.lithology.twilightforest.nagastone` | Nagastone |
| `museumcurator.machinery.twilightforest.carminitemachines` | Carminite Mechanisms |
| `museumcurator.metallurgy.twilightforest.fiery` | Fiery Metal |
| `museumcurator.metallurgy.twilightforest.ironwood` | Ironwood |
| `museumcurator.metallurgy.twilightforest.knightmetal` | Knightmetal |
| `twilightforest.tips.alpha_yeti` | The Alpha Yeti's rampage causes blocks to become dislodged from the ceiling. Watch out for falling icicles! |
| `twilightforest.tips.anvil_squashing` | Bugs can be squashed by Anvils. |
| `twilightforest.tips.arctic_armor` | Arctic Armor can be dyed any color. |
| `twilightforest.tips.baby_jockey` | Baby Skeleton Druids can be found riding Swarm Spiders. |
| `twilightforest.tips.banister_shape` | Banisters can be right-clicked with an axe to change their height. |
| `twilightforest.tips.berry_bushes` | Berry bushes can be found all over the Twilight Forest. |
| `twilightforest.tips.block_and_chain` | Enchanting a Block and Chain with Destruction allows it to break blocks. |
| `twilightforest.tips.boggard` | What the heck is a Boggard? |
| `twilightforest.tips.bugs_on_head` | Bugs will happily sit on your head. |
| `twilightforest.tips.burnt_thorns` | Burnt Thorns will disintegrate when stepped on. |
| `twilightforest.tips.candelabra` | Using Redstone Dust on a Candelabra will make its flames turn red and emit a redstone signal. |
| `twilightforest.tips.carminite_builder` | Carminite Builders place temporary blocks in the direction you're looking. |
| `twilightforest.tips.casket_logging` | The Keepsake Casket can be waterlogged, lavalogged, and encased in a block if it falls victim to a fluid interaction. |
| `twilightforest.tips.casket_usage` | The Keepsake Casket acts as a limited use gravestone. If in your inventory on death, it will place itself and hold all your items inside. |
| `twilightforest.tips.charm_of_keeping` | A Charm of Keeping will return parts of your inventory after death. |
| `twilightforest.tips.charm_of_life` | A Charm of Life can save you from a fatal blow. |
| `twilightforest.tips.clouds` | Rainy and Snowy Clouds can be used to simulate weather effects! |
| `twilightforest.tips.craft_travellers_gear` | Traveller's gear can be crafted from drying Treated Leather and turning it into Tanned Leather. |
| `twilightforest.tips.crumble_horn` | The Crumble Horn will deteriorate nearby blocks when used. |
| `twilightforest.tips.druid_hut` | Druid huts sometimes have hidden basements. |
| `twilightforest.tips.e115_pickup` | Sneak + right-click placed Experiment 115 to pick it back up. |
| `twilightforest.tips.e115_sprinkle` | Redstone can be sprinkled on top of Experiment 115. |
| `twilightforest.tips.emperors_cloth` | Emperor's Cloth will prevent armor from rendering when crafted with it. |
| `twilightforest.tips.essence_charge` | Exanimate Essence will fully charge any scepter it is crafted with. |
| `twilightforest.tips.experiment_115` | Does anyone know what Experiment 115 really is? |
| `twilightforest.tips.feather_fan` | The Peacock Feather Fan can be used to push mobs away, or yourself into the air if used while jumping. It also works well with the Elytra and Mace! |
| `twilightforest.tips.fiery_pickaxe` | A Fiery Pickaxe will smelt any blocks it breaks. |
| `twilightforest.tips.ghast_trap` | Killing Carminite Ghastlings near a Ghast Trap will charge it. |
| `twilightforest.tips.giant_block` | Mining a 4x4x4 area of the same block with a Giant's Pickaxe will drop 1 giant block instead. |
| `twilightforest.tips.glass_sword` | Glass Swords break after a single hit. |
| `twilightforest.tips.hollow_log` | Various things can be placed inside Hollow Logs such as snow, moss, or ladders. |
| `twilightforest.tips.hollow_oak_sapling` | Saplings that grow into giant hollow trees can be found in Druid Huts. |
| `twilightforest.tips.hollow_oak_tree` | Hollow Oak trees sometimes have dungeon-like rooms with unique loot in their leaves. |
| `twilightforest.tips.hydra_chop` | Hydra Chops fill up your hunger bar completely when eaten. |
| `twilightforest.tips.hydra_heads` | For each head slain, the Hydra will regrow two in its place! |
| `twilightforest.tips.hydra_mortars` | You can deflect the Hydra's mortar attack. |
| `twilightforest.tips.ice_core` | Ice Cores and Snow Guardians melt in hot biomes. |
| `twilightforest.tips.jars` | Fireflies and Cicadas can be put into jars. |
| `twilightforest.tips.jerky` | Meat can be dried into jerky using Drying Racks. |
| `twilightforest.tips.key_biome_locations` | Progression biome clusters generate roughly 600 blocks away from each other, meaning the next boss is never too far away. |
| `twilightforest.tips.key_biomes` | Progression biomes generate in clusters of 4 minibosses surrounding a normal boss. |
| `twilightforest.tips.kobold` | Kobold |
| `twilightforest.tips.labyrinth_vault` | The Labyrinth contains a secret room. |
| `twilightforest.tips.lich_deflection` | The Twilight Lich will deflect Scepter of Twilight bolts after his shields are down. |
| `twilightforest.tips.lich_scepters` | The Lich drops a variety of magic-based scepters. |
| `twilightforest.tips.liveroot` | Liveroot is found under most trees. |
| `twilightforest.tips.magic_beans` | Planting Magic Beans on Uberous Soil sprouts a mighty beanstalk. |
| `twilightforest.tips.magic_leaves` | Magic Tree Leaves won't drop saplings when broken. |
| `twilightforest.tips.magic_map` | Magic Maps are used to easily locate structures. |
| `twilightforest.tips.magic_saplings` | Special magic saplings can be found inside Hollow Oak trees. |
| `twilightforest.tips.maze_map_focus` | Minotaurs can occasionally drop Maze Map Focuses, which can be used to make Maze Maps. |
| `twilightforest.tips.mazebreaker` | The Mazebreaker can break Mazestone blocks 16 times faster and doesn't take extra durability damage. |
| `twilightforest.tips.mining_tree` | The Miner's Tree will pull ores up to the surface. |
| `twilightforest.tips.minion_buff` | If the Twilight Lich hits one of his minions with a projectile, it will make the minion stronger and faster.  |
| `twilightforest.tips.minoshroom` | The Minoshroom will perform a slam attack if a player lingers too close to it. |
| `twilightforest.tips.modify_travellers_gear` | Each piece of traveller's gear has 1 built-in ability, and up to 3 more can be added per piece. |
| `twilightforest.tips.moon_dial` | The Moon Dial shows the current phase of the moon. |
| `twilightforest.tips.moonworm_queen` | The Moonworm Queen can be fed Torchberries. |
| `twilightforest.tips.mushglooms` | Mushglooms cannot be bonemealed into giant mushrooms. However, placing them on Uberous Soil will make them grow. |
| `twilightforest.tips.music_disc` | Music Discs are found outside of dungeons. |
| `twilightforest.tips.mystic_crown` | The Mystic Crown slightly buffs scepters when worn. |
| `twilightforest.tips.naga` | The Naga can be stunned by making it ram something hard! |
| `twilightforest.tips.nether_bushes` | Along with other foreign objects and materials, strange plantlife has made itself native to the Dark Tower. |
| `twilightforest.tips.netherite_axe` | There will never be a Netherite Minotaur Axe. |
| `twilightforest.tips.ominous_fire` | Ominous fire can turn creatures undead. |
| `twilightforest.tips.ore_magnet` | The Ore Magnet can pull ore veins up to the surface. |
| `twilightforest.tips.ore_meter` | The Ore Meter will display all nearby ores when turned on. It can also target certain blocks by shift-right clicking them, and will then only show a count of those blocks nearby.  |
| `twilightforest.tips.oreberries` | Metal berry bushes rarely generate underground. |
| `twilightforest.tips.parrying` | A well-timed shield block can parry a projectile back at a mob. |
| `twilightforest.tips.peacock_feather_fan` | The Peacock Feather Fan can be used to extinguish Candles. |
| `twilightforest.tips.phantom_armor` | Phantom Armor is automatically kept on death. |
| `twilightforest.tips.phantoms` | Knight Phantoms take a lot less damage if they're invisible. Try to target the visible one when fighting them! |
| `twilightforest.tips.pocket_watch` | The Rabbit's Pocket Watch will grants additional speed while in the hotbar, and will increase mining speed when held. |
| `twilightforest.tips.potion_flask` | Potion Flasks can hold up to 3 doses of the same potion. |
| `twilightforest.tips.quest_ram` | The Questing Ram will reward anyone who gives it what it's missing. |
| `twilightforest.tips.red_thread` | Red Thread can be seen through walls. |
| `twilightforest.tips.redcap` | Redcaps can place and light TNT. |
| `twilightforest.tips.renewal` | Scepters enchanted with Renewal will automatically recharge themselves using the required items in the player's inventory. |
| `twilightforest.tips.skull_candle` | Candles can be placed on top of mob heads to create a fancy light source. |
| `twilightforest.tips.sorting_tree` | The Sorting Tree will sort chests next to it into other chests nearby. |
| `twilightforest.tips.spooky_forest` | The Spooky Forest is not Halloween themed. |
| `twilightforest.tips.structure_conquering` | Killing a boss will make mobs stop spawning in that structure. |
| `twilightforest.tips.structure_spawning` | Structures spawn in a grid-like pattern. |
| `twilightforest.tips.the_lore` | The story is all here, you just need to figure it out yourself! |
| `twilightforest.tips.the_walls` | The Death Tomes are in your walls. They're in your walls. |
| `twilightforest.tips.time_tree` | The Tree of Time will accelerate the growth of nearby crops. |
| `twilightforest.tips.torchberries` | We did glow berries first! |
| `twilightforest.tips.towerwood` | Towerwood Planks are very resistant, but not immune, to fire. |
| `twilightforest.tips.transformation_tree` | The Tree of Transformation will convert the area around it into an Enchanted Forest. |
| `twilightforest.tips.trollber_ripening` | Killing a Troll will ripen nearby Trollber. |
| `twilightforest.tips.trophy_pedestal` | Trophy Pedestals can only be mined after they have been activated. |
| `twilightforest.tips.twilight_portal` | Throw a diamond into a pool of water surrounded by flowers. |
| `twilightforest.tips.uncrafting_table` | The Uncrafting Table isn't just used to uncraft items. It can also recraft items into other ones, repair tools and armor, and transfer enchantments across gear! |
| `twilightforest.tips.ur_ghast` | The Ur-Ghast can be pulled down from the sky using Ghast Traps. |
| `twilightforest.tips.vanishing_block` | Vanishing Blocks will disappear forever when activated. |
| `twilightforest.tips.worldgen_features` | The forest is filled with many ruins. Some may even contain unique items. |
| `twilightforest.tips.wrought_iron` | Wrought Iron Bars cannot be obtained normally. They can only be obtained by uncrafting blocks made of them. |
| `twilightforest.tips.yeti` | Yetis love throwing things. |
| `twilightforest.tips.zombie_healing` | Zombies summoned with a Zombie Scepter can be healed with Rotten Flesh. |

