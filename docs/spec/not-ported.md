# Not ported (F16 `spec-items`)

What of The Twilight Forest 4.8 (`9e40a757`, MC 1.21.1) GloamingForest does **not** reproduce, or reproduces only in a
degraded form, with the reason and the workaround. Owned by F16 (W0b); W1 units add their own entries through their
`docs/units/<unit>.md` and A7 (assets, licence and vanilla-client matrix audit) and A8 fold them here in W2. Every row
names the owner that decides or implements the workaround. "Client mod" = the GloamingForest client build installed;
"vanilla" = an unmodified Bedrock client joining a BDS (best effort, AGENTS.md).

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
| Dimension height -32..256 and fixed time | depend on spike S1; per-player SetTime / daylight-cycle packets; rendering below y 0 may be wrong if the native definition fails | F4 |
| Sky (no sun/moon, bright stars), aurora, locked-biome weather (blizzard, mosquitoes, ashes, dark streams, big rain), structure sparkles, spooky fog smoothing | no GLSL on Bedrock: client mod renders CPU meshes; vanilla gets client-biome sky/fog colours and `PlayerFogPacket` only | U15, F12 |
| Client-authoritative movement: double jump, sidestep, gradual glide, agile ranger, straight ahead, step-up, water walk, unrestrained, slimy soles bounce, feather fan launch, yeti throw, custom knockback | exact only with the client mod; vanilla gets motion packets on input edges (double jump), Slow Falling for glide, no sidestep, no step-up, no water walk | U12, U8, F12 |
| Climbable hollow logs, rope climbing / tightrope walking | climbing only with the client mod; vanilla cannot climb them | U4, U5 |
| Custom screens: uncrafting table, advancement tab, item display HUD, ore meter HUD, belt / goggles bundle-like slots, flask filling by inventory click | DDUI / forms / chest-like container skins on vanilla; flasks fill by using a potion with the flask in the other hand | U13, U14, U11, U9, F12 |
| Advancement frame textures | Mojang's (ARR): redrawn for the client-mod tab | U14, A7 |
| Boss bars | exact RGB with the client mod, nearest of 8 BossBarColor values on vanilla | F8 |
| Map decorations (15 TF types), magic/maze/ore map rendering | mapped to the 30 vanilla map decoration types; colours from the vanilla palette | U13, F6 |
| Giants' skins (`giantSkinUUIDs`) | client mod only; vanilla sees the default skin | U29 |
| Banner patterns, trim materials, music disc items | depend on spike S6: stripped for vanilla clients if unknown ids crash them; discs may play through a jukebox hook + PlaySoundPacket | F9, U5, U7, U10 |
| Block entities (signs, chests, casket, drying racks, jars, candles, critters, trophies, red thread, skull chests) | spike S3: BlockData + display/container entities; vanilla sees static geometry where no data-driven actor exists (drying rack items, jar contents, critter animation, trophy GUI spin) | F10, F11, owners |
| Emperor's cloth hiding armor, glove first-person overlay, goggles zoom texture, wings animation | client mod only | U9, U11, U12 |
| Mason jar dynamic light from its contents | light 0 on vanilla unless the permutation budget allows a light state | U6 |
| No-template smithing (emperor's cloth smithing) | Bedrock smithing needs a template: special crafting fallback | U7, F9 |
| Crafted items with default enchantments (24 recipes) | enchanted after crafting by a server hook; recipe book preview shows no enchantments | U7, F9 |
| Arctic armor dye colour | client mod tint; vanilla shows the default colour | U7 |
| Keybind-only actions (zoom, red thread vision, map cycle, hotbar swap) | sneak-use fallbacks on the items | U11 |

## 5. How to add an entry

A unit that cannot reproduce a TF behaviour exactly records it in `docs/units/<unit>.md` (section "Not ported") with
the TF class, the reason (platform, licence, spike result) and the workaround; A7/A8 copy it here in W2 with the owner.
Nothing in TF 4.8 may be dropped silently: the W2 "registered but unimplemented" report (R17) must match this list.

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

