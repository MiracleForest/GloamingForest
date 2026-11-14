# Configuration mapping (rule R13)

Upstream 4.8 configures itself with two NeoForge TOML files (`gloamingforest-common.toml`, `gloamingforest-client.toml`,
built by `config/TFCommonConfig.java` and `config/TFClientConfig.java`). GloamingForest has one JSON config section
per unit instead (`GF_REGISTER_CONFIG("<unit>", T)` -> `config/<unit>.json`, edited in game with
`/gloamingforest config [section]`, reloaded with `/gloamingforest reload`). Every upstream key is mapped to exactly one
section below; the owner implements it with the same name (camelCase field), default and range, and keeps the
Upstream translation keys (`config.gloamingforest.<id>`, `.tooltip`) for the config screen.

Client keys live in the client build's config (the client mod reads its own `config/<section>.json`); common keys
are read on the server side and synced to companions where the client needs them (e.g. the uncrafting table keys,
Upstream's SyncUncraftingTableConfigPacket). Shared values that several units read (boss drop chests, the multiplayer
fight adjuster, the portal) live in W0 sections. The Skylight world type, a world preset on Java, becomes a
`terrain` section option (see not-ported.md).

The `config_key` and `config_section` rows of [owners.tsv](owners.tsv) are the machine-readable form of this table.

## Sections

| Section | Owner | Keys |
|---|---|---|
| `artifacts` | U9 | 5 |
| `atmosphere` | U15 | 1 |
| `biomes` | U1 | 0 |
| `bosses` | F8 | 2 |
| `building` | U5 | 1 |
| `client` | F12 | 1 |
| `dimension` | F4 | 11 |
| `flora` | U6 | 2 |
| `giants` | U29 | 3 |
| `items` | F9 | 1 |
| `landmark` | F6 | 0 |
| `lichdecor` | U22 | 1 |
| `mod` | F0 | 0 |
| `progression` | U14 | 1 |
| `questcamps` | U19 | 1 |
| `terrain` | U2 | 0 |
| `tgear` | U11 | 5 |
| `tgmove` | U12 | 1 |
| `trees` | U3 | 4 |
| `uncraftmaps` | U13 | 10 |
| `weapons` | U8 | 2 |
| `worldgen` | F5 | 0 |

`mod` (F0) holds the foundation's own settings (`language`, `usePlayerLocale`, `requireResourcePack`, `logContractStubs`).

## Upstream keys

| Upstream key (`<file>.<TOML section>.<key>`) | Default | Translation id | Owner | GloamingForest section |
|---|---|---|---|---|
| `common.Dimension Settings.newPlayersSpawnInTF` | `false` | `spawn_in_tf` | F4 | `dimension` |
| `common.Dimension Settings.portalForNewPlayer` | `false` | `portal_for_new_player` | F4 | `dimension` |
| `common.Portal Settings.originDimension` | `"minecraft:overworld"` | `origin_dimension` | F4 | `dimension` |
| `common.Portal Settings.allowPortalsInOtherDimensions` | `false` | `portals_in_other_dimensions` | F4 | `dimension` |
| `common.Portal Settings.portalCreationPermission` | `0` | `portal_permission` | F4 | `dimension` |
| `common.Portal Settings.disablePortalCreation` | `false` | `disable_portal` | F4 | `dimension` |
| `common.Portal Settings.checkPortalPlacement` | `true` | `check_portal_placement` | F4 | `dimension` |
| `common.Portal Settings.destructivePortalLightning` | `true` | `destructive_portal_lighting` | F4 | `dimension` |
| `common.Portal Settings.shouldReturnPortalBeUsable` | `true` | `portal_return` | F4 | `dimension` |
| `common.Portal Settings.portalUnlockedByAdvancement` | `""` | `portal_unlocked_by_advancement` | F4 | `dimension` |
| `common.Portal Settings.maxPortalSize` | `64` | `max_portal_size` | F4 | `dimension` |
| `common.casketUUIDLocking` | `false` | `casket_uuid_locking` | U9 | `artifacts` |
| `common.disableSkullCandleCreation` | `false` | `disable_skull_candles` | U22 | `lichdecor` |
| `common.showEnchantmentsOnItems` | `true` | `default_item_enchantments` | F9 | `items` |
| `common.bossesSpawnDropChests` | `true` | `boss_drop_chests` | F8 | `bosses` |
| `common.cloudBlockPrecipitationDistance` | `32` | `cloud_precipitation` | U29 | `giants` |
| `common.multiplayerFightAdjuster` | `TFConfig.MultiplayerFightAdjuster.NONE` | `multiplayer_fight_adjuster` | F8 | `bosses` |
| `common.Uncrafting Table.uncraftingXpCostMultiplier` | `1.0D` | `uncrafting_xp_cost` | U13 | `uncraftmaps` |
| `common.Uncrafting Table.repairingXpCostMultiplier` | `1.0D` | `repairing_xp_cost` | U13 | `uncraftmaps` |
| `common.Uncrafting Table.disableUncraftingRecipes` | `List.of("gloamingforest:giant_log_to_oak_planks")` | `uncrafting_recipes` | U13 | `uncraftmaps` |
| `common.Uncrafting Table.flipRecipeList` | `false` | `uncrafting_recipes_flip` | U13 | `uncraftmaps` |
| `common.Uncrafting Table.blacklistedUncraftingModIds` | `new ArrayList<>()` | `uncrafting_mod_ids` | U13 | `uncraftmaps` |
| `common.Uncrafting Table.flipIdList` | `false` | `uncrafting_mod_id_flip` | U13 | `uncraftmaps` |
| `common.Uncrafting Table.enableShapelessCrafting` | `false` | `shapeless_uncrafting` | U13 | `uncraftmaps` |
| `common.Uncrafting Table.disableIngredientSwitching` | `false` | `ingredient_switching` | U13 | `uncraftmaps` |
| `common.Uncrafting Table.disableUncrafting` | `false` | `disable_uncrafting` | U13 | `uncraftmaps` |
| `common.Uncrafting Table.disableUncraftingTable` | `false` | `disable_uncrafting_table` | U13 | `uncraftmaps` |
| `common.Magic Trees.timeCoreRange` | `16` | `time_range` | U3 | `trees` |
| `common.Magic Trees.transformationCoreRange` | `16` | `transformation_range` | U3 | `trees` |
| `common.Magic Trees.miningCoreRange` | `16` | `mining_range` | U3 | `trees` |
| `common.Magic Trees.sortingCoreRange` | `16` | `sorting_range` | U3 | `trees` |
| `common.Shield Parrying.parryNonGloamingAttacks` | `false` | `parry_non_gloaming` | U8 | `weapons` |
| `common.Shield Parrying.shieldParryTicksArrow` | `40` | `parry_window` | U8 | `weapons` |
| `client.silentCicadas` | `false` | `silent_cicadas` | U6 | `flora` |
| `client.silentCicadasOnHead` | `false` | `silent_cicadas_on_head` | U6 | `flora` |
| `client.screenShakingEffect` | `true` | `screen_shake` | F12 | `client` |
| `client.rotateTrophyHeadsGui` | `true` | `animate_trophies` | U5 | `building` |
| `client.disableOptifineNagScreen` | `false` | `optifine` | F16 | `(not ported)` |
| `client.disableLockedBiomeToasts` | `false` | `locked_toasts` | U14 | `progression` |
| `client.questRamWoolIndicator` | `true` | `ram_indicator` | U19 | `questcamps` |
| `client.fortificationShieldIndicator` | `true` | `shield_indicator` | U9 | `artifacts` |
| `client.fortificationShieldIndicatorInCreative` | `false` | `shield_indicator_creative` | U9 | `artifacts` |
| `client.cloudBlockPrecipitationDistance` | `-1` | `cloud_precipitation` | U29 | `giants` |
| `client.giantSkinUUIDs` | `new ArrayList<>()` | `giant_skin_uuid_list` | U29 | `giants` |
| `client.auroraBiomes` | `List.of("gloamingforest:glacier")` | `aurora_biomes` | U15 | `atmosphere` |
| `client.prettifyOreMeterGui` | `true` | `prettify_ore_meter_gui` | U9 | `artifacts` |
| `client.totemCharmAnimation` | `false` | `totem_charm_animation` | U9 | `artifacts` |
| `client.travellersWingsGradualGlide` | `true` | `manual_travellers_wings_gradual_glide` | U12 | `tgmove` |
| `client.firstPersonGloveOverlay` | `true` | `first_person_glove_overlay` | U11 | `tgear` |
| `client.Item Display Modifier Settings.screenOffsetX` | `4` | `screen_offset_x` | U11 | `tgear` |
| `client.Item Display Modifier Settings.screenOffsetY` | `4` | `screen_offset_y` | U11 | `tgear` |
| `client.Item Display Modifier Settings.screenScale` | `1.0D` | `screen_scale` | U11 | `tgear` |
| `client.Item Display Modifier Settings.twentyFourHourFormat` | `TFConfig.use24HourTimeDefault()` | `twenty_four_hour_format` | U11 | `tgear` |

`client.disableOptifineNagScreen` has no counterpart (there is no OptiFine on Bedrock); F16 lists it in not-ported.md.
