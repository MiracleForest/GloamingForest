// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/init/TFLoot.java
//   src/main/java/gloamingforest/loot/conditions/ModExistsCondition.java
//   src/main/java/gloamingforest/loot/TFLootTables.java (the container / entity drops use)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Contract "loot" (F10, rule R5): tables, rolls, the global modifiers, containers and drops.
//
// - Tables are the Java data pack files <mod>/data/<ns>/loot_table/<path>.json (F1 imports upstream's
//   634; units' own data lands there too), parsed once per world; LootEval.cpp interprets them.
// - roll(): a seeded LootContext uses Java's LegacyRandomSource (RandomSource.create(seed)) like
//   LootTable#fill; seed 0 is random. Global modifiers (NeoForge IGlobalLootModifier) run after
//   every roll with the table id.
// - placeContainer(): vanilla containers (chest, barrel, ...) get the table as the native loot
//   field ("gf_loot:<table>#<seed>"); the game unpacks it when the container is first opened,
//   emptied by a hopper or broken, and one RandomizableBlockActorContainerBase#unPackLootTable
//   hook rolls the Java table into it with LootTable#fill's slot spreading. Custom container
//   blocks get it through gf::inventory's setLootTable.
// - Entity drops: a mob that dies rolls <ns>:entities/<path> when that Java table exists (upstream's
//   entities; vanilla mobs keep Bedrock's drops) and drops it in the death tick (Java's
//   LivingEntity#dropFromLootTable, the dead mob in the context); the onEntityDrops handlers then
//   see those stacks plus every item the game itself spawned for the death (Bedrock's loot of
//   vanilla mobs, equipment; Java's captured LivingDropsEvent list) and may change them: changed
//   drops replace the item entities. Bedrock spawns a mob's own loot right after `die` returns, so
//   only this merge waits for the next tick.
// - Block drops: blocks of the gloamingforest namespace drop from <ns>:blocks/<path> (survives
//   explosion, silk touch, fortune, shears ...), and nothing without that table (upstream's
//   noLootTable() blocks); other blocks keep Bedrock's drops, run through the global modifiers
//   (upstream's fiery pickaxe smelting, giant pickaxe grouping) when any is registered. Bedrock's silk
//   touch break pops the block itself without BlockType::spawnResources: a Block::playerDestroy
//   hook suppresses those items for upstream blocks and rolls the Java table instead.

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
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
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
#include "mc/world/level/block/actor/RandomizableBlockActorContainer.h"
#include "mc/world/level/block/actor/RandomizableBlockActorContainerBase.h"
#include "mc/world/level/block/actor/RandomizableBlockActorFillingContainer.h"
#include "mc/world/inventory/FillingContainer.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/level/Tick.h"

#include "feature/boss/Boss.h"
#include "feature/common/Interactions.h"
#include "feature/common/BlockData.h"
#include "feature/ext/combat/Handles.h"
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
    std::vector<std::uint64_t>                    entityDropIds; ///< parallel to entityDrops (ext handles; 0: none)
    std::uint64_t                                 nextDropId = 0;
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

namespace {
struct Erased {
    Json          doc;
    std::uint64_t tick = 0;
};
std::map<core::GlobalPos, Erased>& erasedDocs() {
    static std::map<core::GlobalPos, Erased> m;
    return m;
}
std::uint64_t currentTick() {
    auto level = ll::service::getLevel();
    return level ? static_cast<std::uint64_t>(level->getCurrentTick().tickID) : 0;
}
} // namespace

Json const* recentlyErased(core::GlobalPos const& pos) {
    auto it = erasedDocs().find(pos);
    if (it == erasedDocs().end() || it->second.tick != currentTick()) return nullptr;
    return &it->second.doc;
}
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

namespace {
/// The `bosses` section's adjuster, read once per config load (nullopt: not read yet).
std::optional<bool>& adjusterCache() {
    static std::optional<bool> v;
    return v;
}
bool readAdjuster();
} // namespace

void invalidateAdjuster() {
    std::lock_guard lock(registry().mutex);
    adjusterCache().reset();
}

bool adjustsLootRolls() {
    std::lock_guard lock(registry().mutex);
    if (registry().testAdjusts) return *registry().testAdjusts;
    if (!adjusterCache()) adjusterCache() = readAdjuster();
    return *adjusterCache();
}

namespace {
bool readAdjuster() {
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
    return false; // upstream's default NONE
}
} // namespace

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
    // the caches are node-based maps: nested tables loading during the roll keep `t` valid
    return applyModifiers(rollRaw(*t, tableId, ctx, random), ctx, tableId);
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

void onEntityDrops(EntityDropsFn fn) { (void)ext::addEntityDrops(std::move(fn)); }

namespace ext {
DropsHandle addEntityDrops(EntityDropsFn fn) {
    if (!fn) return 0;
    std::lock_guard lock(registry().mutex);
    auto            id = ++registry().nextDropId;
    registry().entityDrops.push_back(std::move(fn));
    registry().entityDropIds.push_back(id);
    return id;
}
bool removeEntityDrops(DropsHandle handle) {
    if (handle == 0) return false;
    EntityDropsFn gone; // destroyed outside the lock
    {
        std::lock_guard lock(registry().mutex);
        auto&           ids = registry().entityDropIds;
        auto            it  = std::find(ids.begin(), ids.end(), handle);
        if (it == ids.end()) return false;
        auto i = static_cast<std::size_t>(it - ids.begin());
        gone   = std::move(registry().entityDrops[i]);
        registry().entityDrops.erase(registry().entityDrops.begin() + static_cast<std::ptrdiff_t>(i));
        ids.erase(it);
    }
    return true;
}
} // namespace ext

// ---- containers ------------------------------------------------------------------------------------

namespace {

/// The randomizable vanilla containers (hoppers have no loot table field).
bool isVanillaLootContainer(BlockActor const& actor) {
    switch (actor.getType()) {
    case BlockActorType::Chest:
    case BlockActorType::BarrelBlock:
    case BlockActorType::ShulkerBox:
    case BlockActorType::Dispenser:
    case BlockActorType::Dropper:
    case BlockActorType::DecoratedPot: return true; // DecoratedPotBlockEntity is a RandomizableContainer (upstream's camp pots)
    default: return false;
    }
}

/// The slots of a randomizable vanilla container (chests, barrels and shulker boxes are
/// FillingContainers, dispensers and droppers plain Containers).
Container* vanillaContainerOf(BlockActor& actor) {
    switch (actor.getType()) {
    case BlockActorType::Chest:
    case BlockActorType::BarrelBlock:
    case BlockActorType::ShulkerBox:
        return static_cast<Container*>(static_cast<FillingContainer*>(static_cast<RandomizableBlockActorFillingContainer*>(&actor)));
    case BlockActorType::Dispenser:
    case BlockActorType::Dropper:
    case BlockActorType::DecoratedPot: return static_cast<Container*>(static_cast<RandomizableBlockActorContainer*>(&actor));
    default: return nullptr;
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
        if (auto const* b = game::blockWithStates(blockId, {{"gloamingforest:facing", names[facing]}})) return b;
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
    // a container of this block already standing there keeps its states (its facing, a double
    // chest's half): only the loot table is set (a template's loot marker on its own chest)
    if (game::blockIdAt(region, pos) != blockId) {
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

float luckOf(Actor* actor) {
    if (!actor || !actor->isPlayer()) return 0.f;
    auto                     ref      = actor->getAttribute(SharedAttributes::LUCK());
    AttributeInstance const* instance = ref.mPtr;
    return instance ? static_cast<float>(instance->mCurrentValue) : 0.f;
}

namespace {
/// Rolls the marker table of a vanilla container into `container` (RandomizableContainer#unpackLootTable:
/// THIS_ENTITY is the opener and the luck the opener's; there is no killer player).
void fillVanilla(RandomizableBlockActorContainerBase& actor, Container& container, BlockSource* region, Actor* opener) {
    auto [tableId, seed] = parseMarker(actor.mLootTable.get());
    actor.eraseLootTable(); // once, whatever happens (Java clears the table before filling)
    auto        pos = actor.getPosition();
    LootContext ctx;
    ctx.region     = region;
    ctx.origin     = Vec3{static_cast<float>(pos.x) + .5f, static_cast<float>(pos.y) + .5f, static_cast<float>(pos.z) + .5f};
    ctx.thisEntity = opener;
    ctx.luck       = luckOf(opener);
    ctx.seed       = seed;
    fill(
        tableId,
        ctx,
        container.getContainerSize(),
        [&](int slot) -> ItemStack const& { return container.getItem(slot); },
        [&](int slot, ItemStack const& s) { container.setItem(slot, s); }
    );
    log().info("loot: unpacked {} at {} {} {}", tableId, pos.x, pos.y, pos.z);
}
} // namespace

bool unpackVanilla(BlockSource& region, BlockPos const& pos, Actor* opener) {
    if (!pendingVanilla(region, pos)) return false;
    auto* actor     = region.getBlockEntity(pos);
    auto* container = actor ? vanillaContainerOf(*actor) : nullptr;
    if (!container) return false;
    fillVanilla(static_cast<RandomizableBlockActorContainerBase&>(*actor), *container, &region, opener);
    return true;
}

// ---- entity drops ----------------------------------------------------------------------------------

namespace {
/// The item entities the game spawns for a dying mob, as Java's Entity#captureDrops collects them
/// for LivingDropsEvent: everything spawned during its `die` (DropCapture), then, until the
/// next tick, every item entity spawned with the dead mob as the spawner (Bedrock spawns a mob's
/// loot right after `die` returns).
thread_local std::vector<std::int64_t>* tCaptured = nullptr;

std::unordered_map<std::int64_t, std::vector<std::int64_t>>& pendingDeaths() {
    static std::unordered_map<std::int64_t, std::vector<std::int64_t>> m;
    return m;
}

bool sameDrops(std::vector<ItemStack> const& a, std::vector<ItemStack> const& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!game::items::sameItemSameData(a[i], b[i]) || game::items::countOf(a[i]) != game::items::countOf(b[i])) return false;
    return true;
}

/// What the deferred drop pass needs of a death (ids, no pointers across ticks: R16).
struct MobDeath {
    std::int64_t dead = 0, attacker = 0, direct = 0, killer = 0;
    std::string  type, damageType;
    Vec3         origin{};
    int          dim = 0;
};

Actor* fetchActor(Level& level, std::int64_t raw, bool removedToo) {
    if (!raw) return nullptr;
    ActorUniqueID id;
    id.rawID = raw;
    return level.fetchEntity(id, removedToo);
}

/// The loot context of a death: the dead mob, its killer (Java's LootContextParams of
/// LivingEntity#dropFromLootTable).
LootContext deathContext(Level& level, BlockSource* region, MobDeath const& d, Actor* dead) {
    LootContext ctx;
    ctx.region     = region;
    ctx.origin     = d.origin;
    ctx.thisEntity = dead;
    ctx.attacker   = fetchActor(level, d.attacker, false);
    ctx.direct     = fetchActor(level, d.direct, false);
    ctx.damageType = d.damageType;
    if (d.killer) {
        ActorUniqueID id;
        id.rawID   = d.killer;
        ctx.player = level.getPlayer(id);
        ctx.luck   = luckOf(ctx.player);
    }
    return ctx;
}

/// The merge pass of one death, a tick after it (Bedrock spawns a mob's own loot right after
/// `die` returns): the onEntityDrops handlers see every item entity of the death (the Java table's,
/// spawned at death, and the game's own) and may change them: changed drops replace them.
void finishMobDeath(MobDeath const& d) {
    auto items = std::move(pendingDeaths()[d.dead]);
    pendingDeaths().erase(d.dead);
    auto level  = ll::service::getLevel();
    auto region = game::regionOf(d.dim);
    if (!level || !region) return;
    std::vector<EntityDropsFn> handlers;
    {
        std::lock_guard lock(registry().mutex);
        handlers = registry().entityDrops;
    }
    Actor* dead = fetchActor(*level, d.dead, true);
    if (!dead || handlers.empty()) return;
    auto                    ctx = deathContext(*level, region, d, dead);
    std::vector<ItemActor*> spawned;
    for (auto raw : items)
        if (auto* a = fetchActor(*level, raw, false); a && a->isType(ActorType::ItemEntity)) spawned.push_back(static_cast<ItemActor*>(a));
    std::vector<ItemStack> drops;
    for (auto* item : spawned) drops.push_back(item->item());
    auto const before = drops;
    for (auto const& fn : handlers) {
        try {
            fn(*dead, drops, ctx);
        } catch (std::exception const& ex) {
            log().error("loot: entity drops handler failed: {}", ex.what());
        }
    }
    std::erase_if(drops, [](ItemStack const& s) { return game::items::isEmpty(s); });
    if (sameDrops(drops, before)) return; // the drops stay as they fell
    for (auto* item : spawned) item->remove();
    for (auto const& s : drops) spawnAt(*region, d.origin, s);
    log().info("loot: drop handlers changed the drops of {} ({} -> {} stack(s))", d.type, before.size(), drops.size());
}
} // namespace

DropCapture::DropCapture() : mPrevious(tCaptured) { tCaptured = &mIds; }
DropCapture::~DropCapture() { tCaptured = mPrevious; }

void captureSpawnedItem(Actor const& item, Actor const* spawner) {
    auto id = const_cast<Actor&>(item).getOrCreateUniqueID().rawID;
    if (tCaptured) {
        tCaptured->push_back(id);
        return;
    }
    if (!spawner || pendingDeaths().empty()) return;
    if (auto it = pendingDeaths().find(const_cast<Actor*>(spawner)->getOrCreateUniqueID().rawID); it != pendingDeaths().end()) it->second.push_back(id);
}

void onMobDied(Actor& dead, combat::DamageInfo const& damage, std::int64_t killerId, std::vector<std::int64_t> const& gameDrops) {
    if (dead.isClientSide()) return;
    bool anyHandler;
    {
        std::lock_guard lock(registry().mutex);
        anyHandler = !registry().entityDrops.empty();
    }
    auto const& type    = dead.getTypeName();
    auto        tableId = tableIdFor(type, "entities");
    // upstream's entities drop from their Java table (doMobLoot off: none, Java's dropAllDeathLoot)
    bool own = type.rfind("minecraft:", 0) != 0 && table(tableId) != nullptr && gameRule(GameRules::GameRulesIndex::DoMobLoot, true);
    if (!own && !anyHandler) return;
    auto level  = ll::service::getLevel();
    auto region = game::regionOf(static_cast<int>(dead.getDimensionId()));
    if (!level || !region) return;
    MobDeath d;
    d.dead       = dead.getOrCreateUniqueID().rawID;
    d.attacker   = damage.attacker ? damage.attacker->getOrCreateUniqueID().rawID : 0;
    d.direct     = damage.direct ? damage.direct->getOrCreateUniqueID().rawID : 0;
    d.killer     = killerId;
    d.type       = type;
    d.damageType = damage.type;
    d.origin     = dead.getPosition();
    d.dim        = static_cast<int>(dead.getDimensionId());
    // LivingEntity#dropFromLootTable: the Java table rolls and drops in the death tick, with the
    // dead mob still in the context
    std::vector<std::int64_t> ids;
    if (own) {
        auto ctx   = deathContext(*level, region, d, &dead);
        auto drops = roll(tableId, ctx);
        for (auto const& s : drops) {
            if (game::items::isEmpty(s)) continue;
            if (auto* item = region->getLevel().getSpawner().spawnItem(*region, s, nullptr, d.origin, 0)) ids.push_back(item->getOrCreateUniqueID().rawID);
        }
        log().info("loot: {} dropped {} stack(s) from {}", type, drops.size(), tableId);
    }
    if (!anyHandler) return;
    // the merge with the game's own drops waits for them (the next tick); the game's later spawns
    // for this mob join the list (captureSpawnedItem)
    ids.insert(ids.end(), gameDrops.begin(), gameDrops.end());
    pendingDeaths()[d.dead] = std::move(ids);
    interactions::nextTick([d] { finishMobDeath(d); });
}

std::size_t pendingDeathCount() { return pendingDeaths().size(); }

} // namespace gf::loot

// ---- hooks ---------------------------------------------------------------------------------------

namespace gf::loot {
namespace {

/// An upstream block broken by a player with a silk touch tool (Block::playerDestroy): Bedrock's silk touch
/// path pops the block itself without spawnResources, so its items are suppressed and the Java
/// table rolls instead.
struct SilkBreak {
    int  inside     = 0;     // inside the block-drop hook (its items pass)
    bool reached    = false; // the block-drop hook ran for this break
    int  suppressed = 0;     // items of the game's silk touch pop held back
};
thread_local SilkBreak* tSilkBreak = nullptr;

/// Rolls an upstream block's Java table and pops the stacks; a block without a table drops nothing.
void dropTfBlock(BlockSource& region, BlockPos const& pos, Block const& block, std::string const& tableId, ItemStack const* tool,
                 Actor const* actor, std::optional<float> radius) {
    if (!table(tableId)) return; // upstream's noLootTable(): nothing (never Bedrock's own drops)
    LootContext ctx;
    ctx.region     = &region;
    ctx.origin     = Vec3{static_cast<float>(pos.x) + .5f, static_cast<float>(pos.y) + .5f, static_cast<float>(pos.z) + .5f};
    ctx.thisEntity = const_cast<Actor*>(actor);
    ctx.player     = actor && actor->isPlayer() ? static_cast<Player*>(const_cast<Actor*>(actor)) : nullptr;
    ctx.tool       = tool;
    ctx.block      = &block;
    std::vector<ItemStack> drops;
    {
        ExplosionScope scope(radius);
        drops = roll(tableId, ctx);
    }
    for (auto const& s : drops) BlockType::popResource(region, pos, s, actor);
}

} // namespace
} // namespace gf::loot


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
    if (mLootTable.get().rfind(gf::loot::kMarker, 0) != 0) return origin(level, container, dimensionId, actor);
    (void)level;
    gf::loot::fillVanilla(*this, container, gf::game::regionOf(static_cast<int>(dimensionId)), actor);
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
    bool        tf = id.rfind("gloamingforest:", 0) == 0;
    // vanilla blocks go through the global modifiers only when they are mined with a tool (upstream's
    // modifiers are tool effects) and keep no block actor data (shulker boxes, banners, beds keep
    // the game's own drops)
    bool modifiable = !tf && hasGlobalModifiers() && !static_cast<ItemStack const&>(dropsContext.mUsedItem).isNull()
                   && region.getBlockEntity(pos) == nullptr;
    if (region.getLevel().isClientSide() || (!tf && !modifiable)) return origin(region, pos, block, random, dropsContext, actorContext);
    if (tSilkBreak) {
        tSilkBreak->reached = true;
        ++tSilkBreak->inside;
    }
    struct Leave {
        ~Leave() {
            if (tSilkBreak) --tSilkBreak->inside;
        }
    } leave;
    std::optional<float> radius;
    if (dropsContext.mCause == ResourceDropsCause::Explosion && dropsContext.mExplosionRadius > 0) radius = dropsContext.mExplosionRadius;
    auto const* tool = &static_cast<ItemStack const&>(dropsContext.mUsedItem);
    if (tf) return dropTfBlock(region, pos, block, std::string("gloamingforest:blocks/") + id.substr(15), tool, actorContext, radius);
    // vanilla blocks: Bedrock's drops through the global modifiers
    LootContext ctx;
    ctx.region     = &region;
    ctx.origin     = Vec3{static_cast<float>(pos.x) + .5f, static_cast<float>(pos.y) + .5f, static_cast<float>(pos.z) + .5f};
    ctx.thisEntity = const_cast<Actor*>(actorContext);
    ctx.player     = actorContext && actorContext->isPlayer() ? static_cast<Player*>(const_cast<Actor*>(actorContext)) : nullptr;
    ctx.tool       = tool;
    ctx.block      = &block;
    auto bedrock = getResourceDrops(block, random, dropsContext);
    auto drops   = applyGlobalModifiers(std::move(bedrock.mItems.get()), ctx, tableIdFor(id, "blocks"));
    for (auto const& s : drops) BlockType::popResource(region, pos, s, actorContext);
}

// A player's break of an upstream block with a silk touch tool: Bedrock pops the block itself (no
// spawnResources); those items are suppressed and the Java table rolls (upstream's thorns, experiment 115
// and stronghold shield drop nothing, others what their table gives under silk touch).
LL_TYPE_INSTANCE_HOOK(
    LootPlayerDestroyHook,
    ll::memory::HookPriority::Normal,
    Block,
    &Block::playerDestroy,
    void,
    Player&         player,
    BlockPos const& pos
) {
    using namespace gf::loot;
    auto const& id = getTypeName();
    if (player.isClientSide() || id.rfind("gloamingforest:", 0) != 0) return origin(player, pos);
    ItemStack const tool = player.getCarriedItem(); // a copy: the break may wear the tool out
    if (tool.isNull() || EnchantUtils::getEnchantLevel(Enchant::Type::SilkTouch, tool) <= 0) return origin(player, pos);
    SilkBreak  silk;
    SilkBreak* previous = tSilkBreak;
    tSilkBreak          = &silk;
    struct Restore {
        SilkBreak* previous;
        ~Restore() { tSilkBreak = previous; }
    } restore{previous};
    origin(player, pos);
    tSilkBreak = previous;
    if (silk.reached || silk.suppressed == 0) return; // the table rolled, or the game popped nothing (creative)
    dropTfBlock(player.getDimensionBlockSource(), pos, *this, std::string("gloamingforest:blocks/") + id.substr(15), &tool, &player, std::nullopt);
}

// Item entities spawned while a mob dies (gf::loot::DropCapture, the death pipeline): the game's
// own death drops for the onEntityDrops handlers.
LL_TYPE_INSTANCE_HOOK(
    LootSpawnItemHook,
    ll::memory::HookPriority::Normal,
    Spawner,
    &Spawner::$spawnItem,
    ItemActor*,
    BlockSource&     region,
    ItemStack const& inst,
    Actor*           spawner,
    Vec3 const&      pos,
    int              throwTime
) {
    // Bedrock's silk touch pop of an upstream block (LootPlayerDestroyHook): the Java table rolls instead
    if (gf::loot::tSilkBreak && gf::loot::tSilkBreak->inside == 0) {
        ++gf::loot::tSilkBreak->suppressed;
        return nullptr;
    }
    auto* item = origin(region, inst, spawner, pos, throwTime);
    if (item) gf::loot::captureSpawnedItem(*item, spawner);
    return item;
}

namespace gf::loot {

namespace {

/// The randomizable vanilla container blocks (placeContainer's native loot field).
std::vector<std::string> const& vanillaContainerBlocks() {
    static std::vector<std::string> const v = [] {
        std::vector<std::string> out{"minecraft:chest",   "minecraft:trapped_chest", "minecraft:barrel",
                                     "minecraft:dispenser", "minecraft:dropper",     "minecraft:undyed_shulker_box",
                                     "minecraft:decorated_pot"}; // DecoratedPotBlockEntity#getTheItem unpacks on use
        for (char const* c : {"white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray", "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black"})
            out.push_back(std::string("minecraft:") + c + "_shulker_box");
        return out;
    }();
    return v;
}

class LootModule {
public:
    void install() {
        LootUnpackHook::hook();
        LootBlockDropsHook::hook();
        LootPlayerDestroyHook::hook();
        LootSpawnItemHook::hook();
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldReady([] { clearCache(); });
            game::db::onWorldClosed([] {
                erasedDocs().clear();
                pendingDeaths().clear(); // the client loads many worlds in one process
            });
            // a block's data as it goes: copy_components reads it when the drops roll
            blockdata::onErase([](core::GlobalPos const& pos, Json const& doc, std::string const& previous) {
                if (previous.empty()) return;
                auto now = currentTick();
                std::erase_if(erasedDocs(), [now](auto const& e) { return e.second.tick != now; });
                erasedDocs()[pos] = Erased{doc, now};
            });
            config::onReload([] { invalidateAdjuster(); });
            // The game's container screens do not go through unPackLootTable: a vanilla container
            // placed with a Java table rolls it when a player opens it (a right-click that opens:
            // the first event, not sneaking with an item, which places instead) or breaks it,
            // before vanilla.
            namespace ia = interactions;
            for (auto const& block : vanillaContainerBlocks()) {
                ia::onUseOn({.block = block}, [](ia::UseOnContext const& c) {
                    if (!c.firstEvent || (c.sneaking && !c.itemId.empty())) return ia::Result::Pass;
                    if (auto* region = game::regionOf(c.pos.dim)) unpackVanilla(*region, game::blockPos(c.pos), &c.player);
                    return ia::Result::Pass;
                }, ia::priority::High);
                ia::onDestroy({.block = block}, [](ia::DestroyContext const& c) {
                    if (auto* region = game::regionOf(c.pos.dim)) unpackVanilla(*region, game::blockPos(c.pos), &c.player);
                    return ia::Result::Pass;
                }, ia::priority::High);
            }
            // ModExistsCondition: GloamingForest is upstream and Minecraft (docs/spec deviations)
            registerCondition("gloamingforest:mod_exists", [](LootContext const&, Json const& config, RandomSource&) {
                auto mod = config.value("mod_id", std::string{});
                return mod == "gloamingforest" || mod == "minecraft";
            });
        }
    }
    void uninstall() {
        LootSpawnItemHook::unhook();
        LootPlayerDestroyHook::unhook();
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
