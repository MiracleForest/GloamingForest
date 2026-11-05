// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Selftests of U20 (naga.*, testPos dx 2000-2099): the Naga's attributes and difficulty health,
// its segments, the movement pattern with a seeded random, the home invulnerability, the loot
// table, the boss spawner, a whole fight to the death chest, and the courtyard (layout, landmark
// generator, golden volume hash of a seeded courtyard, rule R19).

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "core/jrand/JRand.h"
#include "core/naga/Courtyard.h"
#include "feature/boss/Boss.h"
#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/landmark/LandmarkTest.h"
#include "feature/inventory/Inventory.h"
#include "feature/landmark/Landmark.h"
#include "feature/loot/Loot.h"
#include "feature/naga/NagaInternal.h"
#include "feature/progression/Progression.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace gf::feature::naga::tests {

using gf::selftest::Context;
namespace ge     = gf::entity;
using Difficulty = SharedTypes::Legacy::Difficulty;

namespace {

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

/// The test players the Naga must not kill while a test watches it (damage to them is cancelled).
std::set<std::int64_t>& protectedPlayers() {
    static std::set<std::int64_t> s;
    return s;
}
struct ProtectScope {
    std::int64_t uid;
    explicit ProtectScope(std::int64_t id) : uid(id) {
        static bool once = false;
        if (!std::exchange(once, true))
            gf::combat::onIncoming(gf::combat::priority::Highest, [](gf::combat::DamageInfo& d) {
                if (d.victim.isPlayer() && protectedPlayers().contains(d.victim.getOrCreateUniqueID().rawID)) return gf::combat::Verdict::Cancel;
                return gf::combat::Verdict::Pass;
            });
        protectedPlayers().insert(uid);
    }
    ~ProtectScope() { protectedPlayers().erase(uid); }
};

Actor* byId(std::int64_t uid) {
    auto level = ll::service::getLevel();
    if (!level || !uid) return nullptr;
    ActorUniqueID id;
    id.rawID = uid;
    return level->fetchEntity(id, false);
}
Mob* mobById(std::int64_t uid) {
    auto* a = byId(uid);
    return a && a->hasCategory(ActorCategory::Mob) ? static_cast<Mob*>(a) : nullptr;
}

Mob* spawnNaga(core::GlobalPos const& at) {
    auto* region = game::regionOf(at.dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level) return nullptr;
    return level->getSpawner().spawnMob(*region, ActorDefinitionIdentifier(kNagaId), nullptr, Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f}, false, false, false);
}

/// Removes a Naga left by a test (its parts go with it).
struct NagaScope {
    std::int64_t uid = 0;
    ~NagaScope() {
        if (auto* a = byId(uid)) a->remove();
    }
};

int activeSegments(NagaState const& s) {
    int n = 0;
    for (auto const& seg : s.segments) n += seg.active ? 1 : 0;
    return n;
}

/// A worldgen::Region over the live test dimension (pieces placed straight into the world).
class LiveRegion final : public worldgen::Region {
public:
    LiveRegion(BlockSource& source, int dim) : mSource(source), mDim(dim) {}
    int          centerChunkX() const override { return 0; }
    int          centerChunkZ() const override { return 0; }
    int          minY() const override { return mSource.getMinHeight(); }
    int          maxY() const override { return mSource.getMaxHeight(); }
    bool         isInside(int, int) const override { return true; }
    Block const& getBlock(BlockPos const& pos) const override { return mSource.getBlock(pos); }
    bool         setBlock(BlockPos const& pos, Block const& block, int) override { return game::setBlock(mSource, pos, block); }
    int          height(worldgen::Heightmap, int, int) const override { return 0; }
    std::string  biome(BlockPos const&) const override { return "twilightforest:twilight_forest"; }
    std::int64_t seed() const override { return 0; }
    int          dimension() const override { return mDim; }

private:
    BlockSource& mSource;
    int          mDim;
};

} // namespace

// ---- the entity ------------------------------------------------------------------------------------

// TFEntities.NAGA and Naga#registerAttributes (tests/golden/mobs.json) plus finalizeSpawn's
// difficulty health boost: EASY 120, NORMAL 200, HARD 250.
GF_REGISTER_SELFTEST("naga.spawn_health", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2000, 12);
    (void)box;
    struct Case {
        Difficulty d;
        int        health;
    };
    for (auto c : {Case{Difficulty::Easy, 120}, Case{Difficulty::Normal, 200}, Case{Difficulty::Hard, 250}}) {
        DifficultyScope scope(c.d);
        NagaScope       n;
        auto*           naga = spawnNaga(ctx.testPos(2005, 2));
        ctx.require(naga != nullptr, "the Naga spawns");
        n.uid = naga->getOrCreateUniqueID().rawID;
        co_await ctx.ticks(2);
        naga = mobById(n.uid);
        ctx.require(naga != nullptr, "the Naga is still there");
        ctx.check(naga->getMaxHealth() == c.health && naga->getHealth() == c.health,
                  "difficulty " + std::to_string(int(c.d)) + ": max health " + std::to_string(naga->getMaxHealth()) + ", health " + std::to_string(naga->getHealth()) +
                      " (expected " + std::to_string(c.health) + ")");
        if (c.d == Difficulty::Easy) {
            auto const& bb = naga->getAABB();
            ctx.check(std::abs((bb.max.x - bb.min.x) - 2.0f) < 0.01f && std::abs((bb.max.y - bb.min.y) - 3.0f) < 0.01f, "the Naga is 2 x 3");
            ctx.check(naga->isFireImmune(), "the Naga is fire immune");
            ctx.check(ge::ext::parts(*naga).size() == kMaxSegments, "the Naga has 12 segment parts");
            ctx.check(naga->getTypeName() == kNagaId, "the id is twilightforest:naga");
        }
    }
});

// Naga#setSegmentsPerHealth: 12 segments at full health, clamp(health / 12 + 2, 0, 12) after
// damage; the lost ones pop one by one ((old - i) * 12 ticks), the others follow the head 2 apart.
GF_REGISTER_SELFTEST("naga.segments", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box = co_await ctx.flatArea(2010, 16);
    (void)box;
    ctx.check(segmentsFor(120.0f, 12.0f) == 12 && segmentsFor(30.0f, 12.0f) == 4 && segmentsFor(1.0f, 12.0f) == 2 && segmentsFor(0.0f, 12.0f) == 0,
              "the segment formula clamp(health / 12 + 2, 0, 12)");
    NagaScope n;
    auto*     naga = spawnNaga(ctx.testPos(2018, 2));
    ctx.require(naga != nullptr, "the Naga spawns");
    n.uid = naga->getOrCreateUniqueID().rawID;
    seedNagaForTest(*naga, 2010);
    co_await ctx.ticks(30);
    naga = mobById(n.uid);
    ctx.require(naga != nullptr, "the Naga is still there");
    auto* s = stateOf(*naga);
    ctx.require(s != nullptr, "the Naga has its state");
    ctx.check(s->currentSegmentCount == 12 && activeSegments(*s) == 12, "12 active segments at full health (" + std::to_string(activeSegments(*s)) + ")");
    // the chain: every segment 2 blocks from its leader, and the part actors where the segments are
    bool  chain = true;
    Vec3  lead  = naga->getFeetPos();
    for (int i = 0; i < kMaxSegments; i++) {
        auto const& p  = s->segments[i].pos;
        double      d  = std::sqrt(double(p.x - lead.x) * (p.x - lead.x) + double(p.y - lead.y) * (p.y - lead.y) + double(p.z - lead.z) * (p.z - lead.z));
        chain         &= std::abs(d - 2.0) < 0.6;
        lead           = p;
    }
    ctx.check(chain, "each segment follows its leader 2 blocks behind");
    auto* part = ge::partActor(s->segments[5].part);
    ctx.require(part != nullptr, "segment 5 has its part actor");
    auto pp = part->getFeetPos();
    ctx.check(std::abs(pp.x - s->segments[5].pos.x) < 1.0f && std::abs(pp.z - s->segments[5].pos.z) < 1.0f, "the part actor follows its segment");
    ctx.check(std::abs((part->getAABB().max.x - part->getAABB().min.x) - 2.0f) < 0.05f, "an active segment is 2 x 2");
    // down to 30 health: 4 segments; segment 11 pops after 12 ticks, segment 4 after 96
    gf::combat::hurt(*naga, 90.0f, "minecraft:generic");
    co_await ctx.ticks(2);
    naga = mobById(n.uid);
    ctx.require(naga != nullptr, "the Naga survives");
    s = stateOf(*naga);
    ctx.check(naga->getHealth() == 30, "the Naga has 30 health (" + std::to_string(naga->getHealth()) + ")");
    ctx.check(s->currentSegmentCount == 4, "4 segments at 30 health (" + std::to_string(s->currentSegmentCount) + ")");
    ctx.check(s->segments[11].deathCounter > 0 && s->segments[4].deathCounter > s->segments[11].deathCounter, "the lost segments self-destruct in turn");
    co_await ctx.ticks(100);
    naga = mobById(n.uid);
    ctx.require(naga != nullptr, "the Naga is still there");
    s = stateOf(*naga);
    ctx.check(activeSegments(*s) == 4, "4 active segments once the others popped (" + std::to_string(activeSegments(*s)) + ")");
    if (auto* gone = ge::partActor(s->segments[8].part)) ctx.check(gone->getAABB().max.x - gone->getAABB().min.x < 0.05f, "a popped segment is 0 x 0");
});

// NagaMovementPattern with a seeded random: CIRCLE -> INTIMIDATE -> CHARGE (or STUNLESS_CHARGE /
// CRUMBLE) -> CIRCLE; doDaze holds 60..99 ticks; more than 15 damage while dazed forces CIRCLE.
GF_REGISTER_SELFTEST("naga.state_machine", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box  = co_await ctx.flatArea(2020, 48);
    auto            home = ctx.testPos(2044, 2);
    auto&           sp   = ctx.spawnPlayer("naga_pattern");
    auto            spId = sp.getOrCreateUniqueID().rawID;
    ProtectScope    protect(spId);
    sp.teleport(Vec3{home.x + 0.5f, float(home.y), home.z + 14.5f}, DimensionType(home.dim));
    NagaScope n;
    auto*     naga = spawnNaga(home);
    ctx.require(naga != nullptr, "the Naga spawns");
    n.uid = naga->getOrCreateUniqueID().rawID;
    ge::setHome(*naga, home, kHomeRadius);
    seedNagaForTest(*naga, 20201);
    co_await ctx.ticks(2);
    naga = mobById(n.uid);
    ctx.require(naga != nullptr, "the Naga is still there");
    if (auto* p = byId(spId)) naga->setTarget(p);
    std::vector<MovementState> seen;
    for (int t = 0; t < 900 && seen.size() < 6; t++) {
        co_await ctx.ticks(1);
        naga = mobById(n.uid);
        if (!naga) break;
        if (auto* p = byId(spId); p && !naga->mTargetId->rawID) naga->setTarget(p);
        auto* s = stateOf(*naga);
        if (s && (seen.empty() || seen.back() != s->state)) seen.push_back(s->state);
    }
    std::string trace;
    for (auto st : seen) trace += std::string(stateName(st)) + " ";
    ctx.info("states: " + trace);
    ctx.require(seen.size() >= 4, "the pattern went through its states: " + trace);
    // every transition is one of the state machine's edges
    auto edge = [](MovementState a, MovementState b) {
        switch (a) {
        case MovementState::Circle:
            return b == MovementState::Intimidate;
        case MovementState::Intimidate:
            return b == MovementState::Charge || b == MovementState::StunlessCharge || b == MovementState::Crumble;
        case MovementState::Crumble:
            return b == MovementState::Charge || b == MovementState::StunlessCharge;
        case MovementState::Charge:
        case MovementState::StunlessCharge:
        case MovementState::Daze:
            return b == MovementState::Circle;
        }
        return false;
    };
    bool ok = seen.front() == MovementState::Circle;
    for (std::size_t i = 1; i < seen.size(); i++) ok &= edge(seen[i - 1], seen[i]);
    ctx.check(ok, "the transitions follow NagaMovementPattern: " + trace);
    ctx.check(std::find(seen.begin(), seen.end(), MovementState::Intimidate) != seen.end(), "the Naga intimidated (rattle)");
    // a shield block while charging: DAZE for 60..99 ticks, the dazed texture flag set
    naga = mobById(n.uid);
    ctx.require(naga != nullptr, "the Naga is still there");
    doDaze(*naga);
    auto* s = stateOf(*naga);
    ctx.check(s->state == MovementState::Daze && s->stateCounter >= 60 && s->stateCounter < 100, "doDaze: DAZE for " + std::to_string(s->stateCounter) + " ticks");
    co_await ctx.ticks(3);
    naga = mobById(n.uid);
    ctx.require(naga != nullptr, "the Naga is still there");
    s = stateOf(*naga);
    ctx.check(s->dazed, "a dazed Naga sets DATA_DAZE (twilightforest:dazed)");
    // 16 damage while dazed forces the circle
    if (auto* p = byId(spId)) gf::combat::hurt(*naga, 8.0f, "minecraft:player_attack", p);
    co_await ctx.ticks(12);
    naga = mobById(n.uid);
    if (auto* p = byId(spId); naga && p) gf::combat::hurt(*naga, 9.0f, "minecraft:player_attack", p);
    co_await ctx.ticks(3);
    naga = mobById(n.uid);
    ctx.require(naga != nullptr, "the Naga is still there");
    s = stateOf(*naga);
    ctx.check(s->state == MovementState::Circle, std::string("more than 15 damage while dazed forces CIRCLE (") + stateName(s->state) + ")");
    (void)box;
});

// Naga#isInvulnerableTo: no damage from outside the 46 / 7 home box, none from explosions;
// NagaSegment#hurt: 2/3 of a hit on a segment reaches the head.
GF_REGISTER_SELFTEST("naga.home_invulnerable", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box  = co_await ctx.flatArea(2030, 12);
    auto            at   = ctx.testPos(2035, 2);
    auto&           sp   = ctx.spawnPlayer("naga_home");
    auto            spId = sp.getOrCreateUniqueID().rawID;
    ProtectScope    protect(spId);
    sp.teleport(Vec3{at.x + 0.5f, float(at.y), at.z + 4.5f}, DimensionType(at.dim));
    NagaScope n;
    auto*     naga = spawnNaga(at);
    ctx.require(naga != nullptr, "the Naga spawns");
    n.uid = naga->getOrCreateUniqueID().rawID;
    // the home 60 blocks away: the player (and the Naga) are outside it
    ge::setHome(*naga, core::GlobalPos{at.dim, at.x + 60, at.y, at.z}, kHomeRadius);
    co_await ctx.ticks(2);
    naga = mobById(n.uid);
    auto* p = byId(spId);
    ctx.require(naga && p, "the Naga and the player are there");
    ctx.check(!withinHome(*naga, *p), "the player is outside the home box");
    int before = naga->getHealth();
    gf::combat::hurt(*naga, 10.0f, "minecraft:player_attack", p);
    co_await ctx.ticks(1);
    naga = mobById(n.uid);
    ctx.check(naga && naga->getHealth() == before, "no damage from outside the home area");
    // home at the Naga: the player is inside
    ge::setHome(*naga, at, kHomeRadius);
    p = byId(spId);
    ctx.check(withinHome(*naga, *p), "the player is inside the home box");
    gf::combat::hurt(*naga, 9.0f, "minecraft:explosion", p);
    co_await ctx.ticks(12);
    naga = mobById(n.uid);
    ctx.check(naga && naga->getHealth() == before, "no damage from explosions");
    p = byId(spId);
    gf::combat::hurt(*naga, 9.0f, "minecraft:player_attack", p);
    co_await ctx.ticks(12);
    naga = mobById(n.uid);
    ctx.check(naga && naga->getHealth() == before - 9, "a hit from inside lands (" + std::to_string(naga ? naga->getHealth() : -1) + ")");
    // a hit on segment 0 (active): 2/3 to the head
    before    = naga->getHealth();
    auto* s   = stateOf(*naga);
    auto* seg = s ? ge::partActor(s->segments[0].part) : nullptr;
    ctx.require(seg != nullptr, "segment 0 exists");
    p = byId(spId);
    gf::combat::hurt(*seg, 9.0f, "minecraft:player_attack", p);
    co_await ctx.ticks(2);
    naga = mobById(n.uid);
    ctx.check(naga && naga->getHealth() == before - 6, "a hit on a segment deals 2/3 to the Naga (" + std::to_string(naga ? before - naga->getHealth() : -1) + ")");
    (void)box;
});

// TF's loot table entities/naga: 6..11 naga scales and the trophy.
GF_REGISTER_SELFTEST("naga.loot", [](Context& ctx) {
    auto* region = game::regionOf(ctx.dimension());
    ctx.require(region != nullptr, "a region");
    for (int i = 0; i < 20; i++) {
        gf::loot::LootContext lc;
        lc.region = region;
        lc.seed   = 2040 + i;
        auto items = gf::loot::roll("twilightforest:entities/naga", lc);
        int  scales = 0, trophies = 0;
        for (auto const& it : items) {
            auto id = game::items::idOf(it);
            if (id == "twilightforest:naga_scale") scales += game::items::countOf(it);
            if (id == "twilightforest:naga_trophy") trophies += game::items::countOf(it);
        }
        ctx.check(scales >= 6 && scales <= 11, "roll " + std::to_string(i) + ": " + std::to_string(scales) + " naga scales (6..11)");
        ctx.check(trophies == 1, "roll " + std::to_string(i) + ": the naga trophy");
    }
});

// NagaSpawnerBlockEntity: the boss spawner wakes for a player within 50 blocks (LONG_RANGE),
// spawns the Naga with its home at the spawner (radius 40) and is gone.
GF_REGISTER_SELFTEST("naga.spawner", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(2060, 12);
    auto            at  = ctx.testPos(2065, 2);
    auto&           sp  = ctx.spawnPlayer("naga_spawner");
    ProtectScope    protect(sp.getOrCreateUniqueID().rawID);
    // 40 blocks away along the row: inside LONG_RANGE (the default range 9 would not wake it)
    sp.teleport(Vec3{at.x + 0.5f, float(at.y), at.z + 40.5f}, DimensionType(at.dim));
    ctx.require(ctx.setBlock(at.offset(0, 1, 0), kSpawnerId), "the naga boss spawner is placed");
    std::int64_t uid = 0;
    for (int t = 0; t < 200 && !uid; t += 5) {
        co_await ctx.ticks(5);
        if (auto* region = game::regionOf(at.dim))
            for (auto* a : region->getEntities(AABB{Vec3{at.x - 6.0f, at.y - 3.0f, at.z - 6.0f}, Vec3{at.x + 7.0f, at.y + 6.0f, at.z + 7.0f}}, 0.0f))
                if (a && a->getTypeName() == kNagaId && a->isAlive()) uid = a->getOrCreateUniqueID().rawID;
    }
    NagaScope n;
    n.uid = uid;
    ctx.require(uid != 0, "the spawner spawned the Naga for a player 40 blocks away");
    auto* naga = mobById(uid);
    ctx.require(naga != nullptr, "the Naga is there");
    auto h = ge::home(*naga);
    ctx.check(h && h->pos.x == at.x && h->pos.y == at.y + 1 && h->pos.z == at.z && h->radius == kHomeRadius, "its home is the spawner, radius 40");
    ctx.check(ctx.blockAt(at.offset(0, 1, 0)) != kSpawnerId, "the spawner is gone");
    (void)box;
});

// The fight: a qualified player kills the Naga; its death animation (124 ticks) leaves the death
// chest under its home with the scales and the trophy, the player_killed_entity trigger (U14's
// progress_naga) and the hurt_boss trigger fire for the player.
GF_REGISTER_SELFTEST("naga.death", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    static std::map<std::string, int> triggers;
    static bool                       listening = false;
    if (!std::exchange(listening, true)) {
        for (char const* id : {"minecraft:player_killed_entity", "twilightforest:hurt_boss"})
            gf::progression::onTrigger(id, [id = std::string(id)](Player& player, nlohmann::json const&) {
                if (player.getNameTag() == "naga_fight") triggers[id]++;
            });
    }
    triggers.clear();
    auto         box  = co_await ctx.flatArea(2070, 16);
    auto         home = ctx.testPos(2078, 2);
    auto&        sp   = ctx.spawnPlayer("naga_fight");
    auto         spId = sp.getOrCreateUniqueID().rawID;
    ProtectScope protect(spId);
    sp.teleport(Vec3{home.x + 0.5f, float(home.y), home.z + 4.5f}, DimensionType(home.dim));
    auto floor = ctx.blockAt(home.offset(0, -1, 0));
    NagaScope n;
    auto*     naga = spawnNaga(home);
    ctx.require(naga != nullptr, "the Naga spawns");
    n.uid = naga->getOrCreateUniqueID().rawID;
    ge::setHome(*naga, home, kHomeRadius);
    co_await ctx.ticks(3);
    naga = mobById(n.uid);
    auto* p = byId(spId);
    ctx.require(naga && p, "the Naga and the player are there");
    gf::combat::hurt(*naga, 5.0f, "minecraft:player_attack", p);
    co_await ctx.ticks(12);
    naga = mobById(n.uid);
    p    = byId(spId);
    ctx.require(naga && p, "the Naga and the player are there");
    gf::combat::hurt(*naga, 500.0f, "minecraft:player_attack", p);
    co_await ctx.ticks(5);
    if (auto* dying = mobById(n.uid)) ctx.check(gf::boss::ext::deathTime(*dying) > 0, "the Naga is dying");
    co_await ctx.ticks(140);
    auto chestPos = home.offset(0, -1, 0);
    auto chestId  = ctx.blockAt(chestPos);
    ctx.check(chestId == "twilightforest:twilight_oak_chest" || chestId == "twilightforest:canopy_chest", "the death chest is a twilight oak or canopy chest (" + chestId + ")");
    int scales = 0, trophies = 0;
    if (auto* region = game::regionOf(home.dim))
        if (auto handler = gf::inventory::find(*region, game::blockPos(chestPos)); handler && handler->get) {
            for (int i = 0; i < handler->slots; i++) {
                auto it = handler->get(chestPos, i);
                auto id = game::items::idOf(it);
                if (id == "twilightforest:naga_scale") scales += game::items::countOf(it);
                if (id == "twilightforest:naga_trophy") trophies += game::items::countOf(it);
            }
            for (int i = 0; i < handler->slots; i++)
                if (handler->set) handler->set(chestPos, i, game::items::empty());
        }
    ctx.check(scales >= 6, "the chest holds the naga scales (" + std::to_string(scales) + ")");
    ctx.check(trophies == 1, "the chest holds the naga trophy");
    ctx.check(triggers["minecraft:player_killed_entity"] >= 1, "player_killed_entity fired (U14's progress_naga criterion)");
    ctx.check(triggers["twilightforest:hurt_boss"] >= 1, "hurt_boss fired (was_in_fight)");
    ctx.check(mobById(n.uid) == nullptr, "the body is gone after the death animation");
    if (auto* region = game::regionOf(chestPos.dim))
        if (auto const* b = game::blockWith(floor.empty() ? "minecraft:grass_block" : floor)) game::setBlock(*region, game::blockPos(chestPos), *b);
    (void)box;
});

// ---- client display (tools/e2e/client/scenarios/naga.ps1) -----------------------------------------------

// The Naga next to the camera for the client screenshots: 10 s as it is (12 segments, nagahead),
// 8 s charging (nagahead_charging) and 8 s dazed (nagahead_dazed, the head lowered, crit sparkles).
// It is held in place (no target, put back every tick) and removed at the end.
GF_REGISTER_SELFTEST("naga.display", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box = co_await ctx.flatArea(2050, 32);
    auto            at  = ctx.testPos(2066, 2);
    NagaScope       n;
    auto*           naga = spawnNaga(at);
    ctx.require(naga != nullptr, "the Naga spawns");
    n.uid = naga->getOrCreateUniqueID().rawID;
    ge::setHome(*naga, at, kHomeRadius);
    ctx.info("area at " + std::to_string(at.x) + " " + std::to_string(at.y) + " " + std::to_string(at.z));
    char const* const phases[] = {"shown", "charging", "dazed"};
    int const         lengths[] = {200, 160, 160};
    for (int ph = 0; ph < 3; ph++) {
        if (auto* m = mobById(n.uid))
            if (auto* s = stateOf(*m)) {
                s->charging = ph == 1;
                s->dazed    = ph == 2;
                ge::setProperty(*m, "twilightforest:charging", s->charging);
                ge::setProperty(*m, "twilightforest:dazed", s->dazed);
            }
        for (int t = 0; t < lengths[ph]; t++) {
            co_await ctx.ticks(1);
            auto* m = mobById(n.uid);
            ctx.require(m != nullptr, "the Naga is still there");
            m->setTarget(nullptr);
            auto p = m->getFeetPos();
            if (std::abs(p.x - (at.x + 0.5f)) > 0.3f || std::abs(p.z - (at.z + 0.5f)) > 0.3f)
                m->teleportTo(Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f}, true, 0, 0, false);
            if (t == 20) ctx.info(std::string("naga ") + phases[ph]);
        }
    }
    (void)box;
});

// ---- the courtyard ---------------------------------------------------------------------------------

// The landmark generator makes the courtyard's pieces (forced start far from the test area, so
// nothing generates): CourtyardMain first, its 120..400 children, the spawner jigsaw at the center.
GF_REGISTER_SELFTEST("naga.courtyard_start", [](Context& ctx) {
    auto                                pos = core::GlobalPos{ctx.dimension(), 120000 + 7, 80, 120000 + 7};
    gf::landmark::ext::ScopedStart      forced(kCourtyard, pos);
    ctx.require(forced && forced->generated, "the courtyard start was made by U20's generator");
    auto pieces = gf::landmark::ext::startPieces(*forced);
    std::map<std::string, int> types;
    for (auto const& p : pieces) types[p.type]++;
    ctx.check(!pieces.empty() && pieces.front().type == "twilightforest:TFNCMn", "CourtyardMain is the first piece");
    ctx.check(pieces.size() >= 120 && pieces.size() <= 400, "the courtyard has " + std::to_string(pieces.size()) + " pieces");
    ctx.check(types["twilightforest:TFJigsawTemplate"] == 1, "the spawner piece from pool courtyard/center");
    ctx.check(types["twilightforest:TFNCWC"] == 8 && types["twilightforest:TFNCWA"] == 4, "8 outer and 4 inner wall corners");
    ctx.check(types["twilightforest:TFNCPa"] > 0 && types["twilightforest:TFNCWl"] > 0, "paths and walls");
    bool spawnerAtCenter = false;
    for (auto const& p : pieces)
        if (p.type == "twilightforest:TFJigsawTemplate") spawnerAtCenter = p.box.contains(forced->center.x + 1, p.box.minY, forced->center.z + 1);
    ctx.check(spawnerAtCenter, "the spawner dais is at the courtyard's center");
});

// A seeded courtyard placed piece by piece into a flat area: the golden volume hash (rule R19)
// and the spawner block at the center.
GF_REGISTER_SELFTEST("naga.courtyard_layout", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2090, 144, 16);
    ctx.require(box.maxX - box.minX >= 120, "a 144-wide flat area");
    int  cx = box.minX + 70, cz = box.minZ + 70;
    int  cy = box.minY + 4; // the terraces sink 3 blocks: keep them inside the restored volume
    core::jrand::LegacyRandom first(424242), context(171717);
    auto pieces = courtyardPieces(first, context, cx - 1, cy, cz - 1);
    ctx.require(pieces.size() > 100, "the courtyard pieces (" + std::to_string(pieces.size()) + ")");
    auto* source = ctx.region();
    ctx.require(source != nullptr, "the test region");
    LiveRegion                region(*source, ctx.dimension());
    core::jrand::LegacyRandom random(7);
    BlockBox                  all = pieces.front()->bbox;
    for (auto const& p : pieces) {
        all.minX = std::min(all.minX, p->bbox.minX), all.minY = std::min(all.minY, p->bbox.minY), all.minZ = std::min(all.minZ, p->bbox.minZ);
        all.maxX = std::max(all.maxX, p->bbox.maxX), all.maxY = std::max(all.maxY, p->bbox.maxY), all.maxZ = std::max(all.maxZ, p->bbox.maxZ);
    }
    ctx.require(box.minX <= all.minX && all.maxX <= box.maxX && box.minZ <= all.minZ && all.maxZ <= box.maxZ, "the courtyard fits the flat area");
    BlockBox clip{all.minX - 2, all.minY - 2, all.minZ - 2, all.maxX + 2, all.maxY + 8, all.maxZ + 2};
    for (auto& p : pieces) p->postProcess(region, random, clip, BlockPos{cx, cy, cz});
    co_await ctx.ticks(2);
    BlockBox hashBox{all.minX, cy - 3, all.minZ, all.maxX, cy + 6, all.maxZ};
    auto     hash = ctx.snapshotVolume(hashBox);
    ctx.info("courtyard volume hash " + hash + " box " + std::to_string(hashBox.minX) + " " + std::to_string(hashBox.minY) + " " + std::to_string(hashBox.minZ));
    // the dais: the spawner two blocks above the hedge level at the center
    bool spawner = false;
    for (int dx = -2; dx <= 2 && !spawner; dx++)
        for (int dz = -2; dz <= 2 && !spawner; dz++) spawner = ctx.blockAt(core::GlobalPos{ctx.dimension(), cx + dx, cy + 2, cz + dz}) == kSpawnerId;
    ctx.check(spawner, "the naga boss spawner stands on the center dais");
    // hedges and walls exist
    int hedges = 0, bricks = 0;
    for (int x = all.minX; x <= all.maxX; x += 3)
        for (int z = all.minZ; z <= all.maxZ; z += 3) {
            auto id = ctx.blockAt(core::GlobalPos{ctx.dimension(), x, cy + 1, z});
            hedges += id == "twilightforest:hedge";
            bricks += id.find("stone_brick") != std::string::npos;
        }
    ctx.check(hedges > 20, "the maze has hedges (" + std::to_string(hedges) + " samples)");
    ctx.check(bricks > 5, "the wall has stone bricks (" + std::to_string(bricks) + " samples)");
    static char const* const kGolden = "";
    if (*kGolden) ctx.check(hash == kGolden, "the golden volume hash " + std::string(kGolden) + " (got " + hash + ")");
    else ctx.info("no golden hash recorded yet");
});

} // namespace gf::feature::naga::tests

namespace gf::feature::naga::tests {

// The seeded courtyard of naga.courtyard_layout, built for the client screenshots and held for
// 30 s (the flat area is restored afterwards).
GF_REGISTER_SELFTEST("naga.display_courtyard", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2090, 144, 16);
    int  cx = box.minX + 70, cz = box.minZ + 70;
    int  cy = box.minY + 4;
    core::jrand::LegacyRandom first(424242), context(171717);
    auto pieces = courtyardPieces(first, context, cx - 1, cy, cz - 1);
    auto* source = ctx.region();
    ctx.require(source != nullptr && !pieces.empty(), "the courtyard pieces");
    LiveRegion                region(*source, ctx.dimension());
    core::jrand::LegacyRandom random(7);
    BlockBox                  clip{box.minX, box.minY - 4, box.minZ, box.maxX, box.maxY, box.maxZ};
    for (auto& p : pieces) p->postProcess(region, random, clip, BlockPos{cx, cy, cz});
    ctx.info("courtyard at " + std::to_string(cx) + " " + std::to_string(cy) + " " + std::to_string(cz));
    co_await ctx.ticks(600);
});

} // namespace gf::feature::naga::tests
