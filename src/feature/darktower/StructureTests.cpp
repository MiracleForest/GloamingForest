// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// U26 (darktower) selftests of the dark tower structure (rule R12: "darktower.*", testPos dx
// 2600..2699; rule R19 golden hash). The tower (three main towers up to 86 high with their wings,
// about 120 blocks across) is generated on an in-memory region at a fixed seed and start chunk: the
// golden hash covers every placed block (core::VolumeHasher text, the blocks sorted by y, z, x
// relative to the placed bounds). The planter trees grow from the region's random (WorldGenRegion
// #getRandom), which F7 seeds per region, so the hash is stable for one region. A real start is
// forced in the world as well and checked for its pieces.
#include <algorithm>
#include <climits>
#include <map>
#include <tuple>
#include <unordered_map>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"

#include "core/VolumeHash.h"
#include "core/jrand/JRand.h"
#include "feature/darktower/Tower.h"
#include "feature/ext/landmark/LandmarkTest.h"
#include "feature/landmark/Landmark.h"
#include "feature/structure/Internal.h"
#include "game/Blocks.h"
#include "selftest/SelfTest.h"

namespace gf::feature::darktower::tests {

namespace {

using selftest::Context;
using ::gf::structure::BlockBox;

constexpr std::int64_t kSeed = 20260521; // the e2e world's seed

/// A sparse in-memory region with flat stone below y = 0.
class MemoryRegion final : public worldgen::Region {
public:
    MemoryRegion() { ::gf::structure::resetRegionRandom(this); }
    ~MemoryRegion() override { ::gf::structure::resetRegionRandom(this); }
    [[nodiscard]] int          centerChunkX() const override { return 0; }
    [[nodiscard]] int          centerChunkZ() const override { return 0; }
    [[nodiscard]] int          minY() const override { return -64; }
    [[nodiscard]] int          maxY() const override { return 320; }
    [[nodiscard]] bool         isInside(int, int) const override { return true; }
    [[nodiscard]] Block const& getBlock(BlockPos const& pos) const override {
        auto it = mBlocks.find(key(pos.x, pos.y, pos.z));
        if (it != mBlocks.end()) return *it->second;
        return pos.y < 0 ? ::gf::structure::blocks::java("minecraft:stone") : ::gf::structure::blocks::air();
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

    [[nodiscard]] std::string hash() const {
        std::vector<std::pair<std::tuple<int, int, int>, Block const*>> sorted;
        BlockBox b{INT_MAX, INT_MAX, INT_MAX, INT_MIN, INT_MIN, INT_MIN};
        for (auto const& [k, blk] : mBlocks) {
            auto [x, y, z] = unkey(k);
            b = {std::min(b.minX, x), std::min(b.minY, y), std::min(b.minZ, z), std::max(b.maxX, x), std::max(b.maxY, y), std::max(b.maxZ, z)};
            sorted.push_back({{y, z, x}, blk});
        }
        std::sort(sorted.begin(), sorted.end(), [](auto const& l, auto const& r) { return l.first < r.first; });
        core::VolumeHasher h;
        for (auto const& [pos, blk] : sorted) {
            auto [y, z, x] = pos;
            h.add(x - b.minX, y - b.minY, z - b.minZ, blk->getTypeName(), game::statesOf(*blk));
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
    std::unordered_map<long long, Block const*> mBlocks;
};

/// The pieces of a start in chunk (cx, cz) at y, as F6 builds them.
::gf::structure::bases::PieceList layoutAt(int cx, int cz, int y) {
    auto seed = static_cast<std::int64_t>(static_cast<std::uint64_t>(kSeed) + static_cast<std::uint64_t>(static_cast<std::int64_t>(cx)) * 25117ULL +
                                          static_cast<std::uint64_t>(static_cast<std::int64_t>(cz)) * 151121ULL);
    core::jrand::LegacyRandom   first(seed);
    core::jrand::WorldgenRandom context(std::make_unique<core::jrand::LegacyRandom>(0));
    context.setLargeFeatureSeed(kSeed, cx, cz);
    ::gf::structure::bases::PieceList pieces;
    tower::layout(first, context, cx * 16 + 7, y, cz * 16 + 7, pieces);
    return pieces;
}

void generate(::gf::structure::bases::PieceList& pieces, MemoryRegion& mem) {
    BlockBox all{-30000000, -64, -30000000, 30000000, 319, 30000000};
    for (auto& p : pieces) {
        core::jrand::LegacyRandom r(0);
        p->postProcess(mem, r, all, BlockPos{0, 0, 0});
    }
}

std::map<std::string, int> typesOf(::gf::structure::bases::PieceList const& pieces) {
    std::map<std::string, int> types;
    for (auto const& p : pieces) types[p->typeId]++;
    return types;
}

std::string describe(std::map<std::string, int> const& types, ::gf::structure::bases::PieceList const& pieces) {
    std::string s;
    for (auto const& [t, n] : types) s += t.substr(std::string("twilightforest:").size()) + "=" + std::to_string(n) + " ";
    BlockBox all = pieces.front()->bbox;
    for (auto const& p : pieces)
        all = BlockBox::of(std::min(all.minX, p->bbox.minX), std::min(all.minY, p->bbox.minY), std::min(all.minZ, p->bbox.minZ), std::max(all.maxX, p->bbox.maxX),
                           std::max(all.maxY, p->bbox.maxY), std::max(all.maxZ, p->bbox.maxZ));
    return s + std::to_string(all.minX) + "," + std::to_string(all.minY) + "," + std::to_string(all.minZ) + "," + std::to_string(all.maxX) + "," +
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

// Recorded from the translation (GloamingForest's own goldens: chunk (2, -3), y 8, seed 20260521).
constexpr char kLayout[] = "TFDTBB=4 TFDTBT=4 TFDTBal=9 TFDTBea=38 TFDTBri=38 TFDTEB=2 TFDTEnt=2 TFDTMB=2 TFDTMai=3 TFDTRA=14 TFDTRC=8 TFDTRFP=5 TFDTRR=7 TFDTWin=32 1,8,-103,103,196,-1";
constexpr char kHash[]   = "0394439afaafccd5"; // with vanilla oak / birch planter trees (inline)

} // namespace

GF_REGISTER_SELFTEST("darktower.structure_layout", [](Context& ctx) {
    auto pieces = layoutAt(2, -3, 8);
    auto types  = typesOf(pieces);
    ctx.require(!pieces.empty() && pieces.front()->typeId == tower::kMain, "the main tower comes first");
    auto const& main = static_cast<tower::Main&>(*pieces.front());
    ctx.check(main.size == 19 && main.bbox.minX == 2 * 16 + 7 + 10 - 18 && main.bbox.minY == 8, "main tower of size 19 at (x + 10, y, z + 10) facing north");
    int h = main.bbox.maxY - main.bbox.minY + 1;
    ctx.check(h >= 56 && h <= 86 && h % 5 == 1, "main height 56 + (rand(32) / 5) * 5: " + std::to_string(h));
    ctx.check(types[tower::kMain] == 3, "three main towers (" + describe(types, pieces) + ")");
    ctx.check(types[tower::kMainBridge] == 2, "two main bridges");
    ctx.check(types[tower::kEntrance] == 2 && types[tower::kEntranceBridge] == 2, "two entrance towers with their bridges");
    ctx.check(types[tower::kBossTrap] == 4 && types[tower::kBossBridge] == 4, "four boss trap towers on the top main tower");
    ctx.check(types[tower::kWing] > 4 && types[tower::kBeard] > 4, "wings and beards");
    int keys = 0;
    for (auto const& p : pieces)
        if (auto* w = dynamic_cast<tower::Wing*>(p.get()); w && w->isKeyTower()) {
            keys++;
            ctx.check(w->size == 9, "key towers are small wings");
        }
    ctx.check(keys >= 1 && keys <= 8, "key towers flagged: " + std::to_string(keys));
    // a locked door on the first main tower towards the second
    bool locked = false;
    for (auto d : main.doorTypes()) locked = locked || d == tower::Door::Locked;
    ctx.check(locked, "the first main tower's door to the next one is locked");
    // persistence: every piece survives save / load
    for (auto const& p : pieces) {
        auto loaded = ::gf::structure::loadPiece(p->save());
        ctx.require(loaded != nullptr, "piece " + p->typeId + " loads back");
        ctx.check(loaded->bbox == p->bbox && loaded->save() == p->save(), "same box and saved data: " + p->typeId);
    }
    for (auto const& p : pieces) {
        int si = static_cast<::gf::structure::bases::Component&>(*p).getSpawnIndex();
        if (p->typeId == tower::kBossTrap) ctx.check(si == -1, "no spawns in boss traps");
        if (p->typeId.rfind("twilightforest:TFDTR", 0) == 0) ctx.check(si == 1, "roof spawn list 1: " + p->typeId);
    }
    golden(ctx, "darktower_layout", kLayout, describe(types, pieces));
});

GF_REGISTER_SELFTEST("darktower.golden_hash", [](Context& ctx) {
    auto         pieces = layoutAt(2, -3, 8);
    MemoryRegion mem;
    generate(pieces, mem);
    // the Ur-Ghast spawner at (9, height - 2, 9) of the top main tower
    int spawners = 0;
    for (auto const& p : pieces)
        if (p->typeId == tower::kMain && p->genDepth == 2) {
            int  h   = p->bbox.maxY - p->bbox.minY + 1;
            auto pos = p->getWorldPos(9, h - 2, 9);
            ctx.check(mem.idAt(pos) == "twilightforest:ur_ghast_boss_spawner", "the Ur-Ghast spawner at (9, height - 2, 9): " + mem.idAt(pos));
            spawners++;
        }
    ctx.check(spawners == 1, "one boss tower");
    ctx.check(mem.count("twilightforest:ghast_trap") == 4, "a ghast trap in each boss trap tower");
    ctx.check(mem.count("twilightforest:encased_towerwood") > 1000, "encased towerwood");
    ctx.check(mem.count("twilightforest:towerwood") > 1000, "towerwood walls");
    ctx.check(mem.count("twilightforest:locked_vanishing_block") == 8, "two locked doors with four locks each");
    ctx.check(mem.count("twilightforest:unbreakable_vanishing_block") > 20, "vanishing doors");
    ctx.check(mem.count("twilightforest:reappearing_block") > 0, "reappearing doors");
    ctx.check(mem.count("minecraft:mob_spawner") > 0, "mob spawners");
    ctx.check(mem.count("minecraft:chest") > 0, "chests");
    ctx.check(mem.count("twilightforest:carminite_builder") + mem.count("twilightforest:antibuilder") > 0, "tower devices");
    golden(ctx, "darktower_hash", kHash, mem.hash());
});

GF_REGISTER_SELFTEST("darktower.structure_start", [](Context& ctx) -> ll::coro::CoroTask<> {
    ctx.require(co_await ctx.inTwilight(), "the Twilight Forest is available");
    // a start forced far from the test rows (removed afterwards)
    auto pos = ctx.testPos(2690);
    pos.x += 8192;
    pos.z -= 8192;
    {
        gf::landmark::ext::ScopedStart forced(tower::kDarkTower, pos);
        ctx.require(forced && forced->generated, "forced a dark tower start");
        ctx.check(forced->pieceBoxes.size() > 20, "the tower has its pieces: " + std::to_string(forced->pieceBoxes.size()));
        auto at = gf::landmark::startAt(pos.dim, BlockPos{pos.x + 10, pos.y + 20, pos.z + 10});
        ctx.check(at && at->key == tower::kDarkTower, "startAt finds the tower at the main tower");
        auto const* d = gf::landmark::descriptor(tower::kDarkTower);
        ctx.check(d && !d->lockAdvancements.empty() && d->lockAdvancements.front() == "twilightforest:progress_knights", "locked behind the knight phantoms");
    }
    co_return;
});

} // namespace gf::feature::darktower::tests
