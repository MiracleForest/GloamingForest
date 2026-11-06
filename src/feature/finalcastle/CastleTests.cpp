// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// U30 (finalcastle) selftests of the final castle structure (rule R12: "finalcastle.*", testPos dx
// 3050..3059 and 3090..3099; rule R19 golden hash). The castle (the keep, two tower mazes, the
// dungeon levels and the murals, a few hundred blocks across) is laid out and generated on an
// in-memory region at a fixed seed and start chunk: the golden hash covers every placed block
// (core::VolumeHasher text, the blocks sorted by y, z, x relative to the placed bounds). The start
// chunk is the final castle site nearest to the origin of the test world (the dungeon rooms only
// generate inside the plateau biomes, which the dimension's biome source decides). The gazebo's
// template piece is left out of the in-memory generation: its "final_castle_wip" marker would queue
// the WIP texts for a scratch region (see finalcastle.castle_templates for its content). A real start
// is forced in the world as well and checked for its pieces.
#include <algorithm>
#include <climits>
#include <map>
#include <tuple>
#include <unordered_map>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"

#include "core/VolumeHash.h"
#include "core/jrand/JRand.h"
#include "feature/ext/finalcastle/W0Internals.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/landmark/LandmarkTest.h"
#include "feature/finalcastle/Castle.h"
#include "feature/landmark/Landmark.h"
#include "feature/structure/bases/TemplatePieces.h"
#include "game/Blocks.h"
#include "selftest/SelfTest.h"

namespace gf::feature::finalcastle::castle::tests {

namespace {

using selftest::Context;

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
    [[nodiscard]] std::string  biome(BlockPos const&) const override { return "twilightforest:final_plateau"; }
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

/// The pieces of a start in chunk (cx, cz) at y (the sea level), as F6 builds them.
PieceList layoutAt(int cx, int cz, int y) {
    core::jrand::WorldgenRandom context(std::make_unique<core::jrand::LegacyRandom>(0));
    context.setLargeFeatureSeed(kSeed, cx, cz);
    PieceList pieces;
    layout(context, cx * 16 + 7, y, cz * 16 + 7, kSeed, pieces);
    return pieces;
}

std::string templateOf(Piece const& p) {
    if (auto const* t = dynamic_cast<::gf::structure::bases::TemplatePiece const*>(&p)) return t->getTemplateName();
    return {};
}

BlockBox boundsOf(PieceList const& pieces) {
    BlockBox all = pieces.front()->bbox;
    for (auto const& p : pieces)
        all = BlockBox::of(std::min(all.minX, p->bbox.minX), std::min(all.minY, p->bbox.minY), std::min(all.minZ, p->bbox.minZ), std::max(all.maxX, p->bbox.maxX),
                           std::max(all.maxY, p->bbox.maxY), std::max(all.maxZ, p->bbox.maxZ));
    return all;
}

/// Every piece but the gazebo template, in list order (StructureStart#placeInChunk with one box for
/// the whole start; the pivot is the start box's centre at its bottom).
void generate(PieceList& pieces, MemoryRegion& mem) {
    BlockBox all{-30000000, -64, -30000000, 30000000, 319, 30000000};
    BlockBox b = boundsOf(pieces);
    BlockPos pivot{b.minX + (b.maxX - b.minX + 1) / 2, b.minY, b.minZ + (b.maxZ - b.minZ + 1) / 2};
    for (auto& p : pieces) {
        if (templateOf(*p) == id::GazeboPool) continue;
        core::jrand::LegacyRandom r(0);
        p->postProcess(mem, r, all, pivot);
    }
}

std::map<std::string, int> typesOf(PieceList const& pieces) {
    std::map<std::string, int> types;
    for (auto const& p : pieces) types[p->typeId]++;
    return types;
}

std::string describe(std::map<std::string, int> const& types, PieceList const& pieces) {
    std::string s;
    for (auto const& [t, n] : types) s += t.substr(t.find(':') + 1) + "=" + std::to_string(n) + " ";
    BlockBox all = boundsOf(pieces);
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

/// The final castle site nearest to the test world's origin (its start chunk), from the Twilight
/// Forest's landmark grid; (cx, cz) of kFallback when none is found.
struct Site {
    int  cx = 0, cz = 0;
    bool located = false;
};
constexpr int kFallbackChunk = 64;
Site siteNear(int dim) {
    if (auto hit = gf::landmark::ext::locate(dim, kFinalCastle, 0, 0, 100)) return Site{hit->pos.x >> 4, hit->pos.z >> 4, true};
    return Site{kFallbackChunk, kFallbackChunk, false};
}

// Recorded from the translation (GloamingForest's own goldens, seed 20260521, the site nearest to
// the origin of the e2e world, y 0).
constexpr char kLayout[] = "TFFCBeF21=1 TFFCBelTo=1 TFFCBoGaz=1 TFFCBri=34 TFFCDamT=8 TFFCDunBoR=1 TFFCDunEn=2 TFFCDunEx=2 TFFCDunR31=17 TFFCDunSt=6 TFFCEnBoTo=1 TFFCEnSiTo=1 TFFCEnSt=4 TFFCEnTo=1 TFFCFTh21=12 TFFCLaTo=1 TFFCMain=1 TFFCMur=2 TFFCRo13Con=12 TFFCRo13Cr=7 TFFCRo13Pk=2 TFFCRo48Cr=1 TFFCRo9Cr=4 TFFCSiTo=18 TFFCStTo=3 TFFCToF13=28 TFFCToF48=1 TFFCWrT=4 TFJigsawTemplate=3 -634,-14,374,560,200,651";
constexpr char kHash[]   = "5de8f9303d4ac1b0"; // site chunk (-31, 29)

} // namespace

GF_REGISTER_SELFTEST("finalcastle.castle_layout", [](Context& ctx) -> ll::coro::CoroTask<> {
    ctx.require(co_await ctx.inTwilight(), "the Twilight Forest is available");
    auto site   = siteNear(ctx.testPos(3050).dim);
    ctx.info("final castle site chunk " + std::to_string(site.cx) + " " + std::to_string(site.cz) + (site.located ? "" : " (fallback)"));
    auto pieces = layoutAt(site.cx, site.cz, 0);
    auto types  = typesOf(pieces);
    ctx.require(!pieces.empty() && pieces.front()->typeId == type::Main, "the keep comes first");
    auto const& keep = static_cast<Main&>(*pieces.front());
    int         x    = site.cx * 16 + 7;
    int         z    = site.cz * 16 + 7;
    ctx.check(keep.bbox == BlockBox{x - 24, 120, z - 24, x + 24, 160, z + 24}, "the keep is 49 x 41 x 49 at y 120..160");
    ctx.check(types[type::Foundation48] == 1 && types[type::Roof48] == 1 && types[type::Gazebo] == 1, "keep foundation, roof and gazebo (" + describe(types, pieces) + ")");
    ctx.check(types[type::StairTower] == 3 && types[type::LargeTower] == 1, "three stair towers and the large tower at the keep corners");
    ctx.check(types[type::EntranceTower] == 1, "the yellow maze reaches the entrance tower");
    ctx.check(types[type::BellTower] == 1, "the blue maze reaches the bell tower");
    ctx.check(types[type::MazeTower] >= 2 && types[type::Bridge] >= 3, "maze towers and bridges");
    ctx.check(types[type::EntranceBottom] == 1 && types[type::EntranceStairs] == 4, "the entrance's ground tower with its four stairs");
    ctx.check(types[type::DungeonEntrance] == 2, "two dungeon levels");
    ctx.check(types[type::DungeonExit] >= 1 && types[type::DungeonExit] <= 2, "one exit per level reached");
    ctx.check(types[type::DungeonSteps] >= 5, "four flights from the keep and one per exit");
    ctx.check(types[type::ForgeRoom] <= 1, "at most one forge room");
    ctx.check(types[type::Mural] == 2, "two murals");
    ctx.check(types["twilightforest:TFJigsawTemplate"] == 3, "three template pieces (gazebo, large tower, bell tower)");

    // the murals: 35 x 30 on the front, 19 x 12 inside
    int murals = 0;
    for (auto const& p : pieces)
        if (p->typeId == type::Mural) {
            auto const& m = static_cast<MuralPiece&>(*p).mural();
            if (murals == 0) ctx.check(m.width() == 35 && m.height() == 30, "the front mural is 35 x 30");
            else ctx.check(m.width() == 19 && m.height() == 12, "the inner mural is 19 x 12");
            murals++;
        }

    // the template pieces sit where TF's jigsaws put them
    for (auto const& p : pieces) {
        auto name = templateOf(*p);
        if (name.empty()) continue;
        for (auto const& host : pieces) {
            if (name == id::GazeboPool && host->typeId == type::Gazebo) ctx.check(p->bbox == host->bbox, "the gazebo template fills the gazebo box");
            if (name == id::LargeTowerPool && host->typeId == type::LargeTower) ctx.check(p->bbox == host->bbox, "the large tower template fills the tower box");
            if (name == id::BellTowerPool && host->typeId == type::BellTower)
                ctx.check(p->bbox.minX == host->bbox.minX && p->bbox.minY == host->bbox.minY && p->bbox.minZ == host->bbox.minZ && p->bbox.maxX == host->bbox.maxX &&
                              p->bbox.maxZ == host->bbox.maxZ,
                          "the bell tower template stands in the tower box");
        }
    }

    // rune colours of the mazes (saved as "color")
    for (auto const& p : pieces) {
        auto const* t = dynamic_cast<MazeTower13 const*>(p.get());
        if (!t) continue;
        if (p->typeId == type::BellTower) ctx.check(t->color() == id::BlueRune, "the bell tower is blue");
        if (p->typeId == type::EntranceTower || p->typeId == type::Damaged || p->typeId == type::Wrecked) ctx.check(t->color() == id::YellowRune, "yellow: " + p->typeId);
    }

    // persistence: every piece survives save / load
    for (auto const& p : pieces) {
        auto loaded = ::gf::structure::loadPiece(p->save());
        ctx.require(loaded != nullptr, "piece " + p->typeId + " loads back");
        ctx.check(loaded->bbox == p->bbox && loaded->save() == p->save(), "same box and saved data: " + p->typeId);
    }
    // controlled spawn labels: 1 keep, 2 dungeon, 3 forge, -1 gazebo
    for (auto const& p : pieces) {
        auto const* c = dynamic_cast<::gf::structure::bases::Component const*>(p.get());
        if (!c) continue;
        if (p->typeId == type::Main) ctx.check(c->getSpawnIndex() == 1, "keep spawn list 1");
        if (p->typeId == type::Gazebo) ctx.check(c->getSpawnIndex() == -1, "no spawns in the gazebo");
        if (p->typeId == type::DungeonSteps || p->typeId == type::DungeonRoom || p->typeId == type::DungeonEntrance || p->typeId == type::DungeonExit)
            ctx.check(c->getSpawnIndex() == 2, "dungeon spawn list 2: " + p->typeId);
        if (p->typeId == type::ForgeRoom) ctx.check(c->getSpawnIndex() == 3, "forge spawn list 3");
        if (p->typeId == type::DungeonRoom)
            ctx.check(static_cast<::gf::structure::bases::ComponentOld const*>(c)->getTerrainAdjustment() == ::gf::structure::bases::TerrainAdjustment::Bury,
                      "dungeon rooms bury");
    }
    golden(ctx, "finalcastle_layout", kLayout, describe(types, pieces));
    co_return;
});

GF_REGISTER_SELFTEST("finalcastle.castle_hash", [](Context& ctx) -> ll::coro::CoroTask<> {
    ctx.require(co_await ctx.inTwilight(), "the Twilight Forest is available");
    auto         site   = siteNear(ctx.testPos(3052).dim);
    auto         pieces = layoutAt(site.cx, site.cz, 0);
    MemoryRegion mem;
    generate(pieces, mem);

    auto const& keep = static_cast<Main&>(*pieces.front());
    // the keep's doors: yellow on the ground floor, blue on the second floor
    ctx.check(mem.idAt(keep.getWorldPos(48, 2, 24)) == id::YellowDoor, "the keep's yellow door: " + mem.idAt(keep.getWorldPos(48, 2, 24)));
    ctx.check(mem.idAt(keep.getWorldPos(0, 32, 24)) == id::BlueDoor, "the keep's blue door: " + mem.idAt(keep.getWorldPos(0, 32, 24)));
    ctx.check(mem.idAt(keep.getWorldPos(18, 2, 12)) == id::PinkDoor, "the pink door of the dungeon cage");
    ctx.check(mem.idAt(keep.getWorldPos(12, 5, 15)) == id::PinkField, "the pink force-field cage");

    // rune flow: every maze tower's doors have its rune colour (damaged / wrecked towers excluded)
    int doors = 0, matching = 0;
    for (auto const& p : pieces) {
        auto const* t = dynamic_cast<MazeTower13 const*>(p.get());
        if (!t || p->typeId == type::Damaged || p->typeId == type::Wrecked) continue;
        std::string expected = ::gf::structure::blocks::id(t->doorColor());
        for (auto const& d : t->doors()) {
            doors++;
            if (mem.idAt(t->getWorldPos(d.x, d.y + 1, d.z)) == expected) matching++;
        }
    }
    ctx.check(doors > 6 && matching * 10 >= doors * 9, "maze tower doors of their rune colour: " + std::to_string(matching) + " / " + std::to_string(doors));

    // the murals are yellow rune brick where their bitmap is set
    for (auto const& p : pieces) {
        if (p->typeId != type::Mural) continue;
        auto const& m   = static_cast<MuralPiece&>(*p).mural();
        int         set = 0, ok = 0;
        for (int mx = 0; mx < m.width(); mx++)
            for (int my = 0; my < m.height(); my++)
                if (m.get(mx, my) > 0) {
                    set++;
                    if (mem.idAt(p->getWorldPos(0, my, mx)) == id::YellowRune) ok++;
                }
        ctx.check(set > 20 && ok == set, "mural pixels in yellow runes: " + std::to_string(ok) + " / " + std::to_string(set));
    }

    ctx.check(mem.count("twilightforest:castle_brick") > 10000, "castle brick");
    ctx.check(mem.count("twilightforest:violet_castle_rune_brick") > 2000, "the violet rune roof");
    ctx.check(mem.count("twilightforest:blue_force_field") > 100, "the bell tower's force-field windows");
    ctx.check(mem.count("twilightforest:bold_castle_brick_pillar") > 500, "bold pillars");
    ctx.check(mem.count("minecraft:quartz_stairs") > 300, "quartz stairs");
    ctx.check(mem.count("twilightforest:yellow_castle_door") > 20 && mem.count("twilightforest:blue_castle_door") > 10, "yellow and blue doors");
    ctx.check(mem.count("twilightforest:violet_castle_door") >= 18, "the stair towers' roof doors");
    ctx.check(mem.count("twilightforest:brown_thorns") > 0 || site.located == false, "thorns around the damaged towers");
    golden(ctx, "finalcastle_hash", kHash, mem.hash());
    co_return;
});

GF_REGISTER_SELFTEST("finalcastle.castle_templates", [](Context& ctx) {
    namespace tpl = ::gf::structure::templates;
    struct Expect {
        char const* id;
        int         sx, sy, sz;
        char const* connector;
        BlockPos    at;
        char const* front;
    };
    static Expect const kTemplates[] = {
        {id::GazeboPool, 21, 12, 21, id::FinalBossJigsaw, {10, 0, 10}, "down"},
        {id::LargeTowerPool, 13, 61, 13, id::LargeTowerJigsaw, {1, 1, 2}, "west"},
        {id::BellTowerPool, 21, 65, 21, id::RoomJigsaw, {1, 9, 10}, "west"},
    };
    for (auto const& e : kTemplates) {
        auto const* t = tpl::parsed(e.id);
        ctx.require(t != nullptr, std::string("template ") + e.id + " is loaded");
        ctx.check(t->sx == e.sx && t->sy == e.sy && t->sz == e.sz, std::string(e.id) + " size");
        bool found = false;
        for (auto const& c : t->connectors)
            if (c.name == e.connector && c.pos.x == e.at.x && c.pos.y == e.at.y && c.pos.z == e.at.z && c.front == e.front) found = true;
        ctx.check(found, std::string(e.id) + " has its connector " + e.connector);
    }
    // the gazebo: violet force-field walls and roof, the WIP marker on a placed (air) block at the centre
    auto const* g = tpl::parsed(id::GazeboPool);
    auto        at = [&](int x, int y, int z) -> std::string {
        auto const* b = g->palette[g->cells[static_cast<std::size_t>(g->index(x, y, z))]];
        return b ? game::blockId(*b) : std::string("(void)");
    };
    ctx.check(at(0, 0, 0) == id::VioletField && at(20, 10, 7) == id::VioletField && at(10, 11, 10) == id::VioletField, "violet force-field cage");
    ctx.check(at(10, 0, 10) == "minecraft:air" && at(10, 5, 10) == "minecraft:air", "an open gazebo");
    bool wip = false;
    for (auto const& m : g->markers) wip = wip || (m.marker == "final_castle_wip" && m.pos.x == 10 && m.pos.y == 0 && m.pos.z == 10);
    ctx.check(wip, "the final_castle_wip marker at the gazebo's centre");
    ctx.check(::gf::structure::markerFor("final_castle_wip") != nullptr, "its handler is registered");
});

GF_REGISTER_SELFTEST("finalcastle.castle_start", [](Context& ctx) -> ll::coro::CoroTask<> {
    ctx.require(co_await ctx.inTwilight(), "the Twilight Forest is available");
    // a start forced far from the test rows (removed afterwards; its chunks are not loaded, so no block is placed)
    auto pos = ctx.testPos(3090);
    pos.x = ((pos.x + 12288) >> 8 << 8) + 7; // a chunk centre on the 256 grid, as the landmark grid's
    pos.z = ((pos.z - 12288) >> 8 << 8) + 7; // centres are: the mazes stay within 128 blocks of it
    {
        gf::landmark::ext::ScopedStart forced(kFinalCastle, pos);
        ctx.require(forced && forced->generated, "forced a final castle start");
        ctx.check(forced->pieceBoxes.size() > 60, "the castle has its pieces: " + std::to_string(forced->pieceBoxes.size()));
        auto at = gf::landmark::startAt(pos.dim, BlockPos{forced->center.x, forced->center.y + 130, forced->center.z});
        ctx.check(at && at->key == kFinalCastle, "startAt finds the castle in the keep");
        std::map<std::string, int> types;
        for (auto const& p : gf::landmark::ext::startPieces(*forced)) types[p.type]++;
        ctx.check(types[type::Main] == 1 && types[type::EntranceTower] == 1 && types[type::BellTower] == 1 && types[type::DungeonEntrance] == 2,
                  "the stored start holds the keep, both maze ends and the dungeon levels");
        auto const* d = gf::landmark::descriptor(kFinalCastle);
        ctx.check(d && !d->lockAdvancements.empty() && d->lockAdvancements.front() == "twilightforest:progress_troll", "locked behind the trolls");
    }
    co_return;
});

} // namespace gf::feature::finalcastle::castle::tests
