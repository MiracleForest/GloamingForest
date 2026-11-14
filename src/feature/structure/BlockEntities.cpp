// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/util/entities/EntityUtil.java (tryHangPainting: the spawn)
//   src/main/java/gloamingforest/block/entity/DryingRackBlockEntity.java (fillFromLootTable)
//   src/main/java/gloamingforest/world/components/processors/UpdateMarkingProcessor.java (the post-processing it marks)
//   src/main/java/gloamingforest/block/entity/spawner/SinisterSpawnerLogic.java (load)
//   src/main/java/gloamingforest/block/entity/spawner/SinisterSpawnerBlockEntity.java (loadAdditional)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Block entity work of structure placement (loot containers, drying racks, spawners, signs, flower
// pots, paintings, entities, water in the liquid layer, shape updates) and the BlockSource-backed
// region. During world generation the region is the generator's window without a BlockSource: the
// work is queued per chunk with its dimension and position, persisted in the world DB (one record
// per chunk, gf:structure:pending:<dim>:<cx>:<cz>; shape updates as packed positions, as Java's
// ProtoChunk post-processing lists) and done on the server thread once the chunk is loaded.
#include "feature/structure/Region.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <limits>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/dataloadhelper/DataLoadHelper.h"
#include "mc/dataloadhelper/DefaultDataLoadHelper.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/entity/components/ActorRotationComponent.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/BuiltInActorComponents.h"
#include "mc/world/actor/HangingActor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/Motif.h"
#include "mc/world/actor/Painting.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BaseMobSpawner.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/Container.h"
#include "mc/world/level/block/actor/BedBlockActor.h"
#include "mc/world/level/block/actor/FlowerPotBlockActor.h"
#include "mc/world/level/block/actor/HopperBlockActor.h"
#include "mc/world/level/block/actor/MobSpawnerBlockActor.h"
#include "mc/world/level/block/actor/SignBlockActor.h"
#include "mc/world/level/block/actor/SignTextSide.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/material/Material.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/phys/AABB.h"

#include "core/Keys.h"
#include "feature/common/BlockData.h"
#include "feature/common/Interactions.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/structure/HopperAccess.h"
#include "feature/inventory/Inventory.h"
#include "feature/loot/Loot.h"
#include "feature/ext/combat/LootFill.h" // LootTable#fill (roll and spread with one random), F10's extension
#include "feature/spawn/Spawn.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::structure {

// ---- BlockSourceRegion -----------------------------------------------------------------------------

namespace {
std::mutex                         gAdaptersMutex;
std::unordered_set<Region const*>  gAdapters;
} // namespace

BlockSourceRegion::BlockSourceRegion(BlockSource& source, BlockPos const& center) : mSource(source), mCenter(center) {
    resetRegionRandom(this);
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

namespace bd      = ::gf::feature::common::blockdata;
namespace hoppers = ::gf::structure::ext::hoppers;

/// One record per chunk with queued work: {"jobs": [{x, y, z, action}], "shape": [packed positions]}.
constexpr char kPendingPrefix[] = "gf:structure:pending:";
/// Older builds kept every job in one array under this key (read once, then erased).
constexpr char kLegacyPendingKey[] = "gf:structure:pending";
constexpr char kTestMarkKey[]      = "gf:structure:test_mark";
/// BlockData key: {"spawner": {entity, scale?, spawn_rules?, data?}, "loot": {table, seed}} (loot
/// tables live with the containers, the loot contract keeps them; only a hopper's stays here:
/// Bedrock's hopper has no loot table field)
constexpr char const* kDataKey = kBlockDataKey;
/// Chunks with queued work are checked this often (ticks); a chunk's jobs run together.
constexpr int kFlushInterval = 5;

struct Job {
    int      dim = 0;
    BlockPos pos{0, 0, 0};
    Json     action; ///< {"kind": "container" | "loot" | "spawner" | "sign" | "entity" | "painting" | "flower_pot" | "waterlog" | "rack" | "gf_test_mark", ...}
};
using ChunkKey = std::tuple<int, int, int>; ///< dimension, chunk x, chunk z
/// The queued work of one chunk: its jobs, and the positions marked for a shape update
/// (ProtoChunk#markPosForPostprocessing) packed into one number each (local x, local z, y).
struct ChunkWork {
    std::deque<Job>                   jobs;
    std::vector<std::uint32_t>        shapes;    ///< in the order they were marked
    std::unordered_set<std::uint32_t> shapeSet;  ///< the same, for marking a position only once
    [[nodiscard]] bool         empty() const { return jobs.empty() && shapes.empty(); }
};
std::mutex                    gJobsMutex;
std::map<ChunkKey, ChunkWork> gWork;
/// Chunks whose stored record no longer matches gWork (written or erased at the next save).
std::set<ChunkKey>            gDirtyChunks;
/// The legacy single-key record still exists (erased at the next save).
bool                          gLegacyStored = false;
int                           gTicks        = 0;

ChunkKey chunkKey(int dim, BlockPos const& pos) { return {dim, core::chunkOf(pos.x), core::chunkOf(pos.z)}; }

/// A marked position in its chunk: local x (4 bits), local z (4 bits), y + 2048 (12 bits).
std::uint32_t packShape(BlockPos const& pos) {
    return static_cast<std::uint32_t>(pos.x & 15) | (static_cast<std::uint32_t>(pos.z & 15) << 4) |
           ((static_cast<std::uint32_t>(pos.y + 2048) & 0xfffu) << 8);
}
BlockPos unpackShape(ChunkKey const& key, std::uint32_t packed) {
    return BlockPos{std::get<1>(key) * 16 + static_cast<int>(packed & 15u), static_cast<int>((packed >> 8) & 0xfffu) - 2048,
                    std::get<2>(key) * 16 + static_cast<int>((packed >> 4) & 15u)};
}
/// The 16-block section of a packed position (LevelChunk#postProcessGeneration goes up section by section).
std::uint32_t shapeSection(std::uint32_t packed) { return packed >> 12; }

std::string pendingKey(ChunkKey const& key) {
    return std::string(kPendingPrefix) + std::to_string(std::get<0>(key)) + ":" + std::to_string(std::get<1>(key)) + ":" +
           std::to_string(std::get<2>(key));
}

// ---- queued paintings (the ProtoChunk entities upstream's painting check sees) ----------------------------------

/// A painting job's box, indexed by the chunk of its block apart from the job queue: generation
/// threads check them for every painting they try to hang.
struct PendingPainting {
    std::uint64_t id = 0;
    double        box[6]{};
};
std::shared_mutex                                   gPaintingsMutex;
std::map<ChunkKey, std::vector<PendingPainting>>    gPendingPaintings;
std::atomic<std::uint64_t>                          gNextPaintingId{1};

/// Adds a painting job's box to the index; returns its id (0 when the box is unusable).
std::uint64_t indexPainting(int dim, BlockPos const& pos, Json const& box) {
    if (!box.is_array() || box.size() != 6) return 0;
    PendingPainting p;
    p.id = gNextPaintingId++;
    for (std::size_t i = 0; i < 6; i++) p.box[i] = box[i].get<double>();
    std::unique_lock lock(gPaintingsMutex);
    gPendingPaintings[chunkKey(dim, pos)].push_back(p);
    return p.id;
}

void unindexPainting(int dim, BlockPos const& pos, Json const& action) {
    auto id = action.value("pid", std::uint64_t{0});
    if (id == 0) return;
    std::unique_lock lock(gPaintingsMutex);
    auto             it = gPendingPaintings.find(chunkKey(dim, pos));
    if (it == gPendingPaintings.end()) return;
    std::erase_if(it->second, [&](PendingPainting const& p) { return p.id == id; });
    if (it->second.empty()) gPendingPaintings.erase(it);
}

void pushLocked(Job job) {
    auto key = chunkKey(job.dim, job.pos);
    gWork[key].jobs.push_back(std::move(job));
    gDirtyChunks.insert(key);
}

/// Marks `pos` for a shape update (once: marking a position again changes nothing).
void pushShapeLocked(int dim, BlockPos const& pos) {
    auto  key    = chunkKey(dim, pos);
    auto& work   = gWork[key];
    auto  packed = packShape(pos);
    if (!work.shapeSet.insert(packed).second) return;
    work.shapes.push_back(packed);
    gDirtyChunks.insert(key);
}

/// Queues a painting job (its box indexed first, the job carrying the index id).
void pushPainting(Job job) {
    if (auto id = indexPainting(job.dim, job.pos, job.action.value("box", Json::array()))) job.action["pid"] = id;
    std::lock_guard lock(gJobsMutex);
    pushLocked(std::move(job));
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

/// The Java painting variant id of a Bedrock Motif ("minecraft:kebab"), "" when unknown.
std::string variantOf(Motif const* motif) {
    if (!motif) return {};
    for (auto const& v : paintings::registry())
        if (motifFor(v.id) == motif) return v.id;
    return {};
}

/// Bedrock's HangingActor places a painting from the block it hangs on (the wall behind the block
/// it covers, as the painting item's clicked block), not from the covered block of Java's
/// HangingEntity#pos: HangingActor::_calculateActorPositionFromPlacementPosition(wall) gives the
/// box Painting#calculateBoundingBox gives the covered block (inset by 1/32 at the edges), the
/// covered block gives a box one block further out. Measured by the selftest structure.paintings,
/// which hangs paintings and compares their boxes with upstream's.
BlockPos placementPosition(BlockPos const& covered, Orientation direction) {
    return BlockPos{covered.x - stepX(direction), covered.y, covered.z - stepZ(direction)};
}

/// Calls `fn(painting, owner)` with a painting actor of `motif` facing `direction`, set up for the
/// placement block `placement` (not added to the level; `owner` can be moved into the level). False
/// when the game cannot make one, else what `fn` returns.
template <class Fn>
bool withPainting(BlockPos const& placement, Orientation direction, Motif const& motif, Fn&& fn) {
    auto level = ll::service::getLevel();
    if (!level) return false;
    Vec3 center{static_cast<float>(placement.x) + 0.5f, static_cast<float>(placement.y) + 0.5f, static_cast<float>(placement.z) + 0.5f};
    auto owner = level->getActorFactory().createSpawnedActor(ActorDefinitionIdentifier("minecraft:painting"), nullptr, center, Vec2{0, 0});
    if (!owner) return false;
    auto* actor = Actor::tryGetFromEntity(*owner, false);
    if (!actor || !actor->isType(ActorType::Painting)) return false;
    auto& painting      = static_cast<Painting&>(*actor);
    painting.mDirection = data2D(direction);
    painting.mMotif     = &motif;
    painting._calculateActorPositionFromPlacementPosition(placement);
    painting._calculateAABB();
    return fn(painting, owner);
}

/// Paintings the kit hung carry this actor tag (saved with them): whether they stay is decided by
/// Java's HangingEntity#survives, not by the game's own check (the hook below).
std::string const kJavaPaintingTag = "gf_structure_painting";

/// BlockState#isSolid (the legacy solid flag: a collision shape whose bounds average at least
/// 0.7291666 or are a full block high), or a diode: what HangingEntity#survives accepts behind a
/// painting.
bool javaSupport(BlockSource& source, BlockPos const& pos) {
    Block const& b    = source.getBlock(pos);
    auto const&  name = b.getTypeName();
    if (name == "minecraft:unpowered_repeater" || name == "minecraft:powered_repeater" || name == "minecraft:unpowered_comparator" ||
        name == "minecraft:powered_comparator")
        return true;
    if (!blocks::blocksMotion(b)) return false;
    AABB   shape = b.getBlockType().getCollisionShape(b, source, pos, nullptr);
    double dx = shape.max.x - shape.min.x, dy = shape.max.y - shape.min.y, dz = shape.max.z - shape.min.z;
    if (dx <= 0 || dy <= 0 || dz <= 0) return false;
    return (dx + dy + dz) / 3.0 >= 0.7291666666666666 || dy >= 1.0;
}

/// HangingEntity#survives for a painting of `variant` covering `covered` and facing `direction`:
/// no block that stops movement in its box (Level#noCollision), every block behind it solid or a
/// diode (calculateSupportBox), and no other hanging entity in its box (`self` left out).
bool javaSurvives(BlockSource& source, BlockPos const& covered, Orientation direction, paintings::Variant const& variant, Actor const* self) {
    double box[6];
    paintings::boundingBox(covered, direction, variant, box);
    for (int x = pure::mthFloor(box[0]); x < pure::mthCeil(box[3]); x++)
        for (int y = pure::mthFloor(box[1]); y < pure::mthCeil(box[4]); y++)
            for (int z = pure::mthFloor(box[2]); z < pure::mthCeil(box[5]); z++) {
                if (blocks::blocksMotion(source.getBlock(BlockPos{x, y, z}))) return false;
                if (!javaSupport(source, BlockPos{x - stepX(direction), y, z - stepZ(direction)})) return false;
            }
    AABB range{Vec3{static_cast<float>(box[0]), static_cast<float>(box[1]), static_cast<float>(box[2])},
               Vec3{static_cast<float>(box[3]), static_cast<float>(box[4]), static_cast<float>(box[5])}};
    for (auto* other : source.getEntities(range, 0.0f, ActorType::Painting)) {
        if (!other || other == self) continue;
        AABB const& o = other->getAABB();
        // AABB#intersects: strictly overlapping
        if (range.min.x < o.max.x && range.max.x > o.min.x && range.min.y < o.max.y && range.max.y > o.min.y && range.min.z < o.max.z && range.max.z > o.min.z)
            return false;
    }
    return true;
}

/// Refused paintings described so far this session (the log keeps the first few).
int gRefusalReports = 0;

std::string fmtPos(BlockPos const& p) { return std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z); }

/// One log line when the game's own HangingActor::_wouldSurvive and Java's HangingEntity#survives
/// disagree on a painting upstream's spot check accepted (`java`: Java's verdict; the game's is the
/// other): upstream's box and the game's, and for every block the painting covers the block there and
/// the wall block behind it with what the game's check reads of them, and the entities around.
void reportRefusal(BlockSource& source, Painting& painting, BlockPos const& covered, BlockPos const& at, Orientation direction, Motif const& motif,
                   bool java) {
    if (gRefusalReports >= 64) return;
    gRefusalReports++;
    auto        variant = paintings::byId(variantOf(&motif));
    std::string line    = std::string(java ? "structure: the game would refuse a painting Java keeps: " : "structure: Java refuses a painting the game would keep: ") +
                       (variant ? variant->id : std::string("?")) + " covering " + fmtPos(covered) + " facing " + blocks::directionName(direction) +
                       " (placed from " + fmtPos(at) + ")";
    AABB const& b = painting.getAABB();
    char        buf[160];
    std::snprintf(buf, sizeof(buf), "; game box %.4f %.4f %.4f .. %.4f %.4f %.4f", b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
    line += buf;
    if (variant) {
        double tf[6];
        paintings::boundingBox(covered, direction, *variant, tf);
        std::snprintf(buf, sizeof(buf), ", upstream box %.4f %.4f %.4f .. %.4f %.4f %.4f", tf[0], tf[1], tf[2], tf[3], tf[4], tf[5]);
        line += buf;
        line += "; cells:";
        for (int x = pure::mthFloor(tf[0]); x < pure::mthCeil(tf[3]); x++)
            for (int y = pure::mthFloor(tf[1]); y < pure::mthCeil(tf[4]); y++)
                for (int z = pure::mthFloor(tf[2]); z < pure::mthCeil(tf[5]); z++) {
                    BlockPos     cell{x, y, z};
                    BlockPos     wall{x - stepX(direction), y, z - stepZ(direction)};
                    Block const& front = source.getBlock(cell);
                    Block const& back  = source.getBlock(wall);
                    Material const& m  = back.getMaterial();
                    line += " [" + fmtPos(cell) + " " + front.getTypeName() + (painting._blockIsObstruction(source, cell) ? " obstruction" : "") +
                            (painting._blockContainsObstruction(source, cell) ? " contains-obstruction" : "") + " | wall " + back.getTypeName() +
                            (m.mSolid ? " solid" : " not-solid") + (m.mBlocksMotion ? " blocks-motion" : "") +
                            (back.canProvideFullSupport(static_cast<uchar>(facingIndex(direction))) ? " full-face" : "") + "]";
                }
    }
    int         hanging = 0;
    std::string others;
    AABB        range{Vec3{b.min.x - 0.01f, b.min.y - 0.01f, b.min.z - 0.01f}, Vec3{b.max.x + 0.01f, b.max.y + 0.01f, b.max.z + 0.01f}};
    for (auto* other : source.getEntities(range, 0.0f)) {
        if (!other || other == &painting) continue;
        if (other->isType(ActorType::Painting)) hanging++;
        else others += (others.empty() ? "" : ", ") + other->getTypeName();
    }
    line += "; paintings around " + std::to_string(hanging) + "; other entities around: " + (others.empty() ? std::string("none") : others);
    line += "; wouldSurvive(not placing) " + std::string(painting._wouldSurvive(source, at, false) ? "yes" : "no");
    log().info("{}", line);
}

/// Spawns a painting of `motif` hanging at `pos` (the block it covers) facing `direction` (Java
/// 2D data value, which Bedrock's HangingActor direction shares). upstream adds the painting once its
/// spot check passed and Java keeps it while HangingEntity#survives holds: the painting hangs when
/// Java's check holds (the game's own check is not asked; it refuses spots Java keeps, see
/// reportRefusal), and keeps Java's check for good (kJavaPaintingTag). False when Java's check
/// fails (the caller drops the item, as upstream's painting drops on its next check).
bool spawnPainting(BlockSource& source, BlockPos const& pos, Orientation direction, Motif const& motif) {
    BlockPos at      = placementPosition(pos, direction);
    auto     variant = paintings::byId(variantOf(&motif));
    if (!variant) return false;
    return withPainting(at, direction, motif, [&](Painting& painting, auto& owner) {
        bool java = javaSurvives(source, pos, direction, *variant, nullptr);
        bool game = painting._wouldSurvive(source, at, true);
        if (java != game) reportRefusal(source, painting, pos, at, direction, motif, java);
        if (!java) return false;
        auto level = ll::service::getLevel();
        auto* added = level ? level->addEntity(source, std::move(owner)) : nullptr;
        if (!added) return false;
        auto& hung = static_cast<Painting&>(*added);
        if (hung.mMotif != &motif) hung.mMotif = &motif; // adding may re-run the definition's setup
        (void)hung.addTag(kJavaPaintingTag);
        return true;
    });
}

/// Painting#dropItem when a painting cannot stay (upstream hangs it anyway and it drops off): a painting
/// item at the painting's center, if entities drop items (doEntityDrops).
void dropPaintingItem(BlockSource& source, Json const& box) {
    auto level = ll::service::getLevel();
    if (!level || !box.is_array() || box.size() != 6) return;
    GameRuleId rule;
    rule.mValue = static_cast<int>(GameRules::GameRulesIndex::DoEntityDrops);
    if (!level->getGameRules().getBool(rule, true)) return;
    auto stack = game::items::make("minecraft:painting", 1, 0);
    if (!stack) return;
    Vec3 at{static_cast<float>((box[0].get<double>() + box[3].get<double>()) / 2), static_cast<float>((box[1].get<double>() + box[4].get<double>()) / 2),
            static_cast<float>((box[2].get<double>() + box[5].get<double>()) / 2)};
    (void)level->getSpawner().spawnItem(source, *stack, nullptr, at, 0);
}

// ---- running a job ------------------------------------------------------------------------------------

/// The BaseSpawner fields a spawner's data may set (vanilla block entity names).
constexpr char const* kSpawnerFields[] = {"Delay", "MinSpawnDelay", "MaxSpawnDelay", "SpawnCount", "MaxNearbyEntities", "RequiredPlayerRange", "SpawnRange"};

/// What the kit keeps about a spawner in its BlockData ("structure"."spawner"): the mob, what
/// Bedrock's spawner does not keep for the spawner events (Spawners.cpp: SpawnerProcessor
/// entity_width_max, the generic.scale its spawn data gives the mob, and SpawnData's custom spawn
/// rules, the light ranges BaseSpawner checks instead of the mob's spawn rules) and, for a spawner
/// block the kit does not drive (`withFields`: upstream's sinister spawner), the BaseSpawner fields of
/// its data. Empty when there is nothing to keep.
Json spawnerRecordOf(std::string const& entityId, Json const& data, bool withFields) {
    Json record = Json::object();
    // (a vanilla spawner without a mob keeps nothing; a spawner block of its own keeps its fields)
    if (entityId.empty() && !withFields) return record;
    if (data.contains("entity_scale") && data["entity_scale"].is_number()) {
        float scale = data["entity_scale"].get<float>();
        if (std::isfinite(scale) && scale > 0 && scale != 1.0f) record["scale"] = scale;
    }
    if (data.contains("spawn_rules") && data["spawn_rules"].is_object()) record["spawn_rules"] = data["spawn_rules"];
    if (withFields) {
        Json fields = Json::object();
        for (char const* f : kSpawnerFields)
            if (data.contains(f) && data[f].is_number_integer()) fields[f] = data[f];
        if (!fields.empty() || !entityId.empty()) record["data"] = fields;
    }
    if (!record.empty() && !entityId.empty()) record["entity"] = entityId;
    return record;
}

// ---- the index of the kit's spawner records ---------------------------------------------------------

/// The positions with a kit spawner record, per chunk (dimension, chunk x, chunk z): a chunk's
/// block data is read once (the first time a spawn looks there), then kept by the record writes and
/// the block data's erase / move listeners (a record goes with its block). Chunks no longer loaded
/// are evicted every kIndexSweepInterval ticks (read again when a spawn looks there), and the index
/// is dropped as a whole past kIndexMaxChunks chunks.
struct SpawnerIndex {
    std::mutex                                                            mutex;
    std::map<std::tuple<int, int, int>, std::set<std::tuple<int, int, int>>> byChunk;
};
SpawnerIndex& spawnerIndex() {
    static SpawnerIndex s;
    return s;
}
constexpr int         kIndexSweepInterval = 1200;
constexpr std::size_t kIndexMaxChunks     = 4096;

void indexSpawnerRecord(int dim, BlockPos const& pos, bool present) {
    auto&           idx = spawnerIndex();
    std::lock_guard lock(idx.mutex);
    auto            it = idx.byChunk.find(chunkKey(dim, pos));
    if (it == idx.byChunk.end()) return; // not read yet: it is read with the record in it
    if (present) it->second.insert({pos.x, pos.y, pos.z});
    else it->second.erase({pos.x, pos.y, pos.z});
}
void indexSpawnerRecord(BlockSource& source, BlockPos const& pos, bool present) {
    indexSpawnerRecord(static_cast<int>(source.getDimensionId()), pos, present);
}

/// Evicts the chunks that are not loaded any more (and everything past kIndexMaxChunks).
void sweepSpawnerIndex() {
    auto&           idx = spawnerIndex();
    std::lock_guard lock(idx.mutex);
    if (idx.byChunk.size() > kIndexMaxChunks) {
        idx.byChunk.clear();
        return;
    }
    for (auto it = idx.byChunk.begin(); it != idx.byChunk.end();) {
        auto [dim, cx, cz] = it->first;
        auto* source       = game::regionOf(dim);
        if (!source || !source->getChunk(cx, cz)) it = idx.byChunk.erase(it);
        else ++it;
    }
}

/// Replaces the spawner part of the BlockData at `pos` (nothing kept: removed).
void storeSpawnerRecord(BlockSource& source, BlockPos const& pos, Json const& record) {
    Json doc = Json::object();
    if (auto const* old = bd::get(source, pos, kDataKey); old && old->is_object()) doc = *old;
    doc.erase("spawner");
    if (!record.empty()) doc["spawner"] = record;
    bd::set(source, pos, kDataKey, doc.empty() ? Json() : doc);
    indexSpawnerRecord(source, pos, !record.empty());
}

/// SpawnData#entityToSpawn's id in the kit's spawner data shapes: "entity" as an id or
/// {"id": ...}, or Java's SpawnData {"entity": {"id": ...}} ("" when none).
std::string spawnDataEntity(Json const& spawnData) {
    if (spawnData.is_string()) return spawnData.get<std::string>();
    if (!spawnData.is_object()) return {};
    if (spawnData.contains("id") && spawnData["id"].is_string()) return spawnData["id"].get<std::string>();
    if (spawnData.contains("entity")) return spawnDataEntity(spawnData["entity"]);
    return {};
}

// ---- BaseSpawner#load ------------------------------------------------------------------------------
// The kit keeps a template's NBT as JSON: a number tag is a JSON number.

/// CompoundTag#contains(key, 99): a number tag of any type.
bool nbtHasNumber(Json const& tag, char const* key) {
    if (!tag.is_object()) return false;
    auto it = tag.find(key);
    return it != tag.end() && it->is_number();
}

/// The integer of a number tag (NumericTag#getAsLong: a float tag floored), nullopt when missing
/// or not a number.
std::optional<std::int64_t> nbtLong(Json const& tag, char const* key) {
    if (!nbtHasNumber(tag, key)) return std::nullopt;
    auto const& v = tag[key];
    if (v.is_number_float()) {
        double d = std::floor(v.get<double>());
        if (!(d > -9.2e18 && d < 9.2e18)) return d > 0 ? std::numeric_limits<std::int64_t>::max() : std::numeric_limits<std::int64_t>::min();
        return static_cast<std::int64_t>(d);
    }
    if (v.is_number_unsigned()) return static_cast<std::int64_t>(v.get<std::uint64_t>());
    return v.get<std::int64_t>();
}

/// CompoundTag#getShort: NumericTag#getAsShort (the low 16 bits), 0 when missing or not a number.
int nbtShort(Json const& tag, char const* key) {
    auto v = nbtLong(tag, key);
    return v ? static_cast<int>(static_cast<std::int16_t>(static_cast<std::uint16_t>(static_cast<std::uint64_t>(*v) & 0xffffu))) : 0;
}

/// CompoundTag#getInt: NumericTag#getAsInt (the low 32 bits), 0 when missing or not a number.
int nbtInt(Json const& tag, char const* key) {
    auto v = nbtLong(tag, key);
    return v ? static_cast<int>(static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(*v) & 0xffffffffu))) : 0;
}

/// Codec.INT decoding the number tag `key` of a compound (NbtOps#getNumberValue, Number#intValue: a
/// floating tag truncated toward 0 and saturated, an integer tag's low 32 bits); nullopt when the
/// field is missing or not a number (the field fails to decode).
std::optional<int> codecInt(Json const& tag, char const* key) {
    if (!nbtHasNumber(tag, key)) return std::nullopt;
    auto const& v = tag[key];
    if (v.is_number_float()) {
        double d = v.get<double>();
        if (std::isnan(d)) return 0;
        if (d >= 2147483647.0) return std::numeric_limits<int>::max();
        if (d <= -2147483648.0) return std::numeric_limits<int>::min();
        return static_cast<int>(d);
    }
    std::uint64_t bits = v.is_number_unsigned() ? v.get<std::uint64_t>() : static_cast<std::uint64_t>(v.get<std::int64_t>());
    return static_cast<int>(static_cast<std::int32_t>(static_cast<std::uint32_t>(bits & 0xffffffffu)));
}

/// The BaseSpawner fields BaseSpawner#load reads from a spawner's data: Delay always (0 when the
/// data has none); MinSpawnDelay, MaxSpawnDelay and SpawnCount together, only when MinSpawnDelay is
/// there; MaxNearbyEntities and RequiredPlayerRange together, only when MaxNearbyEntities is there;
/// SpawnRange when it is there. Each a short (CompoundTag#getShort: a missing partner reads 0).
struct SpawnerTag {
    int                               delay = 0;
    std::optional<std::array<int, 3>> delays;  ///< min delay, max delay, spawn count
    std::optional<std::array<int, 2>> nearby;  ///< max nearby entities, required player range
    std::optional<int>                range;
};
SpawnerTag readSpawnerTag(Json const& data) {
    SpawnerTag t;
    t.delay = nbtShort(data, "Delay");
    if (nbtHasNumber(data, "MinSpawnDelay")) t.delays = std::array<int, 3>{nbtShort(data, "MinSpawnDelay"), nbtShort(data, "MaxSpawnDelay"), nbtShort(data, "SpawnCount")};
    if (nbtHasNumber(data, "MaxNearbyEntities")) t.nearby = std::array<int, 2>{nbtShort(data, "MaxNearbyEntities"), nbtShort(data, "RequiredPlayerRange")};
    if (nbtHasNumber(data, "SpawnRange")) t.range = nbtShort(data, "SpawnRange");
    return t;
}

// ---- SinisterSpawnerLogic's ParticleOptions ------------------------------------------------------------

/// What a particle type's MapCodec asks for (ParticleTypes.CODEC dispatches on "type").
enum class ParticleShape { Simple, Dust, DustTransition, BlockState, Item, Color, Vibration, Roll, Delay, Leaf };

/// The particle types of Minecraft 1.21.1 (ParticleTypes: the SimpleParticleTypes, which take no
/// options, and the option types; read from the server jar's ParticleTypes.<clinit>) and upstream 4.8
/// (TFParticleType), by id; nullopt for an unknown id (Registry#byNameCodec fails).
std::optional<ParticleShape> particleShape(std::string const& id) {
    static std::unordered_map<std::string, ParticleShape> const table = [] {
        std::unordered_map<std::string, ParticleShape> t;
        for (auto const* s :
             {"angry_villager", "bubble", "cloud", "crit", "damage_indicator", "dragon_breath", "dripping_lava", "falling_lava", "landing_lava",
              "dripping_water", "falling_water", "effect", "elder_guardian", "enchanted_hit", "enchant", "end_rod", "explosion_emitter", "explosion",
              "gust", "small_gust", "gust_emitter_large", "gust_emitter_small", "sonic_boom", "firework", "fishing", "flame", "infested",
              "cherry_leaves", "sculk_soul", "sculk_charge_pop", "soul_fire_flame", "soul", "flash", "happy_villager", "composter", "heart",
              "instant_effect", "item_slime", "item_cobweb", "item_snowball", "large_smoke", "lava", "mycelium", "note", "poof", "portal", "rain",
              "smoke", "white_smoke", "sneeze", "spit", "squid_ink", "sweep_attack", "totem_of_undying", "underwater", "splash", "witch",
              "bubble_pop", "current_down", "bubble_column_up", "nautilus", "dolphin", "campfire_cosy_smoke", "campfire_signal_smoke",
              "dripping_honey", "falling_honey", "landing_honey", "falling_nectar", "falling_spore_blossom", "ash", "crimson_spore", "warped_spore",
              "spore_blossom_air", "dripping_obsidian_tear", "falling_obsidian_tear", "landing_obsidian_tear", "reverse_portal", "white_ash",
              "small_flame", "snowflake", "dripping_dripstone_lava", "falling_dripstone_lava", "dripping_dripstone_water",
              "falling_dripstone_water", "glow_squid_ink", "glow", "wax_on", "wax_off", "electric_spark", "scrape", "egg_crack", "dust_plume",
              "trial_spawner_detection", "trial_spawner_detection_ominous", "vault_connection", "ominous_spawning", "raid_omen", "trial_omen"})
            t[std::string("minecraft:") + s] = ParticleShape::Simple;
        t["minecraft:dust"]                  = ParticleShape::Dust;           // color [3], scale 0.01..4
        t["minecraft:dust_color_transition"] = ParticleShape::DustTransition; // from_color [3], to_color [3], scale 0.01..4
        for (auto const* s : {"block", "block_marker", "falling_dust", "dust_pillar"}) t[std::string("minecraft:") + s] = ParticleShape::BlockState;
        t["minecraft:item"]          = ParticleShape::Item;      // item
        t["minecraft:entity_effect"] = ParticleShape::Color;     // color (ARGB int or [4])
        t["minecraft:vibration"]     = ParticleShape::Vibration; // destination (a block position source), arrival_in_ticks
        t["minecraft:sculk_charge"]  = ParticleShape::Roll;      // roll
        t["minecraft:shriek"]        = ParticleShape::Delay;     // delay
        for (auto const* s : {"large_flame", "leaf_rune", "boss_tear", "ghast_trap", "protection", "snow", "snow_warning", "extended_snow_warning",
                              "snow_guardian", "ice_beam", "annihilate", "perfect_dodge", "double_jump", "huge_smoke", "firefly", "wandering_firefly",
                              "particle_spawner_firefly", "dim_flame", "ominous_flame", "sorting_particle", "transformation_particle",
                              "log_core_particle", "cloud_puff", "angry_lich", "gloaming_orb", "shield_break", "drying_rack"})
            t[std::string("gloamingforest:") + s] = ParticleShape::Simple;
        t["gloamingforest:fallen_leaf"]  = ParticleShape::Leaf;  // LeafParticleData: r, g, b
        t["gloamingforest:magic_effect"] = ParticleShape::Color; // ColorParticleOption
        return t;
    }();
    auto it = table.find(id);
    return it == table.end() ? std::nullopt : std::optional<ParticleShape>(it->second);
}

/// ExtraCodecs.VECTOR3F / VECTOR4F: a list of exactly `n` numbers.
bool isVector(Json const& v, std::size_t n) {
    if (!v.is_array() || v.size() != n) return false;
    return std::all_of(v.begin(), v.end(), [](Json const& e) { return e.is_number(); });
}

/// ScalableParticleOptionsBase.SCALE: a float in 0.01..4.
bool validScale(Json const& options) {
    if (!nbtHasNumber(options, "scale")) return false;
    float scale = options["scale"].get<float>();
    return scale >= 0.01f && scale <= 4.0f;
}

/// Whether `options` decode with the MapCodec of a particle of `shape`.
bool validParticleOptions(ParticleShape shape, Json const& options) {
    switch (shape) {
    case ParticleShape::Simple:
        return true;
    case ParticleShape::Dust:
        return options.contains("color") && isVector(options["color"], 3) && validScale(options);
    case ParticleShape::DustTransition:
        return options.contains("from_color") && isVector(options["from_color"], 3) && options.contains("to_color") && isVector(options["to_color"], 3) &&
               validScale(options);
    case ParticleShape::BlockState: {
        // BlockParticleOption: BlockState.CODEC ({"Name", "Properties"}) or a block id
        if (!options.contains("block_state")) return false;
        auto const& s    = options["block_state"];
        std::string name = s.is_string() ? s.get<std::string>() : (s.is_object() && s.contains("Name") && s["Name"].is_string() ? s["Name"].get<std::string>() : "");
        return !name.empty() && blocks::fromJava(name) != nullptr;
    }
    case ParticleShape::Item: {
        // ItemParticleOption: ItemStack.SINGLE_ITEM_CODEC ({"id", "components"?}) or an item id; never air
        if (!options.contains("item")) return false;
        auto const& s  = options["item"];
        std::string id = s.is_string() ? s.get<std::string>() : (s.is_object() && s.contains("id") && s["id"].is_string() ? s["id"].get<std::string>() : "");
        if (id.find(':') == std::string::npos && !id.empty()) id = "minecraft:" + id;
        return !id.empty() && id != "minecraft:air" && game::items::make(id).has_value();
    }
    case ParticleShape::Color:
        // ExtraCodecs.ARGB_COLOR_CODEC: an int, or [a, r, g, b] floats
        return options.contains("color") && (options["color"].is_number() || isVector(options["color"], 4));
    case ParticleShape::Vibration: {
        // VibrationParticleOption: a block position source (an entity one is refused), arrival_in_ticks
        if (!options.contains("destination") || !options["destination"].is_object() || !nbtHasNumber(options, "arrival_in_ticks")) return false;
        auto const& d    = options["destination"];
        auto        type = d.value("type", std::string{});
        return (type == "minecraft:block" || type == "block") && d.contains("pos") && isVector(d["pos"], 3);
    }
    case ParticleShape::Roll:
        return nbtHasNumber(options, "roll");
    case ParticleShape::Delay:
        return nbtHasNumber(options, "delay");
    case ParticleShape::Leaf:
        return nbtHasNumber(options, "r") && nbtHasNumber(options, "g") && nbtHasNumber(options, "b");
    }
    return false;
}

} // namespace

std::optional<ParticleList> parseParticleOptions(Json const* list) {
    // PARTICLES_CODEC = ParticleTypes.CODEC.listOf(): every entry a compound {"type": id, options...}
    // (a bare id is not a compound and fails); a missing tag fails too
    if (!list || !list->is_array()) return std::nullopt;
    ParticleList out;
    out.params = Json::object();
    std::vector<Json> seen; // SinisterSpawnerLogic keeps them in a HashSet: a repeated entry once
    for (auto const& e : *list) {
        if (!e.is_object() || !e.contains("type") || !e["type"].is_string()) return std::nullopt;
        std::string id = e["type"].get<std::string>();
        if (id.find(':') == std::string::npos) id = "minecraft:" + id;
        auto shape = particleShape(id);
        if (!shape || !validParticleOptions(*shape, e)) return std::nullopt;
        Json options = e;
        options.erase("type");
        Json entry{{"type", id}, {"options", options}};
        if (std::find(seen.begin(), seen.end(), entry) != seen.end()) continue;
        seen.push_back(entry);
        if (out.params.contains(id)) {
            // F8's spawner keeps one set of options per particle type
            warnOnce("particle-options:" + id, "structure: a sinister spawner shows " + id + " with one set of options (its data lists several)");
            continue;
        }
        out.ids.push_back(id);
        if (!options.empty()) out.params[id] = std::move(options);
    }
    return out;
}

void loadSinisterSpawnerData(boss::ext::SinisterSpawner& spawner, std::string const& entityId, Json const& data) {
    Json const& tag = data;
    // BaseSpawner#load: Delay, unconditionally
    SpawnerTag fields  = readSpawnerTag(tag);
    spawner.spawnDelay = fields.delay;
    // SpawnData (a compound) is the next spawn; the kit's "entity" is the SpawnData a processor wrote
    if (!entityId.empty()) spawner.nextEntity = entityId;
    else if (tag.is_object() && tag.contains("SpawnData") && tag["SpawnData"].is_object()) spawner.nextEntity = spawnDataEntity(tag["SpawnData"]);
    if (tag.is_object() && tag.contains("SpawnPotentials") && tag["SpawnPotentials"].is_array()) {
        // SpawnData.LIST_CODEC (SimpleWeightedRandomList.wrappedCodecAllowingEmpty: each entry a
        // WeightedEntry.Wrapper {"data": SpawnData, "weight": Weight.CODEC = Codec.INT}, both fields
        // required; SpawnData needs its "entity" compound); resultOrPartial keeps the entries that
        // parse. (F8's spawner has no entry that spawns nothing: an entity compound without an id,
        // or a weight of 0, which is never drawn, is left out; a negative weight throws in Java's
        // Weight.of and is left out too.)
        spawner.potentials.clear();
        for (auto const& e : tag["SpawnPotentials"]) {
            auto weight = codecInt(e, "weight");
            if (!weight || !e.contains("data") || !e["data"].is_object() || !e["data"].contains("entity") || !e["data"]["entity"].is_object()) continue;
            std::string id = spawnDataEntity(e["data"]["entity"]);
            if (!id.empty() && *weight > 0) spawner.potentials.emplace_back(id, *weight);
        }
    } else {
        // SimpleWeightedRandomList.single(nextSpawnData, or an empty SpawnData that spawns nothing)
        spawner.potentials.clear();
        if (!spawner.nextEntity.empty()) spawner.potentials.emplace_back(spawner.nextEntity, 1);
    }
    if (fields.delays) {
        spawner.minSpawnDelay = (*fields.delays)[0];
        spawner.maxSpawnDelay = (*fields.delays)[1];
        spawner.spawnCount    = (*fields.delays)[2];
    }
    if (fields.nearby) {
        spawner.maxNearbyEntities   = (*fields.nearby)[0];
        spawner.requiredPlayerRange = (*fields.nearby)[1];
    }
    if (fields.range) spawner.spawnRange = *fields.range;
    // SinisterSpawnerLogic#load: the particles are cleared, then the data's, only when the whole list
    // parses (DataResult#isSuccess)
    spawner.particles.clear();
    spawner.particleParams = Json::object();
    if (auto particles = parseParticleOptions(tag.is_object() && tag.contains("ParticleOptions") ? &tag["ParticleOptions"] : nullptr)) {
        spawner.particles      = std::move(particles->ids);
        spawner.particleParams = std::move(particles->params);
    }
    // EntityScanRange (any tag: CompoundTag#contains(key)), else the SpawnRange just loaded
    spawner.entityScanRange = tag.is_object() && tag.contains("EntityScanRange") ? nbtInt(tag, "EntityScanRange") : spawner.spawnRange;
}

namespace {

/// BaseSpawner#load + SinisterSpawnerLogic#load (SinisterSpawnerBlockEntity#loadAdditional) of a
/// template's spawner data into the sinister spawner block entity F8 keeps at `pos` (nothing for
/// any other block).
void loadSinisterSpawner(BlockSource& source, BlockPos const& pos, std::string const& entityId, Json const& data) {
    auto spawner = boss::ext::sinisterSpawner(source, pos);
    if (!spawner) return;
    loadSinisterSpawnerData(*spawner, entityId, data);
    if (!boss::ext::setSinisterSpawner(source, pos, *spawner))
        warnOnce("sinister:" + source.getBlock(pos).getTypeName(), "structure: the sinister spawner refused its template data");
}

// ---- hopper loot tables --------------------------------------------------------------------------------
// Java's hopper is a RandomizableContainer: its loot table is unpacked on any access to its
// container (getItem, setItem, removeItem, isEmpty: a hopper pushing into it or pulling from it, its
// first tick while enabled, a comparator's reading, Containers#dropContents when it is removed) and
// when a player opens it. Bedrock's hopper has no loot table field: the kit keeps the table in the
// hopper's block data and unpacks it on the same accesses (the hopper access dispatch of
// ext/structure/HopperAccess.h, the interactions contract for players).

bool isHopper(BlockActor const& actor) { return actor.getType() == BlockActorType::Hopper; }

/// The hoppers with a pending loot table, (x, y, z, dimension): watched through the hopper access
/// dispatch, and persisted in the world DB (kHoppersKey, read at world load).
///
/// The list is written by the block data's write-back, which runs right before every block data
/// write, and a hopper unpacked since the last write-back stays in it for one more write: a crash
/// between the two writes (or in the middle of the block data's) never leaves a table in the block
/// data that the list misses; a listed hopper without a table is dropped at its first access.
constexpr char kHoppersKey[] = "gf:structure:hoppers";
using HopperKey              = std::tuple<int, int, int, int>;
std::mutex                   gHoppersMutex;
std::set<HopperKey>          gHoppers;
std::set<HopperKey>          gHoppersRemoved; ///< unpacked or gone since the last write-back
bool                         gHoppersDirty = false;
/// Set while a hopper's table is rolled into it (its own reads and writes unpack nothing).
thread_local bool            tUnpacking = false;

/// HopperBlock.ENABLED: Bedrock's toggle_bit is set while the hopper is powered.
bool hopperEnabled(Block const& block) {
    auto states = game::statesOf(block);
    auto it     = states.find("toggle_bit");
    return it == states.end() || it->second == "false" || it->second == "0";
}

void noteHopper(int dim, BlockPos const& pos, bool pending) {
    std::lock_guard lock(gHoppersMutex);
    HopperKey       key{pos.x, pos.y, pos.z, dim};
    bool            changed = pending ? gHoppers.insert(key).second : gHoppers.erase(key) > 0;
    if (!changed) return;
    gHoppersDirty = true;
    if (pending) {
        hoppers::watch(dim, pos);
    } else {
        hoppers::unwatch(dim, pos);
        gHoppersRemoved.insert(key);
    }
}

bool hopperPending(int dim, BlockPos const& pos) {
    std::lock_guard lock(gHoppersMutex);
    return gHoppers.contains(HopperKey{pos.x, pos.y, pos.z, dim});
}

/// The list to store: the pending hoppers and those unpacked since the last write-back (the caller
/// holds gHoppersMutex).
Json hopperListLocked() {
    std::set<HopperKey> all = gHoppers;
    all.insert(gHoppersRemoved.begin(), gHoppersRemoved.end());
    Json list = Json::array();
    for (auto const& [x, y, z, dim] : all) list.push_back(Json::array({dim, x, y, z}));
    return list;
}

/// Writes the list when it changed. `writeBack`: the block data writes next, so the hoppers unpacked
/// before now leave the list at the following write.
void storeHoppers(bool writeBack) {
    Json list;
    {
        std::lock_guard lock(gHoppersMutex);
        if (!gHoppersDirty) return;
        list = hopperListLocked();
        if (writeBack) {
            gHoppersDirty = !gHoppersRemoved.empty();
            gHoppersRemoved.clear();
        } else {
            // (the unpacked ones stay listed until a write-back, which then writes again)
            gHoppersDirty = !gHoppersRemoved.empty();
        }
    }
    if (!game::db::available()) return;
    if (list.empty()) game::db::erase(kHoppersKey);
    else game::db::put(kHoppersKey, list);
}

/// LootTable#fill(container, params, seed) into a hopper (unpackLootTable(null): no player, no
/// luck): the loot contract's fill, which rolls the table and spreads the stacks over the empty
/// slots with the same random, as Java does.
void fillHopper(BlockSource& source, BlockPos const& pos, Container& container, std::string const& tableId, std::int64_t seed) {
    loot::LootContext ctx;
    ctx.region = &source;
    ctx.origin = Vec3{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    ctx.seed   = seed;
    loot::fill(
        tableId,
        ctx,
        container.getContainerSize(),
        [&](int slot) -> ItemStack const& { return container.getItem(slot); },
        [&](int slot, ItemStack const& stack) { container.setItem(slot, stack); }
    );
    log().info("structure: unpacked {} into the hopper at {} {} {}", tableId, pos.x, pos.y, pos.z);
}

/// RandomizableContainer#setLootTable for a hopper: the kit keeps the table in the hopper's block
/// data. A hopper unpacks its table on its first tick while it is enabled (HopperBlockEntity
/// #pushItemsTick -> isEmpty -> unpackLootTable(null)): an enabled hopper is filled at once; a
/// powered one on its first container access. False while the hopper's block actor is not there yet.
bool setHopperLoot(BlockSource& source, BlockPos const& pos, std::string const& tableId, std::int64_t seed) {
    auto* actor = source.getBlockEntity(pos);
    if (!actor || !isHopper(*actor)) return false;
    Json doc = Json::object();
    if (auto const* old = bd::get(source, pos, kDataKey); old && old->is_object()) doc = *old;
    doc["loot"] = Json{{"table", tableId}, {"seed", seed}};
    bd::set(source, pos, kDataKey, doc);
    noteHopper(static_cast<int>(source.getDimensionId()), pos, true);
    if (hopperEnabled(source.getBlock(pos))) (void)unpackHopper(source, pos);
    return true;
}

/// The kit's listener of the hopper access dispatch: a pending hopper's table is unpacked before
/// its container is read or written by anything but its own tick, before it is removed, before a
/// comparator reads it, and at its own tick while it is enabled (HopperBlockEntity#tryMoveItems
/// needs ENABLED: a powered hopper's own tick leaves its container alone).
void onHopperAccess(BlockSource& source, BlockPos const& pos, hoppers::Access access) {
    if (tUnpacking || !hopperPending(static_cast<int>(source.getDimensionId()), pos)) return;
    if (access == hoppers::Access::Tick && !hopperEnabled(source.getBlock(pos))) return;
    (void)unpackHopper(source, pos);
}

bool runSpawner(BlockSource& source, BlockPos const& pos, Json const& action) {
    auto        entityId = action.value("entity", std::string{});
    auto const& data     = action.value("data", Json::object());
    auto const& b        = source.getBlock(pos);
    if (b.getTypeName() != "minecraft:mob_spawner") {
        // placeSpawner / setSpawner place the vanilla spawner (upstream's setSpawner places
        // Blocks.SPAWNER); a template's block entity data stays with the block the template put
        // there (StructureTemplate loads it into that block's own block entity)
        if (!action.value("place", true)) {
            // nothing there (a processor dropped it): vanilla has no block entity to load the data into
            if (b.isAir() || blocks::isLiquid(b)) return true;
            // a spawner block of its own (upstream's sinister spawner, SpawnerProcessor handles it like
            // the vanilla one): the block stays and its block entity (F8's, in block data) loads
            // the data as BaseSpawner#load does
            loadSinisterSpawner(source, pos, entityId, data);
            // what F8's spawner does not carry (SpawnData's generic.scale and custom spawn rules)
            // and the fields, for the kit's spawn events (Spawners.cpp)
            storeSpawnerRecord(source, pos, spawnerRecordOf(entityId, data, true));
            return true;
        }
        if (auto const* sp = game::blockWith("minecraft:mob_spawner")) game::setBlock(source, pos, *sp);
    }
    auto* actor = source.getBlockEntity(pos);
    if (!actor || actor->getType() != BlockActorType::MobSpawner) return false; // the actor is created a tick later
    auto& spawnerActor = static_cast<MobSpawnerBlockActor&>(*actor);
    if (!entityId.empty()) spawnerActor.setMob(source, ActorDefinitionIdentifier(entityId), nullptr);
    if (BaseMobSpawner* spawner = spawnerActor.mSpawner.get()) {
        if (!action.value("place", true)) {
            // a template's spawner data: BaseSpawner#load (SpawnerBlockEntity#loadAdditional)
            auto fields           = readSpawnerTag(data);
            spawner->mSpawnDelay = fields.delay;
            if (fields.delays) {
                spawner->mMinSpawnDelay = (*fields.delays)[0];
                spawner->mMaxSpawnDelay = (*fields.delays)[1];
                spawner->mSpawnCount    = (*fields.delays)[2];
            }
            if (fields.nearby) {
                spawner->mMaxNearbyEntities   = (*fields.nearby)[0];
                spawner->mRequiredPlayerRange = (*fields.nearby)[1];
            }
            if (fields.range) spawner->mSpawnRange = *fields.range;
        } else {
            // upstream's setSpawner: its modifier sets single fields of the new spawner
            auto field = [&](char const* name, auto& out) {
                if (nbtHasNumber(data, name)) out = nbtInt(data, name);
            };
            field("Delay", spawner->mSpawnDelay);
            field("MinSpawnDelay", spawner->mMinSpawnDelay);
            field("MaxSpawnDelay", spawner->mMaxSpawnDelay);
            field("SpawnCount", spawner->mSpawnCount);
            field("MaxNearbyEntities", spawner->mMaxNearbyEntities);
            field("RequiredPlayerRange", spawner->mRequiredPlayerRange);
            field("SpawnRange", spawner->mSpawnRange);
        }
    }
    storeSpawnerRecord(source, pos, spawnerRecordOf(entityId, data, false));
    actor->refresh(source);
    return true;
}

// ---- template entities ------------------------------------------------------------------------------

/// The game's DefaultDataLoadHelper: it holds no data, only its vtable (the way Levitate v0.1.0's
/// src/feature/schematics/World.cpp, LGPL-3.0-or-later, loads saved actors as they are).
DataLoadHelper& defaultLoadHelper() {
    static void** helper = DefaultDataLoadHelper::$vftable();
    return *reinterpret_cast<DataLoadHelper*>(&helper);
}

/// Faces an actor `yaw` / `pitch` (degrees; a mob's body and head too).
void applyRotation(Actor& actor, float yaw, float pitch) {
    auto& rotation     = *actor.mBuiltInComponents->mActorRotationComponent;
    rotation.mRot      = Vec2{pitch, yaw};
    rotation.mRotPrev  = Vec2{pitch, yaw};
    actor.setYHeadRotations(yaw, yaw);
    if (actor.hasCategory(ActorCategory::Mob)) static_cast<Mob&>(actor).setYBodyRotations(yaw, yaw);
}

/// StructureTemplate#placeEntities for one entity: the entity from its saved data (data.nbt, a
/// Bedrock actor tag in SNBT, as capture writes it: everything the actor saves, its position,
/// rotation and identity replaced) or a new one of its type, at its turned position and facing
/// data.Rotation (already turned by the placement).
bool runEntity(BlockSource& source, Json const& action) {
    auto id = action.value("id", std::string{});
    if (id == "minecraft:painting") return true; // (a job queued by an older build: paintings have their own job)
    auto level = ll::service::getLevel();
    if (!level) return false;
    double      x = action.value("x", 0.0), y = action.value("y", 0.0), z = action.value("z", 0.0);
    Vec3        at{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
    auto const& data = action.value("data", Json::object());
    std::optional<std::pair<float, float>> rotation; // yaw, pitch
    if (data.is_object() && data.contains("Rotation") && data["Rotation"].is_array() && data["Rotation"].size() == 2 && data["Rotation"][0].is_number() &&
        data["Rotation"][1].is_number())
        rotation = std::pair{data["Rotation"][0].get<float>(), data["Rotation"][1].get<float>()};
    if (data.is_object() && data.contains("nbt") && data["nbt"].is_string()) {
        auto parsed = CompoundTag::fromSnbt(data["nbt"].get<std::string>());
        if (!parsed) {
            warnOnce("entity-nbt:" + id, "structure: template entity " + id + " has unreadable nbt; spawned without its data");
        } else {
            CompoundTag& tag = *parsed;
            if (!tag.contains("identifier")) tag["identifier"] = id;
            tag["Pos"] = ListTag{at.x, at.y, at.z};
            if (rotation) tag["Rotation"] = ListTag{rotation->first, rotation->second};
            // a new entity: the game gives it its own id; riders and leashes of the original are not linked
            for (char const* k : {"UniqueID", "LinksTag", "LeasherID"}) tag.erase(k);
            auto owner = level->getActorFactory().loadActor(&tag, defaultLoadHelper(), source.getDimension().mHeightRange.get(), nullptr);
            if (owner && level->addEntity(source, std::move(owner))) return true;
            warnOnce("entity-load:" + id, "structure: could not load template entity " + id + " from its nbt; spawned without its data");
        }
    }
    Actor* actor = spawn::spawnMob(source, id, at, spawn::SpawnReason::Structure);
    if (!actor) actor = level->getSpawner().spawnMob(source, ActorDefinitionIdentifier(id), nullptr, at, false, false, false);
    if (!actor) {
        warnOnce("entity:" + id, "structure: could not spawn template entity " + id);
        return true;
    }
    if (rotation) applyRotation(*actor, rotation->first, rotation->second);
    return true;
}

/// LevelChunk#postProcessGeneration for one marked position: the block takes its shape from its
/// neighbours (Block.updateFromNeighbourShapes in UPDATE_SHAPE_ORDER: west, east, north, south,
/// down, up). Bedrock: what a structure block does after placing it (BlockType#onStructureBlockPlace:
/// walls fix their connection states), then a neighbour update from each side (blocks that keep
/// their shape in states, upstream's connected blocks through gf::blockevents, recompute it). A marked
/// block that can no longer stand breaks as a neighbour update breaks it on Bedrock (with its
/// drop); Java's updateShape turns it into air. upstream marks walls, fences and stairs, which always
/// stand.
void runUpdateShape(BlockSource& source, BlockPos const& pos) {
    if (source.getBlock(pos).isAir()) return;
    source.getBlock(pos).getBlockType().onStructureBlockPlace(source, pos);
    static constexpr int kOrder[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {0, -1, 0}, {0, 1, 0}};
    for (auto const& d : kOrder) {
        Block const& block = source.getBlock(pos);
        if (block.isAir()) return;
        block.neighborChanged(source, pos, BlockPos{pos.x + d[0], pos.y + d[1], pos.z + d[2]});
    }
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

/// A bed's colour (Java's <colour>_bed block; Bedrock: BedBlockActor#mDyeColor).
bool runBed(BlockSource& source, BlockPos const& pos, Json const& action) {
    if (source.getBlock(pos).getTypeName() != "minecraft:bed") return true; // replaced since
    auto* actor = source.getBlockEntity(pos);
    if (!actor || actor->getType() != BlockActorType::Bed) return false; // the actor is created a tick later
    auto& bed     = static_cast<BedBlockActor&>(*actor);
    bed.mDyeColor = action.value("color", 14);
    bed.mDirty    = true;
    bed.onChanged(source);
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
        if ((bid.empty() ? id : bid) == "minecraft:hopper") {
            // a hopper: the block, then the loot table the kit keeps for it
            Json props = Json::object();
            if (action.contains("direction")) props["facing"] = action["direction"];
            if (auto const* b = blocks::fromJava(id, props); b && &source.getBlock(pos) != b) game::setBlock(source, pos, *b);
            return setHopperLoot(source, pos, table, seed);
        }
        // the container and its loot table (RandomizableContainer#setLootTable): the loot
        // contract keeps the table with the container and rolls it when it is first opened
        if (loot::placeContainer(source, pos, bid.empty() ? id : bid, action.value("facing", 2), table, seed)) return true;
        // not a container the loot contract knows: the block stands without loot
        Json props = Json::object();
        if (action.contains("direction")) props["facing"] = action["direction"];
        if (auto const* b = blocks::fromJava(id, props)) {
            if (&source.getBlock(pos) != b) game::setBlock(source, pos, *b);
        }
        warnOnce("loot-container:" + id, "structure: " + id + " is not a container the loot contract knows; placed without loot table " + table);
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
        if (block.getTypeName() == "minecraft:hopper") return setHopperLoot(source, pos, table, seed);
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
        // upstream hangs a painting once its spot check passed; one the game refuses (no wall behind it)
        // drops off as an item, as upstream's painting does on its next survival check
        if (!spawnPainting(source, pos, dir, *motif)) dropPaintingItem(source, action.value("box", Json::array()));
        return true;
    }
    if (kind == "update_shape") { // (a job queued by an older build: shape updates are kept per chunk now)
        runUpdateShape(source, pos);
        return true;
    }
    if (kind == "flower_pot") return runFlowerPot(source, pos, action);
    if (kind == "bed") return runBed(source, pos, action);
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
    if (kind == "entity") return runEntity(source, action);
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
    queue(region, pos, Json{{"kind", "spawner"}, {"entity", entityId}, {"data", data}, {"place", true}});
}

void spawnerData(Region& region, BlockPos const& pos, std::string const& entityId, Json const& data) {
    queue(region, pos, Json{{"kind", "spawner"}, {"entity", entityId}, {"data", data}, {"place", false}});
}

void sign(Region& region, BlockPos const& pos, std::vector<std::string> const& lines) {
    queue(region, pos, Json{{"kind", "sign"}, {"lines", lines}});
}

void flowerPot(Region& region, BlockPos const& pos, std::string const& plantId) {
    if (plantId.empty()) return;
    queue(region, pos, Json{{"kind", "flower_pot"}, {"plant", plantId}});
}

void bedColor(Region& region, BlockPos const& pos, int color) {
    if (color < 0 || color > 15) return;
    queue(region, pos, Json{{"kind", "bed"}, {"color", color}});
}

void waterlog(Region& region, BlockPos const& pos, std::string const& blockId) {
    if (region.dimension() < 0 || sourceOf(region)) return; // live regions set the liquid layer themselves
    std::lock_guard lock(gJobsMutex);
    pushLocked(Job{region.dimension(), pos, Json{{"kind", "waterlog"}, {"block", blockId}}});
}

bool entity(Region& region, double x, double y, double z, std::string const& entityId, Json const& data) {
    if (entityId.empty()) return false;
    if (entityId == "minecraft:painting") {
        // not a mob: paintings hang through paintings::tryHang (upstream's spot check, the motif, the facing)
        warnOnce("entity-painting", "structure: a painting was passed as an entity; paintings hang through paintings::tryHang");
        return false;
    }
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
        // EntityUtil.tryHangPainting is true once upstream's spot check passed (the caller's): upstream adds
        // the painting even without a wall behind it and it drops off as an item on its next
        // survival check; the game does not hang such a painting at all, so the item drops now.
        // Either way the result (and so upstream's retry loop) is upstream's.
        if (!spawnPainting(*source, pos, direction, *motif)) dropPaintingItem(*source, box);
        return true;
    }
    pushPainting(Job{region.dimension(), pos, Json{{"kind", "painting"}, {"motive", motive}, {"direction", blocks::directionName(direction)}, {"box", box}}});
    return true;
}

// ---- structure paintings keep Java's survival check ------------------------------------------------
// An engine hook outside the W0 contracts (no contract dispatches HangingActor's survival check;
// disclosed for A8): the game checks a hanging actor's spot every few seconds and drops it when its
// own rules refuse it. For the paintings the kit hung (kJavaPaintingTag) Java's
// HangingEntity#survives decides instead, as in upstream; every other painting keeps the game's rules.

LL_TYPE_INSTANCE_HOOK(
    StructurePaintingSurvivalHook,
    ll::memory::HookPriority::Normal,
    HangingActor,
    &HangingActor::$wouldSurvive,
    bool,
    ::BlockSource& region
) {
    if (!isClientSide() && isType(ActorType::Painting) && hasTags() && hasTag(kJavaPaintingTag)) {
        auto hung    = describePainting(*this);
        auto variant = hung ? paintings::byId(hung->variant) : std::nullopt;
        if (hung && variant) return javaSurvives(region, hung->block, hung->facing, *variant, this);
    }
    return origin(region);
}

namespace {
bool gPaintingHooked = false;
} // namespace

std::optional<bool> javaSurvives(Actor& actor) {
    auto hung    = describePainting(actor);
    auto variant = hung ? paintings::byId(hung->variant) : std::nullopt;
    if (!hung || !variant) return std::nullopt;
    return javaSurvives(actor.getDimensionBlockSource(), hung->block, hung->facing, *variant, &actor);
}

void install() {
    static bool handlers = false;
    if (!handlers) {
        handlers = true;
        // a player opening a powered hopper: RandomizableContainer#unpackLootTable(player) from
        // createMenu, before vanilla opens it
        namespace ia = interactions;
        ia::onUseOn({.block = "minecraft:hopper"}, [](ia::UseOnContext const& c) {
            if (!c.firstEvent || (c.sneaking && !c.itemId.empty())) return ia::Result::Pass;
            if (auto* region = game::regionOf(c.pos.dim)) (void)unpackHopper(*region, game::blockPos(c.pos));
            return ia::Result::Pass;
        }, ia::priority::High);
        // a player breaking it (the hopper access dispatch's removal covers every other cause)
        ia::onDestroy({.block = "minecraft:hopper"}, [](ia::DestroyContext const& c) {
            if (auto* region = game::regionOf(c.pos.dim)) (void)unpackHopper(*region, game::blockPos(c.pos));
            return ia::Result::Pass;
        }, ia::priority::High);
        // a record goes with its block: the block data of a changed block is erased (the hopper's
        // table, the spawner record), a moved block's data moves with it (take, then put)
        bd::onErase([](core::GlobalPos const& pos, Json const& doc, std::string const&) {
            auto it = doc.find(kDataKey);
            if (it == doc.end() || !it->is_object()) return;
            BlockPos p{pos.x, pos.y, pos.z};
            if (it->contains("loot")) noteHopper(pos.dim, p, false);
            if (it->contains("spawner")) indexSpawnerRecord(pos.dim, p, false);
        });
        bd::onWriteBack([](core::GlobalPos const* pos) {
            // (before every block data write: the hopper list goes first)
            if (!pos) return storeHoppers(true);
            BlockPos p{pos->x, pos->y, pos->z};
            noteHopper(pos->dim, p, false);
            indexSpawnerRecord(pos->dim, p, false);
        });
        // any other access to a pending hopper (the hopper access dispatch: transfers, container
        // reads, comparators, removal by any cause, its own tick while enabled)
        hoppers::onAccess(onHopperAccess);
        bd::onPut([](core::GlobalPos const& pos, Json const& doc) {
            auto it = doc.find(kDataKey);
            if (it == doc.end() || !it->is_object()) return;
            BlockPos p{pos.x, pos.y, pos.z};
            if (it->contains("loot")) noteHopper(pos.dim, p, true);
            if (it->contains("spawner")) indexSpawnerRecord(pos.dim, p, true);
        });
    }
    if (gPaintingHooked) return;
    StructurePaintingSurvivalHook::hook();
    gPaintingHooked = true;
}

void uninstall() {
    if (!gPaintingHooked) return;
    StructurePaintingSurvivalHook::unhook();
    gPaintingHooked = false;
}

bool pendingPaintingIntersects(int dim, double const (&box)[6]) {
    std::shared_lock lock(gPaintingsMutex);
    if (gPendingPaintings.empty()) return false;
    // only the chunks the box can reach (a painting is at most 4 blocks wide)
    for (int cx = core::chunkOf(pure::mthFloor(box[0])) - 1; cx <= core::chunkOf(pure::mthFloor(box[3])) + 1; cx++)
        for (int cz = core::chunkOf(pure::mthFloor(box[2])) - 1; cz <= core::chunkOf(pure::mthFloor(box[5])) + 1; cz++) {
            auto it = gPendingPaintings.find(ChunkKey{dim, cx, cz});
            if (it == gPendingPaintings.end()) continue;
            for (auto const& p : it->second) {
                double const* b = p.box;
                // AABB#intersects: strictly overlapping
                if (box[0] < b[3] && box[3] > b[0] && box[1] < b[4] && box[4] > b[1] && box[2] < b[5] && box[5] > b[2]) return true;
            }
        }
    return false;
}

void updateShape(Region& region, BlockPos const& pos) {
    if (region.dimension() < 0) return; // scratch regions keep no world
    // always queued, live regions too: Java post-processes once the chunk is complete, after every
    // piece of the structure stands
    int dim = region.dimension();
    if (auto* source = sourceOf(region)) dim = static_cast<int>(source->getDimensionId());
    std::lock_guard lock(gJobsMutex);
    pushShapeLocked(dim, pos);
}

std::optional<HungPainting> describePainting(Actor& actor) {
    if (!actor.isType(ActorType::Painting)) return std::nullopt;
    auto& painting = static_cast<Painting&>(actor);
    auto  id       = variantOf(painting.mMotif);
    auto  variant  = paintings::byId(id);
    if (!variant) return std::nullopt;
    Orientation facing = from2D(painting.mDirection);
    // the covered block from the box center (Painting#calculateBoundingBox backwards)
    AABB const& b      = actor.getAABB();
    double      cx     = (b.min.x + b.max.x) / 2.0, cy = (b.min.y + b.max.y) / 2.0, cz = (b.min.z + b.max.z) / 2.0;
    auto        offset = [](int size) { return size % 2 == 0 ? 0.5 : 0.0; };
    Orientation ccw    = static_cast<Orientation>((static_cast<int>(facing) + 3) & 3);
    cx += stepX(facing) * 0.46875 - stepX(ccw) * offset(variant->width);
    cz += stepZ(facing) * 0.46875 - stepZ(ccw) * offset(variant->width);
    cy -= offset(variant->height);
    return HungPainting{id, facing, BlockPos{pure::mthFloor(cx), pure::mthFloor(cy), pure::mthFloor(cz)}};
}

std::optional<std::array<double, 6>> paintingBoxFor(BlockPos const& placement, Orientation direction, std::string const& motive) {
    auto const* motif = motifFor(motive);
    if (!motif) return std::nullopt;
    std::optional<std::array<double, 6>> out;
    (void)withPainting(placement, direction, *motif, [&](Painting& painting, auto&) {
        AABB const& b = painting.getAABB();
        out           = std::array<double, 6>{b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z};
        return true;
    });
    return out;
}

BlockPos paintingPlacement(BlockPos const& covered, Orientation direction) { return placementPosition(covered, direction); }

std::optional<Json> spawnerRecord(BlockSource& source, BlockPos const& pos) {
    auto const* d = bd::get(source, pos, kDataKey);
    if (!d || !d->is_object() || !d->contains("spawner") || !(*d)["spawner"].is_object()) return std::nullopt;
    return (*d)["spawner"];
}

void forgetSpawnerRecord(BlockSource& source, BlockPos const& pos) {
    if (spawnerRecord(source, pos)) storeSpawnerRecord(source, pos, Json::object());
}

std::vector<BlockPos> spawnerRecordsIn(BlockSource& source, int cx, int cz) {
    int   dim = static_cast<int>(source.getDimensionId());
    auto& idx = spawnerIndex();
    std::set<std::tuple<int, int, int>> positions;
    bool                                known = false;
    {
        std::lock_guard lock(idx.mutex);
        if (auto it = idx.byChunk.find({dim, cx, cz}); it != idx.byChunk.end()) {
            positions = it->second;
            known     = true;
        }
    }
    if (!known) {
        // the chunk's block data, read once (a hopper table found there is watched too, should the
        // stored hopper list have missed it)
        bd::forEachInChunk(dim, cx, cz, [&](core::GlobalPos const& gp, Json const& doc) {
            auto it = doc.find(kDataKey);
            if (it == doc.end() || !it->is_object()) return;
            if (it->contains("spawner")) positions.insert({gp.x, gp.y, gp.z});
            if (it->contains("loot")) noteHopper(gp.dim, BlockPos{gp.x, gp.y, gp.z}, true);
        });
        std::lock_guard lock(idx.mutex);
        idx.byChunk.emplace(std::tuple<int, int, int>{dim, cx, cz}, positions);
    }
    // (the record writes and the block data listeners keep the index: no block data is read here)
    std::vector<BlockPos> out;
    out.reserve(positions.size());
    for (auto const& [x, y, z] : positions) out.push_back(BlockPos{x, y, z});
    return out;
}

std::optional<std::pair<std::string, std::int64_t>> pendingHopperLoot(BlockSource& source, BlockPos const& pos) {
    auto const* d = bd::get(source, pos, kDataKey);
    if (!d || !d->is_object() || !d->contains("loot") || !(*d)["loot"].is_object()) return std::nullopt;
    auto const& l = (*d)["loot"];
    return std::pair<std::string, std::int64_t>{l.value("table", std::string{}), l.value("seed", std::int64_t{0})};
}

bool unpackHopper(BlockSource& source, BlockPos const& pos) {
    int  dim     = static_cast<int>(source.getDimensionId());
    auto pending = pendingHopperLoot(source, pos);
    if (!pending) {
        noteHopper(dim, pos, false);
        return false;
    }
    auto dropTable = [&] {
        noteHopper(dim, pos, false);
        Json doc = Json::object();
        if (auto const* d = bd::get(source, pos, kDataKey); d && d->is_object()) doc = *d;
        doc.erase("loot");
        bd::set(source, pos, kDataKey, doc.empty() ? Json() : doc);
    };
    auto* actor = source.getBlockEntity(pos);
    if (!actor || !isHopper(*actor)) {
        // a hopper whose block actor is not there yet keeps its table; a block that is no longer a
        // hopper (its block data outlived it: a change no block event reported) drops it
        if (source.hasChunksAt(pos, 0, false) && source.getBlock(pos).getTypeName() != "minecraft:hopper") dropTable();
        return false;
    }
    // RandomizableContainer#unpackLootTable: the table goes first, then the fill
    dropTable();
    if (!pending->first.empty()) {
        // (the fill's own slot reads and writes unpack nothing)
        struct Guard {
            bool outer = tUnpacking;
            Guard() { tUnpacking = true; }
            ~Guard() { tUnpacking = outer; }
        } guard;
        fillHopper(source, pos, static_cast<Container&>(static_cast<HopperBlockActor&>(*actor)), pending->first, pending->second);
    }
    return true;
}

std::size_t pendingHoppers() {
    std::lock_guard lock(gHoppersMutex);
    return gHoppers.size();
}

void flush() {
    if (gTicks % kIndexSweepInterval == 0) sweepSpawnerIndex();
    if (++gTicks % kFlushInterval != 0) return;
    // the chunks with work whose chunk is loaded now
    std::vector<std::pair<ChunkKey, ChunkWork>> ready;
    {
        std::lock_guard lock(gJobsMutex);
        if (gWork.empty()) return;
        for (auto it = gWork.begin(); it != gWork.end();) {
            auto [dim, cx, cz] = it->first;
            auto* source       = game::regionOf(dim);
            if (!source || !source->hasChunksAt(BlockPos{cx * 16 + 8, source->getMinHeight(), cz * 16 + 8}, 0, false)) {
                ++it;
                continue;
            }
            gDirtyChunks.insert(it->first);
            ready.emplace_back(it->first, std::move(it->second));
            it = gWork.erase(it);
        }
    }
    if (ready.empty()) return;
    std::vector<Job> keep;
    for (auto& [key, work] : ready) {
        auto* source = game::regionOf(std::get<0>(key));
        if (!source) {
            for (auto& job : work.jobs) keep.push_back(std::move(job));
            std::lock_guard lock(gJobsMutex);
            for (auto s : work.shapes) pushShapeLocked(std::get<0>(key), unpackShape(key, s));
            continue;
        }
        // the block entities and entities of the chunk first (a LevelChunk is made of the
        // ProtoChunk with them), then its post-processing (LevelChunk#postProcessGeneration)
        for (auto& job : work.jobs) {
            if (run(*source, job.pos, job.action)) {
                if (job.action.contains("pid")) unindexPainting(job.dim, job.pos, job.action);
                continue;
            }
            int tries = job.action.value("tries", 0);
            if (tries < 20) {
                job.action["tries"] = tries + 1;
                keep.push_back(std::move(job));
            } else {
                if (job.action.contains("pid")) unindexPainting(job.dim, job.pos, job.action);
                warnOnce("job-dropped:" + job.action.value("kind", std::string{}),
                         "structure: gave up a queued " + job.action.value("kind", std::string{}) + " at " + std::to_string(job.pos.x) + " " +
                             std::to_string(job.pos.y) + " " + std::to_string(job.pos.z));
            }
        }
        // section by section from the bottom, in the order the positions were marked
        std::stable_sort(work.shapes.begin(), work.shapes.end(), [](std::uint32_t a, std::uint32_t b) { return shapeSection(a) < shapeSection(b); });
        for (auto s : work.shapes) runUpdateShape(*source, unpackShape(key, s));
    }
    if (keep.empty()) return;
    std::lock_guard lock(gJobsMutex);
    // retried jobs keep their order and their place before work queued meanwhile
    for (auto it = keep.rbegin(); it != keep.rend(); ++it) {
        auto key = chunkKey(it->dim, it->pos);
        gWork[key].jobs.push_front(std::move(*it));
        gDirtyChunks.insert(key);
    }
}

std::size_t pending() {
    std::lock_guard lock(gJobsMutex);
    std::size_t     n = 0;
    for (auto const& [key, work] : gWork) n += work.jobs.size() + work.shapes.size();
    return n;
}

void queueTestMark(int dim, BlockPos const& pos, std::string const& nonce) {
    std::lock_guard lock(gJobsMutex);
    pushLocked(Job{dim, pos, Json{{"kind", "gf_test_mark"}, {"nonce", nonce}}});
}

namespace {

/// Queues a stored job (a shape update of an older build goes to its chunk's list).
void restoreLocked(Job job) {
    job.action.erase("pid"); // index ids are per session
    auto kind = job.action.value("kind", std::string{});
    if (kind == "update_shape") {
        pushShapeLocked(job.dim, job.pos);
        return;
    }
    if (kind == "painting")
        if (auto id = indexPainting(job.dim, job.pos, job.action.value("box", Json::array()))) job.action["pid"] = id;
    pushLocked(std::move(job));
}

} // namespace

void load() {
    {
        std::unique_lock lock(gPaintingsMutex);
        gPendingPaintings.clear();
    }
    {
        // (a world opened or closed: its chunks' block data is read again)
        auto&           idx = spawnerIndex();
        std::lock_guard lock(idx.mutex);
        idx.byChunk.clear();
    }
    {
        // the hoppers with a pending loot table (a world opened or closed: the old ones unwatched)
        std::optional<Json> stored;
        if (game::db::available()) stored = game::db::get(kHoppersKey);
        std::lock_guard lock(gHoppersMutex);
        for (auto const& [x, y, z, dim] : gHoppers) hoppers::unwatch(dim, BlockPos{x, y, z});
        gHoppers.clear();
        gHoppersRemoved.clear();
        gHoppersDirty = false;
        if (stored && stored->is_array())
            for (auto const& h : *stored)
                if (h.is_array() && h.size() == 4 && std::all_of(h.begin(), h.end(), [](Json const& v) { return v.is_number_integer(); }) &&
                    gHoppers.insert(HopperKey{h[1].get<int>(), h[2].get<int>(), h[3].get<int>(), h[0].get<int>()}).second)
                    hoppers::watch(h[0].get<int>(), BlockPos{h[1].get<int>(), h[2].get<int>(), h[3].get<int>()});
    }
    // the chunk records (collected first: nothing is read while the DB iterates)
    std::vector<ChunkKey> keys;
    game::db::forEachKey(kPendingPrefix, [&](std::string_view suffix) {
        int dim = 0, cx = 0, cz = 0;
        if (core::parseChunkSuffix(suffix, dim, cx, cz)) keys.emplace_back(dim, cx, cz);
    });
    std::vector<std::pair<ChunkKey, Json>> docs;
    for (auto const& key : keys)
        if (auto doc = game::db::get(pendingKey(key))) docs.emplace_back(key, std::move(*doc));
    auto legacy = game::db::get(kLegacyPendingKey);

    std::lock_guard lock(gJobsMutex);
    gWork.clear();
    gDirtyChunks.clear();
    gLegacyStored = false;
    std::set<ChunkKey> stale; ///< stored records with nothing usable in them (erased at the next save)
    for (auto const& [key, doc] : docs) {
        if (doc.is_object()) {
            for (auto const& j : doc.value("jobs", Json::array()))
                restoreLocked(Job{std::get<0>(key), BlockPos{j.value("x", 0), j.value("y", 0), j.value("z", 0)}, j.value("action", Json::object())});
            for (auto const& p : doc.value("shape", Json::array())) {
                if (!p.is_number_unsigned()) continue;
                auto& work = gWork[key];
                if (work.shapeSet.insert(p.get<std::uint32_t>()).second) work.shapes.push_back(p.get<std::uint32_t>());
            }
        }
        if (auto it = gWork.find(key); it == gWork.end() || it->second.empty()) {
            if (it != gWork.end()) gWork.erase(it);
            stale.insert(key);
        }
    }
    // records just read are up to date; the single array of older builds moves into chunk records
    gDirtyChunks = std::move(stale);
    if (legacy) {
        gLegacyStored = true;
        if (legacy->is_array())
            for (auto const& j : *legacy)
                restoreLocked(Job{j.value("dim", 0), BlockPos{j.value("x", 0), j.value("y", 0), j.value("z", 0)}, j.value("action", Json::object())});
    }
}

void save() {
    std::vector<std::pair<std::string, std::optional<Json>>> writes;
    bool                                                     eraseLegacy = false;
    // the hoppers with a pending loot table, [dim, x, y, z] each (also written by the block data's
    // write-back, right before the block data)
    storeHoppers(false);
    {
        std::lock_guard lock(gJobsMutex);
        // only the chunks whose work changed since the last save
        for (auto const& key : gDirtyChunks) {
            auto it = gWork.find(key);
            if (it == gWork.end() || it->second.empty()) {
                writes.emplace_back(pendingKey(key), std::nullopt);
                continue;
            }
            Json jobs = Json::array();
            for (auto const& j : it->second.jobs) {
                Json action = j.action;
                action.erase("pid");
                jobs.push_back(Json{{"x", j.pos.x}, {"y", j.pos.y}, {"z", j.pos.z}, {"action", std::move(action)}});
            }
            writes.emplace_back(pendingKey(key), Json{{"jobs", std::move(jobs)}, {"shape", it->second.shapes}});
        }
        gDirtyChunks.clear();
        eraseLegacy   = gLegacyStored;
        gLegacyStored = false;
    }
    for (auto const& [key, doc] : writes) {
        if (doc) game::db::put(key, *doc);
        else game::db::erase(key);
    }
    if (eraseLegacy) game::db::erase(kLegacyPendingKey);
}

} // namespace blockentities
} // namespace gf::structure
