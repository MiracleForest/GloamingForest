// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Selftests of U18 (minorlandmarks), testPos dx 1800..1899 (rule R12):
//   minorlandmarks.hedge_damage          the hedge's contact and punch damage, spiders spared
//   minorlandmarks.hollow_hill_layout    the three hills in memory (golden hashes), the TFHill tag
//   minorlandmarks.hedge_maze_layout     the hedge maze in memory (golden hash)
//   minorlandmarks.hedge_maze_world      a forced hedge maze start in the world: rooms, spawners, chests
//   minorlandmarks.hollow_tree_layout    a hollow tree in memory (golden hash), its piece tags
//   minorlandmarks.hollow_tree_sapling   landmark::generateAt grows a hollow tree in the world
//   minorlandmarks.fallen_trunk          the three trunk sizes in memory (golden hashes), the terraformer
//   minorlandmarks.features              every feature type in memory (golden hashes)
//   minorlandmarks.monolith_ravens       a monolith in the world and its two ravens
//   minorlandmarks.registered            generators, piece types and descriptors of the seven structures
//   minorlandmarks.client_showcase       (client acceptance) the structures and the hedge in the world
#include "feature/minorlandmarks/Internal.h"

#include <climits>
#include <cmath>
#include <map>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "core/VolumeHash.h"
#include "core/jrand/JRand.h"
#include "feature/common/Interactions.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/landmark/LandmarkTest.h"
#include "feature/ext/structure/Placement.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/features/Features.h"
#include "feature/landmark/Landmark.h"
#include "feature/spawn/Spawn.h"
#include "game/Blocks.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace gf::feature::minorlandmarks {

namespace {

using selftest::Context;

// ---- an in-memory region ------------------------------------------------------------------------------

class MemoryRegion final : public worldgen::Region {
public:
    explicit MemoryRegion(std::int64_t seed = 20240611) : mSeed(seed) { st::ext::regionStarted(*this); }
    [[nodiscard]] int          centerChunkX() const override { return 0; }
    [[nodiscard]] int          centerChunkZ() const override { return 0; }
    [[nodiscard]] int          minY() const override { return -32; }
    [[nodiscard]] int          maxY() const override { return 256; }
    [[nodiscard]] bool         isInside(int, int) const override { return true; }
    [[nodiscard]] Block const& getBlock(BlockPos const& pos) const override {
        auto it = mBlocks.find(key(pos.x, pos.y, pos.z));
        return it == mBlocks.end() ? st::blocks::air() : *it->second;
    }
    bool setBlock(BlockPos const& pos, Block const& block, int) override {
        if (pos.y < minY() || pos.y >= maxY()) return false;
        if (block.isAir()) mBlocks.erase(key(pos.x, pos.y, pos.z));
        else {
            mBlocks[key(pos.x, pos.y, pos.z)] = &block;
            auto& top                         = mTop[colKey(pos.x, pos.z)];
            if (!mTop.contains(colKey(pos.x, pos.z)) || top < pos.y) top = pos.y;
        }
        return true;
    }
    [[nodiscard]] int height(worldgen::Heightmap type, int x, int z) const override {
        auto it = mTop.find(colKey(x, z));
        if (it == mTop.end()) return minY();
        for (int y = it->second; y >= minY(); y--) {
            Block const& b = getBlock(BlockPos{x, y, z});
            if (b.isAir()) continue;
            bool liquid = st::blocks::isLiquid(b);
            switch (type) {
            case worldgen::Heightmap::WorldSurfaceWg:
            case worldgen::Heightmap::WorldSurface: return y + 1;
            case worldgen::Heightmap::OceanFloorWg:
            case worldgen::Heightmap::OceanFloor:
                if (!liquid && !st::blocks::canBeReplaced(b)) return y + 1;
                break;
            default:
                if (liquid || !st::blocks::canBeReplaced(b)) return y + 1;
            }
        }
        return minY();
    }
    [[nodiscard]] std::string  biome(BlockPos const&) const override { return "twilightforest:forest"; }
    [[nodiscard]] std::int64_t seed() const override { return mSeed; }
    [[nodiscard]] int          dimension() const override { return -1; } // scratch: no block entity work

    /// The golden volume hash of `box` (snapshotVolume's order and text; TF blocks by id only).
    [[nodiscard]] std::string hash(BlockBox const& box) const {
        core::VolumeHasher h;
        for (int y = box.minY; y <= box.maxY; y++)
            for (int z = box.minZ; z <= box.maxZ; z++)
                for (int x = box.minX; x <= box.maxX; x++) {
                    auto const& b = getBlock(BlockPos{x, y, z});
                    if (b.getTypeName().starts_with("minecraft:")) h.add(x - box.minX, y - box.minY, z - box.minZ, b.getTypeName(), game::statesOf(b));
                    else h.add(x - box.minX, y - box.minY, z - box.minZ, b.getTypeName());
                }
        return h.hex();
    }
    [[nodiscard]] int count(BlockBox const& box, std::string const& bedrockId) const {
        int n = 0;
        for (auto const& [k, b] : mBlocks) {
            int x = static_cast<int>(k >> 40) - (1 << 23), y = static_cast<int>((k >> 20) & 0xfffff) - (1 << 19), z = static_cast<int>(k & 0xfffff) - (1 << 19);
            if (box.contains(x, y, z) && b->getTypeName() == bedrockId) ++n;
        }
        return n;
    }
    [[nodiscard]] std::size_t size() const { return mBlocks.size(); }

private:
    static long long key(int x, int y, int z) {
        return (static_cast<long long>(x + (1 << 23)) << 40) | (static_cast<long long>(y + (1 << 19)) << 20) | static_cast<long long>(z + (1 << 19));
    }
    static long long colKey(int x, int z) { return (static_cast<long long>(x) << 32) ^ static_cast<unsigned int>(z); }
    std::unordered_map<long long, Block const*> mBlocks;
    std::unordered_map<long long, int>          mTop;
    std::int64_t                                mSeed;
};

/// The golden volume hashes (reviewed once with screenshots, rule R19).
std::map<std::string, std::string> const& goldens() {
    static std::map<std::string, std::string> const g = {
#include "feature/minorlandmarks/Goldens.inc"
    };
    return g;
}

void checkGolden(Context& ctx, std::string const& name, std::string const& actual) {
    auto it = goldens().find(name);
    ctx.info("golden " + name + " = " + actual);
    ctx.check(it != goldens().end() && it->second == actual,
              "golden hash " + name + ": expected " + (it == goldens().end() ? std::string("<unset>") : it->second) + ", got " + actual);
}

/// A flat ground: stone up to y - 3, dirt, then grass at `y` over the box's columns.
void flatGround(MemoryRegion& mem, BlockBox const& box, int y) {
    auto const& stone = st::blocks::java("minecraft:stone");
    auto const& dirt  = st::blocks::java("minecraft:dirt");
    auto const& grass = st::blocks::java("minecraft:grass_block");
    for (int x = box.minX; x <= box.maxX; x++)
        for (int z = box.minZ; z <= box.maxZ; z++)
            for (int yy = y - 6; yy <= y; yy++) mem.setBlock(BlockPos{x, yy, z}, yy == y ? grass : (yy >= y - 2 ? dirt : stone), 2);
}

/// Places pieces chunk by chunk (StructureStart#placeInChunk: each chunk with its own random, the
/// pivot the first piece's box center at its minY).
void placeByChunks(MemoryRegion& mem, std::vector<std::unique_ptr<st::Piece>> const& pieces, std::int64_t seed) {
    if (pieces.empty()) return;
    BlockBox all = pieces.front()->bbox;
    for (auto const& p : pieces)
        all = {std::min(all.minX, p->bbox.minX), std::min(all.minY, p->bbox.minY), std::min(all.minZ, p->bbox.minZ),
               std::max(all.maxX, p->bbox.maxX), std::max(all.maxY, p->bbox.maxY), std::max(all.maxZ, p->bbox.maxZ)};
    auto const& first = pieces.front()->bbox;
    auto        c     = st::boxCenter(first);
    BlockPos    pivot{c.x, first.minY, c.z};
    for (int cx = all.minX >> 4; cx <= all.maxX >> 4; cx++)
        for (int cz = all.minZ >> 4; cz <= all.maxZ >> 4; cz++) {
            BlockBox                  chunk{cx * 16, mem.minY() + 1, cz * 16, cx * 16 + 15, mem.maxY() - 1, cz * 16 + 15};
            core::jrand::LegacyRandom random(seed ^ (static_cast<std::int64_t>(cx) * 341873128712LL + static_cast<std::int64_t>(cz) * 132897987541LL));
            for (auto const& p : pieces)
                if (p->bbox.intersects(chunk)) p->postProcess(mem, random, chunk, pivot);
        }
}

/// The hollow hill's ground as the terraformer shapes it: solid below `groundY` and inside the
/// mound, carved where the terraformer is negative.
void hillGround(MemoryRegion& mem, int hillSize, BlockBox const& startBox, int groundY) {
    auto const& stone = st::blocks::java("minecraft:stone");
    auto const& dirt  = st::blocks::java("minecraft:dirt");
    auto const& grass = st::blocks::java("minecraft:grass_block");
    int         r     = static_cast<int>((hillSize * 4 + 0.8F) * 8) + 2;
    auto        c     = cml::boxCenter(startBox);
    for (int x = c.x - r; x <= c.x + r; x++)
        for (int z = c.z - r; z <= c.z + r; z++) {
            int top = INT_MIN;
            for (int y = groundY - 8; y <= groundY + 48; y++) {
                double t     = cml::hollowHillTerraformer(hillSize, startBox, x, y, z);
                bool   solid = t > 0 || (y <= groundY && t >= 0);
                if (solid) {
                    mem.setBlock(BlockPos{x, y, z}, stone, 2);
                    top = y;
                }
            }
            if (top != INT_MIN) {
                mem.setBlock(BlockPos{x, top, z}, grass, 2);
                if (mem.getBlock(BlockPos{x, top - 1, z}).getTypeName() == "minecraft:stone") mem.setBlock(BlockPos{x, top - 1, z}, dirt, 2);
            }
        }
}

BlockBox unionOf(std::vector<std::unique_ptr<st::Piece>> const& pieces) {
    BlockBox all = pieces.front()->bbox;
    for (auto const& p : pieces)
        all = {std::min(all.minX, p->bbox.minX), std::min(all.minY, p->bbox.minY), std::min(all.minZ, p->bbox.minZ),
               std::max(all.maxX, p->bbox.maxX), std::max(all.maxY, p->bbox.maxY), std::max(all.maxZ, p->bbox.maxZ)};
    return all;
}

BlockBox grow(BlockBox b, int n) { return {b.minX - n, b.minY - n, b.minZ - n, b.maxX + n, b.maxY + n, b.maxZ + n}; }

/// A piece written with save() and read back through its registered factory saves the same tag.
bool roundTrips(st::Piece const& piece) {
    Json saved = piece.save();
    auto back  = st::loadPiece(saved);
    return back && back->save() == saved;
}

Actor* fetch(ActorUniqueID id) {
    auto level = ll::service::getLevel();
    return level ? level->fetchEntity(id, false) : nullptr;
}

} // namespace

// ---- the hedge ------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("minorlandmarks.hedge_damage", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(1800, 1);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto region = ctx.region();
    ctx.require(region != nullptr, "test region");
    auto at = [&](int dx, int dy, int dz) { return core::GlobalPos{p.dim, p.x + dx, p.y + dy, p.z + dz}; };
    for (int dx = 0; dx <= 6; dx++)
        for (int dz = 0; dz <= 2; dz++) {
            ctx.setBlock(at(dx, -1, dz), "minecraft:stone");
            for (int dy = 0; dy <= 3; dy++) ctx.setBlock(at(dx, dy, dz), "minecraft:air");
        }
    ctx.require(ctx.setBlock(at(1, 0, 1), kHedge), "hedge placed");
    ctx.require(ctx.setBlock(at(5, 0, 1), kHedge), "second hedge placed");

    // a player standing on the hedge (stepOn / entityInside) takes 3 cactus damage
    auto& sp = ctx.player();
    sp.teleport(Vec3{p.x + 1.5f, static_cast<float>(p.y) + 1.0f, p.z + 1.5f}, sp.getDimensionId());
    int before = sp.getHealth();
    co_await ctx.ticks(20);
    int after = ctx.player().getHealth();
    ctx.check(after < before, "a player on the hedge is hurt (" + std::to_string(before) + " -> " + std::to_string(after) + ")");

    // a spider on the other hedge is spared, a zombie is not
    auto*        spider = gf::spawn::spawnMob(*region, "minecraft:spider", Vec3{p.x + 5.5f, static_cast<float>(p.y) + 1.0f, p.z + 1.5f}, gf::spawn::SpawnReason::Command);
    ctx.require(spider != nullptr, "spider spawned");
    ActorUniqueID spiderId = spider->getOrCreateUniqueID();
    int           spiderHp = spider->getHealth();
    co_await ctx.ticks(20);
    if (auto* s = fetch(spiderId)) {
        ctx.check(s->getHealth() == spiderHp, "the spider is spared (" + std::to_string(spiderHp) + " -> " + std::to_string(s->getHealth()) + ")");
        s->remove();
    } else ctx.fail("the spider vanished");

    auto* zombie = gf::spawn::spawnMob(*ctx.region(), "minecraft:zombie", Vec3{p.x + 5.5f, static_cast<float>(p.y) + 1.0f, p.z + 1.5f}, gf::spawn::SpawnReason::Command);
    ctx.require(zombie != nullptr, "zombie spawned");
    ActorUniqueID zombieId = zombie->getOrCreateUniqueID();
    int           zombieHp = zombie->getHealth();
    co_await ctx.ticks(20);
    if (auto* z = fetch(zombieId)) {
        ctx.check(z->getHealth() < zombieHp, "a zombie on the hedge is hurt (" + std::to_string(zombieHp) + " -> " + std::to_string(z->getHealth()) + ")");
        z->remove();
    } else ctx.fail("the zombie vanished");

    // punching the hedge hurts (HedgeBlock#attack)
    auto& pl = ctx.player();
    pl.teleport(Vec3{p.x + 3.5f, static_cast<float>(p.y), p.z + 1.5f}, pl.getDimensionId());
    co_await ctx.ticks(25); // past the hurt cooldown
    int    hp0 = ctx.player().getHealth();
    auto&  pp  = ctx.player();
    auto   blk = at(5, 0, 1);
    std::string empty;
    std::string hedgeId = kHedge;
    interactions::DestroyContext dc{pp, ItemStack::EMPTY_ITEM(), empty, region->getBlock(BlockPos{blk.x, blk.y, blk.z}), hedgeId, blk, 1, false};
    (void)interactions::dispatchStartDestroy(dc);
    co_await ctx.ticks(2);
    ctx.check(ctx.player().getHealth() < hp0, "punching the hedge hurts (" + std::to_string(hp0) + " -> " + std::to_string(ctx.player().getHealth()) + ")");
    ctx.check(isSpiderType("twilightforest:hedge_spider") && isSpiderType("minecraft:cave_spider") && !isSpiderType("minecraft:zombie"),
              "Spider and its subclasses are spared");
});

// ---- hollow hills ----------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("minorlandmarks.hollow_hill_layout", [](Context& ctx) {
    static constexpr char const* kKeys[] = {kSmallHill, kMediumHill, kLargeHill};
    for (int size = 1; size <= 3; size++) {
        MemoryRegion mem;
        int          ox = size == 1 ? 3 : size == 2 ? 7 : 11, oy = size == 1 ? 2 : 5;
        int          cx = 8, cy = 0, cz = 8; // the landmark center (chunk 0, 0 + 7, sea level 0)
        std::vector<std::unique_ptr<st::Piece>> pieces;
        pieces.push_back(makeHollowHill(size, cx - ox, cy - oy, cz - ox, kKeys[size - 1]));
        BlockBox box = pieces.front()->bbox;
        int      r   = (size * 2 + 1) * 8 - 6;
        ctx.check(box.maxX - box.minX == 2 * r && box.maxZ - box.minZ == 2 * r, "hill " + std::to_string(size) + ": the box spans 2r (" + std::to_string(2 * r) + ")");
        hillGround(mem, size, box, cy);
        placeByChunks(mem, pieces, 1234 + size);
        BlockBox vol = grow(box, 4);
        vol.maxY += 40;
        int spawners = mem.count(vol, "minecraft:mob_spawner");
        int chests   = mem.count(vol, "minecraft:chest");
        ctx.info("hill " + std::to_string(size) + ": " + std::to_string(spawners) + " spawners, " + std::to_string(chests) + " chests, " + std::to_string(mem.size()) + " blocks");
        ctx.check(spawners + chests > 0, "hill " + std::to_string(size) + " has spawners or chests");
        checkGolden(ctx, "hollow_hill_" + std::to_string(size), mem.hash(vol));
        ctx.check(roundTrips(*pieces.front()), "hill " + std::to_string(size) + ": the TFHill tag reads back");
        // the dome: inside the box half-dome the hill's monster list applies (F6 asks canSpawnMob)
        auto c = cml::boxCenter(box);
        ctx.check(cml::hollowHillTerraformer(size, box, c.x, box.minY + 2, c.z) < 0, "hill " + std::to_string(size) + ": the terraformer carves the inside");
    }
});

// ---- the hedge maze --------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("minorlandmarks.hedge_maze_layout", [](Context& ctx) {
    MemoryRegion                            mem;
    std::vector<std::unique_ptr<st::Piece>> pieces;
    pieces.push_back(makeHedgeMaze(0, 8, 4, 8)); // HedgeMazeStructure: start (7, 0, 7) + (1, 4, 1)
    BlockBox box = pieces.front()->bbox;
    ctx.check(box.maxX - box.minX == 50 && box.maxZ - box.minZ == 50 && box.maxY - box.minY == 10, "the maze box is 51 x 11 x 51");
    flatGround(mem, grow(box, 2), box.minY + 2);
    placeByChunks(mem, pieces, 77);
    int hedges   = mem.count(box, kHedge);
    int lanterns = mem.count(box, "minecraft:lit_pumpkin");
    int spawners = mem.count(box, "minecraft:mob_spawner");
    int chests   = mem.count(box, "minecraft:chest");
    ctx.info("hedge maze: " + std::to_string(hedges) + " hedges, " + std::to_string(lanterns) + " jack o'lanterns, " + std::to_string(spawners) + " spawners, " +
             std::to_string(chests) + " chests");
    ctx.check(hedges > 1500, "the maze walls are hedges");
    ctx.check(lanterns >= 8 + 5, "8 entrance lanterns and one per room at least");
    ctx.check(spawners == 5, "one spawner in each of the 5 rooms");
    ctx.check(chests >= 5 && chests <= 10, "1-2 chests per room");
    checkGolden(ctx, "hedge_maze", mem.hash(box));
    ctx.check(roundTrips(*pieces.front()), "the TFHedge tag reads back");
});

GF_REGISTER_SELFTEST("minorlandmarks.hedge_maze_world", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto area   = co_await ctx.flatArea(1810, 64, 24);
    auto region = ctx.region();
    ctx.require(region != nullptr, "test region");
    // the start center: HedgeMazeComponent's floor (piece y 0 = start y + 1) on the flat area's surface
    core::GlobalPos center{ctx.testPos(1810).dim, area.minX + 31, area.minY - 1, area.minZ + 31};
    gf::landmark::ext::ScopedStart forced(kHedgeMaze, center);
    ctx.require(forced && forced->generated, "forced the hedge maze start");
    ctx.check(forced->pieceBoxes.size() == 1, "one TFHedge piece");
    auto box = forced->pieceBoxes.front();
    int  spawners = 0, chests = 0, hedges = 0;
    for (int x = box.minX; x <= box.maxX; x++)
        for (int z = box.minZ; z <= box.maxZ; z++)
            for (int y = box.minY; y <= box.maxY; y++) {
                auto id = game::blockIdAt(*region, BlockPos{x, y, z});
                if (id == "minecraft:mob_spawner") ++spawners;
                else if (id == "minecraft:chest") ++chests;
                else if (id == kHedge) ++hedges;
            }
    ctx.info("forced hedge maze: " + std::to_string(hedges) + " hedges, " + std::to_string(spawners) + " spawners, " + std::to_string(chests) + " chests");
    ctx.check(spawners == 5, "5 room spawners in the world");
    ctx.check(chests >= 5, "the rooms' chests in the world");
    ctx.check(hedges > 1500, "the hedge walls in the world");
    auto desc = gf::landmark::descriptor(kHedgeMaze);
    ctx.check(desc && desc->icon == "twilightforest:hedge_maze", "the hedge maze map icon");
    gf::log().info("minorlandmarks: hedge maze placed at {} {} {}", box.minX, box.minY, box.minZ);
});

// ---- hollow trees ----------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("minorlandmarks.hollow_tree_layout", [](Context& ctx) {
    for (int radius : {1, 3}) {
        MemoryRegion              mem;
        core::jrand::LegacyRandom random(4242 + radius);
        auto                      pieces = hollowTreePieces(HollowTreeConfig::defaults(), 48, radius, 8, 0, 8, random);
        ctx.check(pieces.size() > 10, "radius " + std::to_string(radius) + ": the trunk with its branches and roots (" + std::to_string(pieces.size()) + " pieces)");
        BlockBox all = unionOf(pieces);
        flatGround(mem, grow(all, 2), -1);
        placeByChunks(mem, pieces, 99);
        int logs   = mem.count(all, "twilightforest:twilight_oak_log");
        int wood   = mem.count(all, "twilightforest:twilight_oak_wood");
        int leaves = mem.count(all, "twilightforest:twilight_oak_leaves");
        ctx.info("hollow tree r" + std::to_string(radius) + ": " + std::to_string(logs) + " logs, " + std::to_string(wood) + " wood, " + std::to_string(leaves) + " leaves");
        ctx.check(logs > 48 && wood > 0 && leaves > 0, "radius " + std::to_string(radius) + ": trunk, branches and leaves");
        checkGolden(ctx, "hollow_tree_r" + std::to_string(radius), mem.hash(all));
        bool all_ok = true;
        for (auto const& p : pieces) all_ok = all_ok && roundTrips(*p);
        ctx.check(all_ok, "radius " + std::to_string(radius) + ": every piece tag reads back");
    }
});

GF_REGISTER_SELFTEST("minorlandmarks.hollow_tree_sapling", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto area   = co_await ctx.flatArea(1850, 48, 128);
    auto region = ctx.region();
    ctx.require(region != nullptr, "test region");
    BlockPos pos{area.minX + 24, area.minY, area.minZ + 24};
    // HollowTreeStructure#generateFromSapling through the landmark contract (U3's sapling grower)
    bool ok = gf::landmark::generateAt(kHollowTree, *region, pos);
    ctx.require(ok, "the hollow tree start was made at the sapling");
    int logs = 0;
    for (int dx = -5; dx <= 5; dx++)
        for (int dz = -5; dz <= 5; dz++)
            for (int dy = 0; dy < 32; dy++)
                if (game::blockIdAt(*region, BlockPos{pos.x + dx, pos.y + dy, pos.z + dz}) == "twilightforest:twilight_oak_log") ++logs;
    ctx.check(logs > 32, "the hollow trunk stands at the sapling (" + std::to_string(logs) + " logs)");
    (void)gf::landmark::ext::forgetStartForTest(game::globalPos(*region, pos), kHollowTree);
});

// ---- fallen trunks ----------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("minorlandmarks.fallen_trunk", [](Context& ctx) {
    FallenTrunkConfig config = FallenTrunkConfig::fromJson(Json::object());
    struct Case {
        int      radius, length;
        cml::Dir dir;
    };
    for (auto c : {Case{1, 20, cml::Dir::South}, Case{2, 22, cml::Dir::East}, Case{4, 26, cml::Dir::North}}) {
        MemoryRegion mem;
        int          xy  = c.radius > 1 ? c.radius * 2 + 1 : 4;
        BlockBox     box = cml::orientBox(8, 1, 8, 0, 0, 0, xy, xy, c.length - 1, c.dir);
        auto         pieces = fallenTrunkPieces(config, c.length, c.radius, c.dir, box, 1357 + c.radius);
        ctx.check(pieces.size() == 3, "radius " + std::to_string(c.radius) + ": the trunk and its two utility pieces");
        flatGround(mem, grow(box, 4), 0);
        placeByChunks(mem, pieces, 5);
        int logs = mem.count(box, "twilightforest:twilight_oak_log");
        ctx.info("fallen trunk r" + std::to_string(c.radius) + ": " + std::to_string(logs) + " logs, " + std::to_string(mem.count(grow(box, 1), "minecraft:mob_spawner")) +
                 " spawners, " + std::to_string(mem.count(grow(box, 4), "twilightforest:twilight_oak_chest")) + " chests");
        ctx.check(logs > c.length, "radius " + std::to_string(c.radius) + ": the trunk's logs");
        checkGolden(ctx, "fallen_trunk_r" + std::to_string(c.radius), mem.hash(grow(box, 4)));
        ctx.check(roundTrips(*pieces.front()), "radius " + std::to_string(c.radius) + ": the TFFallenTrunk tag reads back");
        // the terraformer: flat inside the trunk, filled under it, nothing far away
        auto mid = cml::boxCenter(box);
        ctx.check(fallenTrunkTerraform(box, c.dir, c.length, c.radius, 1357 + c.radius, mid.x, box.minY + 1, mid.z) < 0, "the terraformer clears the trunk");
        ctx.check(fallenTrunkTerraform(box, c.dir, c.length, c.radius, 1357 + c.radius, mid.x + 60, box.minY, mid.z + 60) == 0.0, "the terraformer stays near");
    }
});

// ---- feature types ----------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("minorlandmarks.features", [](Context& ctx) {
    struct Case {
        char const* name;
        Json        feature; // a configured feature id (string) or an inline configured feature
        int         groundY;
        bool        withTrees = false;
        bool        withStone = false;
        char const* layerBelow = nullptr; ///< the block of the ground's second layer (y - 1)
    };
    Json woodRoots{{"type", "twilightforest:wood_roots"},
                   {"config", {{"root_block", simpleProvider("twilightforest:root")}, {"root_ore", simpleProvider("twilightforest:liveroot_block")}}}};
    std::vector<Case> cases = {
        {"block_spike", "twilightforest:outside_stalagmite", 0, false, false, "twilightforest:deadrock"}, // only #supports_stalagmites holds one
        {"dark_forest_placer", "twilightforest:dark_grass", 0},
        {"fallen_hollow_log", "twilightforest:hollow_log", 0},
        {"fallen_small_log", "twilightforest:tf_oak_fallen_log", 0},
        {"foundation", "twilightforest:foundation", 0},
        {"hollow_stump", "twilightforest:hollow_stump", 0},
        {"lampposts", "twilightforest:pumpkin_lamppost", 0},
        {"monolith", "twilightforest:monolith", 0},
        {"mycelium_blob", "twilightforest:mycelium_blob", 0},
        {"small_lake", "twilightforest:water_lake", 0},
        {"webs", "twilightforest:webs", 0, true},
        {"wood_roots", woodRoots, 30, false, true},
    };
    for (auto const& c : cases) {
        bool        placed = false;
        std::string hash;
        int         seedUsed = -1;
        for (int seed = 1; seed <= 40 && !placed; seed++) {
            MemoryRegion mem(9000 + seed);
            BlockBox     ground{-24, 0, -24, 24, 0, 24};
            flatGround(mem, BlockBox{ground.minX, c.groundY, ground.minZ, ground.maxX, c.groundY, ground.maxZ}, c.groundY);
            if (c.layerBelow)
                for (int x = ground.minX; x <= ground.maxX; x++)
                    for (int z = ground.minZ; z <= ground.maxZ; z++) mem.setBlock(BlockPos{x, c.groundY - 1, z}, st::blocks::java(c.layerBelow), 2);
            if (c.withStone)
                for (int x = -12; x <= 12; x++)
                    for (int z = -12; z <= 12; z++)
                        for (int y = 0; y < c.groundY - 6; y++) mem.setBlock(BlockPos{x, y, z}, st::blocks::java("minecraft:stone"), 2);
            if (c.withTrees) {
                // a canopy of leaves over logs for the webs
                for (int x = -4; x <= 4; x++)
                    for (int z = -4; z <= 4; z++) {
                        mem.setBlock(BlockPos{x, 12, z}, st::blocks::java("twilightforest:twilight_oak_leaves", Json{{"persistent", "true"}}), 2);
                        if (x == 0 && z == 0)
                            for (int y = 1; y < 12; y++) mem.setBlock(BlockPos{x, y, z}, st::blocks::java("twilightforest:twilight_oak_log"), 2);
                    }
            }
            core::jrand::LegacyRandom random(seed);
            // webs: a column beside the log; wood roots: in the stone body
            BlockPos origin{c.withTrees ? 1 : 0, c.withStone ? 15 : c.groundY + 1, c.withTrees ? 1 : 0};
            placed = gf::features::kit::placeConfigured(c.feature, mem, origin, random); // the configured feature (no placement modifiers)
            if (placed) {
                seedUsed = seed;
                hash     = mem.hash(BlockBox{-24, c.groundY - 30, -24, 24, c.groundY + 30, 24});
            }
        }
        ctx.check(placed, std::string(c.name) + " placed (seed " + std::to_string(seedUsed) + ")");
        if (placed) checkGolden(ctx, std::string("feature_") + c.name, hash);
    }
});

GF_REGISTER_SELFTEST("minorlandmarks.monolith_ravens", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto area   = co_await ctx.flatArea(1880, 16, 32);
    auto region = ctx.region();
    ctx.require(region != nullptr, "test region");
    BlockPos origin{area.minX + 7, area.minY + 1, area.minZ + 7};
    bool     placed = false;
    {
        st::BlockSourceRegion live(*region, origin);
        for (int seed = 1; seed <= 20 && !placed; seed++) {
            core::jrand::LegacyRandom random(seed);
            placed = gf::features::kit::placeConfigured(Json("twilightforest:monolith"), live, origin, random);
        }
    }
    ctx.require(placed, "the monolith was placed");
    int obsidian = 0, lapis = 0;
    for (int dx = 0; dx <= 1; dx++)
        for (int dz = 0; dz <= 1; dz++)
            for (int dy = -1; dy <= 20; dy++) {
                auto id = game::blockIdAt(*region, BlockPos{origin.x + dx, origin.y + dy, origin.z + dz});
                if (id == "minecraft:obsidian") ++obsidian;
                if (id == "minecraft:lapis_block") ++lapis;
            }
    ctx.check(obsidian >= 4 * 5 && lapis == 1, "a 2x2 obsidian pillar with one lapis block on top (" + std::to_string(obsidian) + ", " + std::to_string(lapis) + ")");
    co_await ctx.ticks(2);
    auto sel = "@e[type=twilightforest:raven,x=" + std::to_string(origin.x) + ",y=" + std::to_string(origin.y) + ",z=" + std::to_string(origin.z) + ",r=16]";
    auto r   = selftest::runServerCommand("testfor " + sel, ctx.testPos(1880).dim);
    ctx.check(r.successCount >= 1, "ravens spawned next to the monolith (" + r.messages + ")");
    (void)selftest::runServerCommand("kill " + sel, ctx.testPos(1880).dim);
});

// ---- registration ---------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("minorlandmarks.registered", [](Context& ctx) {
    for (char const* key : {kSmallHill, kMediumHill, kLargeHill, kHedgeMaze, kHollowTree, kSwampHollowTree, kFallenTrunk})
        ctx.check(gf::landmark::descriptor(key) != nullptr, std::string(key) + ": descriptor");
    for (char const* type : {kTFHill, kTFHedge, kTFFallenTrunk, kTFHTTr, kTFHTLB, kTFHTMB, kTFHTSB, kTFHTRo, kTFHTLD}) {
        Json probe{{"id", type}, {"BB", Json::array({0, 0, 0, 1, 1, 1})}, {"O", 0}, {"GD", 0}};
        ctx.check(st::loadPiece(probe) != nullptr, std::string(type) + ": piece factory");
    }
    auto hill = gf::landmark::descriptor(kSmallHill);
    ctx.check(hill && hill->icon == "twilightforest:small_hollow_hill", "the small hill's map icon");
    auto large = gf::landmark::descriptor(kLargeHill);
    ctx.check(large && large->extra.value("speleothem_config", std::string{}) == "twilightforest:large_hollow_hill", "the large hill's speleothem config");
});

// ---- client acceptance (post-merge BeDD run; not part of the server scenario's checks) ------------------------

GF_REGISTER_SELFTEST("minorlandmarks.client_showcase", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto area   = co_await ctx.flatArea(1810, 64, 24);
    auto region = ctx.region();
    ctx.require(region != nullptr, "test region");
    core::GlobalPos center{ctx.testPos(1810).dim, area.minX + 31, area.minY - 1, area.minZ + 31};
    gf::landmark::ext::ScopedStart maze(kHedgeMaze, center);
    ctx.require(maze && maze->generated, "the hedge maze");
    auto& sp = ctx.player();
    sp.teleport(Vec3{area.minX + 31.5f, static_cast<float>(area.minY) + 14.0f, area.minZ - 4.0f}, sp.getDimensionId());
    gf::log().info("minorlandmarks.client_showcase: hedge maze ready");
    co_await ctx.ticks(100);
    sp.teleport(Vec3{area.minX + 6.5f, static_cast<float>(area.minY) + 2.0f, area.minZ + 31.5f}, sp.getDimensionId());
    gf::log().info("minorlandmarks.client_showcase: hedge close ready");
    co_await ctx.ticks(100);
});

} // namespace gf::feature::minorlandmarks
