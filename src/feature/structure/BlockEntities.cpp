// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/util/entities/EntityUtil.java (tryHangPainting: the spawn)
//   src/main/java/twilightforest/block/entity/DryingRackBlockEntity.java (fillFromLootTable)
//   src/main/java/twilightforest/world/components/processors/SpawnerProcessor.java (generic.scale of spawned mobs)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Block entity work of structure placement (loot containers, drying racks, spawners, signs, flower
// pots, paintings, entities, water in the liquid layer) and the BlockSource-backed region. During
// world generation the region is the generator's window without a BlockSource: the work is queued
// per chunk with its dimension and position, persisted in the world DB (gf:structure:pending) and
// done on the server thread once the chunk is loaded.
#include "feature/structure/Region.h"

#include <cmath>
#include <deque>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/world/SpawnMobEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/vanilla_components/AABBShapeComponent.h"
#include "mc/deps/vanilla_components/ActorDataBoundingBoxComponent.h"
#include "mc/deps/vanilla_components/ActorDataDirtyFlagsComponent.h"
#include "mc/world/actor/ActorDataIDs.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/BuiltInActorComponents.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/Motif.h"
#include "mc/world/actor/Painting.h"
#include "mc/world/actor/SynchedActorDataEntityWrapper.h"
#include "mc/world/level/BaseMobSpawner.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/FlowerPotBlockActor.h"
#include "mc/world/level/block/actor/MobSpawnerBlockActor.h"
#include "mc/world/level/block/actor/SignBlockActor.h"
#include "mc/world/level/block/actor/SignTextSide.h"

#include "core/Keys.h"
#include "feature/common/BlockData.h"
#include "feature/inventory/Inventory.h"
#include "feature/loot/Loot.h"
#include "feature/spawn/Spawn.h"
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
    resetRegionRandom();
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
    // a Java state without "waterlogged = true" carries no fluid: the block replaces the water
    return setBlockWithLiquid(pos, block, flags, false);
}

bool BlockSourceRegion::setBlockWithLiquid(BlockPos const& pos, Block const& block, int flags, bool water) {
    if (pos.y < minY() || pos.y >= maxY() || !isInside(pos.x, pos.z)) return false;
    BlockChangeContext ctx{};
    // Java's flags: 1 neighbours, 2 clients; Bedrock's low bits mean the same
    bool ok = mSource.setBlock(pos, block, flags & 3, nullptr, ctx);
    // (Level#setBlock is false for an unchanged state; the liquid layer still follows the state)
    if (!ok && &mSource.getBlock(pos) != &block) return false;
    // the liquid layer: water for a waterlogged Java state, empty otherwise (Bedrock moves the
    // water of a replaced water block there by itself for blocks that can hold it)
    bool wet = !blocks::isAir(mSource.getExtraBlock(pos));
    if (water && !blocks::isLiquid(block)) {
        if (!wet || !liquids::isWaterSource(mSource.getExtraBlock(pos))) mSource.setExtraBlock(pos, blocks::java("minecraft:water"), flags & 3);
    } else if (wet) {
        mSource.setExtraBlock(pos, blocks::air(), flags & 3);
    }
    return ok;
}

Block const& BlockSourceRegion::getLiquidLayer(BlockPos const& pos) const {
    if (pos.y < minY() || pos.y >= maxY() || !isInside(pos.x, pos.z)) return blocks::air();
    return mSource.getExtraBlock(pos);
}

bool BlockSourceRegion::setLiquidLayer(BlockPos const& pos, Block const& block, int flags) {
    if (pos.y < minY() || pos.y >= maxY() || !isInside(pos.x, pos.z)) return false;
    return mSource.setExtraBlock(pos, block, flags & 3);
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

namespace bd = ::gf::feature::common::blockdata;

constexpr char kPendingKey[]  = "gf:structure:pending";
constexpr char kTestMarkKey[] = "gf:structure:test_mark";
constexpr char kDataKey[]     = "structure"; ///< BlockData key: {"loot": {table, seed}, "spawner_scale": {entity, scale}}
/// Chunks with queued work are checked this often (ticks); a chunk's jobs run together.
constexpr int kFlushInterval = 5;

struct Job {
    int      dim = 0;
    BlockPos pos{0, 0, 0};
    Json     action; ///< {"kind": "container" | "loot" | "spawner" | "sign" | "entity" | "painting" | "flower_pot" | "waterlog" | "rack" | "gf_test_mark", ...}
};
using ChunkKey = std::tuple<int, int, int>; ///< dimension, chunk x, chunk z
std::mutex                         gJobsMutex;
std::map<ChunkKey, std::deque<Job>> gJobs;
std::size_t                        gJobCount = 0;
bool                               gDirty    = false;
int                                gTicks    = 0;
ll::event::ListenerPtr             gSpawnListener;

ChunkKey chunkKey(int dim, BlockPos const& pos) { return {dim, core::chunkOf(pos.x), core::chunkOf(pos.z)}; }

void pushLocked(Job job) {
    auto key = chunkKey(job.dim, job.pos);
    gJobs[key].push_back(std::move(job));
    gJobCount++;
    gDirty = true;
}

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

// ---- paintings ----------------------------------------------------------------------------------------

/// Bedrock's Motif of a Java painting variant id.
Motif const* motifFor(std::string id) {
    if (auto c = id.find(':'); c != std::string::npos) id = id.substr(c + 1);
    using Getter = Motif const& (*)();
    static std::unordered_map<std::string, Getter> const table{
        {"alban", &Motif::mAlban},           {"aztec", &Motif::mAztec},         {"aztec2", &Motif::mAztec2},       {"backyard", &Motif::mBackyard},
        {"baroque", &Motif::mBaroque},       {"bomb", &Motif::mBomb},           {"bouquet", &Motif::mBouquet},     {"burning_skull", &Motif::mBurningSkull},
        {"bust", &Motif::mBust},             {"cavebird", &Motif::mCavebird},   {"changing", &Motif::mChainging},  {"cotan", &Motif::mCotan},
        {"courbet", &Motif::mCourbet},       {"creebet", &Motif::mCreebet},     {"donkey_kong", &Motif::mDonkeyKong}, {"earth", &Motif::mEarth},
        {"endboss", &Motif::mEndboss},       {"fern", &Motif::mFern},           {"fighters", &Motif::mFighters},   {"finding", &Motif::mFinding},
        {"fire", &Motif::mFire},             {"graham", &Motif::mGraham},       {"humble", &Motif::mHumble},       {"kebab", &Motif::mKebab},
        {"lowmist", &Motif::mLowmist},       {"match", &Motif::mMatch},         {"meditative", &Motif::mMeditative}, {"orb", &Motif::mOrb},
        {"owlemons", &Motif::mOwlemons},     {"passage", &Motif::mPassage},     {"pigscene", &Motif::mPigscene},   {"plant", &Motif::mPlant},
        {"pointer", &Motif::mPointer},       {"pond", &Motif::mPond},           {"pool", &Motif::mPool},           {"prairie_ride", &Motif::mPrairieRide},
        {"sea", &Motif::mSea},               {"skeleton", &Motif::mSkeleton},   {"skull_and_roses", &Motif::mSkullAndRoses}, {"stage", &Motif::mStage},
        {"sunflowers", &Motif::mSunflowers}, {"sunset", &Motif::mSunset},       {"tides", &Motif::mTides},         {"unpacked", &Motif::mUnpacked},
        {"void", &Motif::mVoid},             {"wanderer", &Motif::mWanderer},   {"wasteland", &Motif::mWasteland}, {"water", &Motif::mWater},
        {"wind", &Motif::mWind},             {"wither", &Motif::mWither},
    };
    auto it = table.find(id);
    return it == table.end() ? nullptr : &it->second();
}

/// Spawns a painting of `motif` hanging at `pos` (the block it covers) facing `direction` (Java
/// 2D data value, which Bedrock's HangingActor direction shares). False when Bedrock's own survival
/// check (wall behind, nothing in the way) refuses the spot.
bool spawnPainting(BlockSource& source, BlockPos const& pos, Orientation direction, Motif const& motif) {
    auto level = ll::service::getLevel();
    if (!level) return false;
    Vec3 center{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    auto owner = level->getActorFactory().createSpawnedActor(ActorDefinitionIdentifier("minecraft:painting"), nullptr, center, Vec2{0, 0});
    if (!owner) return false;
    auto* actor = Actor::tryGetFromEntity(*owner, false);
    if (!actor || !actor->isType(ActorType::Painting)) return false;
    auto& painting      = static_cast<Painting&>(*actor);
    painting.mDirection = data2D(direction);
    painting.mMotif     = &motif;
    // the placement block of a hanging actor is the block it covers (as Java's HangingEntity#pos);
    // the wall behind it is the other candidate the engine might mean
    BlockPos wall{pos.x - stepX(direction), pos.y, pos.z - stepZ(direction)};
    bool     placed = false;
    for (auto const& at : {pos, wall}) {
        painting._calculateActorPositionFromPlacementPosition(at);
        painting._calculateAABB();
        if (painting._wouldSurvive(source, at, true)) {
            placed = true;
            break;
        }
    }
    if (!placed) return false;
    auto* added = level->addEntity(source, std::move(owner));
    if (!added) return false;
    auto& hung = static_cast<Painting&>(*added);
    if (hung.mMotif != &motif) hung.mMotif = &motif; // adding may re-run the definition's setup
    return true;
}

// ---- running a job ------------------------------------------------------------------------------------

bool runSpawner(BlockSource& source, BlockPos const& pos, Json const& action) {
    auto entityId = action.value("entity", std::string{});
    auto const& b = source.getBlock(pos);
    if (b.getTypeName() != "minecraft:mob_spawner") {
        if (auto const* sp = game::blockWith("minecraft:mob_spawner")) game::setBlock(source, pos, *sp);
    }
    auto* actor = source.getBlockEntity(pos);
    if (!actor || actor->getType() != BlockActorType::MobSpawner) return false; // the actor is created a tick later
    auto& spawnerActor = static_cast<MobSpawnerBlockActor&>(*actor);
    if (!entityId.empty()) spawnerActor.setMob(source, ActorDefinitionIdentifier(entityId), nullptr);
    auto const& data = action.value("data", Json::object());
    if (BaseMobSpawner* spawner = spawnerActor.mSpawner.get()) {
        if (data.contains("Delay")) spawner->mSpawnDelay = data["Delay"].get<int>();
        if (data.contains("MinSpawnDelay")) spawner->mMinSpawnDelay = data["MinSpawnDelay"].get<int>();
        if (data.contains("MaxSpawnDelay")) spawner->mMaxSpawnDelay = data["MaxSpawnDelay"].get<int>();
        if (data.contains("SpawnCount")) spawner->mSpawnCount = data["SpawnCount"].get<int>();
        if (data.contains("MaxNearbyEntities")) spawner->mMaxNearbyEntities = data["MaxNearbyEntities"].get<int>();
        if (data.contains("RequiredPlayerRange")) spawner->mRequiredPlayerRange = data["RequiredPlayerRange"].get<int>();
        if (data.contains("SpawnRange")) spawner->mSpawnRange = data["SpawnRange"].get<int>();
    }
    // SpawnerProcessor entity_width_max: the generic.scale its spawn data gives the mob
    if (data.contains("entity_scale") && !entityId.empty()) {
        float scale = data["entity_scale"].get<float>();
        if (std::isfinite(scale) && scale != 1.0f)
            bd::set(source, pos, kDataKey, Json{{"spawner_scale", {{"entity", entityId}, {"scale", scale}}}});
    }
    actor->refresh(source);
    return true;
}

bool runFlowerPot(BlockSource& source, BlockPos const& pos, Json const& action) {
    if (source.getBlock(pos).getTypeName() != "minecraft:flower_pot") return true; // replaced since: nothing to fill
    auto* actor = source.getBlockEntity(pos);
    if (!actor || actor->getType() != BlockActorType::FlowerPot) return false;
    auto const* plant = game::blockWith(action.value("plant", std::string{}));
    if (!plant) {
        warnOnce("pot-plant:" + action.value("plant", std::string{}), "structure: unknown flower pot plant " + action.value("plant", std::string{}));
        return true;
    }
    auto& pot  = static_cast<FlowerPotBlockActor&>(*actor);
    pot.mPlant = plant;
    pot.onChanged(source);
    actor->refresh(source);
    return true;
}

bool run(BlockSource& source, BlockPos const& pos, Json const& action) {
    auto kind = action.value("kind", std::string{});
    if (kind == "container") {
        auto id    = action.value("block", std::string("minecraft:chest"));
        auto table = action.value("table", std::string{});
        auto seed  = action.value("seed", std::int64_t{0});
        auto bid   = blocks::bedrockId(id);
        if (!table.empty()) bd::set(source, pos, kDataKey, Json{{"loot", {{"table", table}, {"seed", seed}}}});
        if (loot::placeContainer(source, pos, bid.empty() ? id : bid, action.value("facing", 2), table, seed)) return true;
        // the loot contract is not there (yet): keep the chest itself
        Json props = Json::object();
        if (action.contains("direction")) props["facing"] = action["direction"];
        if (auto const* b = blocks::fromJava(id, props)) {
            if (&source.getBlock(pos) != b) game::setBlock(source, pos, *b);
        }
        warnOnce("loot-container", "structure: gf::loot::placeContainer refused a container; chests stay empty until the loot contract is implemented");
        return true;
    }
    if (kind == "loot") {
        auto table = action.value("table", std::string{});
        auto seed  = action.value("seed", std::int64_t{0});
        if (!table.empty()) bd::set(source, pos, kDataKey, Json{{"loot", {{"table", table}, {"seed", seed}}}});
        if (auto h = inventory::find(source, pos); h && h->setLootTable) {
            h->setLootTable(game::globalPos(source, pos), table, seed);
            return true;
        }
        auto const& block = source.getBlock(pos);
        if (loot::placeContainer(source, pos, block.getTypeName(), 2, table, seed)) return true;
        warnOnce("loot-table:" + block.getTypeName(), "structure: no container accepts loot table " + table + " at a " + block.getTypeName());
        return true;
    }
    if (kind == "rack") {
        // DryingRackBlockEntity#fillFromLootTable: getRandomItemsRaw keeps the last stack rolled
        auto table = action.value("table", std::string{});
        auto h     = inventory::find(source, pos);
        if (!h || !h->set) {
            warnOnce("rack:" + source.getBlock(pos).getTypeName(), "structure: no drying rack storage at a " + source.getBlock(pos).getTypeName());
            return true;
        }
        loot::LootContext ctx;
        ctx.region = &source;
        ctx.origin = Vec3{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
        ctx.seed   = action.value("seed", std::int64_t{0});
        auto items = loot::roll(table, ctx);
        if (!items.empty()) h->set(game::globalPos(source, pos), 0, items.back());
        return true;
    }
    if (kind == "spawner") return runSpawner(source, pos, action);
    if (kind == "sign") {
        auto* actor = source.getBlockEntity(pos);
        if (!actor || (actor->getType() != BlockActorType::Sign && actor->getType() != BlockActorType::HangingSign)) return false;
        std::string text;
        for (auto const& l : action.value("lines", Json::array())) text += (text.empty() ? "" : "\n") + l.get<std::string>();
        static_cast<SignBlockActor&>(*actor).setMessageForServerScripingOnly(SignTextSide::Front, text, std::string{});
        actor->refresh(source);
        return true;
    }
    if (kind == "painting") {
        auto const* motif = motifFor(action.value("motive", std::string("kebab")));
        if (!motif) {
            warnOnce("motive:" + action.value("motive", std::string{}), "structure: no Bedrock painting for " + action.value("motive", std::string{}));
            return true;
        }
        auto dir = blocks::parseDirection(action.value("direction", std::string("north"))).value_or(Orientation::North);
        // a queued painting the game refuses now (the wall changed since) is dropped
        if (!spawnPainting(source, pos, dir, *motif))
            warnOnce("painting-refused", "structure: the game refused a queued painting (no wall behind it any more)");
        return true;
    }
    if (kind == "flower_pot") return runFlowerPot(source, pos, action);
    if (kind == "waterlog") {
        // only while the block placed then is still there
        if (source.getBlock(pos).getTypeName() != action.value("block", std::string{})) return true;
        if (!liquids::isWaterSource(source.getExtraBlock(pos))) source.setExtraBlock(pos, blocks::java("minecraft:water"), 3);
        return true;
    }
    if (kind == "gf_test_mark") {
        game::db::put(kTestMarkKey, Json{{"nonce", action.value("nonce", std::string{})}, {"pos", {pos.x, pos.y, pos.z}}});
        return true;
    }
    if (kind == "entity") {
        auto   id = action.value("id", std::string{});
        double x = action.value("x", 0.0), y = action.value("y", 0.0), z = action.value("z", 0.0);
        Vec3   at{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
        if (spawn::spawnMob(source, id, at, spawn::SpawnReason::Structure)) return true;
        auto level = ll::service::getLevel();
        if (!level) return false;
        auto* mob = level->getSpawner().spawnMob(source, ActorDefinitionIdentifier(id), nullptr, at, false, false, false);
        if (!mob) warnOnce("entity:" + id, "structure: could not spawn template entity " + id);
        return true;
    }
    return true;
}

/// Runs `action` now when the region is live; queues it for the chunk otherwise. Returns the
/// result of a live run (true when queued).
bool queue(Region& region, BlockPos const& pos, Json action) {
    // a scratch region (selftests' in-memory regions report dimension -1) keeps no block entities
    if (region.dimension() < 0) return true;
    if (auto* source = sourceOf(region)) {
        if (run(*source, pos, action)) return true;
        // the block actor is not there yet: retry with the next flush
        std::lock_guard lock(gJobsMutex);
        pushLocked(Job{static_cast<int>(source->getDimensionId()), pos, std::move(action)});
        return true;
    }
    std::lock_guard lock(gJobsMutex);
    pushLocked(Job{region.dimension(), pos, std::move(action)});
    return true;
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

void fillRack(Region& region, BlockPos const& pos, std::string const& tableId, std::int64_t seed) {
    if (tableId.empty()) return;
    queue(region, pos, Json{{"kind", "rack"}, {"table", tableId}, {"seed", seed}});
}

void spawner(Region& region, BlockPos const& pos, std::string const& entityId, Json const& data) {
    if (auto const* sp = blocks::fromJava("minecraft:spawner")) region.setBlock(pos, *sp, 2);
    queue(region, pos, Json{{"kind", "spawner"}, {"entity", entityId}, {"data", data}});
}

void sign(Region& region, BlockPos const& pos, std::vector<std::string> const& lines) {
    queue(region, pos, Json{{"kind", "sign"}, {"lines", lines}});
}

void flowerPot(Region& region, BlockPos const& pos, std::string const& plantId) {
    if (plantId.empty()) return;
    queue(region, pos, Json{{"kind", "flower_pot"}, {"plant", plantId}});
}

void waterlog(Region& region, BlockPos const& pos, std::string const& blockId) {
    if (region.dimension() < 0 || sourceOf(region)) return; // live regions set the liquid layer themselves
    std::lock_guard lock(gJobsMutex);
    pushLocked(Job{region.dimension(), pos, Json{{"kind", "waterlog"}, {"block", blockId}}});
}

bool entity(Region& region, double x, double y, double z, std::string const& entityId, Json const& data) {
    if (entityId.empty()) return false;
    BlockPos pos{pure::mthFloor(x), pure::mthFloor(y), pure::mthFloor(z)};
    return queue(region, pos, Json{{"kind", "entity"}, {"id", entityId}, {"x", x}, {"y", y}, {"z", z}, {"data", data}});
}

bool painting(Region& region, BlockPos const& pos, Orientation direction, std::string const& motive, Json const& box) {
    if (region.dimension() < 0) return true;
    if (auto* source = sourceOf(region)) {
        auto const* motif = motifFor(motive);
        if (!motif) {
            warnOnce("motive:" + motive, "structure: no Bedrock painting for " + motive);
            return false;
        }
        return spawnPainting(*source, pos, direction, *motif);
    }
    std::lock_guard lock(gJobsMutex);
    pushLocked(Job{region.dimension(), pos, Json{{"kind", "painting"}, {"motive", motive}, {"direction", blocks::directionName(direction)}, {"box", box}}});
    return true;
}

bool pendingPaintingIntersects(int dim, double const (&box)[6]) {
    std::lock_guard lock(gJobsMutex);
    // only the chunks the box can reach (a painting is at most 4 blocks wide)
    for (int cx = core::chunkOf(pure::mthFloor(box[0])) - 1; cx <= core::chunkOf(pure::mthFloor(box[3])) + 1; cx++)
        for (int cz = core::chunkOf(pure::mthFloor(box[2])) - 1; cz <= core::chunkOf(pure::mthFloor(box[5])) + 1; cz++) {
            auto it = gJobs.find(ChunkKey{dim, cx, cz});
            if (it == gJobs.end()) continue;
            for (auto const& job : it->second) {
                if (job.action.value("kind", std::string{}) != "painting") continue;
                auto const& b = job.action.value("box", Json::array());
                if (!b.is_array() || b.size() != 6) continue;
                // AABB#intersects: strictly overlapping
                if (box[0] < b[3].get<double>() && box[3] > b[0].get<double>() && box[1] < b[4].get<double>() && box[4] > b[1].get<double>() &&
                    box[2] < b[5].get<double>() && box[5] > b[2].get<double>())
                    return true;
            }
        }
    return false;
}

std::optional<LootRecord> lootRecord(int dim, BlockPos const& pos) {
    auto const* doc = bd::get(game::globalPos(dim, pos));
    if (!doc || !doc->contains(kDataKey)) return std::nullopt;
    auto const& d = (*doc)[kDataKey];
    if (!d.is_object() || !d.contains("loot")) return std::nullopt;
    return LootRecord{d["loot"].value("table", std::string{}), d["loot"].value("seed", std::int64_t{0})};
}

void forgetLootRecord(int dim, BlockPos const& pos) {
    auto* source = game::regionOf(dim);
    if (!source) return;
    auto const* d = bd::get(*source, pos, kDataKey);
    if (!d || !d->is_object()) return;
    Json rest = *d;
    rest.erase("loot");
    bd::set(*source, pos, kDataKey, rest.empty() ? Json() : rest);
}

void spawnerScale(int dim, BlockPos const& pos, std::string const& entityId, float scale) {
    auto* source = game::regionOf(dim);
    if (!source) return;
    bd::set(*source, pos, kDataKey, Json{{"spawner_scale", {{"entity", entityId}, {"scale", scale}}}});
}

bool applyScale(Actor& actor, float scale) {
    // generic.scale: the entity's size factor (Bedrock: the scale / width / height data of the
    // bounding box component, synced to clients, and the collision box)
    if (!std::isfinite(scale) || scale <= 0) return false;
    auto bb = actor.getEntityContext().tryGetComponent<ActorDataBoundingBoxComponent>();
    if (!bb) return false;
    auto& value = bb->mValue;
    float old   = value[0] > 0 ? value[0] : 1.0f;
    float ratio = scale / old;
    value[0]    = scale;
    value[1] *= ratio;
    value[2] *= ratio;
    auto& dirty = actor.mEntityData.get().mDirtyFlags.get()->mDirtyFlags.get();
    for (auto id : {ActorDataIDs::Reserved038, ActorDataIDs::Reserved053, ActorDataIDs::Reserved054}) dirty.set(static_cast<std::size_t>(id));
    auto& shape  = *actor.mBuiltInComponents->mAABBShapeComponent;
    shape.mBBDim = Vec2{shape.mBBDim->x * ratio, shape.mBBDim->y * ratio};
    actor._refreshAABB();
    return true;
}

void flush() {
    if (++gTicks % kFlushInterval != 0) return;
    // the chunks with work whose chunk is loaded now
    std::vector<std::pair<ChunkKey, std::deque<Job>>> ready;
    {
        std::lock_guard lock(gJobsMutex);
        if (gJobs.empty()) return;
        for (auto it = gJobs.begin(); it != gJobs.end();) {
            auto [dim, cx, cz] = it->first;
            auto* source       = game::regionOf(dim);
            if (!source || !source->hasChunksAt(BlockPos{cx * 16 + 8, source->getMinHeight(), cz * 16 + 8}, 0, false)) {
                ++it;
                continue;
            }
            gJobCount -= it->second.size();
            ready.emplace_back(it->first, std::move(it->second));
            it = gJobs.erase(it);
        }
    }
    if (ready.empty()) return;
    std::vector<Job> keep;
    bool             changed = false;
    for (auto& [key, jobs] : ready) {
        auto* source = game::regionOf(std::get<0>(key));
        for (auto& job : jobs) {
            if (!source) {
                keep.push_back(std::move(job));
                continue;
            }
            if (run(*source, job.pos, job.action)) {
                changed = true;
                continue;
            }
            int tries = job.action.value("tries", 0);
            if (tries < 20) {
                job.action["tries"] = tries + 1;
                keep.push_back(std::move(job));
            } else {
                changed = true;
                warnOnce("job-dropped:" + job.action.value("kind", std::string{}),
                         "structure: gave up a queued " + job.action.value("kind", std::string{}) + " at " + std::to_string(job.pos.x) + " " +
                             std::to_string(job.pos.y) + " " + std::to_string(job.pos.z));
            }
        }
    }
    std::lock_guard lock(gJobsMutex);
    for (auto& j : keep) {
        // retried jobs keep their place before work queued meanwhile
        auto key = chunkKey(j.dim, j.pos);
        gJobs[key].push_front(std::move(j));
        gJobCount++;
    }
    if (changed || !keep.empty()) gDirty = true;
}

std::size_t pending() {
    std::lock_guard lock(gJobsMutex);
    return gJobCount;
}

void queueTestMark(int dim, BlockPos const& pos, std::string const& nonce) {
    std::lock_guard lock(gJobsMutex);
    pushLocked(Job{dim, pos, Json{{"kind", "gf_test_mark"}, {"nonce", nonce}}});
}

void load() {
    std::lock_guard lock(gJobsMutex);
    gJobs.clear();
    gJobCount = 0;
    if (auto doc = game::db::get(kPendingKey); doc && doc->is_array()) {
        for (auto const& j : *doc) pushLocked(Job{j.value("dim", 0), BlockPos{j.value("x", 0), j.value("y", 0), j.value("z", 0)}, j.value("action", Json::object())});
    }
    gDirty = false;
}

void save() {
    Json doc = Json::array();
    {
        std::lock_guard lock(gJobsMutex);
        if (!gDirty) return;
        for (auto const& [key, jobs] : gJobs)
            for (auto const& j : jobs) doc.push_back(Json{{"dim", j.dim}, {"x", j.pos.x}, {"y", j.pos.y}, {"z", j.pos.z}, {"action", j.action}});
        gDirty = false;
    }
    if (doc.empty()) game::db::erase(kPendingKey);
    else game::db::put(kPendingKey, doc);
}

void installEvents() {
    if (gSpawnListener) return;
    // SpawnerProcessor's entity_width_max: mobs from a spawner that recorded a scale get it
    gSpawnListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::SpawnedMobEvent>([](ll::event::SpawnedMobEvent& ev) {
        if (!ev.fromSpawner() || !ev.mob()) return;
        auto&       mob    = *ev.mob();
        auto&       source = ev.blockSource();
        int         dim    = static_cast<int>(source.getDimensionId());
        auto const& at     = ev.pos();
        std::string id     = ev.identifier().mCanonicalName->getString();
        BlockPos    p{pure::mthFloor(at.x), pure::mthFloor(at.y), pure::mthFloor(at.z)};
        // BaseMobSpawner spawns within SpawnRange (+-1 vertically) of the spawner
        constexpr int kReach = 9;
        std::optional<float> scale;
        for (int cx = core::chunkOf(p.x - kReach); cx <= core::chunkOf(p.x + kReach) && !scale; cx++)
            for (int cz = core::chunkOf(p.z - kReach); cz <= core::chunkOf(p.z + kReach) && !scale; cz++)
                bd::forEachInChunk(dim, cx, cz, [&](core::GlobalPos const& gp, Json const& doc) {
                    if (scale || !doc.contains(kDataKey) || !doc[kDataKey].contains("spawner_scale")) return;
                    auto const& s = doc[kDataKey]["spawner_scale"];
                    if (s.value("entity", std::string{}) != id) return;
                    if (std::abs(gp.x - p.x) > kReach || std::abs(gp.z - p.z) > kReach || std::abs(gp.y - p.y) > 3) return;
                    scale = s.value("scale", 1.0f);
                });
        if (scale) applyScale(mob, *scale);
    });
}

void uninstallEvents() {
    if (gSpawnListener) ll::event::EventBus::getInstance().removeListener(gSpawnListener);
    gSpawnListener.reset();
}

} // namespace blockentities
} // namespace gf::structure
