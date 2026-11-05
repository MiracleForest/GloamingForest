// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// U26 (darktower) selftests of the mobs (rule R12: "darktower.*", testPos dx 2600..2699): F13's
// goldens (tests/golden/mobs.json: max health, the collision box, movement speed, follow range and
// attack damage of the six dark tower mobs), the Ur-Ghast fireball's impact (16 damage, an explosion
// that never breaks blocks, even with mob griefing on) and the Ur-Ghast spawner's height gate.
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "ll/api/coro/CoroTask.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/attribute/Attribute.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/MutableAttributeWithContext.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/darktower/UrGhast.h"
#include "feature/darktower/Util.h"
#include "feature/death/Death.h"
#include "feature/loot/Loot.h"
#include "game/Items.h"
#include "game/Blocks.h"
#include "selftest/SelfTest.h"

namespace gf::feature::darktower::tests {

namespace {

using selftest::Context;

struct Golden {
    char const* id;
    float       health, width, height, movement, followRange, attack;
};

// tests/golden/mobs.json (F13, derived from TF 4.8): maxHealth, width, height and the
// movement_speed / follow_range / attack_damage attributes (attack -1: the mob has none)
constexpr Golden kGoldens[] = {
    {"twilightforest:ur_ghast", 250.0f, 14.0f, 18.0f, 0.7f, 128.0f, -1.0f},
    {"twilightforest:carminite_ghastguard", 30.0f, 4.0f, 6.0f, 0.7f, 64.0f, -1.0f},
    {"twilightforest:carminite_ghastling", 10.0f, 1.1f, 1.5f, 0.7f, 16.0f, -1.0f},
    {"twilightforest:carminite_golem", 40.0f, 1.4f, 2.9f, 0.25f, 16.0f, 9.0f},
    {"twilightforest:carminite_broodling", 7.0f, 0.7f, 0.5f, 0.3f, 16.0f, 4.0f},
    {"twilightforest:towerwood_borer", 15.0f, 0.4f, 0.3f, 0.27f, 8.0f, 5.0f},
};

float attribute(Mob& m, Attribute const& a) {
    auto ref = m.getMutableAttribute(a);
    if (auto* inst = ref.mInstance->mPtr) return inst->mCurrentValue;
    return -1.0f;
}

bool near(float a, float b) { return std::abs(a - b) < 0.011f; }

// the actors whose lethal hit the reflected-fireball tests record (and survive), and that hit's amount
std::int64_t sReflectWatch = 0;
float        sReflectLethal = -1.0f;
std::int64_t sGuardWatch = 0;
float        sGuardLethal = -1.0f;

/// mobGriefing on for a test, the previous value restored afterwards.
struct GriefingOn {
    bool previous = mobGriefing();
    GriefingOn() { (void)selftest::runServerCommand("gamerule mobgriefing true"); }
    ~GriefingOn() { (void)selftest::runServerCommand(std::string("gamerule mobgriefing ") + (previous ? "true" : "false")); }
};

/// The mob tests need a non-peaceful world (peaceful removes hostile mobs).
struct EasyDifficulty {
    SharedTypes::Legacy::Difficulty previous = SharedTypes::Legacy::Difficulty::Peaceful;
    EasyDifficulty() {
        if (auto* l = level()) {
            previous = l->getDifficulty();
            l->setDifficulty(SharedTypes::Legacy::Difficulty::Easy);
        }
    }
    ~EasyDifficulty() {
        if (auto* l = level()) l->setDifficulty(previous);
    }
};

} // namespace

GF_REGISTER_SELFTEST("darktower.mob_goldens", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2660, 24, 24);
    (void)box;
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto at = ctx.testPos(2660, 2).offset(0, 3, 0);
    for (auto const& g : kGoldens) {
        auto* m = spawnMob(*region, g.id, Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f});
        ctx.require(m != nullptr, std::string("spawned ") + g.id);
        auto b = m->getAABB();
        ctx.check(near(m->getMaxHealth(), g.health), std::string(g.id) + " max health " + std::to_string(m->getMaxHealth()));
        ctx.check(near(b.max.x - b.min.x, g.width) && near(b.max.y - b.min.y, g.height),
                  std::string(g.id) + " box " + std::to_string(b.max.x - b.min.x) + " x " + std::to_string(b.max.y - b.min.y));
        ctx.check(near(attribute(*m, SharedAttributes::MOVEMENT_SPEED()), g.movement),
                  std::string(g.id) + " movement " + std::to_string(attribute(*m, SharedAttributes::MOVEMENT_SPEED())));
        ctx.check(near(attribute(*m, SharedAttributes::FOLLOW_RANGE()), g.followRange),
                  std::string(g.id) + " follow range " + std::to_string(attribute(*m, SharedAttributes::FOLLOW_RANGE())));
        if (g.attack >= 0)
            ctx.check(near(attribute(*m, SharedAttributes::ATTACK_DAMAGE()), g.attack),
                      std::string(g.id) + " attack " + std::to_string(attribute(*m, SharedAttributes::ATTACK_DAMAGE())));
        m->remove();
    }
    co_return;
});

// UrGhastFireball#onHitBlock: an explosion of power 1 that never breaks blocks (ExplosionInteraction.NONE)
GF_REGISTER_SELFTEST("darktower.urghast_fireball", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2640, 24, 24);
    (void)box;
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    GriefingOn griefing; // restored when the test ends
    auto base = ctx.testPos(2640, 2).offset(0, 1, 0);
    // a stone wall 8 blocks east of the shot
    for (int y = 0; y < 4; y++)
        for (int z = -1; z <= 1; z++) ctx.setBlock(base.offset(8, y, z), "minecraft:stone");
    auto* owner = spawnMob(*region, id::Ghastling, Vec3{base.x + 0.5f, float(base.y + 6), base.z + 0.5f});
    ctx.require(owner != nullptr, "an owner for the fireball");
    auto  ownerId = uidOf(*owner);
    auto* fb      = ghasts::shootUrGhastFireball(*owner, Vec3{base.x + 0.5f, base.y + 1.5f, base.z + 0.5f}, Vec3{1.0f, 0.0f, 0.0f});
    ctx.require(fb != nullptr, "the fireball spawned");
    auto fbId = uidOf(*fb);
    for (int i = 0; i < 60 && actorById(fbId); i++) co_await ctx.ticks(1);
    ctx.check(actorById(fbId) == nullptr, "the fireball hit the wall");
    region = ctx.region();
    int intact = 0;
    for (int y = 0; y < 4; y++)
        for (int z = -1; z <= 1; z++) intact += idAt(*region, game::blockPos(base.offset(8, y, z))) == "minecraft:stone" ? 1 : 0;
    ctx.check(intact == 12, "no block of the wall broke (" + std::to_string(intact) + " of 12)");
    if (auto* o = actorById(ownerId)) o->remove();
    co_return; // the flat area is restored afterwards (the wall and any fire)
});

// A fireball a player sent back (its owner is now the player) hits the Ur-Ghast: 16 fireball damage
// (UrGhastFireball#onHitEntity), let through isInvulnerableTo as a player-reflected large fireball
GF_REGISTER_SELFTEST("darktower.reflected_fireball", [](Context& ctx) -> ll::coro::CoroTask<> {
    EasyDifficulty easy;
    auto box = co_await ctx.flatArea(2640, 48, 40);
    (void)box;
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto at = ctx.testPos(2655, 2).offset(0, 4, 0);
    auto& player = static_cast<Player&>(ctx.player());
    player.setPlayerGameType(GameType::Creative);
    player.teleportTo(Vec3{at.x + 30.5f, float(at.y), at.z + 0.5f}, true, 0, 0, false);
    auto* ur = spawnMob(*region, id::UrGhast, Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f});
    ctx.require(ur != nullptr, "the Ur-Ghast spawned");
    auto urId = uidOf(*ur);
    ghasts::setSolid(*ur, true); // past its first flight point (a noPhysics entity is never hit)
    // the lethal hit is recorded and survived (a real death would leave the boss's death chest)
    static bool sHooked = false;
    if (!std::exchange(sHooked, true))
    {
        // the amount the game's hurt gets (after every incoming handler), and the lethal hit survived
        combat::onIncoming(combat::priority::Lowest - 100, [](combat::DamageInfo& d) {
            if (sReflectWatch != 0 && uidOf(d.victim) == sReflectWatch) sReflectLethal = std::max(sReflectLethal, d.amount);
            return combat::Verdict::Pass;
        });
        death::onLethal(100000, [](Actor& victim, combat::DamageInfo const&) { return sReflectWatch != 0 && uidOf(victim) == sReflectWatch; });
    }
    sReflectWatch = urId;
    // UrGhast#hurt divides by 10 in a tantrum, then Ghast#hurt deals 1000 to a reflected fireball
    for (bool tantrum : {false, true}) {
        auto* u = static_cast<Mob*>(actorById(urId));
        ctx.require(u != nullptr, "the Ur-Ghast is there");
        ghasts::setTantrum(*u, tantrum);
        sReflectLethal = -1.0f;
        // the Ur-Ghast's own fireball, re-owned by the player (what a player's hit does), flying back at it
        auto* fb = ghasts::shootUrGhastFireball(*u, Vec3{at.x + 16.5f, at.y + 9.0f, at.z + 0.5f}, Vec3{-1.0f, 0.0f, 0.0f});
        ctx.require(fb != nullptr, "the fireball spawned");
        fb->setOwner(player.getOrCreateUniqueID());
        auto fbId = uidOf(*fb);
        for (int i = 0; i < 40 && actorById(fbId); i++) co_await ctx.ticks(1);
        ctx.check(actorById(fbId) == nullptr, "the fireball hit");
        ctx.check(sReflectLethal >= 999.0f, std::string("the reflected fireball dealt Ghast#hurt's 1000") + (tantrum ? " in a tantrum" : "") +
                                                " (lethal amount " + std::to_string(sReflectLethal) + ")");
        co_await ctx.ticks(12); // past the hurt cooldown
    }
    sReflectWatch = 0;
    if (auto* again = actorById(urId)) again->remove();
    co_await ctx.ticks(1);
});

// A carminite ghastguard's vanilla LargeFireball a player sent back into the Ur-Ghast: Ghast#hurt's
// 1000 too (Bedrock's fire immunity would refuse the engine's fire impact)
GF_REGISTER_SELFTEST("darktower.reflected_ghastguard_fireball", [](Context& ctx) -> ll::coro::CoroTask<> {
    EasyDifficulty easy;
    auto box = co_await ctx.flatArea(2640, 48, 40);
    (void)box;
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto at = ctx.testPos(2655, 2).offset(0, 4, 0);
    auto& player = static_cast<Player&>(ctx.player());
    player.setPlayerGameType(GameType::Creative);
    player.teleportTo(Vec3{at.x + 30.5f, float(at.y), at.z + 0.5f}, true, 0, 0, false);
    auto* ur = spawnMob(*region, id::UrGhast, Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f});
    ctx.require(ur != nullptr, "the Ur-Ghast spawned");
    auto urId = uidOf(*ur);
    ghasts::setSolid(*ur, true);
    auto* guard = spawnMob(*region, id::Ghastguard, Vec3{at.x + 20.5f, float(at.y + 20), at.z + 0.5f});
    ctx.require(guard != nullptr, "a ghastguard");
    auto guardId = uidOf(*guard);
    static bool sHooked = false;
    if (!std::exchange(sHooked, true))
    {
        combat::onIncoming(combat::priority::Lowest - 100, [](combat::DamageInfo& d) {
            if (sGuardWatch != 0 && uidOf(d.victim) == sGuardWatch) sGuardLethal = std::max(sGuardLethal, d.amount);
            return combat::Verdict::Pass;
        });
        death::onLethal(100000, [](Actor& victim, combat::DamageInfo const&) { return sGuardWatch != 0 && uidOf(victim) == sGuardWatch; });
    }
    sGuardWatch  = urId;
    sGuardLethal = -1.0f;
    auto* fb = ghasts::shootLargeFireball(*guard, Vec3{at.x + 16.5f, at.y + 9.0f, at.z + 0.5f}, Vec3{-1.0f, 0.0f, 0.0f});
    ctx.require(fb != nullptr, "the ghastguard's fireball spawned");
    fb->setOwner(player.getOrCreateUniqueID());
    auto fbId = uidOf(*fb);
    for (int i = 0; i < 40 && actorById(fbId); i++) co_await ctx.ticks(1);
    ctx.check(actorById(fbId) == nullptr, "the fireball hit");
    ctx.check(sGuardLethal >= 999.0f, "the reflected ghastguard fireball dealt 1000 (lethal amount " + std::to_string(sGuardLethal) + ")");
    sGuardWatch = 0;
    if (auto* g = actorById(guardId)) g->remove();
    if (auto* again = actorById(urId)) again->remove();
    co_await ctx.ticks(1);
});

// LootTable entities/carminite_ghastguard / _ghastling: vanilla's entities/ghast (a ghast tear 0-1,
// gunpowder 0-2); a minion ghastling drops nothing (IsMinionCondition, inverse)
GF_REGISTER_SELFTEST("darktower.ghast_loot", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    int tears = 0, powder = 0, other = 0;
    for (auto const* table : {"twilightforest:entities/carminite_ghastguard", "twilightforest:entities/carminite_ghastling"}) {
        for (std::int64_t seed = 1; seed <= 40; seed++) {
            loot::LootContext lc;
            lc.region = region;
            lc.seed   = seed;
            for (auto const& s : loot::roll(table, lc)) {
                auto n = game::items::idOf(s);
                if (n == "minecraft:ghast_tear") tears += s.mCount;
                else if (n == "minecraft:gunpowder") powder += s.mCount;
                else ++other;
            }
        }
    }
    ctx.check(tears > 0 && powder > 0, "ghast tears (" + std::to_string(tears) + ") and gunpowder (" + std::to_string(powder) + ") drop");
    ctx.check(tears <= 80 && powder <= 160 && other == 0, "at most one tear and two gunpowder per roll, nothing else");
    co_return;
});

// Ghast#hurt, kept by the carminite ghasts: a player-reflected large fireball deals 1000
GF_REGISTER_SELFTEST("darktower.ghastguard_reflected", [](Context& ctx) -> ll::coro::CoroTask<> {
    EasyDifficulty easy;
    auto box = co_await ctx.flatArea(2660, 24, 24);
    (void)box;
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto at = ctx.testPos(2665, 2).offset(0, 3, 0);
    auto& player = static_cast<Player&>(ctx.player());
    player.setPlayerGameType(GameType::Creative);
    for (auto const* type : {id::Ghastguard, id::Ghastling}) {
        auto* g = spawnMob(*region, type, Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f});
        ctx.require(g != nullptr, std::string("spawned ") + type);
        auto  gid = uidOf(*g);
        auto* fb  = ghasts::shootUrGhastFireball(*g, Vec3{at.x + 6.5f, at.y + 20.0f, at.z + 0.5f}, Vec3{0.0f, 1.0f, 0.0f});
        ctx.require(fb != nullptr, "a fireball");
        fb->setOwner(player.getOrCreateUniqueID());
        combat::hurt(*g, 6.0f, "minecraft:fireball", &player, fb);
        auto* again = actorById(gid);
        ctx.check(!again || !again->isAlive() || again->getHealth() <= 0, std::string(type) + " died of the reflected fireball");
        fb->remove();
        if (again) again->remove();
        co_await ctx.ticks(2);
    }
});

// InfestedBlock#spawnAfterBreak runs for an explosion too (Block#onExplosionHit): a borer pops out
GF_REGISTER_SELFTEST("darktower.infested_explosion", [](Context& ctx) -> ll::coro::CoroTask<> {
    EasyDifficulty easy;
    auto box = co_await ctx.flatArea(2680, 12);
    (void)box;
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto p = ctx.testPos(2685, 2).offset(0, 1, 0);
    ctx.require(ctx.setBlock(p, id::InfestedTowerwood), "infested towerwood placed");
    auto bp = game::blockPos(p);
    region->getLevel().explode(*region, nullptr, Vec3{bp.x + 0.5f, bp.y + 0.5f, bp.z + 0.5f}, 2.0f, false, true, 3.40282347e38f, false);
    co_await ctx.ticks(2);
    region = ctx.region();
    ctx.check(idAt(*region, bp) != id::InfestedTowerwood, "the explosion broke the infested towerwood");
    int borers = 0;
    AABB around{Vec3{bp.x - 4.0f, bp.y - 4.0f, bp.z - 4.0f}, Vec3{bp.x + 5.0f, bp.y + 5.0f, bp.z + 5.0f}};
    for (auto* a : region->getEntities(around, 0.0f))
        if (a && typeOf(*a) == id::Borer) {
            ++borers;
            a->remove();
        }
    ctx.check(borers == 1, "one towerwood borer popped out (" + std::to_string(borers) + ")");
    for (auto* a : region->getEntities(around, 0.0f))
        if (a && typeOf(*a) == "minecraft:item") a->remove();
});

} // namespace gf::feature::darktower::tests
