// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// Block entity work of structure placement (loot containers, spawners, signs, entities) and the
// BlockSource-backed region. During world generation the region is the generator's window
// without a BlockSource: the work is queued with its dimension and position, persisted in the
// world DB (gf:structure:pending) and done on the server thread once the chunk is loaded.
#include "feature/structure/Region.h"

#include <cmath>
#include <deque>
#include <mutex>
#include <unordered_set>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/level/BaseMobSpawner.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/MobSpawnerBlockActor.h"
#include "mc/world/level/block/actor/RandomizableBlockActorContainerBase.h"
#include "mc/world/level/block/actor/SignBlockActor.h"
#include "mc/world/level/block/actor/SignTextSide.h"

#include "feature/inventory/Inventory.h"
#include "feature/loot/Loot.h"
#include "feature/spawn/Spawn.h"
#include "core/Keys.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::structure {

// ---- BlockSourceRegion -----------------------------------------------------------------------------

namespace {
std::mutex                         gAdaptersMutex;
std::unordered_set<Region const*>  gAdapters;
} // namespace

BlockSourceRegion::BlockSourceRegion(BlockSource& source, BlockPos const& center) : mSource(source), mCenter(center) {
    std::lock_guard lock(gAdaptersMutex);
    gAdapters.insert(this);
}
BlockSourceRegion::~BlockSourceRegion() {
    std::lock_guard lock(gAdaptersMutex);
    gAdapters.erase(this);
}

int  BlockSourceRegion::centerChunkX() const { return core::chunkOf(mCenter.x); }
int  BlockSourceRegion::centerChunkZ() const { return core::chunkOf(mCenter.z); }
int  BlockSourceRegion::minY() const { return mSource.getMinHeight(); }
int  BlockSourceRegion::maxY() const { return mSource.getMaxHeight(); }
bool BlockSourceRegion::isInside(int x, int z) const { return mSource.hasChunksAt(BlockPos{x, mSource.getMinHeight(), z}, 0, false); }

Block const& BlockSourceRegion::getBlock(BlockPos const& pos) const {
    if (pos.y < minY() || pos.y >= maxY() || !isInside(pos.x, pos.z)) return blocks::air();
    return mSource.getBlock(pos);
}

bool BlockSourceRegion::setBlock(BlockPos const& pos, Block const& block, int flags) {
    if (pos.y < minY() || pos.y >= maxY() || !isInside(pos.x, pos.z)) return false;
    BlockChangeContext ctx{};
    // Java's flags: 1 neighbours, 2 clients; Bedrock's low bits mean the same
    bool ok = mSource.setBlock(pos, block, flags & 3, nullptr, ctx);
    // a block placed over water loses the water (Java replaces the fluid state)
    if (ok && mSource.getExtraBlock(pos).getTypeName() != "minecraft:air" && !block.isAir() && !blocks::isLiquid(block))
        mSource.setExtraBlock(pos, blocks::air(), flags & 3);
    return ok;
}

int BlockSourceRegion::height(worldgen::Heightmap type, int x, int z) const {
    if (!isInside(x, z)) return minY();
    for (int y = maxY() - 1; y >= minY(); y--) {
        Block const& b = mSource.getBlock(BlockPos{x, y, z});
        if (b.isAir()) continue;
        bool liquid = blocks::isLiquid(b);
        switch (type) {
        case worldgen::Heightmap::WorldSurfaceWg:
        case worldgen::Heightmap::WorldSurface:
            return y + 1;
        case worldgen::Heightmap::OceanFloorWg:
        case worldgen::Heightmap::OceanFloor:
            if (!liquid && !blocks::canBeReplaced(b)) return y + 1;
            break;
        case worldgen::Heightmap::MotionBlocking:
            if (liquid || !blocks::canBeReplaced(b)) return y + 1;
            break;
        case worldgen::Heightmap::MotionBlockingNoLeaves:
            if ((liquid || !blocks::canBeReplaced(b)) && !blocks::inTag(b, "minecraft:leaves")) return y + 1;
            break;
        }
    }
    return minY();
}

std::string BlockSourceRegion::biome(BlockPos const& pos) const {
    if (!isInside(pos.x, pos.z)) return {};
    return mSource.getBiome(pos).mHash->getString();
}

std::int64_t BlockSourceRegion::seed() const { return static_cast<std::int64_t>(mSource.getLevel().getSeed()); }
int          BlockSourceRegion::dimension() const { return static_cast<int>(mSource.getDimensionId()); }

namespace blockentities {

BlockSource* sourceOf(Region& region) {
    std::lock_guard lock(gAdaptersMutex);
    if (!gAdapters.contains(&region)) return nullptr;
    return &static_cast<BlockSourceRegion&>(region).source();
}

namespace {

constexpr char kPendingKey[] = "gf:structure:pending";

struct Job {
    int      dim = 0;
    BlockPos pos{0, 0, 0};
    Json     action; ///< {"kind": "container" | "loot" | "spawner" | "sign" | "entity", ...}
};
std::mutex       gJobsMutex;
std::deque<Job>  gJobs;
bool             gDirty = false;

int facingIndex(std::optional<Orientation> facing) {
    // FacingID: 2 north, 3 south, 4 west, 5 east
    if (!facing) return 2;
    switch (*facing) {
    case Orientation::North:
        return 2;
    case Orientation::South:
        return 3;
    case Orientation::West:
        return 4;
    default:
        return 5;
    }
}

std::string paintingMotive(std::string const& id) {
    // Bedrock's motive names are the capitalised Java ids ("kebab" -> "Kebab", "skull_and_roses" -> "SkullAndRoses")
    std::string out;
    bool        up = true;
    for (char c : id) {
        if (c == '_') {
            up = true;
            continue;
        }
        out += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        up = false;
    }
    return out;
}

bool run(BlockSource& source, BlockPos const& pos, Json const& action) {
    auto kind = action.value("kind", std::string{});
    if (kind == "container") {
        auto id    = action.value("block", std::string("minecraft:chest"));
        auto table = action.value("table", std::string{});
        auto seed  = action.value("seed", std::int64_t{0});
        auto bid   = blocks::bedrockId(id);
        if (loot::placeContainer(source, pos, bid.empty() ? id : bid, action.value("facing", 2), table, seed)) return true;
        // the loot contract is not there (yet): keep the chest itself
        Json props = Json::object();
        if (action.contains("direction")) props["facing"] = action["direction"];
        if (auto const* b = blocks::fromJava(id, props)) game::setBlock(source, pos, *b);
        warnOnce("loot-container", "structure: gf::loot::placeContainer refused a container; chests stay empty until the loot contract is implemented");
        return true;
    }
    if (kind == "loot") {
        auto table = action.value("table", std::string{});
        auto seed  = action.value("seed", std::int64_t{0});
        if (auto h = inventory::find(source, pos); h && h->setLootTable) {
            h->setLootTable(game::globalPos(source, pos), table, seed);
            return true;
        }
        auto const& block = source.getBlock(pos);
        if (loot::placeContainer(source, pos, block.getTypeName(), 2, table, seed)) return true;
        warnOnce("loot-table:" + block.getTypeName(), "structure: no container accepts loot table " + table + " at a " + block.getTypeName());
        return true;
    }
    if (kind == "spawner") {
        auto entityId = action.value("entity", std::string{});
        auto const& b = source.getBlock(pos);
        if (b.getTypeName() != "minecraft:mob_spawner") {
            if (auto const* sp = game::blockWith("minecraft:mob_spawner")) game::setBlock(source, pos, *sp);
        }
        auto* actor = source.getBlockEntity(pos);
        if (!actor || actor->getType() != BlockActorType::MobSpawner) return false; // the actor is created a tick later
        auto& spawnerActor = static_cast<MobSpawnerBlockActor&>(*actor);
        if (!entityId.empty()) spawnerActor.setMob(source, ActorDefinitionIdentifier(entityId), nullptr);
        if (BaseMobSpawner* spawner = spawnerActor.mSpawner.get()) {
            auto const& data = action.value("data", Json::object());
            if (data.contains("Delay")) spawner->mSpawnDelay = data["Delay"].get<int>();
            if (data.contains("MinSpawnDelay")) spawner->mMinSpawnDelay = data["MinSpawnDelay"].get<int>();
            if (data.contains("MaxSpawnDelay")) spawner->mMaxSpawnDelay = data["MaxSpawnDelay"].get<int>();
            if (data.contains("SpawnCount")) spawner->mSpawnCount = data["SpawnCount"].get<int>();
            if (data.contains("MaxNearbyEntities")) spawner->mMaxNearbyEntities = data["MaxNearbyEntities"].get<int>();
            if (data.contains("RequiredPlayerRange")) spawner->mRequiredPlayerRange = data["RequiredPlayerRange"].get<int>();
            if (data.contains("SpawnRange")) spawner->mSpawnRange = data["SpawnRange"].get<int>();
        }
        return true;
    }
    if (kind == "sign") {
        auto* actor = source.getBlockEntity(pos);
        if (!actor || (actor->getType() != BlockActorType::Sign && actor->getType() != BlockActorType::HangingSign)) return false;
        std::string text;
        for (auto const& l : action.value("lines", Json::array())) text += (text.empty() ? "" : "\n") + l.get<std::string>();
        static_cast<SignBlockActor&>(*actor).setMessageForServerScripingOnly(SignTextSide::Front, text, std::string{});
        return true;
    }
    if (kind == "entity") {
        auto   id   = action.value("id", std::string{});
        double x    = action.value("x", 0.0), y = action.value("y", 0.0), z = action.value("z", 0.0);
        Vec3   at{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
        if (id == "minecraft:painting") {
            // vanilla painting: spawned and hung by the game's own hanging-entity rules
            warnOnce("painting-spawn", "structure: paintings are hung by the entity contract (F8); motive " +
                                           paintingMotive(action.value("data", Json::object()).value("motive", std::string("kebab"))));
            return true;
        }
        if (spawn::spawnMob(source, id, at, spawn::SpawnReason::Structure)) return true;
        auto level = ll::service::getLevel();
        if (!level) return false;
        auto* mob = level->getSpawner().spawnMob(source, ActorDefinitionIdentifier(id), nullptr, at, false, false, false);
        if (!mob) warnOnce("entity:" + id, "structure: could not spawn template entity " + id);
        return true;
    }
    return true;
}

void queue(Region& region, BlockPos const& pos, Json action) {
    // a scratch region (selftests' in-memory regions report dimension -1) keeps no block entities
    if (region.dimension() < 0) return;
    if (auto* source = sourceOf(region)) {
        if (run(*source, pos, action)) return;
        // the block actor is not there yet: retry on the next tick
        std::lock_guard lock(gJobsMutex);
        gJobs.push_back(Job{static_cast<int>(source->getDimensionId()), pos, std::move(action)});
        gDirty = true;
        return;
    }
    std::lock_guard lock(gJobsMutex);
    gJobs.push_back(Job{region.dimension(), pos, std::move(action)});
    gDirty = true;
}

} // namespace

void container(Region& region, BlockPos const& pos, std::string const& blockId, std::optional<Orientation> facing, std::string const& tableId,
               std::int64_t seed) {
    // the block now, the loot table with it (placeContainer places the block too)
    Json props = Json::object();
    if (facing) props["facing"] = blocks::directionName(*facing);
    if (auto const* b = blocks::fromJava(blockId, props)) region.setBlock(pos, *b, 2);
    Json action{{"kind", "container"}, {"block", blockId}, {"facing", facingIndex(facing)}, {"table", tableId}, {"seed", seed}};
    if (facing) action["direction"] = blocks::directionName(*facing);
    queue(region, pos, std::move(action));
}

void lootTable(Region& region, BlockPos const& pos, std::string const& tableId, std::int64_t seed) {
    if (tableId.empty()) return;
    queue(region, pos, Json{{"kind", "loot"}, {"table", tableId}, {"seed", seed}});
}

void spawner(Region& region, BlockPos const& pos, std::string const& entityId, Json const& data) {
    if (auto const* sp = blocks::fromJava("minecraft:spawner")) region.setBlock(pos, *sp, 2);
    queue(region, pos, Json{{"kind", "spawner"}, {"entity", entityId}, {"data", data}});
}

void sign(Region& region, BlockPos const& pos, std::vector<std::string> const& lines) {
    queue(region, pos, Json{{"kind", "sign"}, {"lines", lines}});
}

bool entity(Region& region, double x, double y, double z, std::string const& entityId, Json const& data) {
    if (entityId.empty()) return false;
    BlockPos pos{pure::mthFloor(x), pure::mthFloor(y), pure::mthFloor(z)};
    queue(region, pos, Json{{"kind", "entity"}, {"id", entityId}, {"x", x}, {"y", y}, {"z", z}, {"data", data}});
    return true;
}

void flush() {
    std::deque<Job> work;
    {
        std::lock_guard lock(gJobsMutex);
        if (gJobs.empty()) return;
        work.swap(gJobs);
    }
    std::deque<Job> keep;
    for (auto& job : work) {
        auto* source = game::regionOf(job.dim);
        if (!source || !source->hasChunksAt(job.pos, 0, false)) {
            keep.push_back(std::move(job));
            continue;
        }
        int tries = job.action.value("tries", 0);
        if (!run(*source, job.pos, job.action)) {
            if (tries < 20) {
                job.action["tries"] = tries + 1;
                keep.push_back(std::move(job));
            } else {
                warnOnce("job-dropped:" + job.action.value("kind", std::string{}),
                         "structure: gave up a queued " + job.action.value("kind", std::string{}) + " at " + std::to_string(job.pos.x) + " " +
                             std::to_string(job.pos.y) + " " + std::to_string(job.pos.z));
            }
        }
    }
    std::lock_guard lock(gJobsMutex);
    for (auto& j : keep) gJobs.push_back(std::move(j));
    gDirty = true;
}

std::size_t pending() {
    std::lock_guard lock(gJobsMutex);
    return gJobs.size();
}

void load() {
    std::lock_guard lock(gJobsMutex);
    gJobs.clear();
    if (auto doc = game::db::get(kPendingKey); doc && doc->is_array()) {
        for (auto const& j : *doc)
            gJobs.push_back(Job{j.value("dim", 0), BlockPos{j.value("x", 0), j.value("y", 0), j.value("z", 0)}, j.value("action", Json::object())});
    }
    gDirty = false;
}

void save() {
    Json doc = Json::array();
    {
        std::lock_guard lock(gJobsMutex);
        if (!gDirty) return;
        for (auto const& j : gJobs) doc.push_back(Json{{"dim", j.dim}, {"x", j.pos.x}, {"y", j.pos.y}, {"z", j.pos.z}, {"action", j.action}});
        gDirty = false;
    }
    if (doc.empty()) game::db::erase(kPendingKey);
    else game::db::put(kPendingKey, doc);
}

} // namespace blockentities
} // namespace gf::structure
