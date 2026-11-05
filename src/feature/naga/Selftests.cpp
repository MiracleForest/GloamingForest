// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Selftests of U20 (naga.*, testPos dx 2000-2099): the Naga's attributes and difficulty health,
// its segments, the movement pattern with a seeded random, the home invulnerability, the loot
// table, the boss spawner, a whole fight to the death chest, and the courtyard (layout, landmark
// generator, golden volume hash of a seeded courtyard, rule R19).

#include <algorithm>
#include <cmath>
#include <fstream>
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
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/state/PropertyComponent.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/world/level/block/Block.h"
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
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/landmark/LandmarkTest.h"
#include "feature/inventory/Inventory.h"
#include "feature/item/Item.h"
#include "feature/landmark/Landmark.h"
#include "feature/loot/Loot.h"
#include "feature/naga/NagaInternal.h"
#include "feature/progression/Progression.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "selftest/SelfTest.h"

#ifdef GF_CLIENT
#include "feature/ext/ui/Client.h"
#endif

namespace gf::feature::naga::tests {

using gf::selftest::Context;
namespace ge     = gf::entity;
using Difficulty = SharedTypes::Legacy::Difficulty;
using core::BlockBox;

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

/// A real (not simulated) player is in the level: a client looks at the display tests. They only
/// exist for the client screenshots, so the server's naga.* and full runs skip them.
bool clientWatching() {
    bool found = false;
    if (auto level = ll::service::getLevel())
        level->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
            found = found || !p.isSimulated();
            return !found;
        }));
    return found;
}

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
    // a popped segment is hidden (twilightforest:active false; Bedrock keeps a minimal hitbox for a
    // 0 x 0 size) and takes no hits
    ctx.check(!s->segments[8].active && s->segments[8].deathCounter == 0, "segment 8 popped and is inactive");
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

// Naga#isInvulnerableTo's direct entity: a hit whose direct entity (a projectile) is outside the
// home box deals nothing even when its owner stands inside; with the direct entity inside it lands.
GF_REGISTER_SELFTEST("naga.home_direct", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box   = co_await ctx.flatArea(2080, 12);
    auto            at    = ctx.testPos(2082, 2);
    auto&           in    = ctx.spawnPlayer("naga_direct_in");
    auto            inId  = in.getOrCreateUniqueID().rawID;
    auto&           out   = ctx.spawnPlayer("naga_direct_out");
    auto            outId = out.getOrCreateUniqueID().rawID;
    ProtectScope    protectIn(inId), protectOut(outId);
    in.teleport(Vec3{at.x + 0.5f, float(at.y), at.z + 4.5f}, DimensionType(at.dim));
    out.teleport(Vec3{at.x - 49.5f, float(at.y + 1), at.z + 2.5f}, DimensionType(at.dim)); // 50 west: outside the 46 box
    NagaScope n;
    auto*     naga = spawnNaga(at);
    ctx.require(naga != nullptr, "the Naga spawns");
    n.uid = naga->getOrCreateUniqueID().rawID;
    ge::setHome(*naga, at, kHomeRadius);
    co_await ctx.ticks(3);
    naga    = mobById(n.uid);
    auto* a = byId(inId);
    auto* d = byId(outId);
    ctx.require(naga && a && d, "the Naga and both players are there");
    ctx.check(withinHome(*naga, *a) && !withinHome(*naga, *d), "the owner is inside the home box, the direct entity outside");
    int before = naga->getHealth();
    gf::combat::hurt(*naga, 6.0f, "minecraft:arrow", a, d);
    co_await ctx.ticks(12);
    naga = mobById(n.uid);
    ctx.check(naga && naga->getHealth() == before, "no damage through a direct entity outside the home (" + std::to_string(naga ? naga->getHealth() : -1) + ")");
    a = byId(inId);
    ctx.require(naga && a, "the Naga and the owner are there");
    gf::combat::hurt(*naga, 6.0f, "minecraft:arrow", a, a);
    co_await ctx.ticks(2);
    naga = mobById(n.uid);
    ctx.check(naga && naga->getHealth() == before - 6, "the same hit with the direct entity inside lands (" + std::to_string(naga ? naga->getHealth() : -1) + ")");
    (void)box;
});

// Naga#doHurtTarget against a raised shield: in CHARGE the shield takes 5 damage and the Naga is
// dazed (DAZE); in STUNLESS_CHARGE the shield takes 10, the player stops using it and its cooldown
// (ItemCooldowns#addCooldown, 200 ticks) keeps it down, and the Naga goes back to CIRCLE.
GF_REGISTER_SELFTEST("naga.shield", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box  = co_await ctx.flatArea(2080, 12);
    auto            at   = ctx.testPos(2084, 2);
    auto&           sp   = ctx.spawnPlayer("naga_shield");
    auto            spId = sp.getOrCreateUniqueID().rawID;
    ProtectScope    protect(spId);
    sp.teleport(Vec3{at.x + 0.5f, float(at.y), at.z + 3.5f}, DimensionType(at.dim));
    auto shield = game::items::make("minecraft:shield");
    ctx.require(shield.has_value(), "a shield");
    sp.setOffhandSlot(*shield);
    auto category = shieldCooldownCategory(*shield);
    ctx.info("the shield's cooldown category: " + category);
    NagaScope n;
    auto*     naga = spawnNaga(at);
    ctx.require(naga != nullptr, "the Naga spawns");
    n.uid = naga->getOrCreateUniqueID().rawID;
    ge::setHome(*naga, at, kHomeRadius);
    auto raise = [&]() -> Player* {
        auto* p = byId(spId);
        if (!p || !p->isPlayer()) return nullptr;
        p->setSneaking(true); // Bedrock raises an offhand shield while sneaking
        return static_cast<Player*>(p);
    };
    raise();
    co_await ctx.ticks(10);
    auto* p = raise();
    ctx.require(p != nullptr, "the player is there");
    ctx.require(p->isBlocking(), "the player blocks with the raised shield");
    // CHARGE: the shield blocks, the Naga is dazed
    naga = mobById(n.uid);
    ctx.require(naga != nullptr, "the Naga is there");
    auto* s = stateOf(*naga);
    s->state = MovementState::Charge;
    int damage0 = p->getOffhandSlot().getDamageValue();
    ctx.check(!nagaHurtTarget(*naga, *p), "a charge into the shield deals no hit");
    ctx.check(s->state == MovementState::Daze && s->stateCounter >= 60 && s->stateCounter < 100, std::string("the Naga is dazed (") + stateName(s->state) + ")");
    p = raise();
    ctx.check(p && p->getOffhandSlot().getDamageValue() - damage0 == 5, "the shield took 5 damage (" + std::to_string(p ? p->getOffhandSlot().getDamageValue() - damage0 : -1) + ")");
    co_await ctx.ticks(5);
    // STUNLESS_CHARGE: the shield breaks through and stays down for 200 ticks
    p = raise();
    ctx.require(p && p->isBlocking(), "the player blocks again");
    naga = mobById(n.uid);
    ctx.require(naga != nullptr, "the Naga is there");
    s        = stateOf(*naga);
    s->state = MovementState::StunlessCharge;
    damage0  = p->getOffhandSlot().getDamageValue();
    ctx.check(!nagaHurtTarget(*naga, *p), "a stunless charge returns false");
    ctx.check(s->state == MovementState::Circle, std::string("the Naga circles again (") + stateName(s->state) + ")");
    p = raise();
    ctx.check(p && p->getOffhandSlot().getDamageValue() - damage0 == 10, "the shield took 10 damage");
    ctx.check(p && gf::item::cooldownLeft(*p, category) > 150, "the shield's cooldown runs (" + std::to_string(p ? gf::item::cooldownLeft(*p, category) : -1) + " ticks)");
    co_await ctx.ticks(10);
    p = raise();
    co_await ctx.ticks(10);
    p = raise();
    ctx.check(p && !p->isBlocking(), "the player cannot raise the shield during the cooldown");
    if (p) p->setSneaking(false);
    (void)box;
});

// EntityUtil#canDestroyBlock: containers (by Java's Container: chests, barrels) and the blocks
// whose canEntityDestroy refuses (boss spawners, skull chests, an inactive vanishing block joined
// to a locked one) stay; other block entities (signs, flower pots, note blocks, cauldrons,
// lecterns) and inactive or active vanishing blocks without a lock break; hardness -1 or 50+
// (bedrock, obsidian) never. And the friction the slither reads (Java's 0.6 / 0.98).
GF_REGISTER_SELFTEST("naga.destroy_blocks", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2080, 12);
    auto at  = ctx.testPos(2086, 2);
    struct Case {
        char const*    id;
        nlohmann::json states;
        bool           breaks;
    };
    std::vector<Case> cases{
        {"minecraft:stone", nlohmann::json::object(), true},
        {"minecraft:standing_sign", nlohmann::json::object(), true},
        {"minecraft:flower_pot", nlohmann::json::object(), true},
        {"minecraft:noteblock", nlohmann::json::object(), true},
        {"minecraft:cauldron", nlohmann::json::object(), true},
        {"minecraft:lectern", nlohmann::json::object(), true},
        {"minecraft:chest", nlohmann::json::object(), false},
        {"minecraft:barrel", nlohmann::json::object(), false},
        {"minecraft:bedrock", nlohmann::json::object(), false},
        {"minecraft:obsidian", nlohmann::json::object(), false},
        {"twilightforest:naga_boss_spawner", nlohmann::json::object(), false},
        {"twilightforest:skull_chest", nlohmann::json::object(), false},
        {"twilightforest:vanishing_block", {{"twilightforest:active", false}}, true},
        {"twilightforest:vanishing_block", {{"twilightforest:active", true}}, true},
    };
    for (std::size_t i = 0; i < cases.size(); i++) ctx.require(ctx.setBlock(at.offset(int(i % 7) - 3, 0, int(i / 7) * 2 - 2), cases[i].id, cases[i].states), std::string("placed ") + cases[i].id);
    // an inactive vanishing block joined (through another vanishing block) to a locked one
    auto locked = at.offset(-3, 0, 2); // on the ground: a floating sign or pot pops off
    ctx.require(ctx.setBlock(locked, "twilightforest:locked_vanishing_block", {{"twilightforest:locked", true}}), "a locked vanishing block");
    ctx.require(ctx.setBlock(locked.offset(1, 0, 0), "twilightforest:vanishing_block", {{"twilightforest:active", false}}), "a vanishing block next to it");
    ctx.require(ctx.setBlock(locked.offset(2, 0, 0), "twilightforest:vanishing_block", {{"twilightforest:active", false}}), "a vanishing block one further");
    co_await ctx.ticks(2);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "the test region");
    for (std::size_t i = 0; i < cases.size(); i++) {
        auto pos = at.offset(int(i % 7) - 3, 0, int(i / 7) * 2 - 2);
        bool can = nagaCanDestroyBlock(*region, game::blockPos(pos));
        ctx.check(can == cases[i].breaks, std::string(cases[i].id) + " " + cases[i].states.dump() + (cases[i].breaks ? " breaks" : " stays"));
    }
    ctx.check(!nagaCanDestroyBlock(*region, game::blockPos(locked)), "the locked vanishing block stays");
    ctx.check(!nagaCanDestroyBlock(*region, game::blockPos(locked.offset(2, 0, 0))), "an inactive vanishing block joined to a locked one stays");
    // the friction of the blocks under the slither
    if (auto const* stone = game::blockWith("minecraft:stone")) ctx.check(std::abs(blockFriction(*stone) - 0.6f) < 0.001f, "stone's friction is 0.6 (" + std::to_string(blockFriction(*stone)) + ")");
    if (auto const* ice = game::blockWith("minecraft:ice")) ctx.check(std::abs(blockFriction(*ice) - 0.98f) < 0.001f, "ice's friction is 0.98 (" + std::to_string(blockFriction(*ice)) + ")");
    (void)box;
});

// A Naga whose U20 state is gone while F8 kept its own (its chunk stayed unloaded a while) comes
// back as a fresh one, as in TF (no save data for the segments or the pattern): the synced flags
// off, the 12 parts F8 kept re-used (no new ones), the segments active again.
GF_REGISTER_SELFTEST("naga.reattach", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box = co_await ctx.flatArea(2080, 12);
    NagaScope       n;
    auto*           naga = spawnNaga(ctx.testPos(2088, 2));
    ctx.require(naga != nullptr, "the Naga spawns");
    n.uid = naga->getOrCreateUniqueID().rawID;
    co_await ctx.ticks(20);
    naga = mobById(n.uid);
    ctx.require(naga != nullptr, "the Naga is there");
    auto* s = stateOf(*naga);
    ctx.require(s && activeSegments(*s) == 12, "12 active segments");
    s->dazed = true;
    ge::setProperty(*naga, "twilightforest:dazed", true);
    dropStateForTest(*naga);
    co_await ctx.ticks(3);
    naga = mobById(n.uid);
    ctx.require(naga != nullptr, "the Naga is still there");
    s = stateOf(*naga);
    ctx.check(s && s->initialized && !s->dazed && !s->charging && !s->stunless, "the state came back fresh (flags off)");
    ctx.check(s && activeSegments(*s) == 12, "the segments are active again (" + std::to_string(s ? activeSegments(*s) : -1) + ")");
    ctx.check(ge::ext::parts(*naga).size() == kMaxSegments, "still 12 part actors (" + std::to_string(ge::ext::parts(*naga).size()) + ")");
    bool partsBack = s != nullptr;
    for (int i = 0; s && i < kMaxSegments; i++) partsBack &= ge::partActor(s->segments[i].part) != nullptr;
    ctx.check(partsBack, "every segment has its part actor again");
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
    static std::string                killedEntity, hurtBossData, hurtBossEntity;
    static bool                       sawScale = false, sawTrophy = false;
    static bool                       listening = false;
    if (!std::exchange(listening, true)) {
        for (char const* id : {"minecraft:player_killed_entity", "twilightforest:hurt_boss", "minecraft:inventory_changed"})
            gf::progression::onTrigger(id, [id = std::string(id)](Player& player, nlohmann::json const& data) {
                if (player.getNameTag() != "naga_fight") return;
                triggers[id]++;
                if (id == "minecraft:player_killed_entity") killedEntity = data.value("entity", std::string());
                if (id == "twilightforest:hurt_boss") hurtBossData = data.dump(), hurtBossEntity = data.value("entity", std::string());
                if (id == "minecraft:inventory_changed") {
                    auto const& inv = player.getInventory();
                    for (int i = 0; i < inv.getContainerSize(); i++) {
                        auto itemId = game::items::idOf(inv.getItem(i));
                        sawScale |= itemId == "twilightforest:naga_scale";
                        sawTrophy |= itemId == "twilightforest:naga_trophy";
                    }
                }
            });
    }
    triggers.clear();
    killedEntity.clear();
    hurtBossData.clear();
    hurtBossEntity.clear();
    sawScale = sawTrophy = false;
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
    int segmentsBefore = stateOf(*naga) ? stateOf(*naga)->currentSegmentCount : 0;
    ctx.check(segmentsBefore == 11, "11 segments at 115 health (" + std::to_string(segmentsBefore) + ")");
    gf::combat::hurt(*naga, 500.0f, "minecraft:player_attack", p);
    // NagaSegment#tick: every segment that pops sets naga.deathTime = 0, so the death animation
    // (24 ticks, then the 100-tick trail to the chest) starts over until the last one popped:
    // segment i pops (11 - i) * 12 ticks after the death, the last at 132, the chest 124 later
    auto chestPos  = home.offset(0, -1, 0);
    int  lastPop   = -1, chestAt = -1, maxDeathTimeWhilePopping = 0, prevActive = segmentsBefore;
    bool resetSeen = false;
    for (int t = 1; t <= 420 && chestAt < 0; t++) {
        co_await ctx.ticks(1);
        if (auto* dying = mobById(n.uid)) {
            if (auto* s = stateOf(*dying)) {
                int active = activeSegments(*s);
                int dt     = gf::boss::ext::deathTime(*dying);
                if (active < prevActive) {
                    lastPop    = t;
                    resetSeen |= dt <= 2; // reset to 0 at the pop (F8 counts the tick after)
                }
                if (active > 0) maxDeathTimeWhilePopping = std::max(maxDeathTimeWhilePopping, dt);
                prevActive = active;
            }
        }
        auto id = ctx.blockAt(chestPos);
        if (id == "twilightforest:twilight_oak_chest" || id == "twilightforest:canopy_chest") chestAt = t;
    }
    ctx.info("segments popped until tick " + std::to_string(lastPop) + ", the chest at tick " + std::to_string(chestAt) + ", death time while popping at most " +
             std::to_string(maxDeathTimeWhilePopping));
    ctx.check(lastPop >= 125 && lastPop <= 140, "the last segment popped about 132 ticks after the death (" + std::to_string(lastPop) + ")");
    ctx.check(resetSeen, "a popping segment resets the death time");
    ctx.check(maxDeathTimeWhilePopping <= 13, "the death time never reaches the trail while segments pop (at most " + std::to_string(maxDeathTimeWhilePopping) + ")");
    ctx.check(chestAt > 0 && chestAt - lastPop >= 120 && chestAt - lastPop <= 130,
              "the death chest comes 124 ticks after the last segment popped (" + std::to_string(chestAt - lastPop) + ")");
    co_await ctx.ticks(5);
    auto chestId = ctx.blockAt(chestPos);
    ctx.check(chestId == "twilightforest:twilight_oak_chest" || chestId == "twilightforest:canopy_chest", "the death chest is a twilight oak or canopy chest (" + chestId + ")");
    int                    scales = 0, trophies = 0;
    std::vector<ItemStack> taken;
    if (auto* region = game::regionOf(home.dim))
        if (auto handler = gf::inventory::find(*region, game::blockPos(chestPos)); handler && handler->get) {
            for (int i = 0; i < handler->slots; i++) {
                auto it = handler->get(chestPos, i);
                auto id = game::items::idOf(it);
                if (id == "twilightforest:naga_scale") scales += game::items::countOf(it);
                if (id == "twilightforest:naga_trophy") trophies += game::items::countOf(it);
                if (!game::items::isEmpty(it)) taken.push_back(it);
            }
            for (int i = 0; i < handler->slots; i++)
                if (handler->set) handler->set(chestPos, i, game::items::empty());
        }
    ctx.check(scales >= 6, "the chest holds the naga scales (" + std::to_string(scales) + ")");
    ctx.check(trophies == 1, "the chest holds the naga trophy");
    // progress_naga's criteria (one of them completes it): naga (player_killed_entity of a
    // twilightforest:naga), was_in_fight (hurt_boss of a twilightforest:naga), scale and trophy
    // (inventory_changed with the items). U14's criteria engine grants it; these are its inputs.
    ctx.check(triggers["minecraft:player_killed_entity"] >= 1 && killedEntity == kNagaId,
              "player_killed_entity fired for " + killedEntity + " (progress_naga's naga criterion)");
    ctx.check(triggers["twilightforest:hurt_boss"] >= 1, "hurt_boss fired (progress_naga's was_in_fight criterion): " + hurtBossData);
    // the player takes the chest's loot: the scale and trophy criteria's trigger fires
    triggers["minecraft:inventory_changed"] = 0;
    if (auto* pl = byId(spId); pl && pl->isPlayer())
        for (auto const& it : taken) (void)ctx.give(*static_cast<Player*>(pl), it);
    co_await ctx.ticks(3);
    ctx.check(triggers["minecraft:inventory_changed"] >= 1 && sawScale && sawTrophy,
              "inventory_changed fired with the naga scale and the naga trophy in the inventory (progress_naga's scale and trophy criteria)");
    // progress_naga end to end as far as this build goes: its criteria (the imported advancement
    // JSON the mod ships) matched against the trigger data that fired, the way a vanilla criteria
    // engine reads them (entity_properties type, item predicates), and its requirements met.
    {
        auto file = Mod::getInstance().getSelf().getModDir() / "data" / "twilightforest" / "advancement" / "progress_naga.json";
        std::ifstream in(file);
        ctx.require(in.good(), "the progress_naga advancement ships (" + file.string() + ")");
        auto adv = nlohmann::json::parse(in, nullptr, false);
        ctx.require(adv.is_object() && adv.contains("criteria"), "progress_naga parses");
        auto entityType = [](nlohmann::json const& conds, char const* key) {
            for (auto const& c : conds.value(key, nlohmann::json::array()))
                if (c.value("condition", std::string()) == "minecraft:entity_properties") return c.value("predicate", nlohmann::json::object()).value("type", std::string());
            return std::string();
        };
        std::set<std::string> met;
        for (auto const& [name, crit] : adv["criteria"].items()) {
            auto trig  = crit.value("trigger", std::string());
            auto conds = crit.value("conditions", nlohmann::json::object());
            bool ok    = false;
            if (trig == "minecraft:player_killed_entity") ok = triggers[trig] >= 1 && entityType(conds, "entity") == killedEntity;
            else if (trig == "twilightforest:hurt_boss") ok = triggers[trig] >= 1 && entityType(conds, "hurt_entity") == hurtBossEntity;
            else if (trig == "minecraft:inventory_changed") {
                auto item = conds.value("items", nlohmann::json::array()).empty() ? std::string() : conds["items"][0].value("items", std::string());
                ok        = triggers[trig] >= 1 && ((item == "twilightforest:naga_scale" && sawScale) || (item == "twilightforest:naga_trophy" && sawTrophy));
            }
            ctx.check(ok, "progress_naga criterion " + name + " (" + trig + ") is met by the fight");
            if (ok) met.insert(name);
        }
        bool all = true;
        for (auto const& group : adv.value("requirements", nlohmann::json::array())) {
            bool any = false;
            for (auto const& c : group) any = any || met.contains(c.get<std::string>());
            all = all && any;
        }
        ctx.check(all && met.size() == 4, "progress_naga's requirements are met (" + std::to_string(met.size()) + " of 4 criteria)");
    }
    if (auto* pl = byId(spId); pl && pl->isPlayer()) {
        if (gf::progression::has(*static_cast<Player*>(pl), "twilightforest:progress_naga")) ctx.info("progress_naga granted (U14's criteria engine)");
        else ctx.info("progress_naga not granted: no criteria engine in this build (U14)");
    }
    ctx.check(mobById(n.uid) == nullptr, "the body is gone after the death animation");
    if (auto* region = game::regionOf(chestPos.dim))
        if (auto const* b = game::blockWith(floor.empty() ? "minecraft:grass_block" : floor)) game::setBlock(*region, game::blockPos(chestPos), *b);
    (void)box;
});

#ifdef GF_CLIENT
// TFMapDecorations.NAGA_COURTYARD: the client draws the decoration with TF's map icon.
GF_REGISTER_SELFTEST("naga.map_icon", [](Context& ctx) {
    auto icon = gf::client::maps::iconOf("twilightforest:naga_courtyard");
    ctx.check(icon == "textures/twilightforest/map/decorations/naga_courtyard", "the naga_courtyard map decoration has TF's icon (" + icon + ")");
});
#endif

// ---- client display (tools/e2e/client/scenarios/naga.ps1) -----------------------------------------------

namespace {
/// Turns the display Naga to the camera of the client scenario (7 east, 7 south of it): yaw -45,
/// so the face of the head is in the screenshots.
void faceCamera(Mob& m) {
    float yaw                          = static_cast<float>(std::atan2(7.0, 7.0) * 180.0 / 3.14159265358979323846) - 90.0f;
    const_cast<Vec2&>(m.getRotation()) = Vec2{0.0f, yaw};
    m.setYBodyRotations(yaw, yaw);
    m.setYHeadRotations(yaw, yaw);
}
} // namespace

// The Naga next to the camera for the client screenshots (skipped without a client): 10 s as it is (12 segments, nagahead),
// 8 s charging (nagahead_charging) and 8 s dazed (nagahead_dazed, the head lowered, crit sparkles).
// It is held in place (no target, put back every tick) and removed at the end.
GF_REGISTER_SELFTEST("naga.display", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!clientWatching()) {
        ctx.info("no client connected: the display is skipped");
        co_return;
    }
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
    // the Naga's movement pattern keeps running and sets charging / dazed from its state every
    // tick, so the pattern is held in the phase's state (CIRCLE, CHARGE, DAZE) and the flags are
    // put back every tick
    auto show = [&](Mob& m, int ph) {
        if (auto* s = stateOf(m)) {
            s->state        = ph == 0 ? MovementState::Circle : ph == 1 ? MovementState::Charge : MovementState::Daze;
            s->stateCounter = 1000;
            if (s->charging != (ph == 1) || s->dazed != (ph == 2)) {
                s->charging = ph == 1;
                s->dazed    = ph == 2;
                ge::setProperty(m, "twilightforest:charging", s->charging);
                ge::setProperty(m, "twilightforest:dazed", s->dazed);
            }
        }
    };
    for (int ph = 0; ph < 3; ph++) {
        if (auto* m = mobById(n.uid)) show(*m, ph);
        for (int t = 0; t < lengths[ph]; t++) {
            co_await ctx.ticks(1);
            auto* m = mobById(n.uid);
            ctx.require(m != nullptr, "the Naga is still there");
            show(*m, ph);
            m->setTarget(nullptr);
            auto p = m->getFeetPos();
            if (std::abs(p.x - (at.x + 0.5f)) > 0.3f || std::abs(p.z - (at.z + 0.5f)) > 0.3f)
                m->teleportTo(Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f}, true, 0, 0, false);
            faceCamera(*m);
            if (t == 20) {
                auto prop = [&](char const* name) {
                    auto  props = m->getEntityContext().tryGetComponent<PropertyComponent>();
                    auto  v     = props ? props->tryGetBool(HashedString::computeHash(name)) : std::nullopt;
                    return v ? (*v ? "1" : "0") : "?";
                };
                ctx.info(std::string("naga ") + phases[ph] + " (properties charging=" + prop("twilightforest:charging") + " dazed=" + prop("twilightforest:dazed") + ")");
                ctx.check(std::string(prop("twilightforest:charging")) == (ph == 1 ? "1" : "0") && std::string(prop("twilightforest:dazed")) == (ph == 2 ? "1" : "0"),
                          std::string("the ") + phases[ph] + " Naga carries its synced properties");
            }
        }
    }
    // the death: NagaRenderer#getFlipDegrees is 0 (the body does not tip over), the head shows
    // nagahead_charging (NagaRenderer: deathTime > 0); the segments pop one by one
    if (auto* m = mobById(n.uid)) {
        faceCamera(*m);
        gf::combat::hurt(*m, 1000.0f, "minecraft:generic");
    }
    for (int t = 0; t < 400; t++) {
        co_await ctx.ticks(1);
        if (t == 40) ctx.info("naga dying");
        if (t > 60 && !mobById(n.uid)) break;
    }
    ctx.check(mobById(n.uid) == nullptr, "the dead Naga is gone after its death animation");
    (void)box; // the death chest is under the home, in the flat area's restored ground layer
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

// ---- the seeded courtyard at a fixed site ----------------------------------------------------------

namespace {

// The processors' randomness is positional (StructurePlaceSettings#getRandom(pos): Mth#getSeed of
// the block position), so a golden volume hash only holds at one absolute position: the seeded
// courtyard is built at a fixed site far from the test rows (x, z -4000..-3857, the dais at y 250),
// in any world, with its own ticking area. Nothing there belongs to a test row (rule R12): every
// run snapshots the site, clears it, builds the courtyard and restores what was there at the end
// (also when the test fails), as Context::flatArea does, so no block is left behind.
constexpr int  kSiteMinX = -4000, kSiteMinZ = -4000, kSiteSize = 144;
constexpr int  kSiteCy    = 250;          ///< the courtyard's y (CourtyardMain's origin)
constexpr int  kSiteFloor = kSiteCy - 5;  ///< the grass layer (a flat area's ground layer)
constexpr int  kSiteTop   = kSiteCy + 24; ///< the cleared air reaches up to here
constexpr int  kSiteCx = kSiteMinX + 70, kSiteCz = kSiteMinZ + 70;
constexpr char kSiteArea[] = "gf_naga_courtyard";

/// The site's original blocks (kSiteFloor..kSiteTop, y, z, x order) and its ticking area: when the
/// test ends (also when it fails) the blocks are put back, Nagas a spawner may have made there are
/// removed (not killed: no death chest), then the ticking area.
struct SiteArea {
    int                       dim = 0;
    std::vector<Block const*> blocks;
    template <class Fn>
    static void forEach(Fn&& fn) {
        for (int y = kSiteFloor; y <= kSiteTop; ++y)
            for (int z = kSiteMinZ; z < kSiteMinZ + kSiteSize; ++z)
                for (int x = kSiteMinX; x < kSiteMinX + kSiteSize; ++x) fn(BlockPos{x, y, z});
    }
    ~SiteArea() {
        if (auto* region = game::regionOf(dim); region && !blocks.empty()) {
            std::size_t i = 0;
            forEach([&](BlockPos const& p) {
                Block const* want = blocks[i++];
                if (want && region->hasChunksAt(p, 0, false) && &region->getBlock(p) != want) game::setBlock(*region, p, *want);
            });
        }
        // removed, not killed: a death would place the boss death chest after the restore
        if (auto level = ll::service::getLevel())
            for (auto* a : level->getRuntimeActorList()) {
                if (!a || a->getTypeName() != kNagaId || static_cast<int>(a->getDimensionId()) != dim) continue;
                auto p = a->getFeetPos();
                if (p.x >= kSiteMinX - 16 && p.x < kSiteMinX + kSiteSize + 16 && p.z >= kSiteMinZ - 16 && p.z < kSiteMinZ + kSiteSize + 16) a->remove();
            }
        (void)gf::selftest::runServerCommand(std::string("tickingarea remove ") + kSiteArea, dim);
    }
};

/// Loads the site, snapshots it into `site` and clears it: grass at kSiteFloor, air above up to
/// kSiteTop.
ll::coro::CoroTask<> prepareSite(Context& ctx, SiteArea& site) {
    int dim = ctx.dimension();
    (void)gf::selftest::runServerCommand(std::string("tickingarea remove ") + kSiteArea, dim); // a crashed run's
    auto added = gf::selftest::runServerCommand("tickingarea add " + std::to_string(kSiteMinX) + " 0 " + std::to_string(kSiteMinZ) + " " +
                                                    std::to_string(kSiteMinX + kSiteSize - 1) + " 0 " + std::to_string(kSiteMinZ + kSiteSize - 1) + " " +
                                                    kSiteArea + " true",
                                                dim);
    ctx.require(added.ok, "the courtyard site's ticking area (" + added.messages + ")");
    ctx.require(co_await ctx.ensureLoaded(core::GlobalPos{dim, kSiteCx, kSiteCy, kSiteCz}), "the courtyard site loads");
    bool loaded = false;
    for (int i = 0; i < 1200 && !loaded; ++i) {
        auto* region = ctx.region();
        loaded       = region != nullptr;
        for (int x = kSiteMinX; loaded && x < kSiteMinX + kSiteSize + 15; x += 16)
            for (int z = kSiteMinZ; loaded && z < kSiteMinZ + kSiteSize + 15; z += 16)
                loaded = region->hasChunksAt(BlockPos{std::min(x, kSiteMinX + kSiteSize - 1), kSiteCy, std::min(z, kSiteMinZ + kSiteSize - 1)}, 0, false);
        if (!loaded) co_await ctx.ticks(1);
    }
    ctx.require(loaded, "every chunk of the courtyard site loaded within 60 s");
    auto*       region = ctx.region();
    auto const* grass  = game::blockWith("minecraft:grass_block");
    auto const* air    = game::blockWith("minecraft:air");
    ctx.require(grass && air && region, "vanilla blocks available");
    site.blocks.clear();
    site.blocks.reserve(static_cast<std::size_t>(kSiteSize) * kSiteSize * (kSiteTop - kSiteFloor + 1));
    SiteArea::forEach([&](BlockPos const& p) { site.blocks.push_back(region->hasChunksAt(p, 0, false) ? &region->getBlock(p) : nullptr); });
    for (int x = kSiteMinX; x < kSiteMinX + kSiteSize; ++x)
        for (int z = kSiteMinZ; z < kSiteMinZ + kSiteSize; ++z)
            for (int y = kSiteFloor; y <= kSiteTop; ++y) {
                BlockPos     p{x, y, z};
                Block const* want = y == kSiteFloor ? grass : air;
                if (&region->getBlock(p) != want) game::setBlock(*region, p, *want);
            }
}

/// Places the seeded courtyard (CourtyardMain from LegacyRandom 424242, its children from 171717,
/// the pieces' post-processing from 7) at the site; returns the union of the piece boxes.
BlockBox placeSeededCourtyard(Context& ctx) {
    core::jrand::LegacyRandom first(424242), context(171717);
    auto pieces = courtyardPieces(first, context, kSiteCx - 1, kSiteCy, kSiteCz - 1);
    ctx.require(pieces.size() > 100, "the courtyard pieces (" + std::to_string(pieces.size()) + ")");
    auto* source = ctx.region();
    ctx.require(source != nullptr, "the test region");
    BlockBox all = pieces.front()->bbox;
    for (auto const& p : pieces) {
        all.minX = std::min(all.minX, p->bbox.minX), all.minY = std::min(all.minY, p->bbox.minY), all.minZ = std::min(all.minZ, p->bbox.minZ);
        all.maxX = std::max(all.maxX, p->bbox.maxX), all.maxY = std::max(all.maxY, p->bbox.maxY), all.maxZ = std::max(all.maxZ, p->bbox.maxZ);
    }
    ctx.require(kSiteMinX <= all.minX && all.maxX < kSiteMinX + kSiteSize && kSiteMinZ <= all.minZ && all.maxZ < kSiteMinZ + kSiteSize && kSiteFloor < all.minY &&
                    all.maxY < kSiteTop,
                "the courtyard fits the site (pieces " + std::to_string(all.minX) + " " + std::to_string(all.minY) + " " + std::to_string(all.minZ) + " .. " +
                    std::to_string(all.maxX) + " " + std::to_string(all.maxY) + " " + std::to_string(all.maxZ) + ")");
    LiveRegion                region(*source, ctx.dimension());
    core::jrand::LegacyRandom random(7);
    BlockBox                  clip{all.minX - 2, all.minY - 2, all.minZ - 2, all.maxX + 2, all.maxY + 8, all.maxZ + 2};
    for (auto& p : pieces) p->postProcess(region, random, clip, BlockPos{kSiteCx, kSiteCy, kSiteCz});
    return all;
}

} // namespace

// A seeded courtyard placed piece by piece at the fixed site: the golden volume hash (rule R19,
// compared in every world) and the spawner block at the center.
GF_REGISTER_SELFTEST("naga.courtyard_layout", [](Context& ctx) -> ll::coro::CoroTask<> {
    SiteArea site{ctx.dimension()};
    co_await prepareSite(ctx, site);
    auto all = placeSeededCourtyard(ctx);
    co_await ctx.ticks(2);
    int      cx = kSiteCx, cy = kSiteCy, cz = kSiteCz;
    BlockBox hashBox{all.minX, cy - 3, all.minZ, all.maxX, cy + 6, all.maxZ};
    auto     hash = ctx.snapshotVolume(hashBox);
    ctx.info("courtyard volume hash " + hash + " box " + std::to_string(hashBox.minX) + " " + std::to_string(hashBox.minY) + " " + std::to_string(hashBox.minZ) +
             " " + std::to_string(hashBox.maxX) + " " + std::to_string(hashBox.maxY) + " " + std::to_string(hashBox.maxZ));
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
    static char const* const kGolden = "69dee0ca2240c26d"; // R19 screenshot review pending (client harness)
    ctx.check(hash == kGolden, "the golden volume hash " + std::string(kGolden) + " (got " + hash + ")");
});

// The seeded courtyard of naga.courtyard_layout, built at the same site for the client
// screenshots, held for 30 s and then restored (skipped without a client).
GF_REGISTER_SELFTEST("naga.display_courtyard", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!clientWatching()) {
        ctx.info("no client connected: the display is skipped");
        co_return;
    }
    SiteArea site{ctx.dimension()};
    co_await prepareSite(ctx, site);
    (void)placeSeededCourtyard(ctx);
    ctx.info("courtyard at " + std::to_string(kSiteCx) + " " + std::to_string(kSiteCy) + " " + std::to_string(kSiteCz));
    co_await ctx.ticks(600);
});

} // namespace gf::feature::naga::tests

