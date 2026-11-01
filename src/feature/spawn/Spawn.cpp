// Contract "spawn" (F5): the Twilight Forest's spawn engine. Per-entity placement rules
// (SpawnPlacements), per-biome tables (registered by U1, else the imported biome JSON), special
// spawns (TF's structureSpecialSpawns / landmark controlled spawns), the chunk-generation spawns
// (NaturalSpawner#spawnMobsForChunkGeneration) and the natural spawner of the dimension
// (NaturalSpawner#spawnForChunk with Java's mob caps), all behaviour compatible with 1.21.1.
#include "feature/spawn/Spawn.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/chunk/ChunkState.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/phys/AABB.h"

#include "core/jrand/Mth.h"
#include "feature/dimension/World.h"
#include "feature/features/Internal.h"
#include "feature/landmark/Landmark.h"
#include "feature/spawn/Internal.h"
#include "feature/worldgen/Internal.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::spawn {

using Json = nlohmann::json;
namespace kit = features::kit;

namespace {

struct Placement {
    PlacementRule rule = PlacementRule::OnGround;
    PredicateFn   predicate;
};

struct State {
    std::shared_mutex                              mutex;
    std::unordered_map<std::string, Placement>     placements;
    std::unordered_map<std::string, Table>         tables;
    std::vector<SpecialSpawnFn>                    special;
};
State& state() {
    static State s;
    return s;
}

/// Java's MobCategory: max per chunk, friendly, persistent, despawn distance.
struct Category {
    char const* name;
    int         max;
    bool        friendly;
    bool        persistent;
    int         despawnDistance;
};
constexpr Category kCategories[] = {
    {"monster", 70, false, false, 128},
    {"creature", 10, true, true, 128},
    {"ambient", 15, true, false, 128},
    {"axolotls", 5, true, false, 128},
    {"underground_water_creature", 5, true, false, 128},
    {"water_creature", 5, true, false, 128},
    {"water_ambient", 20, true, false, 64},
};

/// The spawn table of a biome: registered, else read from its imported JSON (spawners,
/// creature_spawn_probability).
Table tableOf(std::string const& biomeId) {
    {
        std::shared_lock lock(state().mutex);
        if (auto it = state().tables.find(biomeId); it != state().tables.end()) return it->second;
    }
    Table       t;
    auto const& biomes = features::detail::data().biomes;
    auto        it     = biomes.find(biomeId);
    if (it == biomes.end()) return t;
    t.creatureProbability = it->second.value("creature_spawn_probability", 0.1F);
    for (auto const& [category, list] : it->second.value("spawners", Json::object()).items()) {
        auto& out = t.byCategory[category];
        for (auto const& e : list)
            out.push_back({features::detail::normalizeId(e.value("type", std::string())), e.value("weight", 1), e.value("minCount", 1), e.value("maxCount", 1)});
    }
    std::unique_lock lock(state().mutex);
    state().tables.emplace(biomeId, t);
    return t;
}

/// WeightedRandomList#getRandom.
SpawnEntry const* pick(std::vector<SpawnEntry> const& list, RandomSource& random) {
    int total = 0;
    for (auto const& e : list) total += e.weight;
    if (total <= 0) return nullptr;
    int i = random.nextInt(total);
    for (auto const& e : list) {
        i -= e.weight;
        if (i < 0) return &e;
    }
    return nullptr;
}

Placement placementOf(std::string const& id) {
    std::shared_lock lock(state().mutex);
    auto             it = state().placements.find(id);
    return it == state().placements.end() ? Placement{} : it->second;
}

/// NaturalSpawner#isValidEmptySpawnBlock.
bool emptySpawnBlock(Block const& b) {
    return !kit::blocksMotion(b) && !kit::isFluid(b) && b.getTypeName() != "minecraft:powder_snow";
}

/// SpawnPlacements#isSpawnPositionOk for `rule` through a Region.
bool positionOk(PlacementRule rule, worldgen::Region& region, BlockPos const& pos) {
    switch (rule) {
    case PlacementRule::NoRestrictions:
        return true;
    case PlacementRule::InWater:
        return kit::isWater(region.getBlock(pos)) && !kit::blocksMotion(region.getBlock({pos.x, pos.y + 1, pos.z}));
    case PlacementRule::InLava: {
        auto const& n = region.getBlock(pos).getTypeName();
        return n == "minecraft:lava" || n == "minecraft:flowing_lava";
    }
    case PlacementRule::OnGround: {
        auto const& below = region.getBlock({pos.x, pos.y - 1, pos.z});
        auto const& id    = below.getTypeName();
        if (!kit::blocksMotion(below) || id == "minecraft:bedrock" || id == "minecraft:barrier") return false;
        return emptySpawnBlock(region.getBlock(pos)) && emptySpawnBlock(region.getBlock({pos.x, pos.y + 1, pos.z}));
    }
    }
    return false;
}

/// A spawn chosen on a generation thread, done on the server thread.
struct Queued {
    int         dimension;
    float       x, y, z;
    std::string entityId;
    int         chunkX, chunkZ;
};
std::mutex& queueMutex() {
    static std::mutex m;
    return m;
}
std::deque<Queued>& queue() {
    static std::deque<Queued> q;
    return q;
}

} // namespace

// ==== contract ========================================================================================

void registerPlacement(std::string const& entityId, PlacementRule rule, PredicateFn predicate) {
    std::unique_lock lock(state().mutex);
    state().placements[features::detail::normalizeId(entityId)] = {rule, std::move(predicate)};
}

Actor* spawnMob(BlockSource& region, std::string const& entityId, Vec3 const& pos, SpawnReason reason, InitFn init) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    bool natural = reason == SpawnReason::Natural || reason == SpawnReason::ChunkGeneration;
    Mob* mob     = level->getSpawner().spawnMob(region, ActorDefinitionIdentifier(entityId), nullptr, pos, natural, true, reason == SpawnReason::Spawner);
    if (!mob) {
        features::detail::warnOnce("spawn " + entityId, "cannot spawn " + entityId + " (unknown entity?)");
        return nullptr;
    }
    if (init) init(*mob);
    return mob;
}

void setBiomeSpawns(std::string const& biomeId, Table table) {
    std::unique_lock lock(state().mutex);
    state().tables[features::detail::normalizeId(biomeId)] = std::move(table);
}

void onSpecialSpawn(SpecialSpawnFn fn) {
    std::unique_lock lock(state().mutex);
    state().special.push_back(std::move(fn));
}

// ==== chunk generation spawns ===========================================================================

namespace detail {

void spawnForChunkGeneration(worldgen::Region& region, std::string const& biomeId, int chunkX, int chunkZ, RandomSource& random) {
    Table t  = tableOf(biomeId);
    auto  it = t.byCategory.find("creature");
    if (it == t.byCategory.end() || it->second.empty()) return;
    auto const& list = it->second;
    int         i    = chunkX * 16, j = chunkZ * 16;
    std::vector<Queued> out;
    while (random.nextFloat() < t.creatureProbability) {
        auto const* data = pick(list, random);
        if (!data) continue;
        int k  = data->minCount + random.nextInt(1 + data->maxCount - data->minCount);
        int l  = i + random.nextInt(16);
        int i1 = j + random.nextInt(16);
        int j1 = l, k1 = i1;
        auto placement = placementOf(data->entityId);
        for (int l1 = 0; l1 < k; ++l1) {
            bool flag = false;
            for (int i2 = 0; !flag && i2 < 4; ++i2) {
                // getTopNonCollidingPos (MOTION_BLOCKING_NO_LEAVES)
                int      top = region.height(worldgen::Heightmap::MotionBlockingNoLeaves, l, i1);
                BlockPos pos{l, top, i1};
                if (positionOk(placement.rule, region, pos)) {
                    float  f  = 0.6F; // EntityType#getWidth: the common mob width
                    double d0 = core::mth::clamp(static_cast<double>(l), static_cast<double>(i) + f, static_cast<double>(i) + 16.0 - f);
                    double d1 = core::mth::clamp(static_cast<double>(i1), static_cast<double>(j) + f, static_cast<double>(j) + 16.0 - f);
                    (void)random.nextFloat(); // the yaw (Entity#moveTo)
                    out.push_back({region.dimension(), static_cast<float>(d0), static_cast<float>(top), static_cast<float>(d1), data->entityId, chunkX, chunkZ});
                    flag = true;
                }
                l += random.nextInt(5) - random.nextInt(5);
                for (i1 += random.nextInt(5) - random.nextInt(5); l < i || l >= i + 16 || i1 < j || i1 >= j + 16;
                     i1 = k1 + random.nextInt(5) - random.nextInt(5)) {
                    l = j1 + random.nextInt(5) - random.nextInt(5);
                }
            }
        }
    }
    if (out.empty()) return;
    std::lock_guard lock(queueMutex());
    for (auto& q : out) queue().push_back(std::move(q));
}

} // namespace detail

// ==== natural spawner ===================================================================================

namespace {

std::vector<SpawnEntry> entriesOf(Json const& list) {
    std::vector<SpawnEntry> out;
    if (!list.is_array()) return out;
    for (auto const& e : list)
        out.push_back({features::detail::normalizeId(e.value("type", std::string())), e.value("weight", 1), e.value("minCount", 1), e.value("maxCount", 1)});
    return out;
}

/// TF's EntityEvents#structureSpecialSpawns / gatherPotentialSpawns over the landmark descriptors'
/// controlled spawns ({labelled_monster_spawns: {"0": [...]}, ambient_spawns, water_spawns}).
/// Monster lists use index 0 inside a piece (the piece spawn indices are the landmark generator's,
/// which F6 applies through its own onSpecialSpawn).
std::optional<std::vector<SpawnEntry>> controlledSpawns(int dimension, BlockPos const& pos, std::string const& category) {
    for (auto const& start : landmark::startsIntersecting(dimension, {pos.x >> 4, pos.z >> 4})) {
        auto const* d = landmark::descriptor(start.key);
        if (!d || !d->controlledSpawns.is_object() || d->controlledSpawns.empty()) continue;
        auto const& c = d->controlledSpawns;
        if (category != "monster") {
            if (category == "ambient") return entriesOf(c.value("ambient_spawns", Json::array()));
            if (category == "water_creature") return entriesOf(c.value("water_spawns", Json::array()));
            return std::vector<SpawnEntry>{};
        }
        if (start.conquered) return std::nullopt;
        bool inside = false;
        for (auto const& b : start.pieceBoxes.empty() ? std::vector<core::BlockBox>{start.box} : start.pieceBoxes)
            if (b.contains(pos.x, pos.y, pos.z)) inside = true;
        if (!inside) return std::nullopt;
        return entriesOf(c.value("labelled_monster_spawns", Json::object()).value("0", Json::array()));
    }
    return std::nullopt;
}

struct Spawner {
    core::jrand::LegacyRandom random{0};
    std::uint64_t             ticks = 0;

    std::unordered_map<std::string, std::string> categories; ///< entity id -> category (cache)

    /// The category of an entity id: the first table listing it.
    std::string const& categoryOf(std::string const& id, std::vector<std::string> const& biomes) {
        if (auto it = categories.find(id); it != categories.end()) return it->second;
        std::string found;
        for (auto const& b : biomes) {
            for (auto const& [cat, list] : tableOf(b).byCategory)
                for (auto const& e : list)
                    if (found.empty() && e.entityId == id) found = cat;
        }
        return categories.emplace(id, found).first->second;
    }

    std::vector<SpawnEntry> mobsAt(BlockSource& region, BlockPos const& pos, std::string const& category, std::string const& biome) {
        std::vector<SpecialSpawnFn> special;
        {
            std::shared_lock lock(state().mutex);
            special = state().special;
        }
        for (auto const& fn : special)
            if (auto list = fn(region, pos, category)) return *list;
        if (auto list = controlledSpawns(static_cast<int>(region.getDimensionId()), pos, category)) return *list;
        auto t  = tableOf(biome);
        auto it = t.byCategory.find(category);
        return it == t.byCategory.end() ? std::vector<SpawnEntry>{} : it->second;
    }

    void tick() {
        auto level = ll::service::getLevel();
        if (!level || !config::get<worldgen::WorldgenConfig>().naturalSpawning) return;
        auto dimType = world::twilight();
        if (!dimType) return;
        GameRuleId rule;
        rule.mValue = static_cast<int>(GameRules::GameRulesIndex::DoMobSpawning);
        if (!level->getGameRules().getBool(rule, true)) return;
        auto* region = game::regionOf(static_cast<int>(*dimType));
        if (!region) return;
        ++ticks;
        std::vector<Vec3> players;
        region->getDimension().forEachPlayer([&](Player& p) {
            players.push_back(p.getPosition());
            return true;
        });
        if (players.empty()) return;

        // spawnable chunks: within 8 chunks of a player (ChunkMap#anyPlayerCloseEnoughForSpawning)
        std::set<std::pair<int, int>> chunks;
        for (auto const& p : players) {
            int pcx = core::mth::floor(p.x) >> 4, pcz = core::mth::floor(p.z) >> 4;
            for (int dx = -8; dx <= 8; ++dx)
                for (int dz = -8; dz <= 8; ++dz)
                    if (auto* lc = region->getChunk(pcx + dx, pcz + dz); lc && lc->mLoadState->load() == ChunkState::Loaded)
                        chunks.emplace(pcx + dx, pcz + dz);
        }
        if (chunks.empty()) return;

        // NaturalSpawner#createState: the mobs per category in those chunks
        std::map<std::string, int> counts;
        auto                       possible = worldgen::ext::possibleBiomes();
        for (auto const& p : players) {
            AABB box{Vec3{p.x - 128, static_cast<float>(region->getMinHeight()), p.z - 128}, Vec3{p.x + 128, static_cast<float>(region->getMaxHeight()), p.z + 128}};
            for (Actor* a : region->fetchEntities(static_cast<Actor const*>(nullptr), box, false, false)) {
                if (a->isPlayer()) continue;
                auto cat = categoryOf(a->getTypeName(), possible);
                if (!cat.empty()) ++counts[cat];
            }
        }
        bool spawnPersistent = ticks % 400 == 0;
        std::vector<std::pair<int, int>> order(chunks.begin(), chunks.end());
        for (std::size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[static_cast<std::size_t>(random.nextInt(static_cast<int>(i)))]);
        for (auto const& [cx, cz] : order) {
            for (auto const& cat : kCategories) {
                if (cat.persistent && !spawnPersistent) continue;
                int cap = cat.max * static_cast<int>(chunks.size()) / 289; // MAGIC_NUMBER = 17 * 17
                if (counts[cat.name] >= cap) continue;
                counts[cat.name] += spawnCategoryForChunk(*region, cat, cx, cz, players);
            }
        }
    }

    int spawnCategoryForChunk(BlockSource& region, Category const& cat, int cx, int cz, std::vector<Vec3> const& players) {
        worldgen::detail::SourceRegionAdapter view(region, cx, cz, 1, region.getMinHeight(), region.getMaxHeight(), worldgen::seed(), static_cast<int>(region.getDimensionId()));
        int x = cx * 16 + random.nextInt(16), z = cz * 16 + random.nextInt(16);
        // getRandomPosWithin: up to the WORLD_SURFACE height + 1 (the engine's heightmap)
        int top = region.getHeightmapPos(BlockPos{x, 0, z}).y + 1;
        int y   = random.nextIntBetweenInclusive(region.getMinHeight(), top);
        if (y < region.getMinHeight() + 1) return 0;
        if (kit::blocksMotion(region.getBlock({x, y, z}))) return 0; // isRedstoneConductor
        int spawned = 0;
        for (int k = 0; k < 3; ++k) {
            int               l = x, i1 = z;
            SpawnEntry const* data = nullptr;
            std::vector<SpawnEntry> mobs;
            int               k1 = core::mth::ceil(static_cast<double>(random.nextFloat() * 4.0F));
            for (int i2 = 0; i2 < k1; ++i2) {
                l += random.nextInt(6) - random.nextInt(6);
                i1 += random.nextInt(6) - random.nextInt(6);
                BlockPos pos{l, y, i1};
                double   d0 = l + 0.5, d1 = i1 + 0.5, d2 = 1e300;
                for (auto const& p : players) d2 = std::min(d2, core::mth::square(p.x - d0) + core::mth::square(p.y - y) + core::mth::square(p.z - d1));
                if (d2 <= 576.0) continue; // isRightDistanceToPlayerAndSpawnPoint
                if (!data) {
                    mobs = mobsAt(region, pos, cat.name, view.biome(pos));
                    data = pick(mobs, random);
                    if (!data) break;
                    k1 = data->minCount + random.nextInt(1 + data->maxCount - data->minCount);
                }
                if (d2 > static_cast<double>(cat.despawnDistance) * cat.despawnDistance && !cat.persistent) continue;
                auto placement = placementOf(data->entityId);
                if (!positionOk(placement.rule, view, pos)) continue;
                if (placement.predicate && !placement.predicate(region, data->entityId, pos, SpawnReason::Natural, random)) continue;
                (void)random.nextFloat(); // the yaw
                if (spawnMob(region, data->entityId, Vec3{static_cast<float>(d0), static_cast<float>(y), static_cast<float>(d1)}, SpawnReason::Natural)) {
                    ++spawned;

                    if (spawned >= 4) return spawned; // Mob#getMaxSpawnClusterSize
                }
            }
        }
        return spawned;
    }
};

Spawner& spawner() {
    static Spawner s;
    return s;
}

/// Spawns queued chunk-generation mobs whose chunks are loaded (server thread).
void drainQueue() {
    std::deque<Queued> later;
    std::deque<Queued> now;
    {
        std::lock_guard lock(queueMutex());
        now.swap(queue());
    }
    for (auto& q : now) {
        auto* region = game::regionOf(q.dimension);
        if (!region) continue;
        auto* lc = region->getChunk(q.chunkX, q.chunkZ);
        if (!lc || lc->mLoadState->load() != ChunkState::Loaded) {
            later.push_back(std::move(q));
            continue;
        }
        spawnMob(*region, q.entityId, Vec3{q.x, q.y, q.z}, SpawnReason::ChunkGeneration);
    }
    if (later.empty()) return;
    std::lock_guard lock(queueMutex());
    while (later.size() > 4096) later.pop_front(); // chunks that never load again
    for (auto& q : later) queue().push_back(std::move(q));
}

} // namespace

class SpawnModule {
public:
    void install() {
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            drainQueue();
            spawner().tick();
        });
        static bool once = false;
        if (!once) {
            once = true;
            game::db::onWorldClosed([] {
                std::lock_guard lock(queueMutex());
                queue().clear();
                spawner().categories.clear();
                std::unique_lock tables(state().mutex);
                state().tables.clear();
            });
        }
    }
    void uninstall() { ll::event::EventBus::getInstance().removeListener(mTick); }

private:
    ll::event::ListenerPtr mTick;
};

} // namespace gf::spawn

GF_REGISTER_MODULE_ORDERED(gf::spawn::SpawnModule, gf::ModuleOrder::Contracts);
