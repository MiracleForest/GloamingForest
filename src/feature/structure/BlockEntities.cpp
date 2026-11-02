// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/util/entities/EntityUtil.java (tryHangPainting: the spawn)
//   src/main/java/twilightforest/block/entity/DryingRackBlockEntity.java (fillFromLootTable)
//   src/main/java/twilightforest/world/components/processors/UpdateMarkingProcessor.java (the post-processing it marks)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
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
#include <atomic>
#include <cmath>
#include <cstdio>
#include <deque>
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
#include "mc/world/level/block/actor/FlowerPotBlockActor.h"
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
#include "feature/ext/entity/EntityExt.h"
#include "feature/inventory/Inventory.h"
#include "feature/loot/Loot.h"
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

namespace bd = ::gf::feature::common::blockdata;

/// One record per chunk with queued work: {"jobs": [{x, y, z, action}], "shape": [packed positions]}.
constexpr char kPendingPrefix[] = "gf:structure:pending:";
/// Older builds kept every job in one array under this key (read once, then erased).
constexpr char kLegacyPendingKey[] = "gf:structure:pending";
constexpr char kTestMarkKey[]      = "gf:structure:test_mark";
/// BlockData key: {"spawner": {entity, scale?, spawn_rules?, data?}} (loot tables live with the
/// containers: the loot contract keeps them)
constexpr char kDataKey[]     = "structure";
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

// ---- queued paintings (the ProtoChunk entities TF's painting check sees) ----------------------------------

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
/// which hangs paintings and compares their boxes with TF's.
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
/// disagree on a painting TF's spot check accepted (`java`: Java's verdict; the game's is the
/// other): TF's box and the game's, and for every block the painting covers the block there and
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
        std::snprintf(buf, sizeof(buf), ", TF box %.4f %.4f %.4f .. %.4f %.4f %.4f", tf[0], tf[1], tf[2], tf[3], tf[4], tf[5]);
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
/// 2D data value, which Bedrock's HangingActor direction shares). TF adds the painting once its
/// spot check passed and Java keeps it while HangingEntity#survives holds: the painting hangs when
/// Java's check holds (the game's own check is not asked; it refuses spots Java keeps, see
/// reportRefusal), and keeps Java's check for good (kJavaPaintingTag). False when Java's check
/// fails (the caller drops the item, as TF's painting drops on its next check).
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

/// Painting#dropItem when a painting cannot stay (TF hangs it anyway and it drops off): a painting
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
/// block the kit does not drive (`withFields`: TF's sinister spawner), the BaseSpawner fields of
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

/// Replaces the spawner part of the BlockData at `pos` (nothing kept: removed).
void storeSpawnerRecord(BlockSource& source, BlockPos const& pos, Json const& record) {
    Json doc = Json::object();
    if (auto const* old = bd::get(source, pos, kDataKey); old && old->is_object()) doc = *old;
    doc.erase("spawner");
    if (!record.empty()) doc["spawner"] = record;
    bd::set(source, pos, kDataKey, doc.empty() ? Json() : doc);
}

/// BaseSpawner#load (SinisterSpawnerBlockEntity#loadAdditional) of a template's spawner data into
/// the sinister spawner block entity F8 keeps at `pos` (nothing for any other block): Delay,
/// SpawnData (the next mob; without SpawnPotentials the only potential, weight 1), MinSpawnDelay,
/// MaxSpawnDelay, SpawnCount, MaxNearbyEntities, RequiredPlayerRange and SpawnRange when given;
/// what the data leaves out keeps the spawner's value (a template saves every field).
void loadSinisterSpawner(BlockSource& source, BlockPos const& pos, std::string const& entityId, Json const& data) {
    auto spawner = boss::ext::sinisterSpawner(source, pos);
    if (!spawner) return;
    if (!entityId.empty()) {
        spawner->nextEntity = entityId;
        spawner->potentials = {{entityId, 1}};
    }
    auto field = [&](char const* name, int& out) {
        if (data.contains(name) && data[name].is_number_integer()) out = data[name].get<int>();
    };
    field("Delay", spawner->spawnDelay);
    field("MinSpawnDelay", spawner->minSpawnDelay);
    field("MaxSpawnDelay", spawner->maxSpawnDelay);
    field("SpawnCount", spawner->spawnCount);
    field("MaxNearbyEntities", spawner->maxNearbyEntities);
    field("RequiredPlayerRange", spawner->requiredPlayerRange);
    field("SpawnRange", spawner->spawnRange);
    if (!boss::ext::setSinisterSpawner(source, pos, *spawner))
        warnOnce("sinister:" + source.getBlock(pos).getTypeName(), "structure: the sinister spawner refused its template data");
}

bool runSpawner(BlockSource& source, BlockPos const& pos, Json const& action) {
    auto        entityId = action.value("entity", std::string{});
    auto const& data     = action.value("data", Json::object());
    auto const& b        = source.getBlock(pos);
    if (b.getTypeName() != "minecraft:mob_spawner") {
        // placeSpawner / setSpawner place the vanilla spawner (TF's setSpawner places
        // Blocks.SPAWNER); a template's block entity data stays with the block the template put
        // there (StructureTemplate loads it into that block's own block entity)
        if (!action.value("place", true)) {
            // nothing there (a processor dropped it): vanilla has no block entity to load the data into
            if (b.isAir() || blocks::isLiquid(b)) return true;
            // a spawner block of its own (TF's sinister spawner, SpawnerProcessor handles it like
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
        if (data.contains("Delay")) spawner->mSpawnDelay = data["Delay"].get<int>();
        if (data.contains("MinSpawnDelay")) spawner->mMinSpawnDelay = data["MinSpawnDelay"].get<int>();
        if (data.contains("MaxSpawnDelay")) spawner->mMaxSpawnDelay = data["MaxSpawnDelay"].get<int>();
        if (data.contains("SpawnCount")) spawner->mSpawnCount = data["SpawnCount"].get<int>();
        if (data.contains("MaxNearbyEntities")) spawner->mMaxNearbyEntities = data["MaxNearbyEntities"].get<int>();
        if (data.contains("RequiredPlayerRange")) spawner->mRequiredPlayerRange = data["RequiredPlayerRange"].get<int>();
        if (data.contains("SpawnRange")) spawner->mSpawnRange = data["SpawnRange"].get<int>();
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
/// their shape in states, TF's connected blocks through gf::blockevents, recompute it). A marked
/// block that can no longer stand breaks as a neighbour update breaks it on Bedrock (with its
/// drop); Java's updateShape turns it into air. TF marks walls, fences and stairs, which always
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

bool run(BlockSource& source, BlockPos const& pos, Json const& action) {
    auto kind = action.value("kind", std::string{});
    if (kind == "container") {
        auto id    = action.value("block", std::string("minecraft:chest"));
        auto table = action.value("table", std::string{});
        auto seed  = action.value("seed", std::int64_t{0});
        auto bid   = blocks::bedrockId(id);
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
        // TF hangs a painting once its spot check passed; one the game refuses (no wall behind it)
        // drops off as an item, as TF's painting does on its next survival check
        if (!spawnPainting(source, pos, dir, *motif)) dropPaintingItem(source, action.value("box", Json::array()));
        return true;
    }
    if (kind == "update_shape") { // (a job queued by an older build: shape updates are kept per chunk now)
        runUpdateShape(source, pos);
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

void waterlog(Region& region, BlockPos const& pos, std::string const& blockId) {
    if (region.dimension() < 0 || sourceOf(region)) return; // live regions set the liquid layer themselves
    std::lock_guard lock(gJobsMutex);
    pushLocked(Job{region.dimension(), pos, Json{{"kind", "waterlog"}, {"block", blockId}}});
}

bool entity(Region& region, double x, double y, double z, std::string const& entityId, Json const& data) {
    if (entityId.empty()) return false;
    if (entityId == "minecraft:painting") {
        // not a mob: paintings hang through paintings::tryHang (TF's spot check, the motif, the facing)
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
        // EntityUtil.tryHangPainting is true once TF's spot check passed (the caller's): TF adds
        // the painting even without a wall behind it and it drops off as an item on its next
        // survival check; the game does not hang such a painting at all, so the item drops now.
        // Either way the result (and so TF's retry loop) is TF's.
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
// HangingEntity#survives decides instead, as in TF; every other painting keeps the game's rules.

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

void flush() {
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
    {
        std::lock_guard lock(gJobsMutex);
        if (gDirtyChunks.empty() && !gLegacyStored) return;
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
