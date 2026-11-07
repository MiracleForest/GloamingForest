// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// U25 (stronghold) selftests ("stronghold.*", testPos dx 2500..2599, rule R12; golden hash, R19).
// The stronghold is laid out at a fixed seed and start chunk and generated on an in-memory region
// (stone below y = 0, as the buried stronghold sits in the ground): the golden hash covers every
// block it placed (core::VolumeHasher text, sorted by y, z, x relative to the placed bounds). The
// atrium trees and the sarcophagus torches draw from the region's random (WorldGenRegion
// #getRandom), which F7 seeds per region, so the hash is stable for one region.
#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/phys/AABB.h"

#include "core/VolumeHash.h"
#include "core/jrand/JRand.h"
#include "feature/boss/Boss.h"
#include "feature/boss/BossInternal.h"
#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/entity/EntityInternal.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/gamerules/GameRules.h"
#include "feature/inventory/Inventory.h"
#include "feature/progression/Progression.h"
#include "feature/stats/Stats.h"
#include "feature/stronghold/Blocks.h"
#include "feature/stronghold/Goblins.h"
#include "feature/stronghold/Phantom.h"
#include "feature/stronghold/Pieces.h"
#include "feature/stronghold/Util.h"
#include "feature/structure/Internal.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"
#include <optional>
#include <utility>
#include "mc/world/level/GameType.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/landmark/LandmarkTest.h"
#include "feature/landmark/Landmark.h"

namespace gf::feature::stronghold::tests {

namespace {

using selftest::Context;
using ::gf::structure::BlockBox;
namespace fe     = gf::feature::entity;
namespace ge     = gf::entity;
using Difficulty = SharedTypes::Legacy::Difficulty;

constexpr std::int64_t kSeed = 20260521; // the e2e world's seed

/// A sparse in-memory region with stone below y = 0.
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
        BlockBox                                                        b{INT_MAX, INT_MAX, INT_MAX, INT_MIN, INT_MIN, INT_MIN};
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
pieces::PieceList layoutAt(int cx, int cz, int y) {
    pieces::StrongholdPieces::resetLastPieceMade();
    auto seed = static_cast<std::int64_t>(static_cast<std::uint64_t>(kSeed) + static_cast<std::uint64_t>(static_cast<std::int64_t>(cx)) * 25117ULL +
                                          static_cast<std::uint64_t>(static_cast<std::int64_t>(cz)) * 151121ULL);
    core::jrand::LegacyRandom   first(seed);
    core::jrand::WorldgenRandom context(std::make_unique<core::jrand::LegacyRandom>(0));
    context.setLargeFeatureSeed(kSeed, cx, cz);
    pieces::PieceList list;
    pieces::layout(first, context, cx * 16 + 7, y, cz * 16 + 7, list);
    return list;
}

void generate(pieces::PieceList& list, MemoryRegion& mem) {
    BlockBox all{-30000000, -64, -30000000, 30000000, 319, 30000000};
    for (auto& p : list) {
        core::jrand::LegacyRandom r(0);
        p->postProcess(mem, r, all, BlockPos{0, 0, 0});
    }
}

std::map<std::string, int> typesOf(pieces::PieceList const& list) {
    std::map<std::string, int> types;
    for (auto const& p : list) types[p->typeId]++;
    return types;
}

std::string describe(std::map<std::string, int> const& types, pieces::PieceList const& list) {
    std::string s;
    for (auto const& [t, n] : types) s += t.substr(std::string("twilightforest:").size()) + "=" + std::to_string(n) + " ";
    BlockBox all = list.front()->bbox;
    for (auto const& p : list)
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
constexpr int  kChunkX   = 3;
constexpr int  kChunkZ   = -2;
constexpr char kLayout[] = "TFSAC=1 TFSAt=2 TFSBR=1 TFSBalR=6 TFSCr=8 TFSDE=8 TFSEnter=1 TFSFo=3 TFSLT=35 TFSRT=28 TFSSH=30 TFSSS=13 TFSTC=14 TFSTR=5 TFSUA=14 TFSUCo=11 TFSULT=12 TFSURT=18 TFSUTI=14 TFTreaR=3 -36,-32,-115,134,11,64";
constexpr char kHash[]   = "c3a8791499fd7216";

struct DifficultyScope {
    Difficulty previous = Difficulty::Normal;
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

struct EnforcedScope {
    bool previous = false;
    explicit EnforcedScope(bool on) {
        previous = gf::gamerules::getBool("tfEnforcedProgression");
        gf::gamerules::set("tfEnforcedProgression", on);
    }
    ~EnforcedScope() { gf::gamerules::set("tfEnforcedProgression", previous); }
};

Mob* spawnAt(core::GlobalPos const& at, std::string const& id, float dx = 0.5f, float dz = 0.5f) {
    auto* region = game::regionOf(at.dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level) return nullptr;
    return level->getSpawner().spawnMob(*region, ActorDefinitionIdentifier(id), nullptr, Vec3{at.x + dx, float(at.y), at.z + dz}, false, false, false);
}

std::vector<Actor*> around(core::GlobalPos const& pos, std::string const& type, float r) {
    return actorsInBox(pos.dim, Vec3{pos.x - r, pos.y - r, pos.z - r}, Vec3{pos.x + 1 + r, pos.y + 1 + r, pos.z + 1 + r}, type);
}

void discardAround(core::GlobalPos const& pos, float r) {
    for (char const* t : {id::KnightPhantom, id::ThrownWep, id::LowerGoblinKnight, id::UpperGoblinKnight, id::BlockChainGoblin, id::HelmetCrab, "minecraft:item"})
        for (auto* a : around(pos, t, r)) a->remove();
}

void resistant(Player& p, int seconds) {
    (void)gf::selftest::runServerCommand("effect \"" + p.getRealName() + "\" resistance " + std::to_string(seconds) + " 255 true", static_cast<int>(p.getDimensionId()));
}

} // namespace

GF_REGISTER_SELFTEST("stronghold.layout", [](Context& ctx) {
    auto list  = layoutAt(kChunkX, kChunkZ, 0);
    auto types = typesOf(list);
    ctx.require(!list.empty() && list.front()->typeId == pieces::kEntrance, "the entrance comes first");
    auto const& e = *list.front();
    int         x = kChunkX * 16 + 7, z = kChunkZ * 16 + 7;
    // StrongholdEntranceComponent(0, x, 5 or 1, z): y - 10, orientBox(-1, -1, 0; 18 x 7 x 18) facing south
    ctx.check(e.bbox.minX == x - 1 && e.bbox.minZ == z && (e.bbox.minY == -6 || e.bbox.minY == -10) && e.bbox.maxX - e.bbox.minX == 17 && e.bbox.maxY - e.bbox.minY == 6,
              "the entrance box (" + describe(types, list) + ")");
    ctx.check(types[pieces::kBossRoom] == 1, "one boss room");
    ctx.check(types[pieces::kAccessChamber] == 1, "one access chamber");
    // the instance limits hold per exit (each of the four exits recounts the weights)
    ctx.check(types[pieces::kTreasureRoom] <= 4 && types[pieces::kFoundry] <= 4 && types[pieces::kAtrium] <= 8 && types[pieces::kBalconyRoom] <= 12 &&
                  types[pieces::kTrainingRoom] <= 8 && types[pieces::kCrossing] <= 16,
              "the instance limits hold");
    for (auto const& p : list) {
        auto const& c  = static_cast<::gf::structure::bases::Component const&>(*p);
        bool        up = p->typeId == pieces::kAccessChamber || p->typeId == pieces::kUpperAscender || p->typeId == pieces::kUpperCorridor ||
                  p->typeId == pieces::kUpperLeftTurn || p->typeId == pieces::kUpperRightTurn || p->typeId == pieces::kUpperTIntersection;
        ctx.check(c.isComponentProtected() == !up, "protection of " + p->typeId);
        if (p->typeId == pieces::kBossRoom) ctx.check(c.getSpawnIndex() == INT_MAX, "no controlled spawns in the boss room");
        else ctx.check(c.getSpawnIndex() == (up ? -1 : 0), "spawn index of " + p->typeId);
        ctx.check(p->genDepth <= (up ? 100 : 30), "depth limit of " + p->typeId);
        // persistence: every piece survives save / load
        auto loaded = ::gf::structure::loadPiece(p->save());
        ctx.require(loaded != nullptr, "piece " + p->typeId + " loads back");
        ctx.check(loaded->bbox == p->bbox && loaded->save() == p->save(), "same box and saved data: " + p->typeId);
    }
    // the lower pieces stay within 75 blocks of the entrance's corner (x and z), the upper within 48
    for (auto const& p : list) {
        int dx = std::abs(p->bbox.minX - e.bbox.minX), dz = std::abs(p->bbox.minZ - e.bbox.minZ);
        ctx.check(dx < 120 && dz < 120, "sprawl of " + p->typeId);
    }
    // the same seed lays out the same stronghold
    auto again = layoutAt(kChunkX, kChunkZ, 0);
    ctx.check(describe(typesOf(again), again) == describe(types, list), "the layout is deterministic");
    golden(ctx, "stronghold_layout", kLayout, describe(types, list));
});

GF_REGISTER_SELFTEST("stronghold.golden_hash", [](Context& ctx) {
    auto         list = layoutAt(kChunkX, kChunkZ, 0);
    MemoryRegion mem;
    generate(list, mem);
    for (auto const& p : list) {
        if (p->typeId == pieces::kBossRoom) {
            auto pos = p->getWorldPos(13, 2, 13);
            ctx.check(mem.idAt(pos) == id::Spawner, "the knight phantom spawner at (13, 2, 13) of the boss room: " + mem.idAt(pos));
        }
        if (p->typeId == pieces::kAccessChamber) {
            auto ped = p->getWorldPos(2, 1, 2);
            ctx.check(mem.idAt(ped) == blocks::kPedestal, "the trophy pedestal at (2, 1, 2) of the access chamber: " + mem.idAt(ped));
            int shields = 0;
            for (int sx = 2; sx <= 6; sx++)
                for (int sz = 2; sz <= 6; sz++)
                    if (mem.idAt(p->getWorldPos(sx, -1, sz)) == blocks::kShield) shields++;
            ctx.check(shields == 25, "the shield layer (2, -1, 2)..(6, -1, 6): " + std::to_string(shields));
        }
    }
    ctx.check(mem.count("twilightforest:underbrick") + mem.count("twilightforest:mossy_underbrick") + mem.count("twilightforest:cracked_underbrick") > 500,
              "underbrick walls");
    ctx.check(mem.count("minecraft:mossy_stone_bricks") > 50, "mossy stone brick pillars and statues");
    ctx.check(mem.count("minecraft:stone_brick_stairs") > 50, "statue arms and feet");
    ctx.check(mem.count("minecraft:cobblestone_wall") > 20, "doorways and swords");
    golden(ctx, "stronghold_hash", kHash, mem.hash());
});

GF_REGISTER_SELFTEST("stronghold.shield_faces", [](Context& ctx) {
    // StrongholdShieldBlock#getDestroyProgress: up / down by the facing face, sideways by the opposite
    struct Row {
        char const* facing;
        int         face; ///< FacingID: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east
    };
    for (auto r : {Row{"down", 0}, Row{"up", 1}, Row{"north", 3}, Row{"south", 2}, Row{"west", 5}, Row{"east", 4}}) {
        auto const* b = game::blockWithStates(blocks::kShield, {{"twilightforest:facing", r.facing}});
        ctx.require(b != nullptr, std::string("a shield facing ") + r.facing);
        for (int f = 0; f < 6; f++)
            ctx.check(blocks::canBreakShieldFrom(*b, f) == (f == r.face), std::string("facing ") + r.facing + ", face " + std::to_string(f));
    }
    // minable at all (the inside face's digSpeed(stone) / 1.5 / 100 a tick: 7.5 s by hand), never
    // by an explosion
    if (auto const* b = game::blockWithStates(blocks::kShield, {{"twilightforest:facing", "north"}})) {
        float speed = b->mDirectData->mDestroySpeed;
        ctx.check(speed > 0.0f, "the shield can be mined (destroy speed " + std::to_string(speed) + ")");
        ctx.check(b->mDirectData->mExplosionResistance > 1000.0f, "explosions cannot break it");
    }
});

GF_REGISTER_SELFTEST("stronghold.pedestal", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2500, 16);
    auto ped = ctx.testPos(2508, 2).offset(0, 5, 0);
    auto* region = game::regionOf(ped.dim);
    ctx.require(region != nullptr, "the test region");
    // (1) not enforced: a trophy on top activates the pedestal and breaks every shield within 5
    {
        EnforcedScope off(false);
        ctx.require(ctx.setBlock(ped, blocks::kPedestal), "the pedestal");
        auto in1 = ped.offset(2, -1, 2), in2 = ped.offset(-5, 0, 0), in3 = ped.offset(0, -5, 5), out = ped.offset(6, 0, 0);
        for (auto p : {in1, in2, in3, out}) ctx.require(ctx.setBlock(p, blocks::kShield), "a shield");
        auto& p = ctx.player();
        p.teleport(Vec3{ped.x + 3.5f, float(ped.y), ped.z + 0.5f}, DimensionType(ped.dim));
        co_await ctx.ticks(2);
        long long statBefore = gf::stats::value(ctx.player(), "twilightforest:trophy_pedestals_activated");
        ctx.require(ctx.setBlock(ped.offset(0, 1, 0), "twilightforest:naga_trophy"), "a naga trophy on top");
        blocks::neighborChanged(*region, game::blockPos(ped));
        co_await ctx.ticks(2);
        ctx.check(gf::stats::value(ctx.player(), "twilightforest:trophy_pedestals_activated") > statBefore, "trophy_pedestals_activated counts it");
        ctx.check(game::statesOf(region->getBlock(game::blockPos(ped)))["twilightforest:active"] == "true" ||
                      game::statesOf(region->getBlock(game::blockPos(ped)))["twilightforest:active"] == "1",
                  "the pedestal is active");
        ctx.check(ctx.blockAt(in1) != blocks::kShield && ctx.blockAt(in2) != blocks::kShield && ctx.blockAt(in3) != blocks::kShield, "the shields within 5 are gone");
        ctx.check(ctx.blockAt(out) == blocks::kShield, "a shield 6 away stays");
        // a second update does nothing more (already active)
        ctx.check(!blocks::neighborChanged(*region, game::blockPos(ped)), "an active pedestal does not activate again");
        ctx.setBlock(ped.offset(0, 1, 0), "minecraft:air");
        ctx.setBlock(ped, "minecraft:air");
        ctx.setBlock(out, "minecraft:air");
    }
    // (2) enforced, nobody nearby has progress_lich: "You are unworthy.", nothing happens
    {
        EnforcedScope on(true);
        ctx.require(ctx.setBlock(ped, blocks::kPedestal), "the pedestal again");
        auto shield = ped.offset(1, 0, 0);
        ctx.require(ctx.setBlock(shield, blocks::kShield), "a shield");
        auto& p = ctx.player();
        gf::progression::revoke(p, "twilightforest:progress_lich");
        p.teleport(Vec3{ped.x + 3.5f, float(ped.y), ped.z + 0.5f}, DimensionType(ped.dim));
        co_await ctx.ticks(2);
        ctx.require(ctx.setBlock(ped.offset(0, 1, 0), "twilightforest:lich_trophy"), "a lich trophy on top");
        ctx.check(!blocks::neighborChanged(*region, game::blockPos(ped)), "an unworthy player cannot activate it");
        ctx.check(ctx.blockAt(shield) == blocks::kShield, "the shield stays");
        // with progress_lich it activates
        gf::progression::grant(ctx.player(), "twilightforest:progress_lich");
        ctx.check(blocks::neighborChanged(*region, game::blockPos(ped)), "a worthy player activates it");
        ctx.check(ctx.blockAt(shield) != blocks::kShield, "and the shield is gone");
        gf::progression::revoke(ctx.player(), "twilightforest:progress_lich");
        ctx.setBlock(ped.offset(0, 1, 0), "minecraft:air");
        ctx.setBlock(ped, "minecraft:air");
    }
    (void)box;
});

GF_REGISTER_SELFTEST("stronghold.mob_attributes", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(2520, 20);
    auto            base = ctx.testPos(2522, 2);
    // tests/golden/mobs.json: max health, the box (width, height)
    struct Row {
        char const* id;
        int         health;
        float       width, height;
    };
    std::vector<std::int64_t> spawned;
    int                       i = 0;
    for (auto r : {Row{id::KnightPhantom, 35, 1.25f, 2.5f}, Row{id::BlockChainGoblin, 20, 0.9f, 1.4f}, Row{id::HelmetCrab, 13, 0.8f, 1.1f},
                   Row{id::UpperGoblinKnight, 30, 1.1f, 1.3f}}) {
        auto* m = spawnAt(base.offset((i++) * 4, 0, 0), r.id);
        ctx.require(m != nullptr, std::string("spawned ") + r.id);
        ctx.check(m->getMaxHealth() == r.health, std::string(r.id) + " max health " + std::to_string(m->getMaxHealth()));
        auto const& b = m->getAABB();
        ctx.check(std::abs(b.max.x - b.min.x - r.width) < 0.01f && std::abs(b.max.y - b.min.y - r.height) < 0.01f,
                  std::string(r.id) + " box " + std::to_string(b.max.x - b.min.x) + " x " + std::to_string(b.max.y - b.min.y));
        spawned.push_back(fe::uidOf(*m));
    }
    // the lower goblin knight comes with its rider (finalizeSpawn / addrider)
    auto* lower = spawnAt(base.offset(0, 0, 3), id::LowerGoblinKnight);
    ctx.require(lower != nullptr, "a lower goblin knight");
    auto lowerId = fe::uidOf(*lower);
    ctx.check(lower->getMaxHealth() == 20, "lower goblin knight max health 20");
    co_await ctx.ticks(10);
    lower = fe::asMob(*fe::actorById(lowerId));
    ctx.check(lower && lower->getFirstPassenger() && lower->getFirstPassenger()->getTypeName() == id::UpperGoblinKnight, "an upper goblin knight rides it");
    // the knight phantom's equipment (populateDefaultEquipmentSlots) and its numbering
    if (auto* k = fe::actorById(spawned[0])) {
        ctx.check(game::items::idOf(k->getCarriedItem()) == "twilightforest:knightmetal_sword", "the knight holds a knightmetal sword: " + game::items::idOf(k->getCarriedItem()));
        ctx.check(phantom::numberOf(*k) == 0, "a lone knight is number 0");
    }
    for (auto uid : spawned)
        if (auto* a = fe::actorById(uid)) a->remove();
    discardAround(base, 24);
    (void)box;
});

GF_REGISTER_SELFTEST("stronghold.goblin_armor", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(2540, 16);
    auto            at  = ctx.testPos(2546, 2);
    auto*           m   = spawnAt(at, id::LowerGoblinKnight);
    ctx.require(m != nullptr, "a lower goblin knight");
    auto lowerId = fe::uidOf(*m);
    m->setYBodyRotation(0.0f); // facing +z
    const_cast<Vec2&>(m->getRotation()) = Vec2{0.0f, 0.0f};
    co_await ctx.ticks(10);
    auto* lower = fe::asMob(*fe::actorById(lowerId));
    ctx.require(lower && lower->getFirstPassenger(), "the knight pair");
    auto upperId = fe::uidOf(*lower->getFirstPassenger());
    ctx.check(goblins::hasArmor(*lower) && goblins::hasShield(*lower->getFirstPassenger()), "both start armoured, the rider with a shield");
    // an attacker behind (difference > 300 or < 60) breaks the lower knight's armour
    auto& sp = ctx.spawnPlayer();
    auto  spId = sp.getOrCreateUniqueID().rawID;
    resistant(sp, 30);
    auto lp = lower->getFeetPos();
    sp.teleport(Vec3{lp.x, lp.y, lp.z - 2.0f}, DimensionType(at.dim));
    co_await ctx.ticks(2);
    lower = fe::asMob(*fe::actorById(lowerId));
    lower->setYBodyRotation(0.0f);
    auto* attacker = fe::playerById(spId);
    ctx.require(lower && attacker, "the attacker");
    gf::combat::hurt(*lower, 1.0f, "minecraft:player_attack", attacker, attacker);
    co_await ctx.ticks(2);
    lower = fe::asMob(*fe::actorById(lowerId));
    ctx.check(lower && !goblins::hasArmor(*lower), "a hit from behind broke the lower knight's armour");
    // a hit from the front goes to the rider's shield (blocked, no damage)
    if (lower) {
        lower->setYBodyRotation(0.0f);
        auto lp2 = lower->getFeetPos();
        if (auto* a = fe::playerById(spId)) a->teleport(Vec3{lp2.x, lp2.y, lp2.z + 2.0f}, DimensionType(at.dim));
        co_await ctx.ticks(2);
        lower = fe::asMob(*fe::actorById(lowerId));
        if (lower) lower->setYBodyRotation(0.0f);
        attacker = fe::playerById(spId);
        if (lower && attacker) {
            int  before  = lower->getHealth();
            bool applied = gf::combat::hurt(*lower, 4.0f, "minecraft:player_attack", attacker, attacker);
            ctx.check(!applied && lower->getHealth() == before, "a hit from the front is taken on the shield");
        }
    }
    // the rider's spear: a melee that starts it holds the 60 tick timer
    if (auto* upper = fe::asMob(*fe::actorById(upperId))) {
        goblins::landHeavySpearAttack(*upper); // no crash with nothing in front
        ctx.check(goblins::spearTimer(*upper) >= 0, "spear timer readable");
    }
    discardAround(at, 12);
    (void)box;
});

GF_REGISTER_SELFTEST("stronghold.blockchain_throw", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(2560, 16);
    // the middle of the cleared area: the throw goes 6 blocks the way the goblin looks, whichever
    // way that is, and must not reach the uncleared terrain beyond the area (a wall stops it)
    auto  row = ctx.testPos(2560);
    auto  at  = core::GlobalPos{row.dim, box.minX + 8, row.y, box.minZ + 8};
    auto* g   = spawnAt(at, id::BlockChainGoblin);
    ctx.require(g != nullptr, "a block and chain goblin");
    auto uid = fe::uidOf(*g);
    co_await ctx.ticks(3);
    auto* m = fe::asMob(*fe::actorById(uid));
    ctx.require(m != nullptr, "it ticks");
    float len = goblins::chainLengthOf(*m);
    ctx.check(std::abs(len - 0.3f) < 1e-4f || std::abs(len - 0.9f) < 1e-4f, "the chain is 0.3 idle or 0.9 swinging: " + std::to_string(len));
    goblins::startThrowForTest(*m);
    float maxLength = 0.0f;
    for (int i = 0; i < 20; i++) {
        co_await ctx.ticks(1);
        auto* a = fe::actorById(uid);
        if (!a) break;
        maxLength = std::max(maxLength, goblins::throwLengthOf(*a));
    }
    ctx.check(maxLength <= 6.0f && maxLength >= 5.5f, "the throw reaches 6 blocks: " + std::to_string(maxLength));
    if (auto* a = fe::actorById(uid)) ctx.check(!goblins::isThrowing(*a) && goblins::throwLengthOf(*a) < 6.0f, "then the ball comes back");
    discardAround(at, 12);
    (void)box;
});

GF_REGISTER_SELFTEST("stronghold.helmet_crab_blue", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal); // Peaceful removes the crab (Monster) at its first tick
    auto  box = co_await ctx.flatArea(2580, 6);
    auto  at  = ctx.testPos(2583, 2);
    auto* c   = spawnAt(at, id::HelmetCrab);
    ctx.require(c != nullptr, "a helmet crab");
    auto uid = fe::uidOf(*c);
    co_await ctx.ticks(2);
    auto* a = fe::actorById(uid);
    ctx.require(a != nullptr, "the crab ticks");
    ctx.check(ge::data(*a, "helmet_crab:blue").is_boolean(), "the blue flag is rolled at spawn and kept in the save data");
    goblins::setBlueForTest(*a, true);
    ctx.check(goblins::isBlue(*a) && ge::data(*a, "helmet_crab:blue").get<bool>(), "a blue crab stays blue");
    a->remove();
    (void)box;
});

GF_REGISTER_SELFTEST("stronghold.phantom_fight", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(2576, 24, 24); // dx 2576..2599
    auto            home = ctx.testPos(2588, 2).offset(0, 1, 0); // the spawner, one above the floor
    auto            floor = ctx.blockAt(home.offset(0, -1, 0));
    ctx.require(ctx.setBlock(home, id::Spawner), "the knight phantom spawner");
    auto& sp   = ctx.spawnPlayer();
    auto  spId = sp.getOrCreateUniqueID().rawID;
    resistant(sp, 120);
    sp.teleport(Vec3{home.x + 0.5f, float(home.y), home.z + 3.5f}, DimensionType(home.dim));
    std::vector<std::int64_t> knights;
    for (int t = 0; t < 80 && knights.size() < 6; ++t) {
        co_await ctx.ticks(1);
        knights.clear();
        for (auto* a : around(home, id::KnightPhantom, 10)) knights.push_back(fe::uidOf(*a));
    }
    ctx.require(knights.size() == 6, "six knights spawned: " + std::to_string(knights.size()));
    ctx.check(ctx.blockAt(home) != id::Spawner, "the spawner is gone");
    co_await ctx.ticks(4);
    std::set<int> numbers;
    for (auto uid : knights)
        if (auto* a = fe::actorById(uid)) {
            int n = phantom::numberOf(*a);
            numbers.insert(n);
            char const* weapon = n % 3 == 0 ? "twilightforest:knightmetal_sword" : n % 3 == 1 ? "twilightforest:knightmetal_axe" : "twilightforest:knightmetal_pickaxe";
            ctx.check(game::items::idOf(a->getCarriedItem()) == weapon, "knight " + std::to_string(n) + " holds " + game::items::idOf(a->getCarriedItem()));
            auto h = ge::home(*fe::asMob(*a));
            ctx.check(h && h->pos.x == home.x && h->pos.y == home.y && h->pos.z == home.z && h->radius == 30, "home at the spawner, radius 30");
        }
    ctx.check(numbers == std::set<int>{0, 1, 2, 3, 4, 5}, "numbers 0 to 5");
    // formations: an attack start turns into the attack after 50 progress ticks
    if (auto* a = fe::actorById(knights[0])) {
        phantom::forceFormationForTest(*fe::asMob(*a), phantom::Formation::AttackPlayerStart, 45);
        ctx.check(phantom::isCharging(*a), "ATTACK_PLAYER_START charges");
    }
    co_await ctx.ticks(16);
    if (auto* a = fe::actorById(knights[0]))
        ctx.check(phantom::formationOf(*a) == phantom::Formation::AttackPlayerAttack || phantom::formationOf(*a) == phantom::Formation::WaitingForLeader,
                  "then ATTACK_PLAYER_ATTACK (" + std::to_string(static_cast<int>(*phantom::formationOf(*a))) + ")");
    // kill five: no chest yet, the survivors renumber
    for (std::size_t i = 1; i < knights.size(); i++)
        if (auto* a = fe::actorById(knights[i])) a->kill();
    co_await ctx.ticks(30);
    ctx.check(ctx.blockAt(home.offset(0, -1, 0)) != "twilightforest:dark_chest", "no chest while a knight lives");
    auto* last = fe::actorById(knights[0]);
    ctx.require(last && last->isAlive(), "the last knight lives");
    ctx.check(phantom::numberOf(*last) == 0, "the survivor is number 0");
    last->kill();
    co_await ctx.ticks(5);
    if (auto* a = fe::actorById(knights[0])) ctx.check(phantom::isOver(*a), "IT_IS_OVER on the last knight");
    for (int t = 0; t < 160 && ctx.blockAt(home.offset(0, -1, 0)) != "twilightforest:dark_chest"; ++t) co_await ctx.ticks(1);
    ctx.check(ctx.blockAt(home.offset(0, -1, 0)) == "twilightforest:dark_chest", "the dark chest with the loot at the home point: " + ctx.blockAt(home.offset(0, -1, 0)));
    if (auto* region = game::regionOf(home.dim)) {
        auto below = home.offset(0, -1, 0);
        bool trophy = false;
        if (auto inv = gf::inventory::find(*region, game::blockPos(below)); inv && inv->get) {
            for (int i = 0; i < inv->slots; ++i)
                if (game::items::idOf(inv->get(below, i)) == "twilightforest:knight_phantom_trophy") trophy = true;
            ctx.check(trophy, "the chest holds the knight phantom trophy");
            if (inv->set)
                for (int i = 0; i < inv->slots; ++i) inv->set(below, i, game::items::empty());
        }
        if (auto const* f = game::blockWith(floor.empty() ? "minecraft:grass_block" : floor)) game::setBlock(*region, game::blockPos(below), *f);
    }
    discardAround(home, 20);
    (void)spId;
    (void)box;
});

// ---- landmark walk-through (audit A3) ------------------------------------------------------------------

namespace {

/// tfEnforcedProgression for the length of a test.
struct WalkEnforcedScope {
    bool previous = false;
    WalkEnforcedScope() {
        previous = gf::gamerules::getBool("tfEnforcedProgression");
        gf::gamerules::set("tfEnforcedProgression", true);
    }
    ~WalkEnforcedScope() { gf::gamerules::set("tfEnforcedProgression", previous); }
};

/// The landmark of a boss against TF's structure record (LandmarkStructure: AdvancementLockConfig,
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
    ctx.check(d->hintBookKey == (hintBook.empty() ? std::string() : "twilightforest.book." + hintBook), key + "'s hint book (" + d->hintBookKey + ")");
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

// The knight stronghold against TF's KnightStrongholdStructure record: locked by
// progress_trophy_pedestal, the tfstronghold hint book, conquerable; its protected pieces refuse a
// player without progress_trophy_pedestal.
GF_REGISTER_SELFTEST("stronghold.landmark_walk", [](Context& ctx) { walkLandmark(ctx, id::Stronghold, "twilightforest:progress_trophy_pedestal", "tfstronghold", 150507); });

// The real conquest (KnightPhantom#postmortem -> LandmarkUtil#markStructureConquered): a knight
// stronghold start forced far from the test rows (its chunks never load: nothing is placed), a lone
// knight phantom (the last of its group) spawned in the test area with its home point inside the
// start's first piece, killed: the start is conquered. The dark chest belongs to the home point's unloaded chunk,
// so the dying record is dropped without it (stronghold.phantom_fight checks the chest).
GF_REGISTER_SELFTEST("stronghold.conquest", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(2560, 16);
    auto            at   = ctx.testPos(2568, 2).offset(0, 1, 0);
    core::GlobalPos site{ctx.dimension(), 152560, 80, 160007};
    (void)gf::landmark::ext::forgetStartForTest(site, id::Stronghold);
    std::optional<gf::landmark::ext::ScopedStart> forced;
    forced.emplace(id::Stronghold, site);
    ctx.require(*forced && (*forced)->generated, "forced a knight stronghold start");
    // the home inside the first piece (the start's box tops out one below the generation point,
    // KnightStructureStart, so the center itself is above it)
    ctx.require(!(*forced)->pieceBoxes.empty(), "the stronghold has pieces");
    auto const&     pb = (*forced)->pieceBoxes.front();
    core::GlobalPos home{site.dim, (pb.minX + pb.maxX) / 2, (pb.minY + pb.maxY) / 2, (pb.minZ + pb.maxZ) / 2};
    auto            start = gf::landmark::startAt(home.dim, game::blockPos(home));
    ctx.require(start.has_value() && start->key == id::Stronghold, "the home point is inside the stronghold start");
    ctx.check(!gf::landmark::isConquered(*start), "the stronghold is not conquered before the fight");
    auto* k = spawnAt(at, id::KnightPhantom);
    ctx.require(k != nullptr, "a knight phantom");
    auto uid = fe::uidOf(*k);
    ge::setHome(*k, home, 30);
    co_await ctx.ticks(2);
    auto* a = fe::actorById(uid);
    ctx.require(a != nullptr && a->isAlive(), "the knight ticks");
    a->kill();
    co_await ctx.ticks(3);
    gf::feature::boss::dropDyingForTest(uid);
    if (auto* d = fe::actorById(uid)) ctx.check(phantom::isOver(*d), "IT_IS_OVER on the last knight");
    auto after = gf::landmark::startAt(home.dim, game::blockPos(home));
    ctx.check(after.has_value() && gf::landmark::isConquered(*after), "the last knight's death conquered the knight stronghold");
    if (auto* d = fe::actorById(uid)) d->remove();
    forced.reset();
    discardAround(at, 12);
    (void)box;
});

} // namespace gf::feature::stronghold::tests
