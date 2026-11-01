// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFLoot.java
//   src/main/java/twilightforest/loot/conditions/ModExistsCondition.java
//   src/main/java/twilightforest/loot/TFLootTables.java (the container / entity drops use)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Contract "loot" (F10, rule R5): tables, rolls, the global modifiers, containers and drops.
//
// - Tables are the Java data pack files <mod>/data/<ns>/loot_table/<path>.json (F1 imports TF's
//   634; units' own data lands there too), parsed once per world; LootEval.cpp interprets them.
// - roll(): a seeded LootContext uses Java's LegacyRandomSource (RandomSource.create(seed)) like
//   LootTable#fill; seed 0 is random. Global modifiers (NeoForge IGlobalLootModifier) run after
//   every roll with the table id.
// - placeContainer(): vanilla containers (chest, barrel, ...) get the table as the native loot
//   field ("gf_loot:<table>#<seed>"); the game unpacks it when the container is first opened,
//   emptied by a hopper or broken, and one RandomizableBlockActorContainerBase#unPackLootTable
//   hook rolls the Java table into it with LootTable#fill's slot spreading. Custom container
//   blocks get it through gf::inventory's setLootTable.
// - Entity drops: a mob that dies rolls <ns>:entities/<path> when that Java table exists (TF's
//   entities; vanilla mobs keep Bedrock's drops), then the onEntityDrops handlers change the drops.
// - Block drops: blocks of the twilightforest namespace drop from <ns>:blocks/<path> (survives
//   explosion, silk touch, fortune, shears ...); other blocks keep Bedrock's drops, run through the
//   global modifiers (TF's fiery pickaxe smelting, giant pickaxe grouping) when any is registered.

#include "feature/loot/Loot.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <unordered_map>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/IRandom.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/ResourceDrops.h"
#include "mc/world/level/block/ResourceDropsContext.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/RandomizableBlockActorContainerBase.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"

#include "feature/boss/Boss.h"
#include "feature/inventory/Inventory.h"
#include "feature/loot/JavaRandom.h"
#include "feature/loot/LootInternal.h"
#include "feature/loot/LootRegistry.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

namespace gf::loot {

namespace {

constexpr char kMarker[] = "gf_loot:";

struct Registry {
    std::recursive_mutex                          mutex;
    std::unordered_map<std::string, Json>         files;      ///< parsed table files (cache)
    std::set<std::string>                         missing;    ///< ids without a file (cache)
    std::unordered_map<std::string, Json>         registered; ///< registerTable (fixtures)
    std::unordered_map<std::string, ConditionFn>  conditions;
    std::unordered_map<std::string, FunctionFn>   functions;
    std::vector<std::pair<std::string, ModifierFn>> modifiers;
    std::vector<EntityDropsFn>                    entityDrops;
    std::set<std::string>                         warned;
    int                                           testParticipants = -1;
    std::optional<bool>                           testAdjusts;
};

Registry& registry() {
    static Registry r;
    return r;
}

thread_local std::optional<float> tExplosion;

std::filesystem::path tableFile(std::string const& id) {
    auto colon = id.find(':');
    if (colon == std::string::npos) return {};
    try {
        auto data = Mod::getInstance().getSelf().getModDir() / "data" / id.substr(0, colon);
        for (char const* dir : {"loot_table", "loot_tables"}) {
            auto p = data / dir / (id.substr(colon + 1) + ".json");
            std::error_code ec;
            if (std::filesystem::is_regular_file(p, ec)) return p;
        }
    } catch (...) {}
    return {};
}

void warnOnce(std::string const& what) {
    std::lock_guard lock(registry().mutex);
    if (registry().warned.insert(what).second) log().warn("loot: {}", what);
}

std::int64_t randomSeed() {
    static std::mt19937_64 gen{std::random_device{}()};
    std::int64_t           s = 0;
    while (s == 0) s = static_cast<std::int64_t>(gen());
    return s;
}

BlockPos floorPos(Vec3 const& v) {
    return BlockPos{static_cast<int>(std::floor(v.x)), static_cast<int>(std::floor(v.y)), static_cast<int>(std::floor(v.z))};
}

void spawnAt(BlockSource& region, Vec3 const& pos, ItemStack const& stack) {
    if (game::items::isEmpty(stack)) return;
    region.getLevel().getSpawner().spawnItem(region, stack, nullptr, pos, 0);
}

bool gameRule(GameRules::GameRulesIndex rule, bool fallback) {
    auto level = ll::service::getLevel();
    if (!level) return fallback;
    GameRuleId id;
    id.mValue = static_cast<int>(rule);
    return level->getGameRules().getBool(id, fallback);
}

/// <ns>:<kind>/<path> for a block or entity type id.
std::string tableIdFor(std::string const& typeId, char const* kind) {
    auto colon = typeId.find(':');
    if (colon == std::string::npos) return "minecraft:" + std::string(kind) + "/" + typeId;
    return typeId.substr(0, colon) + ":" + kind + "/" + typeId.substr(colon + 1);
}

std::vector<ItemStack> applyModifiers(std::vector<ItemStack> loot, LootContext const& ctx, std::string const& tableId) {
    std::vector<std::pair<std::string, ModifierFn>> modifiers;
    {
        std::lock_guard lock(registry().mutex);
        modifiers = registry().modifiers;
    }
    for (auto const& [id, fn] : modifiers) {
        try {
            fn(loot, ctx, tableId);
        } catch (std::exception const& ex) {
            log().error("loot: global modifier {} failed: {}", id, ex.what());
        }
    }
    std::erase_if(loot, [](ItemStack const& s) { return game::items::isEmpty(s); });
    return loot;
}

/// Util.shuffle (Collections-like Fisher-Yates from the end).
template <class T>
void javaShuffle(std::vector<T>& list, RandomSource& random) {
    for (int i = static_cast<int>(list.size()); i > 1; --i) {
        int j = random.nextInt(i);
        std::swap(list[static_cast<std::size_t>(i - 1)], list[static_cast<std::size_t>(j)]);
    }
}

} // namespace

// ---- registry --------------------------------------------------------------------------------------

Json const* table(std::string const& id) {
    auto&           r = registry();
    std::lock_guard lock(r.mutex);
    if (auto it = r.registered.find(id); it != r.registered.end()) return &it->second;
    if (auto it = r.files.find(id); it != r.files.end()) return &it->second;
    if (r.missing.contains(id)) return nullptr;
    auto file = tableFile(id);
    if (file.empty()) {
        r.missing.insert(id);
        return nullptr;
    }
    try {
        std::ifstream in(file);
        auto          j = Json::parse(in, nullptr, true, true);
        return &(r.files[id] = std::move(j));
    } catch (std::exception const& ex) {
        log().error("loot: bad loot table {}: {}", file.string(), ex.what());
        r.missing.insert(id);
        return nullptr;
    }
}

void registerTable(std::string const& id, Json t) {
    std::lock_guard lock(registry().mutex);
    if (t.is_null()) registry().registered.erase(id);
    else registry().registered[id] = std::move(t);
}

void clearCache() {
    std::lock_guard lock(registry().mutex);
    registry().files.clear();
    registry().missing.clear();
}

std::optional<ConditionFn> customCondition(std::string const& id) {
    std::lock_guard lock(registry().mutex);
    auto            it = registry().conditions.find(id);
    if (it == registry().conditions.end()) return std::nullopt;
    return it->second;
}

std::optional<FunctionFn> customFunction(std::string const& id) {
    std::lock_guard lock(registry().mutex);
    auto            it = registry().functions.find(id);
    if (it == registry().functions.end()) return std::nullopt;
    return it->second;
}

std::optional<float> currentExplosionRadius() { return tExplosion; }
ExplosionScope::ExplosionScope(std::optional<float> radius) : mPrevious(tExplosion) { tExplosion = radius; }
ExplosionScope::~ExplosionScope() { tExplosion = mPrevious; }

int qualifiedPlayers(Actor const* boss) {
    {
        std::lock_guard lock(registry().mutex);
        if (registry().testParticipants >= 0) return registry().testParticipants;
    }
    if (!boss || !const_cast<Actor*>(boss)->hasCategory(ActorCategory::Mob)) return 0;
    return static_cast<int>(boss::participants(*static_cast<Mob const*>(boss)).size());
}

void setTestParticipants(int n) {
    std::lock_guard lock(registry().mutex);
    registry().testParticipants = n;
}

bool adjustsLootRolls() {
    {
        std::lock_guard lock(registry().mutex);
        if (registry().testAdjusts) return *registry().testAdjusts;
    }
    // TFConfig.multiplayerFightAdjuster lives in the `bosses` section (F8): MORE_LOOT and
    // MORE_LOOT_AND_HEALTH add loot
    for (auto const& entry : config::ConfigRegistry::entries()) {
        if (entry.section != "bosses" || !entry.values) continue;
        auto values = entry.values();
        auto v      = values.value("multiplayerFightAdjuster", nlohmann::ordered_json{});
        if (v.is_string()) {
            auto s = v.get<std::string>();
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return s.find("LOOT") != std::string::npos;
        }
        if (v.is_number_integer()) return v.get<int>() == 1 || v.get<int>() == 3;
    }
    return false; // TF's default NONE
}

void setTestAdjustsLoot(std::optional<bool> value) {
    std::lock_guard lock(registry().mutex);
    registry().testAdjusts = value;
}

// ---- rolls ---------------------------------------------------------------------------------------

std::vector<ItemStack> rollWith(std::string const& tableId, LootContext const& ctx, RandomSource& random) {
    auto const* t = table(tableId);
    if (!t) {
        warnOnce("unknown loot table " + tableId);
        return {};
    }
    Json copy = *t; // the cache may change while rolling (nested tables load lazily)
    return applyModifiers(rollRaw(copy, tableId, ctx, random), ctx, tableId);
}

std::vector<ItemStack> roll(std::string const& tableId, LootContext const& ctx) {
    JavaRandom random(ctx.seed != 0 ? ctx.seed : randomSeed());
    return rollWith(tableId, ctx, random);
}

void fill(
    std::string const&                                tableId,
    LootContext const&                                ctx,
    int                                               size,
    std::function<ItemStack const&(int)> const&       get,
    std::function<void(int, ItemStack const&)> const& set
) {
    // LootTable#fill
    JavaRandom random(ctx.seed != 0 ? ctx.seed : randomSeed());
    auto       stacks = rollWith(tableId, ctx, random);
    // getAvailableSlots
    std::vector<int> slots;
    for (int i = 0; i < size; ++i)
        if (game::items::isEmpty(get(i))) slots.push_back(i);
    javaShuffle(slots, random);
    // shuffleAndSplitItems
    std::vector<ItemStack> list;
    std::vector<ItemStack> kept;
    for (auto& s : stacks) {
        if (game::items::isEmpty(s)) continue;
        if (game::items::countOf(s) > 1) list.push_back(s);
        else kept.push_back(s);
    }
    stacks = std::move(kept);
    while (static_cast<int>(slots.size()) - static_cast<int>(stacks.size()) - static_cast<int>(list.size()) > 0 && !list.empty()) {
        auto idx  = static_cast<std::size_t>(nextIntBetween(random, 0, static_cast<int>(list.size()) - 1));
        auto whole = list[idx];
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(idx));
        int  n     = game::items::countOf(whole);
        int  i     = nextIntBetween(random, 1, n / 2);
        auto part  = game::items::withCount(whole, i);
        auto rest  = game::items::withCount(whole, n - i);
        if (game::items::countOf(rest) > 1 && random.nextBoolean()) list.push_back(rest);
        else stacks.push_back(rest);
        if (game::items::countOf(part) > 1 && random.nextBoolean()) list.push_back(part);
        else stacks.push_back(part);
    }
    for (auto& s : list) stacks.push_back(s);
    javaShuffle(stacks, random);
    for (auto const& s : stacks) {
        if (slots.empty()) {
            warnOnce("tried to over-fill a container with " + tableId);
            return;
        }
        set(slots.back(), s);
        slots.pop_back();
    }
}

void registerFunction(std::string const& id, FunctionFn fn) {
    std::lock_guard lock(registry().mutex);
    registry().functions[id] = std::move(fn);
}

void registerCondition(std::string const& id, ConditionFn fn) {
    std::lock_guard lock(registry().mutex);
    registry().conditions[id] = std::move(fn);
}

void addGlobalModifier(std::string const& id, ModifierFn fn) {
    std::lock_guard lock(registry().mutex);
    auto&           list = registry().modifiers;
    std::erase_if(list, [&](auto const& e) { return e.first == id; });
    list.emplace_back(id, std::move(fn));
}

void onEntityDrops(EntityDropsFn fn) {
    std::lock_guard lock(registry().mutex);
    registry().entityDrops.push_back(std::move(fn));
}

// ---- containers ------------------------------------------------------------------------------------

namespace {

bool isVanillaLootContainer(BlockActor const& actor) {
    switch (actor.getType()) {
    case BlockActorType::Chest:
    case BlockActorType::BarrelBlock:
    case BlockActorType::ShulkerBox:
    case BlockActorType::Dispenser:
    case BlockActorType::Dropper:
    case BlockActorType::Hopper: return true;
    default: return false;
    }
}

/// The block `blockId` facing `facing` (FacingID 2 north, 3 south, 4 west, 5 east), trying the
/// facing states Bedrock and the families kit use.
Block const* facingBlock(std::string const& blockId, int facing) {
    static constexpr char const* names[] = {"down", "up", "north", "south", "west", "east"};
    if (facing >= 2 && facing <= 5) {
        if (auto const* b = game::blockWithStates(blockId, {{"minecraft:cardinal_direction", names[facing]}})) return b;
        if (auto const* b = game::blockWithStates(blockId, {{"facing_direction", facing}})) return b;
        static constexpr int direction[] = {0, 0, 2, 0, 1, 3}; // Bedrock direction: 0 S, 1 W, 2 N, 3 E
        if (auto const* b = game::blockWithStates(blockId, {{"direction", direction[facing]}})) return b;
        if (auto const* b = game::blockWithStates(blockId, {{"twilightforest:facing", names[facing]}})) return b;
    }
    return game::blockWith(blockId);
}

std::pair<std::string, std::int64_t> parseMarker(std::string const& field) {
    auto body = field.substr(sizeof(kMarker) - 1);
    auto hash = body.rfind('#');
    if (hash == std::string::npos) return {body, 0};
    std::int64_t seed = 0;
    try {
        seed = std::stoll(body.substr(hash + 1));
    } catch (...) {}
    return {body.substr(0, hash), seed};
}

} // namespace

bool placeContainer(BlockSource& region, BlockPos const& pos, std::string const& blockId, int facing, std::string const& tableId, std::int64_t seed) {
    auto const* block = facingBlock(blockId, facing);
    if (!block) {
        warnOnce("placeContainer: unknown block " + blockId);
        return false;
    }
    if (game::blockIdAt(region, pos) != blockId || &region.getBlock(pos) != block) {
        if (!game::setBlock(region, pos, *block)) return false;
    }
    if (auto* actor = region.getBlockEntity(pos); actor && isVanillaLootContainer(*actor)) {
        auto& container = static_cast<RandomizableBlockActorContainerBase&>(*actor);
        container.setLootTable(std::string(kMarker) + tableId + "#" + std::to_string(seed), 0);
        log().info("loot: placed {} with {} at {} {} {}", blockId, tableId, pos.x, pos.y, pos.z);
        return true;
    }
    if (auto h = inventory::find(region, pos); h && h->setLootTable) {
        h->setLootTable(game::globalPos(region, pos), tableId, seed);
        log().info("loot: placed {} with {} at {} {} {}", blockId, tableId, pos.x, pos.y, pos.z);
        return true;
    }
    warnOnce("placeContainer: " + blockId + " is not a container");
    return false;
}

std::optional<std::pair<std::string, std::int64_t>> pendingVanilla(BlockSource& region, BlockPos const& pos) {
    auto* actor = region.getBlockEntity(pos);
    if (!actor || !isVanillaLootContainer(*actor)) return std::nullopt;
    auto const& field = static_cast<RandomizableBlockActorContainerBase&>(*actor).mLootTable.get();
    if (field.rfind(kMarker, 0) != 0) return std::nullopt;
    return parseMarker(field);
}

// ---- entity drops ----------------------------------------------------------------------------------

void onMobDied(Actor& dead, combat::DamageInfo const& damage, std::int64_t killerId) {
    if (dead.isClientSide()) return;
    std::vector<EntityDropsFn> handlers;
    {
        std::lock_guard lock(registry().mutex);
        handlers = registry().entityDrops;
    }
    auto const& type    = dead.getTypeName();
    auto        tableId = tableIdFor(type, "entities");
    bool        own     = type.rfind("minecraft:", 0) != 0 && table(tableId) != nullptr;
    if (!own && handlers.empty()) return;
    if (!gameRule(GameRules::GameRulesIndex::DoMobLoot, true)) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    Player* killer = nullptr;
    if (killerId) {
        ActorUniqueID id;
        id.rawID = killerId;
        killer   = level->getPlayer(id);
    }
    auto&       region = dead.getDimensionBlockSource();
    LootContext ctx;
    ctx.region     = &region;
    ctx.origin     = dead.getPosition();
    ctx.thisEntity = &dead;
    ctx.player     = killer;
    ctx.attacker   = damage.attacker;
    ctx.direct     = damage.direct;
    ctx.damageType = damage.type;
    std::vector<ItemStack> drops;
    if (own) drops = roll(tableId, ctx);
    for (auto const& fn : handlers) {
        try {
            fn(dead, drops, ctx);
        } catch (std::exception const& ex) {
            log().error("loot: entity drops handler failed: {}", ex.what());
        }
    }
    for (auto const& s : drops) spawnAt(region, ctx.origin, s);
    if (own) log().info("loot: {} dropped {} stack(s) from {}", type, drops.size(), tableId);
}

} // namespace gf::loot

// ---- hooks ---------------------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    LootUnpackHook,
    ll::memory::HookPriority::Normal,
    RandomizableBlockActorContainerBase,
    &RandomizableBlockActorContainerBase::unPackLootTable,
    void,
    Level&        level,
    Container&    container,
    DimensionType dimensionId,
    Actor*        actor
) {
    std::string field = mLootTable.get();
    if (field.rfind(gf::loot::kMarker, 0) != 0) return origin(level, container, dimensionId, actor);
    auto [tableId, seed] = gf::loot::parseMarker(field);
    eraseLootTable(); // once, whatever happens (Java clears the table before filling)
    auto  pos    = getPosition();
    auto* region = gf::game::regionOf(static_cast<int>(dimensionId));
    gf::loot::LootContext ctx;
    ctx.region     = region;
    ctx.origin     = Vec3{static_cast<float>(pos.x) + .5f, static_cast<float>(pos.y) + .5f, static_cast<float>(pos.z) + .5f};
    ctx.thisEntity = actor;
    ctx.player     = actor && actor->isPlayer() ? static_cast<Player*>(actor) : nullptr;
    ctx.seed       = seed;
    gf::loot::fill(
        tableId,
        ctx,
        container.getContainerSize(),
        [&](int slot) -> ItemStack const& { return container.getItem(slot); },
        [&](int slot, ItemStack const& s) { container.setItem(slot, s); }
    );
    gf::log().info("loot: unpacked {} at {} {} {}", tableId, pos.x, pos.y, pos.z);
}

LL_TYPE_INSTANCE_HOOK(
    LootBlockDropsHook,
    ll::memory::HookPriority::Normal,
    BlockType,
    &BlockType::spawnResources,
    void,
    BlockSource&                region,
    BlockPos const&             pos,
    Block const&                block,
    IRandom&                    random,
    ResourceDropsContext const& dropsContext,
    Actor const*                actorContext
) {
    using namespace gf::loot;
    auto const& id = getTypeName();
    bool        tf = id.rfind("twilightforest:", 0) == 0;
    // vanilla blocks go through the global modifiers only when they are mined with a tool (TF's
    // modifiers are tool effects) and keep no block actor data (shulker boxes, banners, beds keep
    // the game's own drops)
    bool modifiable = !tf && hasGlobalModifiers() && !static_cast<ItemStack const&>(dropsContext.mUsedItem).isNull()
                   && region.getBlockEntity(pos) == nullptr;
    if (region.getLevel().isClientSide() || (!tf && !modifiable)) return origin(region, pos, block, random, dropsContext, actorContext);
    std::string tableId = tf ? std::string("twilightforest:blocks/") + id.substr(15) : tableIdFor(id, "blocks");
    std::optional<float> radius;
    if (dropsContext.mCause == ResourceDropsCause::Explosion && dropsContext.mExplosionRadius > 0) radius = dropsContext.mExplosionRadius;
    LootContext ctx;
    ctx.region     = &region;
    ctx.origin     = Vec3{static_cast<float>(pos.x) + .5f, static_cast<float>(pos.y) + .5f, static_cast<float>(pos.z) + .5f};
    ctx.thisEntity = const_cast<Actor*>(actorContext);
    ctx.player     = actorContext && actorContext->isPlayer() ? static_cast<Player*>(const_cast<Actor*>(actorContext)) : nullptr;
    ctx.tool       = &static_cast<ItemStack const&>(dropsContext.mUsedItem);
    ctx.block      = &block;
    std::vector<ItemStack> drops;
    if (tf) {
        if (!table(tableId)) return origin(region, pos, block, random, dropsContext, actorContext); // no Java table: Bedrock's drops
        ExplosionScope scope(radius);
        drops = roll(tableId, ctx);
    } else {
        // vanilla blocks: Bedrock's drops through the global modifiers
        auto bedrock = getResourceDrops(block, random, dropsContext);
        drops        = applyGlobalModifiers(std::move(bedrock.mItems.get()), ctx, tableId);
    }
    for (auto const& s : drops) BlockType::popResource(region, pos, s, actorContext);
}

namespace gf::loot {

namespace {

class LootModule {
public:
    void install() {
        LootUnpackHook::hook();
        LootBlockDropsHook::hook();
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldReady([] { clearCache(); });
            // ModExistsCondition: GloamingForest is TF and Minecraft (docs/spec deviations)
            registerCondition("twilightforest:mod_exists", [](LootContext const&, Json const& config, RandomSource&) {
                auto mod = config.value("mod_id", std::string{});
                return mod == "twilightforest" || mod == "minecraft";
            });
        }
    }
    void uninstall() {
        LootBlockDropsHook::unhook();
        LootUnpackHook::unhook();
    }
};

} // namespace

bool hasGlobalModifiers() {
    std::lock_guard lock(registry().mutex);
    return !registry().modifiers.empty();
}

std::vector<ItemStack> applyGlobalModifiers(std::vector<ItemStack> loot, LootContext const& ctx, std::string const& tableId) {
    return applyModifiers(std::move(loot), ctx, tableId);
}

} // namespace gf::loot

GF_REGISTER_MODULE_ORDERED(gf::loot::LootModule, gf::ModuleOrder::Contracts);
