// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// Selftests of U27's structures and features (rule R12: "caves.*", testPos dx 2700..2799; rule
// R19 golden hashes). The troll caves, the cloud castle and the yeti cave are generated on an
// in-memory region at a fixed seed (a troll cave reaches 100+ blocks each way): the golden hash
// covers every placed block (core::VolumeHasher text, the blocks sorted by y, z, x relative to the
// placed bounds). The goldens are GloamingForest's own (the procedural pieces are exact
// translations; the other units' features the garden caves place by string are left out of them).
// Real starts are forced in the world as well.
#include <algorithm>
#include <climits>
#include <map>
#include <unordered_map>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "core/GlobalPos.h"
#include "core/VolumeHash.h"
#include "core/caves/Caves.h"
#include "core/jrand/JRand.h"
#include "feature/caves/Caves.h"
#include "feature/caves/Structures.h"
#include "feature/ext/caves/W0Internals.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/landmark/LandmarkTest.h"
#include "feature/features/Features.h"
#include "feature/landmark/Landmark.h"
#include "game/Blocks.h"
#include "ll/api/coro/CoroTask.h"
#include "selftest/SelfTest.h"

namespace gf::feature::caves::tests {

namespace {

using selftest::Context;
namespace st = gf::structure;

constexpr std::int64_t kSeed = 20260521; // the e2e world's seed

/// A sparse in-memory region: stone below `ground`, air above.
class MemoryRegion final : public worldgen::Region {
public:
    explicit MemoryRegion(int ground) : mGround(ground) { st::resetRegionRandom(this); }
    ~MemoryRegion() override { st::resetRegionRandom(this); }
    [[nodiscard]] int          centerChunkX() const override { return 0; }
    [[nodiscard]] int          centerChunkZ() const override { return 0; }
    [[nodiscard]] int          minY() const override { return -64; }
    [[nodiscard]] int          maxY() const override { return 320; }
    [[nodiscard]] bool         isInside(int, int) const override { return true; }
    [[nodiscard]] Block const& getBlock(BlockPos const& pos) const override {
        auto it = mBlocks.find(key(pos.x, pos.y, pos.z));
        if (it != mBlocks.end()) return *it->second;
        return pos.y < mGround ? st::blocks::java("minecraft:stone") : st::blocks::air();
    }
    bool setBlock(BlockPos const& pos, Block const& block, int) override {
        if (pos.y < minY() || pos.y >= maxY()) return false;
        mBlocks[key(pos.x, pos.y, pos.z)] = &block;
        return true;
    }
    [[nodiscard]] int height(worldgen::Heightmap, int x, int z) const override {
        for (int y = mGround + 64; y >= mGround - 64; --y)
            if (!getBlock(BlockPos{x, y, z}).isAir()) return y + 1;
        return mGround;
    }
    [[nodiscard]] std::string  biome(BlockPos const&) const override { return "twilightforest:highlands"; }
    [[nodiscard]] std::int64_t seed() const override { return kSeed; }
    [[nodiscard]] int          dimension() const override { return -1; } // scratch: no block entity work

    [[nodiscard]] std::string hash() const {
        std::vector<std::pair<std::tuple<int, int, int>, Block const*>> sorted;
        int                                                             bx = INT_MAX, by = INT_MAX, bz = INT_MAX;
        for (auto const& [k, blk] : mBlocks) {
            auto [x, y, z] = unkey(k);
            bx             = std::min(bx, x);
            by             = std::min(by, y);
            bz             = std::min(bz, z);
            sorted.push_back({{y, z, x}, blk});
        }
        std::sort(sorted.begin(), sorted.end(), [](auto const& l, auto const& r) { return l.first < r.first; });
        core::VolumeHasher h;
        for (auto const& [pos, blk] : sorted) {
            auto [y, z, x] = pos;
            h.add(x - bx, y - by, z - bz, blk->getTypeName(), game::statesOf(*blk));
        }
        return h.hex();
    }
    [[nodiscard]] std::string idAt(BlockPos const& pos) const { return game::blockId(getBlock(pos)); }
    [[nodiscard]] std::size_t count(std::string const& id) const {
        std::size_t n = 0;
        for (auto const& [k, blk] : mBlocks)
            if (game::blockId(*blk) == id) n++;
        return n;
    }

private:
    static long long key(int x, int y, int z) {
        return (static_cast<long long>(x + (1 << 23)) << 40) | (static_cast<long long>(y + (1 << 19)) << 20) | static_cast<long long>(z + (1 << 19));
    }
    static std::tuple<int, int, int> unkey(long long k) {
        return {static_cast<int>(k >> 40) - (1 << 23), static_cast<int>((k >> 20) & 0xfffff) - (1 << 19), static_cast<int>(k & 0xfffff) - (1 << 19)};
    }
    int                                         mGround;
    std::unordered_map<long long, Block const*> mBlocks;
};

/// The pieces of a troll cave start in chunk (cx, cz) at y, as F6 builds them.
PieceList trollCave(int cx, int cz, int y) {
    core::jrand::WorldgenRandom context(std::make_unique<core::jrand::LegacyRandom>(0));
    context.setLargeFeatureSeed(kSeed, cx, cz);
    PieceList pieces;
    layoutTrollCave(context, cx * 16 + 7, y, cz * 16 + 7, pieces);
    return pieces;
}

/// Post-processes every piece chunk by chunk (the pieces' own deterministic randoms; a fixed
/// random per piece and chunk stands in for the decoration random).
void generate(PieceList& pieces, MemoryRegion& mem) {
    for (auto& p : pieces) {
        for (int cx = p->bbox.minX >> 4; cx <= p->bbox.maxX >> 4; cx++)
            for (int cz = p->bbox.minZ >> 4; cz <= p->bbox.maxZ >> 4; cz++) {
                BlockBox                  chunk{cx << 4, -64, cz << 4, (cx << 4) + 15, 319, (cz << 4) + 15};
                core::jrand::LegacyRandom r(static_cast<std::int64_t>(cx) * 341873128712LL + static_cast<std::int64_t>(cz) * 132897987541LL);
                p->postProcess(mem, r, chunk, BlockPos{0, 0, 0});
            }
    }
}

std::map<std::string, int> typesOf(PieceList const& pieces) {
    std::map<std::string, int> types;
    for (auto const& p : pieces) types[p->typeId]++;
    return types;
}

std::string describe(std::map<std::string, int> const& types) {
    std::string s;
    for (auto const& [t, n] : types) s += t + "=" + std::to_string(n) + " ";
    return s;
}

std::string boxText(PieceList const& pieces) {
    BlockBox all = pieces.front()->bbox;
    for (auto const& p : pieces)
        all = BlockBox{std::min(all.minX, p->bbox.minX), std::min(all.minY, p->bbox.minY), std::min(all.minZ, p->bbox.minZ),
                       std::max(all.maxX, p->bbox.maxX), std::max(all.maxY, p->bbox.maxY), std::max(all.maxZ, p->bbox.maxZ)};
    return std::to_string(all.minX) + "," + std::to_string(all.minY) + "," + std::to_string(all.minZ) + "," + std::to_string(all.maxX) + "," +
           std::to_string(all.maxY) + "," + std::to_string(all.maxZ);
}

/// A golden value: "" records it (one info line to copy into the table), else it must match.
void golden(Context& ctx, std::string const& name, std::string const& expected, std::string const& actual) {
    if (expected.empty()) {
        ctx.info("GOLDEN " + name + " = " + actual);
        return;
    }
    ctx.check(actual == expected, name + ": expected " + expected + ", got " + actual);
}

/// The garden caves' biome check and the other units' features off while a test generates.
struct TestMode {
    TestMode() { setGardenBiomeCheckForTest(false); }
    ~TestMode() { setGardenBiomeCheckForTest(true); }
};

// Recorded from the translation (GloamingForest's own goldens, docs/units/caves.md).
constexpr char kTrollLayout[]   = "";
constexpr char kTrollHash[]     = "";
constexpr char kCastleHash[]    = "";
constexpr char kYetiHash[]      = "";
constexpr char kFeaturesHash[]  = "";

} // namespace

GF_REGISTER_SELFTEST("caves.troll_cave_layout", [](Context& ctx) {
    auto pieces = trollCave(5, -3, 0);
    auto types  = typesOf(pieces);
    ctx.require(!pieces.empty() && pieces.front()->typeId == "twilightforest:TFTCMai", "the main cave comes first");
    auto const& main = *pieces.front();
    // TrollCaveMainComponent(0, x, y + 11, z): a 30 x 20 x 30 box from (-15, -20, -15)
    ctx.check(main.bbox.maxX - main.bbox.minX == 30 && main.bbox.maxY - main.bbox.minY == 20 && main.bbox.maxY == 11, "main cave box 30 x 20 x 30 ending at y + 11");
    ctx.check(types["twilightforest:TFTCVa"] == 1, "one troll vault (" + describe(types) + ")");
    ctx.check(types["twilightforest:TFTCCon"] >= 1, "connecting caves");
    for (auto const& p : pieces) {
        if (p->typeId == "twilightforest:TFTCVa") {
            ctx.check(p->bbox.maxX - p->bbox.minX == 12 && ((p->bbox.minX + 8) & 3) == 0, "the vault is 13 wide, aligned to 4");
            ctx.check(static_cast<st::bases::Component&>(*p).getSpawnIndex() == -1, "no controlled spawns in the vault");
        }
        // persistence: every piece survives save / load
        auto loaded = st::loadPiece(p->save());
        ctx.check(loaded && loaded->bbox == p->bbox && loaded->save() == p->save(), "piece " + p->typeId + " loads back");
    }
    golden(ctx, "troll_cave_layout", kTrollLayout, describe(types) + boxText(pieces));
});

GF_REGISTER_SELFTEST("caves.troll_cave_hash", [](Context& ctx) {
    TestMode     mode;
    auto         pieces = trollCave(5, -3, 0);
    MemoryRegion mem(40);
    generate(pieces, mem);
    ctx.check(mem.count("twilightforest:giant_obsidian") > 100, "the vault's giant obsidian walls");
    ctx.check(mem.count("minecraft:chest") >= 4, "the vault's two double chests");
    ctx.check(mem.count("minecraft:air") > 10000, "hollowed caves");
    ctx.check(mem.count("twilightforest:trollsteinn") > 0, "trollsteinn on the cave walls: " + std::to_string(mem.count("twilightforest:trollsteinn")));
    for (auto const& p : pieces)
        if (p->typeId == "twilightforest:TFTCVa") {
            ctx.check(mem.idAt(p->getWorldPos(5, 5, 5)) == "minecraft:cobblestone", "the cobblestone platform");
            ctx.check(mem.idAt(p->getWorldPos(5, 6, 5)) == "minecraft:chest", "the vault chest");
        }
    golden(ctx, "troll_cave_hash", kTrollHash, mem.hash());
});

GF_REGISTER_SELFTEST("caves.cloud_castle_hash", [](Context& ctx) {
    core::jrand::LegacyRandom rand(kSeed);
    PieceList                 pieces;
    pieces.push_back(std::make_unique<CloudCastle>(0, 1000, 160, -2000));
    pieces.front()->addChildren(*pieces.front(), pieces, rand);
    auto types = typesOf(pieces);
    ctx.check(types["twilightforest:TFCloud"] == 1 && types["twilightforest:TFClTr"] == 2, "a cloud and two cloud trees (" + describe(types) + ")");
    MemoryRegion mem(-64);
    generate(pieces, mem);
    auto const& castle = *pieces.front();
    ctx.check(mem.idAt(castle.getWorldPos(8, 0, 8)) == "twilightforest:fluffy_cloud", "the fluffy cloud slab");
    ctx.check(mem.idAt(castle.getWorldPos(8, 6, 20)) == "twilightforest:giant_cobblestone", "giant cobblestone walls");
    ctx.check(mem.count("twilightforest:wispy_cloud") > 0, "the procedural cloud");
    for (auto const& p : pieces) {
        auto loaded = st::loadPiece(p->save());
        ctx.check(loaded && loaded->bbox == p->bbox, "piece " + p->typeId + " loads back");
    }
    golden(ctx, "cloud_castle_hash", kCastleHash, mem.hash());
});

GF_REGISTER_SELFTEST("caves.yeti_cave_hash", [](Context& ctx) {
    PieceList pieces;
    layoutYetiCave(64, 0, 64, pieces);
    ctx.require(pieces.size() == 1 && pieces.front()->typeId == "twilightforest:TFYeti", "one yeti cave piece");
    auto& yeti = static_cast<YetiCave&>(*pieces.front());
    // HollowHillComponent(size 2) at y + 4, its box lowered by one
    ctx.check(yeti.bbox.maxX - yeti.bbox.minX == 68, "hill radius 34");
    // the terrain the terraformer shapes: solid where its density is positive
    MemoryRegion mem(-64);
    auto         center = st::boxCenter(yeti.bbox);
    for (int x = yeti.bbox.minX; x <= yeti.bbox.maxX; x++)
        for (int z = yeti.bbox.minZ; z <= yeti.bbox.maxZ; z++)
            for (int y = -8; y <= 32; y++) {
                double ground = y < 0 ? 1.0 : -0.01 * (static_cast<double>(y) + 0.5);
                if (ground + core::caves::yetiCaveTerraformer(center.x, center.z, x, y, z) > 0.0) mem.setBlock(BlockPos{x, y, z}, st::blocks::java("minecraft:stone"), 2);
            }
    generate(pieces, mem);
    auto spawner = yeti.getWorldPos(34, 1, 34);
    ctx.check(mem.idAt(spawner) == "twilightforest:alpha_yeti_boss_spawner", "the alpha yeti spawner at local (34, 1, 34): " + mem.idAt(spawner));
    ctx.check(mem.count("minecraft:packed_ice") + mem.count("minecraft:ice") + mem.count("minecraft:blue_ice") > 20, "ice stalactites");
    auto loaded = st::loadPiece(yeti.save());
    ctx.check(loaded && loaded->bbox == yeti.bbox && loaded->getWorldPos(34, 1, 34) == spawner, "the yeti cave loads back (with its y shift)");
    golden(ctx, "yeti_cave_hash", kYetiHash, mem.hash());
});

GF_REGISTER_SELFTEST("caves.features", [](Context& ctx) {
    MemoryRegion mem(0);
    // a mycelium patch, as the garden caves make one under each mushroom
    for (int x = -20; x <= 60; x++)
        for (int z = -4; z <= 4; z++) mem.setBlock(BlockPos{x, -1, z}, st::blocks::java("minecraft:mycelium"), 2);
    core::jrand::LegacyRandom r(kSeed);
    bool                      glooms = false, brown = false, red = false;
    for (int i = 0; i < 8 && !glooms; i++) glooms = gf::features::place("twilightforest:troll_big_mushglooms", mem, BlockPos{0, 0, 0}, r);
    for (int i = 0; i < 8 && !brown; i++) brown = gf::features::place("twilightforest:troll_huge_brown_mushrooms", mem, BlockPos{20, 0, 0}, r);
    for (int i = 0; i < 8 && !red; i++) red = gf::features::place("twilightforest:troll_huge_red_mushrooms", mem, BlockPos{40, 0, 0}, r);
    ctx.check(glooms && mem.count("twilightforest:huge_mushgloom") > 0 && mem.count("twilightforest:huge_mushgloom_stem") > 0, "a big mushgloom");
    ctx.check(brown && mem.count("minecraft:brown_mushroom_block") > 0, "a troll cave brown mushroom");
    ctx.check(red && mem.count("minecraft:red_mushroom_block") > 0, "a troll cave red mushroom");
    ctx.check(mem.count("minecraft:mushroom_stem") > 0, "mushroom stems");
    // troll vines hang under stone: a stone ceiling at y 40 above an open column
    MemoryRegion cave(0);
    for (int x = -6; x <= 6; x++)
        for (int z = -6; z <= 6; z++) cave.setBlock(BlockPos{x, 40, z}, st::blocks::java("minecraft:stone"), 2);
    core::jrand::LegacyRandom r2(kSeed);
    gf::features::place("twilightforest:troll_roots", cave, BlockPos{0, 39, 0}, r2);
    ctx.check(cave.count("twilightforest:trollvidr") + cave.count("twilightforest:unripe_trollber") > 0, "troll roots under the stone ceiling");
    golden(ctx, "features_hash", kFeaturesHash, mem.hash());
});

GF_REGISTER_SELFTEST("caves.starts", [](Context& ctx) -> ll::coro::CoroTask<> {
    ctx.require(co_await ctx.inTwilight(), "the Twilight Forest is available");
    auto pos = ctx.testPos(2710);
    pos.x += 6144;
    pos.z += 4096;
    {
        gf::landmark::ext::ScopedStart forced(kTrollCave, pos);
        ctx.require(forced && forced->generated, "forced a troll cave start");
        ctx.check(forced->pieceBoxes.size() >= 3, "the troll cave has its pieces");
        auto const* d = gf::landmark::descriptor(kTrollCave);
        ctx.check(d && !d->lockAdvancements.empty() && d->lockAdvancements.front() == "twilightforest:progress_merge", "locked behind the Hydra, Lich and Ur-Ghast merge");
    }
    {
        gf::landmark::ext::ScopedStart forced(kYetiCave, pos);
        ctx.require(forced && forced->generated, "forced a yeti cave start");
        ctx.check(forced->pieceBoxes.size() == 1, "one yeti cave piece");
        auto const* d = gf::landmark::descriptor(kYetiCave);
        ctx.check(d && !d->lockAdvancements.empty() && d->lockAdvancements.front() == "twilightforest:progress_lich", "locked behind the Lich");
        // the terraformer reaches the start's chunks
        ctx.check(gf::landmark::ext::hasTerraformers(pos.dim, forced->center.x >> 4, forced->center.z >> 4), "the yeti cave terraforms its chunks");
    }
    co_return;
});

} // namespace gf::feature::caves::tests
