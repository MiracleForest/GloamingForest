// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// Selftests of U28's structures (rule R12: "aurora.*", testPos dx 2800..2899; rule R19 golden
// hashes). The aurora palace and the mushroom tower are generated on an in-memory region at a fixed
// seed and start chunk (their size, 100+ blocks each way and 150 high, is beyond a test row): the
// golden hash covers every placed block (structure::bases' VolumeHasher text, the blocks sorted by
// y, z, x relative to the placed bounds). A real start is forced in the world as well and checked
// for its pieces and the Snow Queen's spawner.
#include <algorithm>
#include <climits>
#include <map>
#include <unordered_map>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "core/VolumeHash.h"
#include "core/jrand/JRand.h"
#include "feature/aurora/IceTower.h"
#include "feature/aurora/MushroomTower.h"
#include "feature/aurora/Structures.h"
#include "feature/ext/landmark/LandmarkTest.h"
#include "feature/landmark/Landmark.h"
#include "feature/structure/Internal.h"
#include "game/Blocks.h"
#include "selftest/SelfTest.h"

namespace gf::feature::aurora::tests {

namespace {

using selftest::Context;
using structure::BlockBox;

constexpr std::int64_t kSeed = 20260521; // the e2e world's seed

/// A sparse in-memory region with flat stone below y = 0 (the bases stop there).
class MemoryRegion final : public worldgen::Region {
public:
    MemoryRegion() { structure::resetRegionRandom(this); }
    ~MemoryRegion() override { structure::resetRegionRandom(this); }
    [[nodiscard]] int          centerChunkX() const override { return 0; }
    [[nodiscard]] int          centerChunkZ() const override { return 0; }
    [[nodiscard]] int          minY() const override { return -64; }
    [[nodiscard]] int          maxY() const override { return 320; }
    [[nodiscard]] bool         isInside(int, int) const override { return true; }
    [[nodiscard]] Block const& getBlock(BlockPos const& pos) const override {
        auto it = mBlocks.find(key(pos.x, pos.y, pos.z));
        if (it != mBlocks.end()) return *it->second;
        return pos.y < 0 ? structure::blocks::java("minecraft:stone") : structure::blocks::air();
    }
    bool setBlock(BlockPos const& pos, Block const& block, int) override {
        if (pos.y < minY() || pos.y >= maxY()) return false;
        mBlocks[key(pos.x, pos.y, pos.z)] = &block;
        return true;
    }
    [[nodiscard]] int          height(worldgen::Heightmap, int, int) const override { return 0; }
    [[nodiscard]] std::string  biome(BlockPos const&) const override { return {}; }
    [[nodiscard]] std::int64_t seed() const override { return kSeed; }
    [[nodiscard]] int          dimension() const override { return -1; } // scratch: no block entity work

    /// The golden hash of every block placed (air included: what the pieces carved).
    [[nodiscard]] std::string hash(BlockBox* boundsOut = nullptr) const {
        std::vector<std::pair<std::tuple<int, int, int>, Block const*>> sorted;
        BlockBox b{INT_MAX, INT_MAX, INT_MAX, INT_MIN, INT_MIN, INT_MIN};
        for (auto const& [k, blk] : mBlocks) {
            auto [x, y, z] = unkey(k);
            b              = {std::min(b.minX, x), std::min(b.minY, y), std::min(b.minZ, z), std::max(b.maxX, x), std::max(b.maxY, y), std::max(b.maxZ, z)};
            sorted.push_back({{y, z, x}, blk});
        }
        std::sort(sorted.begin(), sorted.end(), [](auto const& l, auto const& r) { return l.first < r.first; });
        core::VolumeHasher h;
        for (auto const& [pos, blk] : sorted) {
            auto [y, z, x] = pos;
            h.add(x - b.minX, y - b.minY, z - b.minZ, blk->getTypeName(), game::statesOf(*blk));
        }
        if (boundsOut) *boundsOut = b;
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
    std::unordered_map<long long, Block const*> mBlocks;
};

/// The pieces of a start in chunk (cx, cz) at y, as F6 builds them (getFirstPiece's random, then
/// the generation context's random for addChildren).
template <class Layout>
structure::bases::PieceList layout(Layout fn, int cx, int cz, int y) {
    // RandomSource.create(seed + chunkX * 25117L + chunkZ * 151121L)
    auto seed = static_cast<std::int64_t>(static_cast<std::uint64_t>(kSeed) + static_cast<std::uint64_t>(static_cast<std::int64_t>(cx)) * 25117ULL +
                                          static_cast<std::uint64_t>(static_cast<std::int64_t>(cz)) * 151121ULL);
    core::jrand::LegacyRandom      first(seed);
    core::jrand::WorldgenRandom    context(std::make_unique<core::jrand::LegacyRandom>(0));
    context.setLargeFeatureSeed(kSeed, cx, cz);
    structure::bases::PieceList pieces;
    fn(first, context, cx * 16 + 7, y, cz * 16 + 7, pieces);
    return pieces;
}

void generate(structure::bases::PieceList& pieces, MemoryRegion& mem) {
    // one pass over the whole box: the pieces draw only from their own deterministic randoms
    BlockBox all{-30000000, -64, -30000000, 30000000, 319, 30000000};
    for (auto& p : pieces) {
        core::jrand::LegacyRandom r(0);
        p->postProcess(mem, r, all, BlockPos{0, 0, 0});
    }
}

std::map<std::string, int> typesOf(structure::bases::PieceList const& pieces) {
    std::map<std::string, int> types;
    for (auto const& p : pieces) types[p->typeId]++;
    return types;
}

std::string describe(std::map<std::string, int> const& types) {
    std::string s;
    for (auto const& [t, n] : types) s += t + "=" + std::to_string(n) + " ";
    return s;
}

/// A golden value: "" records it (one info line to copy into the table), else it must match.
void golden(Context& ctx, std::string const& name, std::string const& expected, std::string const& actual) {
    if (expected.empty()) {
        ctx.info("GOLDEN " + name + " = " + actual);
        return;
    }
    ctx.check(actual == expected, name + ": expected " + expected + ", got " + actual);
}

// Recorded from the translation (docs/spec/aurora.md section 1.3: GloamingForest's own goldens).
constexpr char kPalaceLayout[] = "twilightforest:TFITBea=9 twilightforest:TFITBoss=1 twilightforest:TFITBri=1 twilightforest:TFITEnt=1 twilightforest:TFITMai=1 twilightforest:TFITRoof=20 twilightforest:TFITSt=3 twilightforest:TFITWin=17 twilightforest:TFUtilityPiece=10 26,36,-49,122,181,11";
constexpr char kPalaceHash[]   = "045f1f7f1dab88af";
constexpr char kMushroomLayout[] = "twilightforest:TFMTBri=82 twilightforest:TFMTMB=2 twilightforest:TFMTMai=3 twilightforest:TFMTRoofMush=72 twilightforest:TFMTWin=82 -119,9,25,-11,90,133";
constexpr char kMushroomHash[]   = "d4d677277f4b4327";

} // namespace

GF_REGISTER_SELFTEST("aurora.palace_layout", [](Context& ctx) {
    auto pieces = layout(layoutAuroraPalace, 3, -2, 0);
    auto types  = typesOf(pieces);
    ctx.require(!pieces.empty() && pieces.front()->typeId == "twilightforest:TFITMai", "the main tower comes first");
    auto const& main = static_cast<IceTowerMain&>(*pieces.front());
    ctx.check(main.hasBossWing, "the main tower loops until the boss wing exists");
    ctx.check(types["twilightforest:TFITBoss"] == 1, "one Snow Queen tower (" + describe(types) + ")");
    ctx.check(types["twilightforest:TFITEnt"] == 1 && types["twilightforest:TFITBri"] == 1, "one entrance tower and one bridge");
    ctx.check(types["twilightforest:TFITSt"] == 3, "three outside stairs at the entrance");
    ctx.check(types["twilightforest:TFITWin"] >= 4, "wings");
    ctx.check(types["twilightforest:TFITRoof"] > 0 && (types["twilightforest:TFITBea"] > 0 || types["twilightforest:TFUtilityPiece"] > 0), "roofs, beards and bases");
    // the main tower floats 40 blocks above the start, 11 east and south of it
    ctx.check(main.bbox.minY == 40 && main.bbox.maxY - main.bbox.minY + 1 >= 31, "main tower at y + 40, 31..51 high");
    // every wing stays within RANGE of the main tower's centre
    int cx = main.bbox.minX + (main.bbox.maxX - main.bbox.minX + 1) / 2;
    for (auto const& p : pieces)
        if (p->typeId == "twilightforest:TFITWin") ctx.check(std::abs(p->bbox.minX - cx) < 80, "wing near the main tower");
    // persistence: a wing survives save / load
    for (auto const& p : pieces)
        if (p->typeId == "twilightforest:TFITBoss") {
            auto loaded = structure::loadPiece(p->save());
            ctx.require(loaded != nullptr, "the boss wing loads back");
            ctx.check(loaded->bbox == p->bbox && loaded->save() == p->save(), "same box and saved data");
            ctx.check(static_cast<structure::bases::Component&>(*loaded).getSpawnIndex() == -1, "no controlled spawns in the boss tower");
        }
    BlockBox all = pieces.front()->bbox;
    for (auto const& p : pieces)
        all = BlockBox::of(std::min(all.minX, p->bbox.minX), std::min(all.minY, p->bbox.minY), std::min(all.minZ, p->bbox.minZ), std::max(all.maxX, p->bbox.maxX),
                           std::max(all.maxY, p->bbox.maxY), std::max(all.maxZ, p->bbox.maxZ));
    golden(ctx, "palace_layout", kPalaceLayout,
           describe(types) + std::to_string(all.minX) + "," + std::to_string(all.minY) + "," + std::to_string(all.minZ) + "," + std::to_string(all.maxX) + "," +
               std::to_string(all.maxY) + "," + std::to_string(all.maxZ));
});

GF_REGISTER_SELFTEST("aurora.palace_hash", [](Context& ctx) {
    auto         pieces = layout(layoutAuroraPalace, 3, -2, 0);
    MemoryRegion mem;
    generate(pieces, mem);
    // the Snow Queen's spawner sits at (7, top - 6, 7) of the boss tower
    for (auto const& p : pieces)
        if (p->typeId == "twilightforest:TFITBoss") {
            auto pos = p->getWorldPos(7, 34, 7);
            ctx.check(mem.idAt(pos) == "twilightforest:snow_queen_boss_spawner", "the snow queen spawner at (7, 34, 7) of the boss tower: " + mem.idAt(pos));
            ctx.check(mem.idAt(p->getWorldPos(7, 37, 7)).find("air") != std::string::npos, "the air box above it");
        }
    ctx.check(mem.count("twilightforest:aurora_block") > 1000, "aurora block walls");
    ctx.check(mem.count("twilightforest:aurora_pillar") > 50, "aurora pillars");
    ctx.check(mem.count("minecraft:packed_ice") > 100, "the boss tower's packed ice floors and stairs");
    ctx.check(mem.count("minecraft:birch_planks") > 100, "birch plank floors");
    golden(ctx, "palace_hash", kPalaceHash, mem.hash());
});

GF_REGISTER_SELFTEST("aurora.mushroom_tower_hash", [](Context& ctx) {
    auto pieces = layout(layoutMushroomTower, -5, 4, 5);
    auto types  = typesOf(pieces);
    ctx.require(!pieces.empty() && pieces.front()->typeId == "twilightforest:TFMTMai", "the main tower comes first");
    ctx.check(pieces.front()->bbox.minY == 9, "main tower at y + 4");
    ctx.check(types["twilightforest:TFMTBri"] > 0, "bridges (" + describe(types) + ")");
    ctx.check(types["twilightforest:TFMTRoofMush"] > 0, "mushroom caps");
    MemoryRegion mem;
    generate(pieces, mem);
    ctx.check(mem.count("minecraft:red_mushroom_block") > 100, "red mushroom caps");
    ctx.check(mem.count("minecraft:mushroom_stem") > 100, "stem walls");
    BlockBox all = pieces.front()->bbox;
    for (auto const& p : pieces)
        all = BlockBox::of(std::min(all.minX, p->bbox.minX), std::min(all.minY, p->bbox.minY), std::min(all.minZ, p->bbox.minZ), std::max(all.maxX, p->bbox.maxX),
                           std::max(all.maxY, p->bbox.maxY), std::max(all.maxZ, p->bbox.maxZ));
    golden(ctx, "mushroom_layout", kMushroomLayout,
           describe(types) + std::to_string(all.minX) + "," + std::to_string(all.minY) + "," + std::to_string(all.minZ) + "," + std::to_string(all.maxX) + "," +
               std::to_string(all.maxY) + "," + std::to_string(all.maxZ));
    golden(ctx, "mushroom_tower_hash", kMushroomHash, mem.hash());
});

GF_REGISTER_SELFTEST("aurora.palace_start", [](Context& ctx) -> ll::coro::CoroTask<> {
    ctx.require(co_await ctx.inTwilight(), "the Twilight Forest is available");
    // a start forced far from the test rows (removed afterwards): its pieces generate in loaded
    // chunks only, and nothing loads the chunks out there
    auto pos = ctx.testPos(2810);
    pos.x += 4096;
    pos.z += 4096;
    {
        gf::landmark::ext::ScopedStart forced(kAuroraPalace, pos);
        ctx.require(forced && forced->generated, "forced an aurora palace start");
        ctx.check(forced->pieceBoxes.size() > 10, "the palace has its pieces");
        auto at = gf::landmark::startAt(pos.dim, BlockPos{pos.x + 16, pos.y + 45, pos.z + 16});
        ctx.check(at && at->key == kAuroraPalace, "startAt finds the palace at the main tower");
        auto const* d = gf::landmark::descriptor(kAuroraPalace);
        ctx.check(d && !d->lockAdvancements.empty() && d->lockAdvancements.front() == "twilightforest:progress_yeti", "locked behind the Alpha Yeti");
    }
    {
        gf::landmark::ext::ScopedStart forced(kMushroomTower, pos);
        ctx.check(forced && forced->generated && forced->pieceBoxes.size() > 3, "forced a mushroom tower start");
    }
    co_return;
});

} // namespace gf::feature::aurora::tests
