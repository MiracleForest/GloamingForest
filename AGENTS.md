# AGENTS.md

GloamingForest is an unofficial port of **The Twilight Forest 4.8** (TeamTwilight/twilightforest,
branch `1.21.1`, commit `9e40a75721c716b2fdf80c16d1058fe718825e9f`, MC 1.21.1) to Minecraft: Bedrock
Edition, written as a LeviLamina native mod. It targets MC **1.26.51** / LeviLamina **26.51.7**,
builds with xmake + clang-cl on Windows x64, and runs on **both** the dedicated server (BDS) and the
client (local worlds through LeviLauncher): one code base, two builds (`--target_type=server|client`).

The bar: with the client mod installed, every feature, interaction and screen matches TF. Vanilla
Bedrock clients joining a BDS are best effort (fallbacks: forms, DDUI, lore, carriers, vanilla
icons). The foundation is adapted from [Levitate](https://github.com/LiteLDev/Levitate) v0.1.0.

Read this file, [docs/spec/README.md](docs/spec/README.md), your unit's spec (`docs/spec/<unit>.md`)
and the contract headers before you code. The plan's unit records are in
[docs/spec/units.json](docs/spec/units.json); the owner of every id and path is in
[docs/spec/owners.tsv](docs/spec/owners.tsv).

## Sources and licences

| What | Licence | How GloamingForest uses it |
|---|---|---|
| TF code (Java) | LGPL-2.1-or-later | May be **translated** to C++ (keep the attribution header below). Behaviour reference for everything. |
| TF textures, models, blockstates, lang | CC BY-NC-SA 4.0 | Reused **only through `tools/import`** (F1) into `assets/gen/import/**`; attribution in [NOTICE](NOTICE) and [assets/LICENSE](assets/LICENSE). A hand-made file derived from one (an edited texture, model, blockstate or text overriding the generated one, R3) stays CC BY-NC-SA 4.0 (ShareAlike): put it under a `tf-derived/` directory of your pack folder (`assets/pack/rp/textures/blocks/<unit>/tf-derived/x.png`) with a `tf-derived/SOURCES.json` mapping each file name to the TF file(s) it derives from (`{"x.png": "src/main/resources/assets/twilightforest/textures/block/x.png"}`). Assemble fails without the entry and lists the files in the pack's `tf-derived-sources.txt` next to `LICENSE-ASSETS.txt`. |
| TF sounds, music (229 `.ogg`) and structure templates (274 `.nbt`) | **All Rights Reserved** | Never opened, decoded or copied. Sounds come from the synth toolkit (`tools/sounds`, F3), templates are original `.gft` designs (`tools/structures`, F7). The TF checkout excludes them; `tools/ci/arr-guard.ps1` rejects matching blobs. |
| Levitate v0.1.0 | LGPL-3.0-or-later | The foundation; adapted files say so in their first line. |
| MoreDimensions | LGPL-3.0-or-later | Reference for the custom dimension (spike S1). |
| GloamingForest | LGPL-3.0-or-later | [COPYING.LESSER](COPYING.LESSER), [COPYING](COPYING). |

Read TF **only** from the shared local checkout (rule R24):

```powershell
pwsh tools/tf/fetch.ps1            # -> %LOCALAPPDATA%\gf-tf (read-only, sparse, no .ogg/.nbt, sha verified)
```

### Attribution header for translated TF code

Every source file that translates TF code starts with this header (one `//` line per TF file):

```cpp
// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/<path>/<File>.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
```

`src/core/Holiday.{h,cpp}` is the reference example. Files adapted from Levitate start with
`// Adapted from LiteLDev/Levitate v0.1.0 (<path>), LGPL-3.0-or-later.` Never add AI or Claude
attribution to code, commits or PRs.

## Layout

| Path | What it is |
|---|---|
| `src/core/` | Pure C++ (no `mc/` / `ll/` includes), static lib `gf-core`, gtested in `tests/`: `Format` (`{}` / `%s` formatting), `Glob`, `GlobalPos` + `BlockBox`, `Holiday`, `Protocol`, `Layout`, `Keys` (world DB keys), `Stub`, `VolumeHash`; W0b: `jrand/`, `noise/`, `landmark/`; W1: `src/core/<unit>/`. |
| `src/mod/` | Entry (`Main.cpp`), `Log.h`, `Module.h`, `Config.h`, `ModConfig.h`, `I18n.h`, `Commands.h`, `Registry.h`, `MemoryOperators.cpp`. |
| `src/game/` | Game adapters: `WorldDb` (LevelDB JSON + world lifecycle), `Blocks` (states, placement, positions), `Items` (stacks, item data). |
| `src/selftest/` | In-game test framework (`SelfTest.h`) and the foundation tests (`core.*`). |
| `src/pack/` | `PackDeployer` (BDS: puts the resource packs on the world, appends names to the server language). |
| `src/feature/common/` | W0 services: `Interactions`, `BlockData`, `Redstone`, `Hold` (click and hold), `Screens` (+ the config screen). |
| `src/feature/net/` | The companion protocol (`Net.h`). |
| `src/feature/<unit>/` | W0b contract implementations and W1 feature units (server side; `#ifdef GF_CLIENT` parts allowed). |
| `src/feature/ext/<unit>/` | Contract extensions a W1 unit needs (new files only, rule R18). |
| `src/client/` | Client build only (`#ifdef GF_CLIENT`): `Client.h` (`ctr`, local player), `Trace.cpp` (e2e trace lines), `screen/base/` (screen framework, config screen, hold), W0b `ui/ render/ world/ input/`, W1 `src/client/<unit>/`. |
| `assets/pack/{bp,rp}/` | Hand-made pack content, per unit in `**/<unit>/**` sub-folders and `*.d/<unit>.json` fragments (manual layer). |
| `assets/gen/{import,geo,audio}/<unit>/{bp,rp,data,lang}/` | Generated by the tools (`-Unit <unit>`), never hand-edited (gen layer; the manual layer wins per id). |
| `assets/lang/<locale>/<unit>.json` | Mod strings (`tr` / `ctr`), merged into `lang/<locale>.json` for the 29 Bedrock locales. |
| `assets/data/<kind>/<unit>.json` | Unit data (families, containers, landmarks, ...) -> `<mod>/data/gf/<kind>/`. |
| `assets/tags/<registry>/<ns>/<path>.d/<unit>.json` | Union fragments adding tag members (rule R6). |
| `assets/structures/<unit>/**` | Original `.gft.json` templates -> `<mod>/structures/twilightforest/`. |
| `assets/test/` | The `gf_test:*` fixture packs and data (development builds only). |
| `tools/build.ps1` | The only build entry point (throttle slot, both target types, gtest). |
| `tools/lib/Throttle.psm1` | Machine-wide build / BDS / client slots. |
| `tools/xmake/assemble.lua`, `merges.json` | Post-build assembly and checks (`tools/assemble.ps1 -Check` without building). |
| `tools/tf/fetch.ps1` | The shared TF checkout. |
| `tools/spec/owners.ps1` + `owners.rules.json` | Generates `docs/spec/owners.tsv`. |
| `tools/ci/` | `ownership.ps1`, `arr-guard.ps1` (+ `arr-blobs.txt`). |
| `tools/e2e/` | Server and client harnesses ([tools/e2e/README.md](tools/e2e/README.md)). |
| `tools/{import,models,sounds,structures,families,oracle}/` | W0b toolchains (F1, F2, F3, F7, F11, F13). |
| `docs/spec/` | Specs, `owners.tsv`, `units.json`, `config.md`, `e2e.md`, `worker.md`. `docs/spikes/` holds the spike reports. |

Output: `bin/<target_type>/GloamingForest/` with `GloamingForest.dll`, `manifest.json`, `lang/`,
`data/`, `structures/`, `behavior_packs/GloamingForest_BP` (+ `_Test_BP`) and
`resource_packs/GloamingForest_RP` (+ `_Test_RP`). Copy it to `plugins/` (BDS) or `mods/`
(LeviLauncher client).

## Rules (R1-R25)

These are the cross-unit rules of the plan, verbatim. Every unit follows all of them.

- R1 Ids: every id is TF's own twilightforest:<path> and exists from W0 (F1 skeleton). docs/spec/owners.tsv lists the owner of every id of every kind; a unit edits only files of ids it owns. Block-state names/values are frozen in docs/spec/blockstates.md; later changes may only add.
- R2 Cross-unit references are strings only (block/item/entity/loot/feature/structure/sound/particle/tag ids). No W1 unit includes another W1 unit's headers; integration goes only through W0 contracts and the trigger bus.
- R3 Owned paths per unit <u>: src/feature/<u>/**, src/client/<u>/** (#ifdef GF_CLIENT), src/core/<u>/** + tests/<u>/**, assets/pack/{bp,rp}/**/<u>/**, every *.d/<u>.json fragment and texts.d/<locale>/<u>.lang, assets/data/<kind>/<u>.json, assets/structures/<u>/**, assets/lang/<locale>/<u>.json, tools/{import/overrides,sounds/recipes}/<u>.json, tools/structures/<u>/**, tools/e2e/{server,client}/scenarios/<u>*.ps1, docs/units/<u>.md. assets/gen/{import,geo,audio}/<u>/** may only be regenerated by the tools (-Unit <u>), never hand-edited; a manual file with the same id overrides the generated one.
- R4 Recipes: vanilla-type recipes (crafting, smelting, stonecutting, smithing) belong to the owner of the output item; custom-type recipes belong to the machine/type owner (drying U4, uncrafting U13, travellers U11, map cloning U13, casket/scepter/essence/emperors cloth U9, moonworm queen U8, no_template_smithing U7).
- R5 Loot: block loot to the block owner, entity loot to the entity owner, chest loot to the unit that places the chest; shared tables (useless, just_bones, suspicious_stew, items/*) are W0 (F10).
- R6 Tags live with the member: TF tag JSON is imported W0 data; a unit adds members only through union fragments assets/tags/<registry>/<tag>.d/<u>.json. Behaviour on another unit's blocks is attached only by tag matchers (e.g. mazestone tag for the mazebreaker rule, thorns tag for the lamp of cinders).
- R7 Data maps belong to their consumer (crumble_horn and transformation_powder U8, ore_map_color and magic_map_color U13, ominous_fire U22, parrot_imitations U16); compostables/furnace_fuels rows follow the item owner.
- R8 One hook per engine event, owned by W0: no unit hooks GameMode, interact/use/destroy events, Mob::hurt, die, dropAllItems, block ticks, LocalPlayer movement, LevelRenderer or sky/fog functions directly; register with interactions, blockevents, combat, death, loot, entity, client.* instead.
- R9 Translation keys belong to the feature owner (import partitions the 2061 en_us keys; book keys F6, death messages F10); others use tr(key, 'English fallback'). Duplicate keys in any merged fragment fail the build; the 29 Bedrock locales are generated from TF's lang files.
- R10 Sound events and particles: each event/particle has exactly one owner in owners.tsv and is played/emitted by id through fx (a missing one is silent). Owners refine audio only through tools/sounds/recipes/<u>.json; nobody opens TF .ogg or .nbt (the shared checkout excludes them; CI arr-guard rejects matching hashes).
- R11 Licensing: translated TF code keeps the LGPL attribution header naming the TF source file; textures/models/lang only via tools/import (CC BY-NC-SA 4.0, attribution in assets/LICENSE and NOTICE); sounds, music and structure templates are original (synth toolkit, template DSL) and may follow TF's footprint, slots, spawners, chests and loot from the spec and the LGPL template definitions.
- R12 Selftests: prefix <u>.*; unit U<n> owns testPos dx 100n..100n+99, W0b unit F<k> owns 5000+100k..5000+100k+99, W2 9000+; tests depend only on W0 plus the unit itself (progression::grant, landmark::forceStart, skeleton ids, fixtures) and must pass inside the 'full' run.
- R13 Config: one GF_REGISTER_CONFIG section per unit named after it; every TF config key (TFCommonConfig ~35, TFClientConfig ~21) is mapped to exactly one section in docs/spec/config.md; shared values (boss drop chests, multiplayer adjuster, portal) live in W0 sections.
- R14 Commands and functions: every /twilightforest subcommand and datapack function has one owner in owners.tsv (tp F4, center/info/conquer/generatebook/locate/place F6, structure_util F7, gamerule F10, maplocator/mapbiomes U13, shield U9, spawner U22, travellersgear U11).
- R15 Net: channels are <u>.<name>, one client handler per channel; server-to-client data goes only over the companion protocol; vanilla clients must never receive companion packets.
- R16 World data keys gf:<u>:...; never keep Player* or block pointers across ticks; defer world edits from cancelled interactions to the next tick.
- R17 Unknown ids at runtime are skipped with one warning per id; assemble fails on dangling twilightforest: references in BP/data; W2 runs a registered-but-unimplemented report.
- R18 W0 headers are immutable in W1. A needed extension goes into src/feature/ext/<u>/ (new files only), is called out in the PR, and is folded by A8 in W2b.
- R19 Structures: procedural pieces are translated exactly; template-backed pieces use original .gft templates validated by tools/structures/build.ps1 -Check against the imported template definitions; every structure has a golden volume hash selftest reviewed once with screenshots.
- R20 Bosses and trophies: boss lifecycle, bar, death chest, conquest and generic spawner come from the F8 boss kit; U5 owns all trophy/spawner/banner-pattern/miniature blocks; each boss unit registers boss::registerSpawner for its spawner block id and drops trophies through its loot table.
- R21 Advancements: U14 owns all 45 advancement JSONs and their criteria; units fire their TF triggers by string (progression::trigger) and never award advancements directly; recipe unlocks are generated into recipe JSON by F1.
- R22 Both builds must pass (pwsh tools/build.ps1 -Target all -Tests) and the unit's server + client e2e scenario must pass before the PR; every e2e also runs the remote-vanilla variant when the unit has a vanilla fallback.
- R23 Builds and e2e only through tools/build.ps1 and tools/e2e/*/run.ps1, which take Throttle slots; never run xmake or BDS directly outside a slot.
- R24 Read TF only from the shared local checkout (tools/tf/fetch.ps1), not through the GitHub API (rate limits).
- R25 PR title 'U<n>: <imperative summary>'; tools/ci/ownership.ps1 -Unit <u> must pass (the diff touches only owned paths plus src/feature/ext/<u>/).

## Contracts

Every W0 contract is a documented header (or a tool entry point) written by F0 with a neutral
stub (`GF_STUB`, `src/core/Stub.h`: the first call logs `<name>: not implemented (contract stub)`).
The W0b unit that owns the contract replaces the stub `.cpp` (or the tool script); the header stays
F0's and is immutable from W1 on (R18). `/twilightforest stubs` lists the stubs a world called.

| Contract | Owner | Header / entry point | API |
|---|---|---|---|
| mod | F0 (W0a) | `src/mod/{Module,Config,Commands,I18n,Log}.h, src/selftest/SelfTest.h, src/client/Client.h, src/core/Holiday.h` | GF_REGISTER_MODULE(T) / GF_REGISTER_MODULE_ORDERED(T,n); GF_REGISTER_CONFIG("<u>", T) -> config/<u>.json, gf::config::get<T>(), /twilightforest config; GF_REGISTER_SUBCOMMAND(fn) under /twilightforest (aliases /tf, /tffeature); GF_REGISTER_SELFTEST("<u>.<case>", fn); gf::tr(key, fallback, args...), gf::trFor(player, ...), gf::client::ctr(...); gf::log(); gf::core::holiday::isHalloweenWeek(). |
| db+net | F0 (W0a) | `src/game/WorldDb.h, src/feature/net/Net.h` | gf::game::db::{get,put,erase}("gf:<u>:..."), db::onWorldReady/onWorldSave/onWorldClosed; gf::net::send(Player&, "<u>.<name>", json), net::on(channel, fn), net::onCompanionJoined(fn), net::isCompanion(Player const&), net::client::on(channel, fn). |
| interactions | F0 (W0a) | `src/feature/common/Interactions.h` | interactions::onUseOn/onUse/onDestroy/onStartDestroy/onPlace/onUnusedUseOn/onDestroyed/onInteract/onAttack(Match{block\|item\|entity id or Match::tag(id)}, fn, priority) -> Pass\|Consume\|Deny (first non-Pass by priority wins; F6 protection at Highest); nextTick(fn); mutableItem(ctx.item). |
| blockdata+redstone | F0 (W0a) | `src/feature/common/{BlockData,Redstone}.h` | blockdata::get/set(region, pos, key, json), onRotate(key, fn); redstone::power(region, pos) 0..15, watch(pos, fn), registerEmitter(blockId, fn), registerAnalog(blockId, fn -> optional<int>). |
| selftest | F0 (W0a) | `src/selftest/SelfTest.h (fixtures: assets/test)` | ctx.player(), spawnPlayer(), testPos(dx), flatArea(dx, size), inTwilight(), setBlock(pos, id, states), cleanupBlock, give(id, n), co_await ticks(n), co_await ensureLoaded(pos), check/require/fail/info, captureBossEvent(), snapshotVolume(box) -> hash; fixture ids gf_test:* in assets/test. |
| assemble | F0 (W0a) | `tools/xmake/{assemble.lua,merges.json}, tools/assemble.ps1 -Check` | tools/xmake/merges.json entries {glob, out, wrapper, mode: object\|union}; layers assets/gen/{import,geo,audio}/** then assets/pack/** (manual wins per id; duplicates inside a layer fail); checks: integer block states, permutation budget, dangling twilightforest: references, every pack file has an owner, ARR hash guard; lang -> 29 Bedrock locales. |
| owners | F0 (W0a) | `docs/spec/owners.tsv, tools/spec/owners.ps1, tools/ci/ownership.ps1` | docs/spec/owners.tsv columns kind, id-or-glob, owner, mode(gen\|manual); generated by tools/spec/owners.ps1; tools/ci/ownership.ps1 -Unit <u> [-Base main] fails if the diff touches unowned paths. |
| import | F1 | `tools/import/run.ps1` | pwsh tools/import/run.ps1 -Kinds textures,models,blockstates,lang,tags,loot,recipes,advancements,worldgen,datamaps,registries,particles,sounds,skeleton,catalog,families [-Unit <u>] [-Check]; overrides tools/import/overrides/<u>.json (drop, rename, JSON-pointer patch); output assets/gen/import/<u>/**; docs/spec/blockstates.md. |
| geo | F2 | `tools/models/{convert,attachable}.ps1` | pwsh tools/models/convert.ps1 -Java <ModelClass> [-Unit <u>] -> assets/gen/geo/<u>/{models/entity/<id>.geo.json, bones/<id>.json, animations/<id>.base.json}; tools/models/attachable.ps1 -Item <id> for armor/gear/trophy heads. |
| synth | F3 | `tools/sounds/synth.ps1` | pwsh tools/sounds/synth.ps1 -Unit <u> [-Check]: tools/sounds/recipes/<u>.json {event: {category, subtitle, variants:[{patch, params, seed, volume, pitch}]}} -> assets/gen/audio/<u>/sounds/**.ogg + sound_definitions fragment; tools/sounds/music/<track>.json (tracker) -> .ogg; never reads TF audio. |
| template | F7 | `tools/structures/build.ps1` | Template file assets/structures/<u>/<path>.gft.json {size, palette, blocks (RLE), markers, jigsaw connectors, entities}; pwsh tools/structures/build.ps1 -Unit <u> builds *.gfs.ps1 DSL scripts and -Check validates against the 205 imported template definitions; /tf structure_util place <id> <pos> <rot>. |
| world | F4 | `src/feature/dimension/World.h` | gf::world::{twilight(), isTwilight(Dimension const&), teleport(Player&, DimensionType, Vec3, float yaw), coordinateScale(), fixedTime()}; gf::portal::{tryActivate(BlockSource&, BlockPos), onTeleported(fn), destinationFor(Player const&)}; gamerules playersTfPortalDefaultDelay / playersTfPortalCreativeDelay. |
| worldgen | F5 | `src/feature/worldgen/Worldgen.h, src/core/jrand/JRand.h, src/core/noise/Noise.h` | gf::worldgen::{setBiomeSource(BiomeSourceFn), biomeAt(BlockPos), mainBiomeAt(int bx, int bz), setTerrain(TerrainProvider), setSurface(SurfaceProvider), registerCarver(id, CarverFn), registerBlanket(id, BlanketFn), addStage(Stage, id, order, ChunkFn), seed()}; gf::core::jrand / gf::core::noise (Xoroshiro128++, LegacyRandom, WorldgenRandom, Perlin/Simplex/Normal noise, density evaluator). |
| features | F5 | `src/feature/features/Features.h` | gf::features::{registerType(id, FeatureFn), registerPlacement(id, PlacementFn), registerTrunkPlacer/registerFoliagePlacer/registerTreeDecorator/registerRootPlacer/registerStateProvider(id, fn), place(id, WorldGenRegion&, BlockPos, Random&) -> bool, placeJson(json, ...), LargeWriter (cross-chunk queue)}; built-in vanilla types + weighted_list, avoid_landmark, chunk_center. |
| spawn | F5 | `src/feature/spawn/Spawn.h` | gf::spawn::{registerPlacement(entityId, PlacementRule, PredicateFn = {}), spawnMob(BlockSource&, entityId, Vec3, SpawnReason, InitFn = {}) -> Actor*, setBiomeSpawns(biomeId, Table), onSpecialSpawn(fn)}; controlled spawns read from landmark descriptors. |
| landmark | F6 | `src/core/landmark/Landmark.h, src/feature/landmark/Landmark.h` | gf::core::landmark::{nearestCenter(cx, cz), chunkHasCenter(cx, cz), pickVariety(seed, cx, cz), pickForChunk(seed, cx, cz, BiomeAtFn)} (pure); gf::landmark::{descriptor(key), registerGenerator(key, Generator{layout, terraform?, spawnIndex?, pieceProtected?}), startAt(dim, pos), startsIntersecting(dim, ChunkPos), isConquered(Start const&), setConquered(dim, pos, key, bool) (fires structure_cleared), isProtectedFor(Player const&, pos), trySpawnHint(Player&, Start const&, pos), generateAt(key, BlockSource&, pos), forceStart(key, GlobalPos), enforcedProgression(), addTerraformer(key, fn), decorationAllowed(ChunkPos, DecoKind)}. |
| structure | F7 | `src/feature/structure/Structure.h`, `src/feature/structure/bases/{Component,ComponentOld,TemplatePieces,Maze,HollowHill,Tower}.h` | gf::structure::Piece {bbox, orientation, mirror, placeBlock(x, y, z, state-by-name), getBlock, fill, fillAir, randomFill(selector), placeChest(blockId, tableId), placeSpawner(entityId), placeTemplate(id, origin, Settings)}; registerPieceType(id, Factory); bases TFMaze, HollowHillComponent, TowerWing family (16 classes), TFStructureComponentOld/Template, TwilightTemplate/DoubleTemplate/Jigsaw pieces; templates::{get, place}; registerProcessor(id, fn); registerMarkerHandler(id, fn); palettes::apply; selectors::get(id); jigsaw::assemble(pool, origin, depth, rng); speleothems::place(region, pos, configId, rng, hanging). |
| entity | F8 | `src/feature/entity/Entity.h` | gf::entity::{registerType(id, Behaviour{onSpawn, tick, aiStep, hurt, die, save, load, interact}), data(Actor&, key) -> json&, sync(Actor&, key), setProperty(Actor&, name, value), addGoal(Mob&, prio, Goal), addTargetGoal(...), goals::{charge, breath, flockToSameKind, panicOnFlockDeath, avoidAny, goHome, alwaysWatchTarget, randomLookIfBored, simplifiedAttack}, setMoveControl(Mob&, kind\|fn), setHome(Mob&, GlobalPos, radius), home(Mob const&), addPart(Mob&, partId, size, offsetFn) -> PartHandle, mount::{grab(Mob&, Player&), throwRider(Mob&, Vec3)}, addTargetFilter(fn), seedForTest(Actor&, seed)}. |
| projectile | F8 | `src/feature/projectile/Projectile.h` | gf::projectile::{registerType(id, Spec{gravity, drag, onHitEntity, onHitBlock, tick}), shoot(Actor& shooter, id, Vec3 dir, speed, inaccuracy, json init = {}) -> Actor*, addSteering(SteeringFn)}. |
| boss | F8 | `src/feature/boss/Boss.h` | gf::boss::{registerBoss(BossSpec{entityId, homeStructure, lootTable, deathChestBlock, bar{rgb, darken, fog}, deathTicks}), registerSpawner(SpawnerSpec{blockId, entityId, range = 9, particle, spawnFn}), bar(Mob&), setMinion(Actor&, bool), isMinion(Actor const&), participants(Mob const&), trophyVariant(blockId) -> {sound, volume, pitch, particle, comparator}}; config section 'bosses'. |
| item | F9 | `src/feature/item/Item.h` | gf::item::{registerItem(id, Behaviour{use, useTick, release, finishUsing, hurtEnemy, mineBlock, destroySpeed, inventoryTick, armorTick, onEquip, onUnequip, attackBonus, repairWith}), getData/setData(ItemStack, key, json), cooldown(Player&, category, ticks), addTooltip(id, fn), registerDispense(id, fn), setBar(id, fn)}. |
| enchant+effects | F9 | `src/feature/enchant/Enchant.h, src/feature/effects/Effects.h` | gf::enchant::{registerType(id, Spec), level(ItemStack const&, id), set(ItemStack&, id, lvl), all(ItemStack const&)} (user data + lore + glint; table/anvil/grindstone/book hooks); gf::effects::{registerType(id, Spec{carrier}), apply(Actor&, id, ticks, amp), has, amplifier, remove}. |
| recipes | F9 | `src/feature/recipes/Recipes.h` | gf::recipes::{registerType(typeId, Loader), registerSpecialCrafting(id, Matcher, Assembler, Remainder), producing(itemId) -> vector<CraftingRecipe const*>, ofType(typeId), findFirst(...)}. |
| data | F9 | `src/feature/{tags/Tags,datamaps/DataMaps,stats/Stats,traits/Traits,gear/Gear,catalog/Catalog}.h` | gf::tags::{has(Registry, tag, id), members(Registry, tag)}; gf::datamaps::get(mapId, key) -> json const*; gf::stats::award(Player&, id, n = 1); gf::traits::{set(Player&, id, bool), get(Player const&, id)} (synced); gf::gear::{level(Player const&, modifierId), modifiers(ItemStack const&), add/remove(ItemStack&, modifierId)}; gf::catalog (creative groups). |
| combat | F10 | `src/feature/combat/Combat.h` | gf::combat::{onIncoming(prio, fn(DamageInfo&{type, attacker, direct, amount}) -> Pass\|Cancel), onDealt(prio, fn), hurt(Actor& victim, float amount, typeId, Actor* attacker = nullptr, Actor* direct = nullptr)}; 40 damage types and death messages pre-registered from import. |
| death | F10 | `src/feature/death/Death.h` | gf::death::{onLethal(prio, fn) -> bool survive, registerKeeper(prio, fn(Player&, ItemStack&) -> bool keep), onDropsRemaining(fn), onRespawn(fn)}. |
| loot | F10 | `src/feature/loot/Loot.h` | gf::loot::{roll(tableId, LootContext const&) -> vector<ItemStack>, registerFunction(id, fn), registerCondition(id, fn), addGlobalModifier(id, fn), placeContainer(BlockSource&, pos, blockId, facing, tableId, seed), onEntityDrops(fn)}. |
| inventory | F10 | `src/feature/inventory/Inventory.h` | gf::inventory::{registerBlock(blockId, Handler{slots, get, set, insert, extract, setLootTable, open}), find(BlockSource&, pos) -> optional<Handler>, openChestLike(Player&, GlobalPos, rows, titleKey)}; data-declared containers assets/data/containers/<u>.json; backend chosen by spike S3. |
| progression | F10 | `src/feature/progression/Progression.h` | gf::progression::{has(Player const&, adv), grant, revoke, done, trigger(Player&, triggerId, json = {}), onTrigger(triggerId, fn), onGranted(fn), showMissing(Player&, adv), events::{onInventoryChanged, onKilled, onConsumed, onDimensionChanged, onBiomeEntered, onTick}}; progression::client::{has(adv), enforced(), biomeLocked(BiomeId)}. |
| rules+fx | F10 | `src/feature/gamerules/GameRules.h, src/feature/fx/Fx.h` | gf::gamerules::{registerBool/registerInt(id, default), get, set} (+ /tf gamerule; tfEnforcedProgression, portal delays); gf::fx::{particle(Dimension&, id, Vec3 pos, Vec3 vel = {}, count = 1, json params = {}), sound(Dimension&, event, Vec3, vol, pitch), soundTo(Player&, event, vol, pitch)}. |
| families+blockevents | F11 | `src/feature/families/{BlockEvents,Signs}.h` | assets/data/families/<u>.json [{id, family: stairs\|slab\|fence\|fence_gate\|wall\|door\|trapdoor\|button\|pressure_plate\|pane\|pillar\|log\|wood\|leaves\|sapling\|potted\|sign\|hanging_sign\|chest\|trapped_chest\|horizontal\|directional\|waterloggable\|snowloggable\|climbable\|lightable\|connected_texture, params}] -> BP JSON at assemble when no manual file; gf::blockevents::{onRandomTick, onNeighborChanged, onEntityInside, onSteppedOn, onProjectileHit, onPlaced, onRemoved}(id or tag, fn); gf::signs::{text(pos), setText(pos, lines)}. |
| client.ui | F12 | `src/client/ui/Ui.h, src/feature/ui/Ui.h` | gf::client::screen::registerScreen(type, Factory) (ContainerScreen base, TF GUI skin); hud::addLayer(id, order, DrawFn); toast::show(ToastSpec{iconItem, titleKey, bodyKey, ms}); tooltip::add(itemId, fn); server side gf::ui::{form(...), ddui(...), containerTitleSkin(titleKey, skinId)} for vanilla clients. |
| client.render | F12 | `src/client/render/Render.h` | gf::client::render::{registerBlockRenderer(blockId, fn, Flags{throughWalls}), registerItemRenderer(itemId, fn, Contexts), registerEntityRenderer(entityId, fn), onWorld(fn), Lines, drawGeo(ctx, geoId, texture, Transform)}; tint::registerBlock(blockId, fn(BlockPos) -> rgb); bar colours from item::setBar. |
| client.world | F12 | `src/client/world/Sky.h` | gf::client::sky::setProvider(dim, SkyProvider{sky, stars, fog(biome), clouds, weather}); music::setProvider(fn); particles::registerNative(id, Factory) (29 TF ids pre-declared); maps::{view(MapId) -> MapView{pixels, decorations}, setIcon(decoType, texture)}; camera::{addFovModifier(fn), shake(intensity, ticks)}. |
| client.input+movement | F12 | `src/client/input/Input.h, src/feature/movement/Movement.h` | gf::client::keys::{registerKey(id, defaultKey, category, onPress, onRelease), down(id)}; gf::client::movement::onLocalTick(prio, fn(MoveState&)); server gf::movement::{onInputEdge(fn(Player&, Edge)), applyMotion(Player&, Vec3), validate(id, fn)} for vanilla fallbacks and anti-desync. |

### Registration macros

| Macro | Header | Purpose |
|---|---|---|
| `GF_REGISTER_MODULE(T)` / `GF_REGISTER_MODULE_ORDERED(T, order)` | `mod/Module.h` | `T` has `install()` / `uninstall()` (+ optional `load()` at mod load, + optional `static T& get()`). W0 services use negative `ModuleOrder`s; units use the default 0. |
| `GF_REGISTER_CONFIG("<unit>", T)` | `mod/Config.h` | Reflectable struct with `int version`; file `config/<unit>.json`; `gf::config::get<T>()`; edited with `/twilightforest config`. One section per unit (R13, [docs/spec/config.md](docs/spec/config.md)). |
| `GF_REGISTER_SUBCOMMAND(fn)` | `mod/Commands.h` | Adds overloads to `/twilightforest` (aliases `/tf`, `/tffeature`). `commands::playerOf`, `commands::isAdmin`. One owner per subcommand (R14). |
| `GF_REGISTER_SELFTEST("<unit>.<case>", fn)` | `selftest/SelfTest.h` | `fn(Context&)` or `ll::coro::CoroTask<>(Context&)`. |

Registrations live in `.cpp` files linked directly into the DLL, so they are never stripped.

### Using the foundation

- **Ids** are TF's own: `twilightforest:<path>`. Fixture ids for W0b selftests: `gf_test:*`
  (`gf_test:fixture_block` with integer state `gf_test:level` 0..3 and `gf_test:lit`,
  `gf_test:fixture_item`, the entities `gf_test:boss` + `gf_test:boss_part` (3 parts by property),
  the loot tables `gf_test:chests/fixture_chest` and `gf_test:entities/boss`, the recipes
  `gf_test:fixture_shaped` and `gf_test:fixture_drying`, the feature `gf_test:fixture_column`, the tag
  `gf_test:fixture_blocks` and the template `gf_test:fixture`; see `assets/test`).
- **World data** (`game/WorldDb.h`): JSON in the world's LevelDB, `db::get/put/erase("gf:<unit>:...")`.
  `db::onWorldReady/onWorldSave/onWorldClosed` for the lifecycle (the client loads many worlds in one
  process). Never keep `Player*` or block pointers across ticks (R16).
- **Strings** (`mod/I18n.h`, `client/Client.h`): `gf::tr("key", "English fallback", args...)`,
  `gf::trFor(player, ...)`, client `gf::client::ctr(...)`; placeholders `{}`, `{0}`, `%s`, `%1$s`.
  Keys belong to the feature owner (R9); others pass the English fallback without defining the key.
  Block / item names come from the RP `texts` (imported from TF).
- **Logging**: `gf::log()` (`mod/Log.h`). No per-tick logging; one line per user-visible action that
  e2e scenarios can wait for.
- **Selftests** (`selftest/SelfTest.h`): `ctx.player()` (SimulatedPlayer), `spawnPlayer()`,
  `testPos(dx)` (your dx range, R12), `co_await flatArea(dx, size)` (a cleared flat area, restored
  afterwards), `co_await inTwilight()` (move the test into the Twilight Forest), `setBlock(pos, id,
  {{"state", value}})`, `cleanupBlock`, `give(id, n)`, `co_await ticks(n)`, `co_await ensureLoaded(pos)`,
  `captureBossEvent()` (records BossEventPackets), `snapshotVolume(box)` (golden volume hash),
  `check / require / fail / info`. `/twilightforest selftest` and the server scenario `full` run every
  unit's tests together: keep yours passing there. Rows are 5 blocks apart in z: keep `testPos(dx, dz)`
  within dz 0..4 and leave no blocks behind outside it (a larger `flatArea` reaches into the next
  units' rows and is safe only because it is restored). A flat area is at most
  `Context::kMaxFlatAreaSize` (144) wide and a test holds at most 7 at once (Bedrock: 10 ticking areas
  per world, 100 chunks each). `ctx.player()` is valid for the current tick only: call it again
  after a `co_await` (R16).
- **Interactions** (`feature/common/Interactions.h`, R8): never hook `GameMode::$useItemOn / $useItem
  / baseUseItem / $destroyBlock / $startDestroyBlock / $interact / $attack` or the LeviLamina
  interact / use / destroy events. Register `interactions::onUseOn({.block = id, .item = id}, fn,
  priority)`, `onUse`, `onDestroy`, `onStartDestroy`, `onPlace`, `onUnusedUseOn`, `onDestroyed`,
  `onInteract`, `onAttack`, `onAttacked`; `Match::tag("twilightforest:mazestone")` matches by block
  (entity) tag, `Match::itemTagged(tag)` by item tag. Handlers return `Pass` / `Consume` / `Deny`; the
  first non-`Pass` by priority wins; landmark protection (F6) is `priority::Highest`. `nextTick(fn)`
  defers a world edit; `mutableItem(ctx.item)` is the hand slot. Bedrock repeats use-on while the
  button is held (`firstEvent`): debounce per player.
- **Click and hold** (`feature/common/Hold.h`): `hold::awaitHold(player, pos, onShort, onHold)` asks
  a companion's client to watch the use button; vanilla players get `false` (keep a click fallback).
- **Block data** (`feature/common/BlockData.h`): per-position JSON of custom blocks (no block entities
  on custom blocks), `blockdata::get/set(region, pos, key, json)`; erased when the block type changes;
  `onRotate(key, fn)` turns direction-dependent values.
- **Redstone** (`feature/common/Redstone.h`): custom blocks get no neighbour updates: read
  `redstone::power(region, pos)` (0..15), get told with `redstone::watch(pos, fn)`, register signal
  sources with `registerEmitter(blockId, fn)` and comparator output with `registerAnalog(blockId, fn)`.
  Never hook the circuit system in a unit.
- **Client sync** (`feature/net/Net.h`, R15): server -> client-mod data only over the companion
  protocol (`net::send(player, "<unit>.<name>", json)`, `net::onCompanionJoined`, `net::client::on`).
  `send` refuses vanilla players, so vanilla clients never receive companion packets. One client
  handler per channel.
- **Screens** (`feature/common/Screens.h`, `client/screen/base/Screen.h`, F12's `client/ui/Ui.h`): a
  screen type has native data / actions for companions and a form for vanilla players.
- **Config screen**: `/twilightforest config [section]` edits every config section in game
  (operators only); keep config fields plain reflectable values.

## Build, test, run

In a fresh pwsh refresh PATH first (the build script does it too and adds LLVM if needed):
`$env:Path=[Environment]::GetEnvironmentVariable('Path','Machine')+';'+[Environment]::GetEnvironmentVariable('Path','User')`

```powershell
git config --global core.longpaths true                       # once (long LeviLamina header paths)
pwsh tools/build.ps1 -Target server                            # iterate (one target, a build slot)
pwsh tools/build.ps1 -Target all -Tests                        # before a PR: both targets + gtest
xmake run -P . gf-tests                                        # -P .: worktrees nest in another checkout
pwsh tools/assemble.ps1 -Check                                 # packs + checks without building
pwsh -NoProfile -File tools/e2e/server/run.ps1 -Scenario <unit>
pwsh -NoProfile -File tools/e2e/client/run.ps1 -Scenario <unit> -Mode local    # + remote-vanilla
pwsh tools/ci/ownership.ps1 -Unit <unit>
pwsh tools/ci/arr-guard.ps1
```

- Builds and e2e runs go only through `tools/build.ps1` and `tools/e2e/*/run.ps1` (R23): they take
  slots of `tools/lib/Throttle.psm1` (build 3 with a 10 GB free-memory gate, bds 5, client 2). Exit
  code 2 means **NOT RUN** (no slot in time): retry, never count it as a pass.
- `xmake-requires.lock` holds the package entries of both target types; `tools/build.ps1` keeps it
  as committed (xmake would rewrite it per configure). `-UpdateLock` (with `-Target all`) re-resolves
  both. A build runs in a kill-on-exit job: killing `tools/build.ps1` also stops its xmake and
  clang-cl processes, so a released slot never leaves compiles running.
- In game: `/twilightforest selftest ["glob"]` (op or console; quote globs). Output lines:
  `SELFTEST PASS <name>`, `SELFTEST FAIL <name>: <reason>`, `SELFTEST DONE pass=<N> fail=<M>`.
  `/twilightforest stubs` lists the contract stubs that were called.
- Log contract: `GloamingForest enabled` on enable; `GloamingForest world ready` when a world is
  available.
- clang-cl must be on PATH (`C:\Program Files\LLVM\bin`). LeviLamina headers:
  `%LOCALAPPDATA%\.xmake\packages\l\levilamina\26.51.7\<hash>\include\{ll,mc}` (one hash per target type).

## Bedrock / LeviLamina rules (learned the hard way)

- Check the headers before guessing a name or signature. Callable: `MCAPI`, `MCFOLD`, `MCTAPI`,
  unmarked `virtual`. **Never** `MCNAPI`. `ll::TypedStorage` members are fields (`.get()`, `*`, `->`;
  trivial ones are the type itself). Packets without an MCAPI constructor are created with
  `MinecraftPackets::createPacket(MinecraftPacketIds::X)` + `static_cast`; send with `pkt.sendTo(player)`.
- Hook virtuals via `&Cls::$fn` with `LL_TYPE_INSTANCE_HOOK`; `hook()` in `install()`, `unhook()` in
  `uninstall()`. Disambiguate overloads with a `static_cast` to the member function type.
- In `ServerNetworkHandler::$handle` hooks `this` is the `NetEventCallback` base subobject.
- **No `dynamic_cast` on game types** (no usable RTTI). A Player that is not `isClientSide()` is a `ServerPlayer`.
- Command parameter types need external linkage (not in an anonymous namespace).
- `ItemStack` has no `(name, count, aux)` constructor: use `game::items::make(id, count, aux)`.
- Blocks with states: `game::blockWithStates(id, {{"state", value}})` (it resolves the full
  serialization id; `Block::tryGetFromRegistry(name, states)` returns the default permutation).
- BDS enables mods **after** "Server started" (the world already exists at `install()`); the client
  enables mods at game start (no world yet) and fires `ServerStartedEvent` for every local world.
  Write modules that work in both orders (`game::db::onWorldReady`).
- `ll::service::getLevel()` on the client is the integrated server's level (null outside a world).
- Behavior packs in `mods/<name>/behavior_packs/` are added to every world by LeviLamina; resource
  packs to the client's global stack. On BDS `PackDeployer` adds the RPs to the world.
- Custom blocks cannot have block entities: keep per-block state in block states + BlockData.
- Bedrock reads a list-valued integer block state by the value's position: declare integer states as
  `[0, 1, ..., n]` (assemble fails otherwise) and watch the permutation budget.
- A cancelled `PlayerInteractBlockEvent` rolls back block changes made during it: defer world edits.
- Don't keep `Player*` across ticks; store `getOrCreateUniqueID()` / the UUID and look it up again.
- PowerShell variables are case-insensitive: `$m` and `$M` are the same variable.

## Conventions

- C++20, `.clang-format` (LLVM-based, 4 spaces, 120 columns). Namespaces follow directories
  (`gf::core`, `gf::game`, `gf::feature::<unit>`, `gf::client::...`; contracts use their short
  namespaces `gf::world`, `gf::loot`, ...).
- English in code and comments; user-facing text through `tr` / `trFor` / `ctr` and lang files.
- Every `src/core` change comes with gtests in `tests/`; every feature with selftests and an e2e
  scenario.
- Commit in small, buildable steps with imperative messages. PR titles `U<n>: <imperative summary>`
  / `F<n>: ...` (R25), into `main`. Never add Claude / AI attribution.

## Units

Ids are the plan's; the name is the unit's directory (`src/feature/<name>/`), selftest prefix (`<name>.*`), net
channel prefix (`<name>.`), world DB prefix (`gf:<name>:`) and e2e scenario name. Full records (paths, content, e2e)
are in [docs/spec/units.json](docs/spec/units.json); [docs/spec/README.md](docs/spec/README.md) has the table with every
owned path.

| Unit | Name | Wave | Title | Selftest dx |
|---|---|---|---|---|
| F0 | `core` | W0a | Scaffold, contract headers, owners | 5000-5099 |
| F1 | `import` | W0b | Import tool and content skeleton (S7) | 5100-5199 |
| F2 | `models` | W0b | Java model converter and geometry import | 5200-5299 |
| F3 | `synth` | W0b | Sound synthesis toolkit and baseline audio | 5300-5399 |
| F4 | `dimension` | W0b | Dimension, time, teleport and Twilight portal (S1) | 5400-5499 |
| F5 | `worldgen` | W0b | Generator pipeline, features, spawn, Java-parity core (S2) | 5500-5599 |
| F6 | `landmark` | W0b | Landmarks: grid, placement, descriptors, conquest, protection, hints | 5600-5699 |
| F7 | `structure` | W0b | Structure kit, template toolkit and shared piece libraries | 5700-5799 |
| F8 | `entity` | W0b | Entities, AI, multipart, projectiles and boss kit (S4) | 5800-5899 |
| F9 | `item` | W0b | Items, enchantments, effects, recipes, tags, data maps, stats, gear (S6) | 5900-5999 |
| F10 | `combat` | W0b | Combat, death, loot, inventories, progression bus, gamerules, fx (S3) | 6000-6099 |
| F11 | `families` | W0b | Block families kit and block event dispatch | 6100-6199 |
| F12 | `ui` | W0b | Client frameworks and vanilla UI fallbacks (S5) | 6200-6299 |
| F13 | `oracle` | W0b | Java oracle (derived goldens) | 6300-6399 |
| F14 | `spec-world` | W0b | Spec: world and landmarks | 6400-6499 |
| F15 | `spec-mobs` | W0b | Spec: mobs and late landmarks | 6500-6599 |
| F16 | `spec-items` | W0b | Spec: blocks, items, player systems, client; not-ported list | 6600-6699 |
| U1 | `biomes` | W1 | Biomes and biome layers | 100-199 |
| U2 | `terrain` | W1 | Terrain, carvers and chunk blankets | 200-299 |
| U3 | `trees` | W1 | Trees, leaves, saplings and magic cores | 300-399 |
| U4 | `woodwork` | W1 | Wood sets and wooden furniture | 400-499 |
| U5 | `building` | W1 | Building blocks and boss collectibles | 500-599 |
| U6 | `flora` | W1 | Flora, critters and berries | 600-699 |
| U7 | `armory` | W1 | Armor and tool tiers | 700-799 |
| U8 | `weapons` | W1 | Weapons and throwables | 800-899 |
| U9 | `artifacts` | W1 | Scepters, charms, artifacts and death keeping | 900-999 |
| U10 | `provisions` | W1 | Provisions, enchantments, effects, music discs and soundscape | 1000-1099 |
| U11 | `tgear` | W1 | Travellers' Gear: equipment, goggles and red thread | 1100-1199 |
| U12 | `tgmove` | W1 | Travellers' Gear: movement | 1200-1299 |
| U13 | `uncraftmaps` | W1 | Uncrafting table and maps | 1300-1399 |
| U14 | `progression` | W1 | Progression and advancements | 1400-1499 |
| U15 | `atmosphere` | W1 | Sky, fog, weather and aurora | 1500-1599 |
| U16 | `fauna` | W1 | Passive and ambient fauna | 1600-1699 |
| U17 | `hostiles` | W1 | Forest and hollow-hill hostiles | 1700-1799 |
| U18 | `minorlandmarks` | W1 | Minor landmarks and decorative features | 1800-1899 |
| U19 | `questcamps` | W1 | Quest grove, camps and template features | 1900-1999 |
| U20 | `naga` | W1 | Naga courtyard and Naga | 2000-2099 |
| U21 | `lich` | W1 | Lich tower and Lich | 2100-2199 |
| U22 | `lichdecor` | W1 | Lich tower interiors and decor | 2200-2299 |
| U23 | `labyrinth` | W1 | Labyrinth and Minoshroom | 2300-2399 |
| U24 | `hydra` | W1 | Hydra lair, Hydra and fire swamp | 2400-2499 |
| U25 | `stronghold` | W1 | Knight stronghold and Knight Phantoms | 2500-2599 |
| U26 | `darktower` | W1 | Dark tower, Ur-Ghast and tower devices | 2600-2699 |
| U27 | `caves` | W1 | Troll caves and yeti cave | 2700-2799 |
| U28 | `aurora` | W1 | Aurora palace, Snow Queen and mushroom tower | 2800-2899 |
| U29 | `giants` | W1 | Beanstalk, cloud castle and giants | 2900-2999 |
| U30 | `finalcastle` | W1 | Final castle and thornlands | 3000-3099 |
| A8 | `integration` | W2b | Integration and consistency | 9000-9099 |

## Worker template

Each unit is implemented by one worker in its own worktree (branch per unit), following
[docs/spec/worker.md](docs/spec/worker.md). The worker gets:

- **Goal:** the user's instruction, quoted verbatim: 基于LeviLamina，制作Minecraft Java版模组Twilight Forest的基岩版复刻。要求支持客户端和服务端；所有特性和交互和界面必须完全一致，没有任何缺失。可以借助 LiteLDev/BeDD 来调试。
  Plus the decisions: TF 4.8 at `9e40a757`; reuse the CC BY-NC-SA assets and regenerate the All Rights
  Reserved ones; vanilla clients best effort; client-mod parity first.
- **Its unit record**, verbatim from [docs/spec/units.json](docs/spec/units.json) (id, title, paths,
  content, description, contracts, e2e, size).
- **Conventions:** read AGENTS.md, docs/spec/README.md, its `docs/spec/<unit>.md` and the contract
  headers before coding; follow R1-R25. Reference: Levitate on GitHub; LeviLamina headers (check them
  before guessing a signature; never call MCNAPI); TF source only from `%LOCALAPPDATA%\gf-tf`; never
  open TF `.ogg` or `.nbt` files; no Claude or AI attribution in commits or PRs.
- **The e2e recipe** ([docs/spec/e2e.md](docs/spec/e2e.md)): refresh PATH; `git config --global
  core.longpaths true`; `pwsh tools/build.ps1 -Target all -Tests` then `xmake run -P . gf-tests`;
  `tools/e2e/server/run.ps1 -Scenario <u>` (exit 0 with `SELFTEST DONE ... fail=0`, no ERR / FTL /
  content-log errors; exit 2 is NOT RUN: retry); `tools/e2e/client/run.ps1 -Scenario <u> -Mode local`
  (open every screenshot and compare it with TF's textures and GUIs), plus `-Mode remote-vanilla`
  when the unit has a vanilla fallback; `tools/ci/ownership.ps1 -Unit <u>` and
  `tools/ci/arr-guard.ps1`. F0 runs the `smoke` scenarios; the spec units F14-F16 skip e2e and run
  the doc lint.
- **These steps, verbatim:**

```
After you finish implementing the change:
1. **Code review** — Invoke the `Skill` tool with `skill: "code-review"` to find correctness bugs (it reports findings; it does not edit code). Fix any findings it surfaces before continuing.
2. **Run unit tests** — Run the project's test suite (check for package.json scripts, Makefile targets, or common commands like `npm test`, `bun test`, `pytest`, `go test`). If tests fail, fix them.
3. **Test end-to-end** — Follow the e2e test recipe from the coordinator's prompt (below). If the recipe says to skip e2e for this unit, skip it.
4. **Commit and push** — Commit all changes with a clear message, push the branch, and create a PR with `gh pr create`. Use a descriptive title. If `gh` is not available or the push fails, note it in your final message.
5. **Report** — End with a single line: `PR: <url>` so the coordinator can track it. If no PR was created, end with `PR: none — <reason>`.
```

- **The test suite** is `pwsh tools/build.ps1 -Target all -Tests` followed by `xmake run -P . gf-tests`.
- **The final message also lists:** what remains unimplemented against the unit's content list; any
  `src/feature/ext/` contract extensions it added; spike conclusions (F units).
