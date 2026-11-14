// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
//
// U23 (labyrinth) selftests (rule R12: "labyrinth.*", testPos dx 2300..2399; rule R19 golden hash).
// The structure: the labyrinth is laid out at a fixed seed and start chunk and generated on an
// in-memory region; the golden hash covers every placed block (core::VolumeHasher text, the blocks
// sorted by y, z, x relative to the placed bounds). The mazes' wall selector draws from the
// region's random (WorldGenRegion#getRandom), which F7 seeds per region: the hash is stable for one
// region. A real start is forced in the world as well. The mobs, the slam, the spawner's height
// rule, the maze slime, the maze slime block and the mazebreaker rule run in the test area.
#include <algorithm>
#include <climits>
#include <functional>
#include <cmath>
#include <map>
#include <tuple>
#include <unordered_map>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/deps/shared_types/legacy/EquipmentSlot.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/level/Spawner.h"

#include "core/VolumeHash.h"
#include "core/jrand/JRand.h"
#include "core/labyrinth/Terraformer.h"
#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/ext/labyrinth/W0Internals.h"
#include "feature/ext/landmark/LandmarkTest.h"
#include "feature/labyrinth/Blocks.h"
#include "feature/labyrinth/Mobs.h"
#include "feature/labyrinth/Pieces.h"
#include "feature/landmark/Landmark.h"
#include "feature/recipes/Recipes.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"
#include <optional>
#include <utility>
#include "mc/server/SimulatedPlayer.h"
#include "feature/boss/BossInternal.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/gamerules/GameRules.h"
#include "feature/inventory/Inventory.h"
#include "feature/progression/Progression.h"

namespace gf::feature::labyrinth::tests {

namespace {

using selftest::Context;
using ::gf::structure::BlockBox;
namespace fe = gf::feature::entity;
namespace ia = gf::feature::common::interactions;
using Difficulty = SharedTypes::Legacy::Difficulty;

constexpr std::int64_t kSeed = 20260521; // the e2e world's seed

/// A sparse in-memory region with stone below y = 1 (the labyrinth digs its levels into it).
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
        return pos.y < 1 ? ::gf::structure::blocks::java("minecraft:stone") : ::gf::structure::blocks::air();
    }
    bool setBlock(BlockPos const& pos, Block const& block, int) override {
        if (pos.y < minY() || pos.y >= maxY()) return false;
        mBlocks[key(pos.x, pos.y, pos.z)] = &block;
        return true;
    }
    [[nodiscard]] int          height(worldgen::Heightmap, int, int) const override { return 1; }
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

/// The pieces of a start in chunk (cx, cz) at y, as F6 builds them (the context random of
/// Structure.GenerationContext: WorldgenRandom with the large feature seed).
maze::PieceList layoutAt(int cx, int cz, int y) {
    core::jrand::WorldgenRandom context(std::make_unique<core::jrand::LegacyRandom>(0));
    context.setLargeFeatureSeed(kSeed, cx, cz);
    maze::PieceList pieces;
    maze::layout(context, cx * 16 + 7, y, cz * 16 + 7, pieces);
    return pieces;
}

void generate(maze::PieceList& pieces, MemoryRegion& mem) {
    BlockBox all{-30000000, -64, -30000000, 30000000, 319, 30000000};
    for (auto& p : pieces) {
        core::jrand::LegacyRandom r(0);
        p->postProcess(mem, r, all, BlockPos{0, 0, 0});
    }
}

std::map<std::string, int> typesOf(maze::PieceList const& pieces) {
    std::map<std::string, int> types;
    for (auto const& p : pieces) types[p->typeId]++;
    return types;
}

std::string describe(std::map<std::string, int> const& types, maze::PieceList const& pieces) {
    std::string s;
    for (auto const& [t, n] : types) s += t.substr(std::string("gloamingforest:").size()) + "=" + std::to_string(n) + " ";
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

// Recorded from the translation (GloamingForest's own goldens: chunk (3, -2), y 0, seed 20260521).
constexpr char kLayout[] = "GloamingMMC=2 GloamingMMCIF=7 GloamingMMCR=2 GloamingMMCS=2 GloamingMMDE=12 GloamingMMDEC=9 GloamingMMDEF=6 GloamingMMDEFL=12 GloamingMMDEP=14 GloamingMMDER=14 GloamingMMDES=6 GloamingMMDET=11 GloamingMMDETC=8 GloamingMMDETrC=6 GloamingMMES=1 GloamingMMMR=2 GloamingMMMound=1 GloamingMMR=2 GloamingMMRB=1 GloamingMMRC=2 GloamingMMRE=1 GloamingMMRF=1 GloamingMMRSC=4 GloamingMMRV=1 GloamingMMRuins=1 GloamingMMUE=1 GloamingMMaze=2 5,-24,-75,118,12,38";
constexpr char kHash[]   = "a004339e12d37a67";

struct DifficultyScope {
    Difficulty previous = Difficulty::Peaceful;
    explicit DifficultyScope(Difficulty d) {
        if (auto level = ll::service::getLevel()) {
            previous = level->getDifficulty();
            level->setDifficulty(d);
        }
    }
    ~DifficultyScope() {
        if (auto level = ll::service::getLevel()) level->setDifficulty(previous);
    }
};

Mob* spawnAt(core::GlobalPos const& at, std::string const& id, float dx = 0.5f, float dy = 0.0f, float dz = 0.5f) {
    auto* region = game::regionOf(at.dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level) return nullptr;
    return level->getSpawner().spawnMob(*region, ActorDefinitionIdentifier(id), nullptr, Vec3{at.x + dx, float(at.y) + dy, at.z + dz}, false, false, false);
}

Mob* mobById(std::int64_t uid) {
    auto* a = fe::actorById(uid);
    return a ? fe::asMob(*a) : nullptr;
}

std::vector<Actor*> around(core::GlobalPos const& pos, std::string const& type, float r) {
    std::vector<Actor*> out;
    auto*               region = game::regionOf(pos.dim);
    if (!region) return out;
    for (auto* a : fe::actorsIn(*region, pos.dim, AABB{Vec3{pos.x - r, pos.y - r, pos.z - r}, Vec3{pos.x + 1 + r, pos.y + 1 + r, pos.z + 1 + r}}))
        if (a && a->isAlive() && a->getTypeName() == type) out.push_back(a);
    return out;
}

void discardAround(core::GlobalPos const& pos, float r) {
    for (char const* t : {mobs::kMinotaur, mobs::kMinoshroom, mobs::kMazeSlime, "minecraft:item", "minecraft:xp_orb"})
        for (auto* a : around(pos, t, r)) a->remove();
}

float attr(Mob& m, Attribute const& a) {
    auto        ref  = m.getAttribute(a);
    auto const* inst = static_cast<AttributeInstance const*>(ref.mPtr);
    return inst ? inst->mCurrentValue : -1.0f;
}

core::GlobalPos at(core::GlobalPos p, int dx, int dy, int dz) { return {p.dim, p.x + dx, p.y + dy, p.z + dz}; }

} // namespace

// ---- the structure -------------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("labyrinth.maze_layout", [](Context& ctx) {
    auto pieces = layoutAt(3, -2, 0);
    auto types  = typesOf(pieces);
    ctx.require(!pieces.empty() && pieces.front()->typeId == maze::kRuins, "the ruins come first");
    auto const& ruins = *pieces.front();
    ctx.check(ruins.bbox.minX == 3 * 16 + 7 + 5 && ruins.bbox.minZ == -2 * 16 + 7 + 5, "MazeRuinsComponent at (x + 5, y, z + 5)");
    ctx.check(types[maze::kMaze] == 2, "two maze levels (" + describe(types, pieces) + ")");
    ctx.check(types[maze::kRoom] == 2 && types[maze::kRoomExit] == 1 && types[maze::kRoomBoss] == 1, "a plain room per level, the exit and the boss room");
    ctx.check(types[maze::kRoomCollapse] == 2 && types[maze::kMushRoom] == 2, "two collapsed rooms (level 1) and two mush-rooms (level 2)");
    ctx.check(types[maze::kRoomFountain] == 1 && types[maze::kRoomVault] == 1, "the fountain (level 1) and the vault (level 2)");
    ctx.check(types[maze::kRoomSpawner] == 4, "two spawner rooms per level");
    ctx.check(types[maze::kEntranceShaft] == 1 && types[maze::kMound] == 1 && types[maze::kUpperEntrance] == 1, "the shaft, the mound and the ruin");
    int deadEnds = 0;
    for (auto const& [t, n] : types)
        if (t.rfind("gloamingforest:GloamingMMDE", 0) == 0) deadEnds += n;
    ctx.check(deadEnds > 10, "dead ends decorated: " + std::to_string(deadEnds));
    // the maze levels: 22 x 22 cells, level 2 ten blocks below level 1's floor, under its center,
    // its entrance at level 1's second room
    maze::MinotaurMaze const* l1 = nullptr;
    maze::MinotaurMaze const* l2 = nullptr;
    for (auto const& p : pieces)
        if (p->typeId == maze::kMaze) {
            auto const* m = static_cast<maze::MinotaurMaze const*>(p.get());
            (m->level() == 1 ? l1 : l2) = m;
        }
    ctx.require(l1 && l2, "both levels");
    ctx.check(l1->bbox.minY == -14 && l1->bbox.maxX - l1->bbox.minX == 112, "level 1: 14 below the ruins, 113 wide");
    ctx.check(l2->bbox.minY == -24, "level 2: 10 below level 1");
    ctx.check(l2->roomCoords()[0] == l1->roomCoords()[2] && l2->roomCoords()[1] == l1->roomCoords()[3], "level 2 enters at level 1's exit room");
    ctx.check(l1->roomCoords()[0] == 11 && l1->roomCoords()[1] == 11, "level 1 enters at (11, 11)");
    // spawn lists: none in the boss room, the vault and the chest dead ends; list 0 elsewhere
    for (auto const& p : pieces) {
        int si = static_cast<::gf::structure::bases::Component&>(*p).getSpawnIndex();
        if (maze::deniesSpawns(p->typeId)) ctx.check(si == INT_MAX, "no spawns in " + p->typeId);
        else ctx.check(si == 0, "spawn list 0 in " + p->typeId);
    }
    // persistence: every piece survives save / load (the maze levels rebuild their maze)
    for (auto const& p : pieces) {
        auto loaded = ::gf::structure::loadPiece(p->save());
        ctx.require(loaded != nullptr, "piece " + p->typeId + " loads back");
        ctx.check(loaded->bbox == p->bbox && loaded->save() == p->save(), "same box and saved data: " + p->typeId);
        if (p->typeId == maze::kMaze) {
            auto const& a = static_cast<maze::MinotaurMaze const&>(*p).grid();
            auto const& b = static_cast<maze::MinotaurMaze const&>(*loaded).grid();
            bool        same = true;
            for (int x = 0; x < a.width && same; x++)
                for (int z = 0; z < a.depth && same; z++)
                    for (auto [dx, dz] : {std::pair{1, 0}, std::pair{0, 1}}) same = same && a.isWall(x, z, x + dx, z + dz) == b.isWall(x, z, x + dx, z + dz);
            ctx.check(same, "the reloaded maze has the same walls");
        }
    }
    golden(ctx, "labyrinth_layout", kLayout, describe(types, pieces));
});

GF_REGISTER_SELFTEST("labyrinth.golden_hash", [](Context& ctx) {
    auto         pieces = layoutAt(3, -2, 0);
    MemoryRegion mem;
    generate(pieces, mem);
    // the Minoshroom's spawner at (7, 2, 7) of the boss room
    int bosses = 0;
    for (auto const& p : pieces)
        if (p->typeId == maze::kRoomBoss) {
            auto pos = p->getWorldPos(7, 2, 7);
            ctx.check(mem.idAt(pos) == maze::kBossSpawner, "the minoshroom spawner at (7, 2, 7) of the boss room: " + mem.idAt(pos));
            bosses++;
        }
    ctx.check(bosses == 1, "one boss room");
    ctx.check(mem.count(maze::kBossSpawner) == 1, "one minoshroom spawner");
    ctx.check(mem.count("minecraft:mob_spawner") == 4, "a minotaur spawner in each of the four spawner rooms: " + std::to_string(mem.count("minecraft:mob_spawner")));
    ctx.check(mem.count("gloamingforest:mazestone_brick") + mem.count("gloamingforest:mossy_mazestone") + mem.count("gloamingforest:cracked_mazestone") > 5000,
              "mazestone walls");
    ctx.check(mem.count("gloamingforest:mazestone_mosaic") > 20000, "mosaic floors");
    ctx.check(mem.count("minecraft:bedrock") > 1000, "level 2 is encased in bedrock");
    ctx.check(mem.count("minecraft:chest") + mem.count("minecraft:trapped_chest") > 8, "chests");
    ctx.check(mem.count("minecraft:mycelium") > 0 && mem.count("minecraft:red_mushroom_block") > 0, "the mushroom rooms");
    ctx.check(mem.count("minecraft:iron_bars") > 0 && mem.count("minecraft:tnt") > 0, "iron bars and TNT traps");
    golden(ctx, "labyrinth_hash", kHash, mem.hash());
});

GF_REGISTER_SELFTEST("labyrinth.structure_start", [](Context& ctx) -> ll::coro::CoroTask<> {
    ctx.require(co_await ctx.inGloaming(), "the Gloaming Forest is available");
    // a start forced far from the test rows (removed afterwards)
    auto pos = ctx.testPos(2390);
    pos.x += 8192;
    pos.z += 8192;
    {
        gf::landmark::ext::ScopedStart forced(maze::kLabyrinth, pos);
        ctx.require(forced && forced->generated, "forced a labyrinth start");
        ctx.check(forced->pieceBoxes.size() > 40, "the labyrinth has its pieces: " + std::to_string(forced->pieceBoxes.size()));
        auto at = gf::landmark::startAt(pos.dim, BlockPos{pos.x + 5, pos.y - 12, pos.z + 5});
        ctx.check(at && at->key == maze::kLabyrinth, "startAt finds the labyrinth in its maze");
        auto const* d = gf::landmark::descriptor(maze::kLabyrinth);
        ctx.check(d && !d->lockAdvancements.empty() && d->lockAdvancements.front() == "gloamingforest:progress_lich", "locked behind the lich");
        // the terraformer: the mound over the maze
        auto   c     = gf::core::labyrinth::boxCenter(forced->box);
        double mound = gf::core::labyrinth::terraformer(forced->box, c.x + 1, std::max(c.y + 8, 6), c.z + 2);
        ctx.check(mound > 0.0, "the mound is solid above the maze's center: " + std::to_string(mound));
    }
    co_return;
});

// ---- the mobs -------------------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("labyrinth.minoshroom_stats", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(2300, 16);
    auto            pos = core::GlobalPos{ctx.dimension(), box.minX + 8, box.minY + 1, box.minZ + 8};
    auto*           m   = spawnAt(pos, mobs::kMinoshroom);
    ctx.require(m != nullptr, "a minoshroom");
    auto uid = fe::uidOf(*m);
    co_await ctx.ticks(3);
    m = mobById(uid);
    ctx.require(m != nullptr, "still there");
    ctx.check(std::abs(m->getMaxHealth() - 120.0f) < 0.01f, "120 max health: " + std::to_string(m->getMaxHealth()));
    auto axe = game::items::idOf(m->getCarriedItem());
    ctx.check(axe == "gloamingforest:diamond_minotaur_axe", "holds a diamond minotaur axe: " + axe);
    ctx.check(std::abs(mobs::attackDamage(*m) - 11.0f) < 0.01f, "attack damage 2 + 9");
    ctx.check(std::abs(attr(*m, SharedAttributes::KNOCKBACK_RESISTANCE()) - 0.5f) < 0.01f, "knockback resistance 0.5");
    auto b = m->getAABB();
    ctx.check(std::abs(b.max.x - b.min.x - 1.49f) < 0.02f && std::abs(b.max.y - b.min.y - 2.5f) < 0.02f, "1.49 x 2.5");
    m->remove();
    discardAround(pos, 12);
});

GF_REGISTER_SELFTEST("labyrinth.ground_slam", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(2320, 16);
    auto            pos = core::GlobalPos{ctx.dimension(), box.minX + 8, box.minY + 1, box.minZ + 8};
    auto*           m   = spawnAt(pos, mobs::kMinoshroom);
    ctx.require(m != nullptr, "a minoshroom");
    auto uid = fe::uidOf(*m);
    ctx.player().setPlayerGameType(GameType::Survival);
    ctx.player().teleportTo(Vec3{pos.x + 3.5f, float(pos.y), pos.z + 0.5f}, true, 0, 1, false);
    co_await ctx.ticks(10);
    m = mobById(uid);
    ctx.require(m != nullptr, "still there");
    auto& p      = ctx.player();
    float before = p.getHealth();
    int   hit    = mobs::groundSlamForTest(*m);
    ctx.check(hit == 1, "the slam hits the player on the ground within 7.5: " + std::to_string(hit));
    float lost = before - ctx.player().getHealth();
    ctx.check(lost >= 3.0f && lost <= 9.0f, "about attack x 0.5 = 5.5 slam damage: " + std::to_string(lost));
    // a player 9 blocks away is out of the box
    ctx.player().teleportTo(Vec3{pos.x + 9.5f, float(pos.y), pos.z + 0.5f}, true, 0, 1, false);
    co_await ctx.ticks(5);
    m = mobById(uid);
    ctx.require(m != nullptr, "still there");
    ctx.check(mobs::groundSlamForTest(*m) == 0, "nobody hit 9 blocks away");
    m->remove();
    discardAround(pos, 12);
});

GF_REGISTER_SELFTEST("labyrinth.minotaur_axe_roll", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(2340, 12);
    auto            pos = core::GlobalPos{ctx.dimension(), box.minX + 6, box.minY + 1, box.minZ + 6};
    std::vector<std::int64_t> ids;
    for (int i = 0; i < 4; i++)
        if (auto* m = spawnAt(at(pos, i - 2, 0, 0), mobs::kMinotaur)) ids.push_back(fe::uidOf(*m));
    ctx.require(ids.size() == 4, "four minotaurs");
    co_await ctx.ticks(3);
    for (auto id : ids) {
        auto* m = mobById(id);
        if (!m) continue;
        auto axe = game::items::idOf(m->getCarriedItem());
        ctx.check(axe == "gloamingforest:gold_minotaur_axe" || axe == "minecraft:golden_axe", "a gold minotaur axe or a golden axe: " + axe);
        ctx.check(std::abs(mobs::attackDamage(*m) - 8.0f) < 0.01f, "attack 2 + 6");
        ctx.check(std::abs(m->getMaxHealth() - 30.0f) < 0.01f, "30 health");
        m->remove();
    }
    ctx.check(std::string(mobs::minotaurAxe(0, 2.0f)) == "gloamingforest:gold_minotaur_axe" && std::string(mobs::minotaurAxe(5, 2.0f)) == "minecraft:golden_axe",
              "the roll: (int)(nextInt(10) / (difficulty + 1)) == 0");
    discardAround(pos, 12);
});

GF_REGISTER_SELFTEST("labyrinth.maze_slime_health", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(2360, 12);
    auto            pos = core::GlobalPos{ctx.dimension(), box.minX + 6, box.minY + 1, box.minZ + 6};
    auto*           s   = spawnAt(pos, mobs::kMazeSlime);
    ctx.require(s != nullptr, "a maze slime");
    auto uid = fe::uidOf(*s);
    co_await ctx.ticks(2);
    s = mobById(uid);
    ctx.require(s != nullptr, "still there");
    mobs::setSlimeSize(*s, 4);
    co_await ctx.ticks(2);
    s = mobById(uid);
    ctx.require(s != nullptr, "still there");
    ctx.check(std::abs(s->getMaxHealth() - 48.0f) < 0.01f, "size 4: 3 x 16 = 48 health: " + std::to_string(s->getMaxHealth()));
    ctx.check(mobs::slimeSize(*s) == 4, "size 4");
    // Slime#remove: 2..4 slimes of size 2
    gf::combat::hurt(*s, 1000.0f, "minecraft:generic");
    co_await ctx.ticks(25);
    auto kids = around(pos, mobs::kMazeSlime, 6);
    ctx.check(kids.size() >= 2 && kids.size() <= 4, "it splits into 2..4: " + std::to_string(kids.size()));
    for (auto* k : kids) {
        auto* km = fe::asMob(*k);
        ctx.check(km && mobs::slimeSize(*k) == 2 && std::abs(km->getMaxHealth() - 12.0f) < 0.01f, "the children are size 2 with 12 health");
    }
    discardAround(pos, 12);
});

GF_REGISTER_SELFTEST("labyrinth.spawner_height_rule", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box     = co_await ctx.flatArea(2380, 16, 16);
    auto            spawner = core::GlobalPos{ctx.dimension(), box.minX + 8, box.minY + 2, box.minZ + 8};
    // a player within 9 but at spawner y + 4 or higher does not wake the Minoshroom
    ctx.player().setPlayerGameType(GameType::Survival);
    ctx.player().teleportTo(Vec3{spawner.x + 2.5f, float(spawner.y + 5), spawner.z + 0.5f}, true, 0, 1, false);
    ctx.require(ctx.setBlock(at(spawner, 2, 4, 0), "minecraft:glass"), "a platform above");
    ctx.require(ctx.setBlock(spawner, maze::kBossSpawner), "spawner placed");
    co_await ctx.ticks(40);
    ctx.check(around(spawner, mobs::kMinoshroom, 12).empty(), "no minoshroom for a player 5 above the spawner");
    ctx.player().teleportTo(Vec3{spawner.x + 2.5f, float(box.minY + 1), spawner.z + 0.5f}, true, 0, 1, false);
    bool spawned = false;
    for (int i = 0; i < 20 && !spawned; i++) {
        co_await ctx.ticks(5);
        spawned = !around(spawner, mobs::kMinoshroom, 12).empty();
    }
    ctx.check(spawned, "the minoshroom appears for a player below spawner y + 4");
    ctx.check(ctx.blockAt(spawner) != maze::kBossSpawner, "the spawner is used up");
    discardAround(spawner, 20);
});

// ---- the blocks ----------------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("labyrinth.tool_penalty", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2350, 8);
    auto pos = core::GlobalPos{ctx.dimension(), box.minX + 3, box.minY + 1, box.minZ + 3};
    ctx.require(ctx.setBlock(pos, "gloamingforest:mazestone"), "mazestone placed");
    ctx.require(ctx.setBlock(at(pos, 1, 0, 0), "minecraft:stone"), "stone placed");
    auto& p = ctx.player();
    p.setPlayerGameType(GameType::Survival);
    auto destroyWith = [&](char const* itemId, core::GlobalPos const& where) -> int {
        auto stack = game::items::make(itemId, 1, 0);
        if (!stack) return -1;
        auto&        player  = ctx.player();
        ItemStack    held    = *stack;
        auto*        region  = ctx.region();
        Block const& block   = region->getBlock(game::blockPos(where));
        std::string  blockId = game::blockId(block);
        std::string  id      = itemId;
        ia::DestroyContext c{player, held, id, block, blockId, where, 1, false};
        (void)ia::dispatchDestroy(c);
        return held.getDamageValue();
    };
    ctx.check(destroyWith("minecraft:iron_pickaxe", pos) == blocks::kMazestonePenalty, "an iron pickaxe loses 16 on mazestone");
    ctx.check(destroyWith("gloamingforest:mazebreaker_pickaxe", pos) == 0, "the mazebreaker loses nothing extra");
    ctx.check(destroyWith("minecraft:iron_pickaxe", at(pos, 1, 0, 0)) == 0, "stone is not mazestone");
    ctx.check(destroyWith("minecraft:stick", pos) == 0, "a stick is not damageable");
});

GF_REGISTER_SELFTEST("labyrinth.slime_block", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2310, 8);
    auto pos = core::GlobalPos{ctx.dimension(), box.minX + 3, box.minY, box.minZ + 3};
    ctx.require(ctx.setBlock(pos, blocks::kMazeSlimeBlock), "maze slime block placed");
    ctx.check(ctx.blockAt(pos) == blocks::kMazeSlimeBlock, "the block is there");
    ctx.player().setPlayerGameType(GameType::Survival);
    ctx.player().teleportTo(Vec3{pos.x + 0.5f, float(pos.y + 1), pos.z + 0.5f}, true, 0, 1, false);
    co_await ctx.ticks(10);
    auto& p      = ctx.player();
    float before = p.getHealth();
    bool  hurt   = gf::combat::hurt(p, 6.0f, "minecraft:fall");
    ctx.check(!hurt && std::abs(ctx.player().getHealth() - before) < 0.01f, "no fall damage on a maze slime block");
    // SlimeBlock#stepOn for a vanilla player (the SimulatedPlayer): a movement speed modifier while it
    // stands on the block (read on a tick it is on the ground there; it may still bounce after landing)
    float onBlock = -1.0f;
    for (int t = 0, grounded = 0; t < 60 && onBlock < 0.0f; ++t) {
        co_await ctx.ticks(1);
        grounded = ctx.player().isOnGround() ? grounded + 1 : 0;
        if (grounded >= 3) onBlock = attr(ctx.player(), SharedAttributes::MOVEMENT_SPEED());
    }
    // SlimeBlock#bounceUp: a living entity keeps its speed, others lose a fifth
    ctx.check(std::abs(blocks::bounceSpeed(-0.8, true) - 0.8) < 1e-9 && std::abs(blocks::bounceSpeed(-0.5, false) - 0.4) < 1e-9 && blocks::bounceSpeed(0.3, true) == 0.0,
              "the bounce speed");
    // a mob dropped onto the block bounces (updateEntityAfterFallOn from its last airborne speed)
    ctx.player().teleportTo(Vec3{pos.x + 3.5f, float(pos.y + 1), pos.z + 0.5f}, true, 0, 1, false);
    if (auto* pig = spawnAt(pos, "minecraft:pig", 0.5f, 7.0f, 0.5f)) {
        auto  uid     = pig->getOrCreateUniqueID().rawID;
        float landedY = 0.0f, peak = -1.0f;
        bool  landed  = false;
        for (int t = 0; t < 60; ++t) {
            co_await ctx.ticks(1);
            auto* m = mobById(uid);
            if (!m) break;
            float y = m->getFeetPos().y;
            if (!landed && m->isOnGround()) {
                landed  = true;
                landedY = y;
            } else if (landed) peak = std::max(peak, y - landedY);
        }
        ctx.info("a pig dropped from 6 blocks rose " + std::to_string(peak) + " blocks after landing");
        ctx.check(landed && peak > 1.0f, "a mob bounces on a maze slime block");
        if (auto* m = mobById(uid)) m->remove();
    } else ctx.fail("a pig spawns");
    {
        float off    = attr(ctx.player(), SharedAttributes::MOVEMENT_SPEED());
        float factor = core::labyrinth::slimeStepSpeedFactor(0.8F);
        ctx.info("movement speed on the block " + std::to_string(onBlock) + ", off it " + std::to_string(off));
        ctx.check(off > 0.0f && std::abs(onBlock - off * factor) < 0.002f, "a vanilla player walks slowed on a maze slime block (stepOn)");
    }
    // the sticky piston and the other maze slime recipes are loaded
    ctx.check(gf::recipes::byId("gloamingforest:compressed_blocks/maze_slime") != nullptr, "maze slime block recipe");
    ctx.check(gf::recipes::byId("gloamingforest:sticky_piston_maze_ver") != nullptr, "sticky piston recipe");
    ctx.check(gf::recipes::byId("gloamingforest:lead_maze_ver") != nullptr, "lead recipe");
    ctx.check(gf::recipes::byId("gloamingforest:magma_cream_maze_ver") != nullptr, "magma cream recipe");
    ctx.check(gf::recipes::byId("gloamingforest:compressed_blocks/reversed/maze_slime_block") != nullptr, "maze slime balls recipe");
});

// ---- client acceptance (tools/e2e/client/scenarios/labyrinth.ps1) ---------------------------------------------------
//
// With a real player (the client e2e): a level 1 maze with its rooms and dead ends generated in a
// 144 x 144 flat area (open to the sky: level 1 only replaces an existing ceiling), the boss room
// and the vault beside it, the three mobs and the maze slime block; the player is moved to look at
// each. Without a real player it only checks that it can find none.
GF_REGISTER_SELFTEST("labyrinth.client_showcase", [](Context& ctx) -> ll::coro::CoroTask<> {
    std::int64_t realId = 0;
    if (auto level = ll::service::getLevel())
        level->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
            if (!p.isSimulatedPlayer()) realId = p.getOrCreateUniqueID().rawID;
            return realId == 0;
        }));
    if (!realId) {
        ctx.info("no real player: nothing to show");
        co_return;
    }
    DifficultyScope normal(Difficulty::Normal);
    auto            box    = co_await ctx.flatArea(2300, Context::kMaxFlatAreaSize, 24);
    auto*           region = ctx.region();
    ctx.require(region != nullptr, "test region");
    int dim = ctx.testPos(2300).dim;
    // stone under the maze's floor (the area's surface layer is box.minY)
    int  floorY = box.minY + 1;
    auto pieces = maze::PieceList{};
    {
        core::jrand::WorldgenRandom context(std::make_unique<core::jrand::LegacyRandom>(0));
        context.setLargeFeatureSeed(kSeed, 3, -2);
        // the ruins at (x + 5, y, z + 5): level 1's box starts 55 west / north of them
        maze::layout(context, box.minX + 55 - 5, floorY + 14, box.minZ + 55 - 5, pieces);
    }
    ::gf::structure::BlockSourceRegion world(*region, BlockPos{box.minX + 72, floorY, box.minZ + 72});
    int placed = 0;
    for (auto& p : pieces) {
        bool level1 = p->bbox.minY == floorY && p->typeId != maze::kEntranceShaft;
        if (!level1) continue;
        core::jrand::LegacyRandom r(placed + 1);
        p->postProcess(world, r, box, BlockPos{0, 0, 0});
        placed++;
    }
    ctx.check(placed > 20, "level 1 placed: " + std::to_string(placed) + " pieces");
    // the boss room and the vault on the strip east of the maze
    {
        core::jrand::LegacyRandom r(2301);
        maze::Room boss(maze::kRoomBoss, 4, r, box.maxX - 17, floorY, box.minZ + 4);
        boss.postProcess(world, r, box, BlockPos{0, 0, 0});
        maze::Room vault(maze::kRoomVault, 7, r, box.maxX - 17, floorY, box.minZ + 24);
        vault.postProcess(world, r, box, BlockPos{0, 0, 0});
    }
    auto* real = fe::playerById(realId);
    ctx.require(real != nullptr, "the real player");
    std::string name = "\"" + real->getRealName() + "\"";
    auto        run  = [&](std::string const& cmd) { (void)gf::selftest::runServerCommand(cmd, dim); };
    auto        xyz  = [](int x, int y, int z) { return std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z); };
    run("gamemode spectator " + name);
    run("effect " + name + " night_vision 120 1 true");
    // the maze from above its entrance room
    run("tp " + name + " " + xyz(box.minX + 40, floorY + 22, box.minZ + 40) + " facing " + xyz(box.minX + 60, floorY, box.minZ + 60));
    co_await ctx.ticks(100);
    ctx.info("maze ready");
    co_await ctx.ticks(40);
    // in a corridor
    run("tp " + name + " " + xyz(box.minX + 58, floorY + 2, box.minZ + 58) + " facing " + xyz(box.minX + 70, floorY + 2, box.minZ + 58));
    co_await ctx.ticks(60);
    ctx.info("corridor ready");
    co_await ctx.ticks(40);
    // the boss room (spawner, mushroom chest shelves) and the vault
    run("tp " + name + " " + xyz(box.maxX - 9, floorY + 12, box.minZ + 2) + " facing " + xyz(box.maxX - 9, floorY, box.minZ + 12));
    co_await ctx.ticks(60);
    ctx.info("rooms ready");
    co_await ctx.ticks(40);
    // the mobs on the strip south of the rooms
    auto mobsAt = core::GlobalPos{dim, box.maxX - 12, floorY + 1, box.minZ + 50};
    run("fill " + xyz(box.maxX - 20, floorY, box.minZ + 44) + " " + xyz(box.maxX - 2, floorY, box.minZ + 64) + " stone");
    run("fill " + xyz(box.maxX - 20, floorY + 1, box.minZ + 44) + " " + xyz(box.maxX - 2, floorY + 8, box.minZ + 64) + " air");
    std::vector<std::int64_t> shown;
    if (auto* m = spawnAt(at(mobsAt, -4, 0, 0), mobs::kMinoshroom)) shown.push_back(fe::uidOf(*m));
    if (auto* m = spawnAt(at(mobsAt, 0, 0, 0), mobs::kMinotaur)) shown.push_back(fe::uidOf(*m));
    for (int s : {1, 2, 4})
        if (auto* m = spawnAt(at(mobsAt, 2 + s, 0, 0), mobs::kMazeSlime)) {
            mobs::setSlimeSize(*m, s);
            shown.push_back(fe::uidOf(*m));
        }
    ctx.setBlock(at(mobsAt, -4, 0, 4), blocks::kMazeSlimeBlock);
    run("tp " + name + " " + xyz(mobsAt.x, mobsAt.y + 2, mobsAt.z + 10) + " facing " + xyz(mobsAt.x, mobsAt.y + 1, mobsAt.z));
    co_await ctx.ticks(60);
    ctx.info("mobs ready");
    co_await ctx.ticks(40);
    // the Minoshroom's ground slam pose (fully charged)
    if (auto* m = mobById(shown.empty() ? 0 : shown.front())) {
        gf::entity::setProperty(*m, "gloamingforest:ground_charge", 10);
        gf::entity::setProperty(*m, "gloamingforest:ground_attack", true);
    }
    co_await ctx.ticks(40);
    ctx.info("slam ready");
    co_await ctx.ticks(40);
    // the items and spawn eggs in the hotbar
    char const* items[] = {"gloamingforest:maze_slime_ball", blocks::kMazeSlimeBlock, "gloamingforest:minotaur_spawn_egg", "gloamingforest:minoshroom_spawn_egg",
                           "gloamingforest:maze_slime_spawn_egg"};
    run("gamemode creative " + name);
    for (auto const* id : items) run("give " + name + " " + std::string(id) + " 1");
    co_await ctx.ticks(60);
    ctx.info("items ready");
    co_await ctx.ticks(40);
    for (auto const* id : items) run("clear " + name + " " + std::string(id) + " -1 1");
    run("effect " + name + " clear");
    for (auto id : shown)
        if (auto* a = fe::actorById(id)) a->remove();
    discardAround(mobsAt, 20);
});

// ---- landmark walk-through (audit A3) ------------------------------------------------------------------

namespace {

/// gloamingEnforcedProgression for the length of a test.
struct WalkEnforcedScope {
    bool previous = false;
    WalkEnforcedScope() {
        previous = gf::gamerules::getBool("gloamingEnforcedProgression");
        gf::gamerules::set("gloamingEnforcedProgression", true);
    }
    ~WalkEnforcedScope() { gf::gamerules::set("gloamingEnforcedProgression", previous); }
};

/// The landmark of a boss against upstream's structure record (LandmarkStructure: AdvancementLockConfig,
/// HintConfig, ConquerableStructure) and what the lock does to a survival player: a start forced far
/// from the test rows (its chunks never load: nothing is placed) refuses a player without the lock's
/// advancement inside its protected pieces (ProgressionEvents#isAreaProtected) and lets him in once
/// he has it. `lock` empty: the landmark has no lock and protects nothing.
void walkLandmark(Context& ctx, std::string const& key, std::string const& lock, std::string const& hintBook, int farX) {
    auto const* d = gf::landmark::descriptor(key);
    ctx.require(d != nullptr, key + " has a descriptor");
    ctx.check(d->conquerable, key + " is conquerable");
    if (lock.empty()) ctx.check(d->lockAdvancements.empty(), key + " has no progression lock");
    else ctx.check(d->lockAdvancements == std::vector<std::string>{lock}, key + " is locked by " + lock + " (" + std::to_string(d->lockAdvancements.size()) + " locks)");
    ctx.check(d->hintBookKey == (hintBook.empty() ? std::string() : "gloamingforest.book." + hintBook), key + "'s hint book (" + d->hintBookKey + ")");
    WalkEnforcedScope enforced;
    core::GlobalPos   site{ctx.dimension(), farX, 80, 150007};
    gf::landmark::ext::ScopedStart forced(key, site);
    ctx.require(forced && forced->generated, "forced a " + key + " start");
    auto pieces = gf::landmark::ext::startPieces(*forced);
    ctx.require(!pieces.empty(), key + " has pieces");
    auto& sp = ctx.spawnPlayer("gf_walk");
    sp.setPlayerGameType(GameType::Survival);
    int protectedPieces = 0, refused = 0;
    std::optional<BlockPos> probe;
    for (auto const& p : pieces) {
        if (!p.protectedPiece) continue;
        ++protectedPieces;
        BlockPos c{(p.box.minX + p.box.maxX) / 2, (p.box.minY + p.box.maxY) / 2, (p.box.minZ + p.box.maxZ) / 2};
        if (gf::landmark::isProtectedFor(sp, c)) {
            ++refused;
            if (!probe) probe = c;
        }
    }
    ctx.info(key + ": " + std::to_string(pieces.size()) + " pieces, " + std::to_string(protectedPieces) + " protected, " + std::to_string(refused) +
             " refuse a player without " + (lock.empty() ? std::string("a lock") : lock));
    BlockPos outside{forced->box.maxX + 40, forced->center.y, forced->box.maxZ + 40};
    ctx.check(!gf::landmark::isProtectedFor(sp, outside), "nothing is protected outside the start");
    if (lock.empty()) {
        ctx.check(refused == 0, key + " protects nothing");
        return;
    }
    ctx.check(protectedPieces > 0 && refused > 0, key + "'s protected pieces refuse a player without " + lock);
    if (!probe) return;
    gf::progression::grant(sp, lock);
    ctx.check(!gf::landmark::isProtectedFor(sp, *probe), "with " + lock + " the landmark is open");
    gf::progression::revoke(sp, lock);
    ctx.check(gf::landmark::isProtectedFor(sp, *probe), "without it the landmark is locked again");
    sp.setPlayerGameType(GameType::Creative);
    ctx.check(!gf::landmark::isProtectedFor(sp, *probe), "creative players are never refused");
}

} // namespace

// The labyrinth against upstream's LabyrinthStructure record: locked by progress_lich, the labyrinth hint
// book, conquerable; its protected pieces refuse a player without progress_lich.
GF_REGISTER_SELFTEST("labyrinth.landmark_walk", [](Context& ctx) { walkLandmark(ctx, maze::kLabyrinth, "gloamingforest:progress_lich", "labyrinth", 150307); });

// The boss gauntlet (audit A3): a player in range wakes the Minoshroom at its spawner (home = the
// spawner), hits it and kills it; the mangrove death chest at the home point holds the trophy and
// the meef stroganoff, the labyrinth is the conquered home landmark, and progress_labyrinth's inputs
// fired (player_killed_entity of the Minoshroom, hurt_boss).
GF_REGISTER_SELFTEST("labyrinth.gauntlet", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    static std::map<std::string, int> triggers;
    static std::string                killedEntity;
    static bool                       listening = false;
    if (!std::exchange(listening, true))
        for (char const* id : {"minecraft:player_killed_entity", "gloamingforest:hurt_boss"})
            gf::progression::onTrigger(id, [id = std::string(id)](Player& player, nlohmann::json const& data) {
                if (player.getNameTag() != "gf_minoshroom_gauntlet") return;
                triggers[id]++;
                if (id == "minecraft:player_killed_entity") killedEntity = data.value("entity", std::string());
            });
    triggers.clear();
    killedEntity.clear();
    gf::feature::boss::resetTestRecords();
    auto box     = co_await ctx.flatArea(2320, 16);
    auto spawner = ctx.testPos(2328, 2).offset(0, 1, 0);
    ctx.require(ctx.setBlock(spawner, "gloamingforest:minoshroom_boss_spawner"), "the minoshroom boss spawner is placed");
    auto& sp  = ctx.spawnPlayer("gf_minoshroom_gauntlet");
    auto  pid = sp.getOrCreateUniqueID().rawID;
    sp.teleport(Vec3{spawner.x + 0.5f, float(spawner.y), spawner.z + 4.5f}, DimensionType(spawner.dim));
    (void)gf::selftest::runServerCommand("effect \"" + sp.getRealName() + "\" resistance 60 255 true", spawner.dim);
    std::int64_t uid = 0;
    for (int t = 0; t < 80 && !uid; ++t) {
        co_await ctx.ticks(1);
        auto found = around(spawner, mobs::kMinoshroom, 8.0f);
        if (!found.empty()) uid = found.front()->getOrCreateUniqueID().rawID;
    }
    ctx.require(uid != 0, "the spawner woke the Minoshroom");
    ctx.check(ctx.blockAt(spawner) != "gloamingforest:minoshroom_boss_spawner", "the spawner is gone");
    auto* m = mobById(uid);
    ctx.require(m != nullptr, "the Minoshroom is there");
    auto home = gf::entity::home(*m);
    ctx.check(home.has_value() && home->pos.x == spawner.x && home->pos.z == spawner.z, "its home is the spawner");
    auto* p = fe::actorById(pid);
    ctx.require(p != nullptr, "the player is there");
    gf::combat::hurt(*m, 5.0f, "minecraft:player_attack", p);
    co_await ctx.ticks(12);
    m = mobById(uid);
    p = fe::actorById(pid);
    ctx.require(m && p, "the Minoshroom and the player are there");
    gf::combat::hurt(*m, 1000.0f, "minecraft:player_attack", p);
    std::optional<core::GlobalPos> chest;
    for (int t = 0; t < 200 && !chest; ++t) {
        co_await ctx.ticks(1);
        chest = gf::feature::boss::lastDeathChest();
    }
    ctx.require(chest.has_value(), "the death chest was placed");
    co_await ctx.ticks(3);
    ctx.check(ctx.blockAt(*chest) == "gloamingforest:mangrove_chest", "a mangrove death chest (" + ctx.blockAt(*chest) + ")");
    bool trophy = false, stroganoff = false;
    if (auto* region = game::regionOf(chest->dim))
        if (auto inv = gf::inventory::find(*region, game::blockPos(*chest)); inv && inv->get) {
            for (int i = 0; i < inv->slots; ++i) {
                auto id = game::items::idOf(inv->get(*chest, i));
                trophy |= id == "gloamingforest:minoshroom_trophy";
                stroganoff |= id == "gloamingforest:meef_stroganoff";
            }
            if (inv->set)
                for (int i = 0; i < inv->slots; ++i) inv->set(*chest, i, game::items::empty());
        }
    ctx.check(trophy, "the chest holds the minoshroom trophy");
    ctx.check(stroganoff, "the chest holds the meef stroganoff");
    auto conquest = gf::feature::boss::lastConquest();
    ctx.check(conquest && conquest->second == maze::kLabyrinth, "the Minoshroom's postmortem conquers its home structure key, the labyrinth, at its home (no start stands in the test area) (" + (conquest ? conquest->second : std::string("none")) + ")");
    ctx.check(triggers["minecraft:player_killed_entity"] >= 1 && killedEntity == mobs::kMinoshroom, "player_killed_entity fired for " + killedEntity);
    ctx.check(triggers["gloamingforest:hurt_boss"] >= 1, "hurt_boss fired");
    ctx.cleanupBlock(*chest);
    discardAround(spawner, 16.0f);
    (void)box;
});

// The real conquest (BaseTFBoss#postmortem -> LandmarkUtil#markStructureConquered): a labyrinth start
// forced far from the test rows (its chunks never load: nothing is placed), the Minoshroom spawned in
// the test area with its home point at the start's center, killed: the start is conquered. The death
// chest belongs to the home point's unloaded chunk, so the dying record is dropped without it
// (labyrinth.gauntlet checks the chest).
GF_REGISTER_SELFTEST("labyrinth.conquest", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    gf::feature::boss::resetTestRecords();
    auto            box  = co_await ctx.flatArea(2340, 12);
    auto            at   = ctx.testPos(2346, 2).offset(0, 1, 0);
    core::GlobalPos site{ctx.dimension(), 152340, 80, 160007};
    (void)gf::landmark::ext::forgetStartForTest(site, maze::kLabyrinth);
    std::optional<gf::landmark::ext::ScopedStart> forced;
    forced.emplace(maze::kLabyrinth, site);
    ctx.require(*forced && (*forced)->generated, "forced a labyrinth start");
    auto home  = (*forced)->center;
    auto start = gf::landmark::startAt(home.dim, game::blockPos(home));
    ctx.require(start.has_value() && start->key == maze::kLabyrinth, "the home point is inside the labyrinth start");
    ctx.check(!gf::landmark::isConquered(*start), "the labyrinth is not conquered before the fight");
    auto* m = spawnAt(at, mobs::kMinoshroom);
    ctx.require(m != nullptr, "a Minoshroom");
    auto uid = m->getOrCreateUniqueID().rawID;
    gf::entity::setHome(*m, home, 20);
    co_await ctx.ticks(2);
    m = mobById(uid);
    ctx.require(m != nullptr, "the Minoshroom ticks");
    m->kill();
    co_await ctx.ticks(2);
    gf::feature::boss::dropDyingForTest(uid);
    auto after = gf::landmark::startAt(home.dim, game::blockPos(home));
    ctx.check(after.has_value() && gf::landmark::isConquered(*after), "the Minoshroom's death conquered the labyrinth");
    ctx.check(gf::feature::boss::lastConquestFound(), "and the boss kit found the start");
    if (auto* a = fe::actorById(uid)) a->remove();
    forced.reset();
    discardAround(at, 12.0f);
    (void)box;
});

} // namespace gf::feature::labyrinth::tests
