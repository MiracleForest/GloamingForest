// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// U21 (lich): selftests (lich.*, testPos dx 2100-2199, rule R12).

#include <cmath>
#include <cstdio>
#include <optional>
#include <algorithm>
#include <map>
#include <memory>
#include <string>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/lich/Common.h"
#include "feature/lich/Lich.h"
#include "feature/lich/Tower.h"
#include "feature/structure/Region.h"
#include "feature/structure/Structure.h"
#include "feature/loot/Loot.h"
#include "feature/projectile/Projectile.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/landmark/LandmarkTest.h"
#include "feature/landmark/Landmark.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace gf::feature::lich {

namespace {

using gf::selftest::Context;

Vec3 at(core::GlobalPos const& p, float dy = 0.0f) { return Vec3{p.x + 0.5f, p.y + dy, p.z + 0.5f}; }

Mob* spawnAt(Context& ctx, std::string const& id, core::GlobalPos const& p, float dy = 0.0f) {
    auto* region = ctx.region();
    return region ? spawnMob(*region, id, at(p, dy)) : nullptr;
}

void removeById(std::int64_t uid) {
    if (auto* a = actorById(uid)) a->remove();
}

/// Removes every actor of `id` within `r` blocks of `p`.
int removeAround(Context& ctx, core::GlobalPos const& p, char const* id, float r) {
    auto* region = ctx.region();
    if (!region) return 0;
    AABB box{Vec3{p.x - r, p.y - r, p.z - r}, Vec3{p.x + r, p.y + r, p.z + r}};
    int  n = 0;
    for (auto* a : actorsIn(*region, p.dim, box))
        if (a && !a->isPlayer() && a->getTypeName() == id) a->remove(), ++n;
    return n;
}

int countAround(Context& ctx, core::GlobalPos const& p, char const* id, float r) {
    auto* region = ctx.region();
    if (!region) return 0;
    AABB box{Vec3{p.x - r, p.y - r, p.z - r}, Vec3{p.x + r, p.y + r, p.z + r}};
    int  n = 0;
    for (auto* a : actorsIn(*region, p.dim, box))
        if (a && a->isAlive() && a->getTypeName() == id) ++n;
    return n;
}

/// The difficulty of the level while a test runs (bosses and monsters leave a peaceful world).
struct DifficultyScope {
    SharedTypes::Legacy::Difficulty previous = SharedTypes::Legacy::Difficulty::Peaceful;
    DifficultyScope() {
        if (auto level = ll::service::getLevel()) {
            previous = level->getDifficulty();
            level->setDifficulty(SharedTypes::Legacy::Difficulty::Easy);
        }
    }
    ~DifficultyScope() {
        if (auto level = ll::service::getLevel()) level->setDifficulty(previous);
    }
    DifficultyScope(DifficultyScope const&)            = delete;
    DifficultyScope& operator=(DifficultyScope const&) = delete;
};

float attr(Actor& a, Attribute const& h) {
    auto i = a.getAttribute(h);
    return i.mPtr ? i.mPtr->mCurrentValue : -1.0f;
}

} // namespace

// Lich#getPhase: shield up -> 1; no shield, minions left -> 2; neither -> 3.
GF_REGISTER_SELFTEST("lich.phases", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy; // bosses and monsters leave a peaceful world
    co_await ctx.flatArea(2100, 16);
    auto c    = ctx.testPos(2108, 2);
    Mob* lich = spawnAt(ctx, kLich, c, 1.0f);
    ctx.require(lich != nullptr, "a lich spawned");
    auto uid = uidOf(*lich);
    co_await ctx.ticks(2);
    lich = mobById(uid);
    ctx.require(lich != nullptr, "the lich is still there");
    lichState(*lich)->attackCooldown = 1000; // no shadow attacks or teleports during the test
    ctx.check(shieldOf(*lich) == 6, "a fresh lich has 6 shields");
    ctx.check(phaseOf(*lich) == 1, "shield up: phase 1");
    setShield(*lich, 0);
    ctx.check(phaseOf(*lich) == 2, "no shield, 9 minions to summon: phase 2");
    setMinionsLeft(*lich, 0);
    ctx.check(phaseOf(*lich) == 3, "no shield, no minions: phase 3");
    removeById(uid);
});

// Lich#hurt: a lich bolt (breaks_lich_shields, 6 > 2) takes one shield; a melee hit only blocks.
GF_REGISTER_SELFTEST("lich.shield_break", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy; // bosses and monsters leave a peaceful world
    co_await ctx.flatArea(2120, 16);
    auto c    = ctx.testPos(2128, 2);
    Mob* lich = spawnAt(ctx, kLich, c, 1.0f);
    ctx.require(lich != nullptr, "a lich spawned");
    auto uid = uidOf(*lich);
    co_await ctx.ticks(2);
    lich = mobById(uid);
    ctx.require(lich != nullptr, "the lich is still there");
    lichState(*lich)->attackCooldown = 1000; // no shadow attacks or teleports during the test
    float hp = health(*lich);
    gf::combat::hurt(*lich, 6.0f, "twilightforest:lich_bolt", &ctx.player());
    ctx.check(shieldOf(*lich) == 5, "a lich bolt broke one shield (" + std::to_string(shieldOf(*lich)) + " left)");
    ctx.check(health(*lich) == hp, "no health damage while the shield is up");
    gf::combat::hurt(*lich, 4.0f, "minecraft:player_attack", &ctx.player());
    ctx.check(shieldOf(*lich) == 5, "a melee hit only plays the block sound");
    ctx.check(health(*lich) == hp, "and does no damage");
    gf::combat::hurt(*lich, 2.0f, "minecraft:magic", &ctx.player());
    ctx.check(shieldOf(*lich) == 5, "magic of 2 (not > 2) does not break a shield");
    removeById(uid);
});

// LichShadowsGoal#checkAndSpawnClones: never more than clone_count (2) clones.
GF_REGISTER_SELFTEST("lich.clone_cap", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy; // bosses and monsters leave a peaceful world
    co_await ctx.flatArea(2140, 64);
    auto c    = ctx.testPos(2172, 2);
    Mob* lich = spawnAt(ctx, kLich, c, 1.0f);
    ctx.require(lich != nullptr, "a lich spawned");
    auto uid = uidOf(*lich);
    co_await ctx.ticks(2);
    lich = mobById(uid);
    ctx.require(lich != nullptr, "the lich is still there");
    lichState(*lich)->attackCooldown = 1000; // no shadow attacks or teleports during the test
    Player& p = ctx.player();
    for (int i = 0; i < 6; ++i) checkAndSpawnClones(*lich, p);
    int clones = countMyClones(*lich);
    ctx.check(clones <= 2, "at most 2 clones (" + std::to_string(clones) + ")");
    ctx.check(clones >= 1, "the clones found spots in sight of the target");
    for (auto* clone : clonesOf(*lich)) {
        ctx.check(isShadowClone(*clone), "a clone knows its master");
        ctx.check(phaseOf(*clone) == 1 && shieldOf(*clone) == 0, "a clone is phase 1 without a shield of its own");
        float hp = health(*clone);
        gf::combat::hurt(*clone, 6.0f, "twilightforest:lich_bolt", &p);
        ctx.check(health(*clone) == hp, "a clone ignores damage");
    }
    despawnClones(*lich);
    co_await ctx.ticks(1);
    lich = mobById(uid);
    ctx.check(lich && countMyClones(*lich) == 0, "despawnClones removed them");
    removeAround(ctx, c, kLich, 48.0f);
});

// LichMinionsGoal#checkAndSpawnMinions: at most 3 alive, 9 summons in all.
GF_REGISTER_SELFTEST("lich.minions", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy; // bosses and monsters leave a peaceful world
    co_await ctx.flatArea(2140, 64);
    auto c    = ctx.testPos(2172, 2);
    Mob* lich = spawnAt(ctx, kLich, c, 1.0f);
    ctx.require(lich != nullptr, "a lich spawned");
    auto uid = uidOf(*lich);
    co_await ctx.ticks(2);
    lich = mobById(uid);
    ctx.require(lich != nullptr, "the lich is still there");
    lichState(*lich)->attackCooldown = 1000; // no shadow attacks or teleports during the test
    setShield(*lich, 0);
    // the minions appear around the target: a player near the lich (countMyMinions looks within 32)
    auto& sim = ctx.spawnPlayer("lich_minions");
    auto  pid = uidOf(sim);
    sim.teleport(at(c.offset(-8, 1, 0)), static_cast<DimensionType>(c.dim));
    co_await ctx.ticks(2);
    lich = mobById(uid);
    ctx.require(lich != nullptr && actorById(pid) != nullptr, "the lich and the player are there");
    setTarget(*lich, actorById(pid));
    // checkAndSpawnMinions uses a summon up even when findVecInLOSOf finds no spot (as TF does), so
    // the test counts outcomes: never more than 3 alive, never fewer summons used than minions alive,
    // the summons only go down, and they run out after at most 9 calls that could summon
    int before = lichState(*lich)->minionsLeft;
    for (int i = 0; i < 6; ++i) checkAndSpawnMinions(*lich);
    co_await ctx.ticks(1);
    lich = mobById(uid);
    ctx.require(lich != nullptr, "the lich is still there");
    lichState(*lich)->attackCooldown = 1000; // no shadow attacks or teleports during the test
    int alive = countMyMinions(*lich);
    int left  = lichState(*lich)->minionsLeft;
    ctx.check(before == 9, "a fresh lich has 9 summons (" + std::to_string(before) + ")");
    ctx.check(alive <= 3, "at most 3 minions at once (" + std::to_string(alive) + ")");
    ctx.info("minions: " + std::to_string(alive) + " alive after " + std::to_string(9 - left) + " summons");
    ctx.check(!wantsNewMinion(*lich) || alive < 3, "a lich with 3 minions wants no more");
    ctx.check(9 - left >= alive, "every minion alive used a summon (" + std::to_string(9 - left) + " used, " + std::to_string(alive) + " alive)");
    ctx.check(left < before && left >= 0, "the summons went down (" + std::to_string(left) + " left)");
    // a lich with 3 minions alive uses no summon
    if (alive >= 3) {
        checkAndSpawnMinions(*lich);
        ctx.check(lichState(*lich)->minionsLeft == left, "no summon used while 3 minions live");
    }
    // the summons are used up while the minions die
    int last = left;
    for (int round = 0; round < 12 && last > 0; ++round) {
        removeAround(ctx, c, kMinion, 48.0f);
        co_await ctx.ticks(1);
        lich = mobById(uid);
        if (!lich) break;
        lichState(*lich)->attackCooldown = 1000;
        for (int i = 0; i < 3; ++i) checkAndSpawnMinions(*lich);
        int now = lichState(*lich)->minionsLeft;
        ctx.check(now <= last, "the summons never come back");
        last = now;
    }
    lich = mobById(uid);
    ctx.require(lich != nullptr, "the lich is still there");
    lichState(*lich)->attackCooldown = 1000; // no shadow attacks or teleports during the test
    ctx.check(lichState(*lich)->minionsLeft == 0, "9 summons in all (" + std::to_string(lichState(*lich)->minionsLeft) + " left)");
    removeAround(ctx, c, kMinion, 48.0f);
    co_await ctx.ticks(1);
    lich = mobById(uid);
    ctx.check(lich && phaseOf(*lich) == 3, "no summons and no minions: phase 3");
    removeById(uid);
});

// LichBolt#hurt: a hit re-shoots the bolt along the attacker's look with the attacker as owner; a
// player-owned bolt breaks a shield of a phase-1 lich.
GF_REGISTER_SELFTEST("lich.bolt_reflect", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy; // bosses and monsters leave a peaceful world
    co_await ctx.flatArea(2100, 16);
    auto c    = ctx.testPos(2108, 2);
    Mob* lich = spawnAt(ctx, kLich, c, 1.0f);
    ctx.require(lich != nullptr, "a lich spawned");
    auto uid = uidOf(*lich);
    co_await ctx.ticks(2);
    auto& sim = ctx.spawnPlayer("lich_bolt");
    auto  pid = uidOf(sim);
    sim.teleport(at(c.offset(-6, 1, 0)), static_cast<DimensionType>(c.dim));
    co_await ctx.ticks(2);
    auto* player = static_cast<Player*>(actorById(pid));
    lich         = mobById(uid);
    ctx.require(player && lich, "the player and the lich are there");
    lichState(*lich)->attackCooldown = 1000; // no shadow attacks or teleports during the test
    Vec3   eye = player->getEyePos();
    Vec3   le  = eyeOf(*lich);
    Actor* bolt = gf::projectile::shoot(*lich, kLichBolt, Vec3{eye.x - le.x, eye.y - le.y, eye.z - le.z}, 0.5f, 0.0f);
    ctx.require(bolt != nullptr, "the lich shot a bolt");
    ctx.check(projectileOwner(*bolt) == lich, "the shooter owns it");
    static_cast<SimulatedPlayer*>(player)->simulateLookAt(le, sim::LookDuration::Instant);
    reflectBolt(*bolt, *player, player);
    auto v = gf::projectile::ext::velocity(*bolt);
    double speed = std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z);
    ctx.check(std::abs(speed - 1.5) < 0.05, "reflected at 1.5 (" + std::to_string(speed) + ")");
    ctx.check(projectileOwner(*bolt) == player, "the player owns the reflected bolt");
    if (auto* b = bolt) b->remove();
    // a player's bolt hits a phase-1 lich and takes a shield
    Actor* mine = gf::projectile::shoot(*player, kLichBolt, Vec3{le.x - eye.x, le.y - 0.6f - eye.y, le.z - eye.z}, 1.5f, 0.0f);
    ctx.require(mine != nullptr, "the player shot a bolt");
    bool broke = false;
    for (int i = 0; i < 40 && !broke; ++i) {
        co_await ctx.ticks(1);
        lich  = mobById(uid);
        broke = lich && shieldOf(*lich) == 5;
    }
    ctx.check(broke, "the player's bolt broke a shield");
    removeById(uid);
    removeAround(ctx, c, kLichBolt, 16.0f);
    removeAround(ctx, c, kLich, 48.0f);
});

// LichBolt#hurt / LichBomb#hurt through a real attack (GameMode attack on the projectile actor):
// the bolt is re-shot along the player's look with the player as owner, the bomb explodes, and the
// explosion leaves a dropped item next to it alone (EntityEvents#lichBombsDontBlowUpItems).
GF_REGISTER_SELFTEST("lich.projectile_hits", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy; // bosses and monsters leave a peaceful world
    co_await ctx.flatArea(2120, 16);
    auto c    = ctx.testPos(2128, 2);
    Mob* lich = spawnAt(ctx, kLich, c.offset(6, 1, 0));
    ctx.require(lich != nullptr, "a lich spawned");
    auto uid = uidOf(*lich);
    lichState(*lich)->attackCooldown = 100000;
    auto& sim = ctx.spawnPlayer("lich_hits");
    auto  pid = uidOf(sim);
    sim.teleport(at(c.offset(-2, 1, 0)), static_cast<DimensionType>(c.dim));
    co_await ctx.ticks(2);
    auto* player = static_cast<SimulatedPlayer*>(actorById(pid));
    lich         = mobById(uid);
    ctx.require(player && lich, "the player and the lich are there");
    lichState(*lich)->attackCooldown = 100000;
    // a bolt hanging in front of the player
    Vec3   front{c.x + 0.5f, c.y + 2.5f, c.z + 0.5f};
    Actor* bolt = gf::projectile::shoot(*lich, kLichBolt, Vec3{-1.0f, 0.0f, 0.0f}, 0.01f, 0.0f);
    ctx.require(bolt != nullptr, "the lich shot a bolt");
    auto boltId = uidOf(*bolt);
    teleportTo(*bolt, front);
    gf::projectile::ext::setVelocity(*bolt, Vec3{0.0f, 0.0f, 0.0f});
    player->simulateLookAt(*bolt, sim::LookDuration::Instant);
    co_await ctx.ticks(1);
    player = static_cast<SimulatedPlayer*>(actorById(pid));
    bolt   = actorById(boltId);
    ctx.require(player && bolt, "the bolt waits in front of the player");
    player->simulateLookAt(*bolt, sim::LookDuration::Instant);
    player->simulateAttack(bolt);
    // the attack runs at once: the bolt is re-shot now (a tick later it may already have hit)
    bolt = actorById(boltId);
    ctx.require(bolt != nullptr, "the bolt is still there after the hit");
    auto   v     = gf::projectile::ext::velocity(*bolt);
    double speed = std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z);
    ctx.check(speed > 1.2 && speed < 1.6, "the attacked bolt flies off at about 1.5 (" + std::to_string(speed) + ")");
    ctx.check(projectileOwner(*bolt) == actorById(pid), "the attacking player owns it");
    bolt->remove();
    // a bomb with a dropped item next to it
    lich = mobById(uid);
    ctx.require(lich != nullptr, "the lich is still there");
    Actor* bomb = gf::projectile::shoot(*lich, kLichBomb, Vec3{-1.0f, 0.0f, 0.0f}, 0.01f, 0.0f);
    ctx.require(bomb != nullptr, "the lich threw a bomb");
    auto bombId = uidOf(*bomb);
    teleportTo(*bomb, front);
    gf::projectile::ext::setVelocity(*bomb, Vec3{0.0f, 0.0f, 0.0f});
    auto level = ll::service::getLevel();
    auto bone  = game::items::make("minecraft:bone", 1);
    ctx.require(level && bone && ctx.region(), "an item to drop");
    auto* dropped = level->getSpawner().spawnItem(*ctx.region(), *bone, nullptr, Vec3{front.x, float(c.y + 1), front.z + 1.0f}, 0);
    ctx.require(dropped != nullptr, "the bone was dropped");
    auto boneId = uidOf(*dropped);
    co_await ctx.ticks(2);
    int items = countAround(ctx, c, "minecraft:item", 8.0f);
    ctx.require(items >= 1, "the bone lies next to the bomb");
    player = static_cast<SimulatedPlayer*>(actorById(pid));
    bomb   = actorById(bombId);
    ctx.require(player && bomb, "the bomb waits in front of the player");
    // a few swings (the player's previous swing may still be on its cooldown)
    for (int i = 0; i < 10 && actorById(bombId); ++i) {
        player = static_cast<SimulatedPlayer*>(actorById(pid));
        bomb   = actorById(bombId);
        if (!player || !bomb) break;
        gf::projectile::ext::setVelocity(*bomb, Vec3{0.0f, 0.0f, 0.0f});
        player->simulateLookAt(*bomb, sim::LookDuration::Instant);
        player->simulateAttack(bomb);
        co_await ctx.ticks(2);
    }
    ctx.check(actorById(bombId) == nullptr, "the attacked bomb exploded");
    ctx.check(countAround(ctx, c, "minecraft:item", 8.0f) >= items, "the explosion left the dropped item alone (" + std::to_string(countAround(ctx, c, "minecraft:item", 8.0f)) + " of " + std::to_string(items) + " items near" + (actorById(boneId) ? "" : ", the bone was put back") + ")");
    removeAround(ctx, c, "minecraft:item", 6.0f);
    removeAround(ctx, c, kLichBolt, 16.0f);
    removeById(uid);
});

// DeathTome on a lectern: a player looking at it within 3 blocks wakes it; it leaves the lectern.
GF_REGISTER_SELFTEST("lich.death_tome_lectern", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy; // bosses and monsters leave a peaceful world
    co_await ctx.flatArea(2120, 16);
    auto lect = ctx.testPos(2128, 2).offset(0, 1, 0);
    ctx.require(ctx.setBlock(lect, "minecraft:lectern"), "the lectern is placed");
    co_await ctx.ticks(1);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "the test region");
    BlockPos seat{lect.x, lect.y, lect.z};
    Mob*     tome = spawnLecternTome(*region, seat);
    ctx.require(tome != nullptr, "a death tome was put on the lectern");
    auto uid = uidOf(*tome);
    // with nobody near it stays on its lectern, in the lectern's block (no wake-up without a target)
    for (int i = 0; i < 40; ++i) {
        co_await ctx.ticks(1);
        tome = mobById(uid);
        if (!tome || !tomeOnLectern(*tome)) {
            std::string why = !tome ? std::string("gone")
                                    : "at " + std::to_string(tome->getFeetPos().x) + " " + std::to_string(tome->getFeetPos().y) + " " + std::to_string(tome->getFeetPos().z)
                                          + ", block " + ctx.blockAt(lect);
            ctx.fail("the tome rests on its lectern for 2 s without a player (" + why + ")");
        }
        auto f = tome->getFeetPos();
        if (std::abs(f.x - (seat.x + 0.5f)) > 0.1f || std::abs(f.z - (seat.z + 0.5f)) > 0.1f || std::abs(f.y - (float(seat.y) + kLecternTop)) > 0.1f)
            ctx.fail("the tome stays in its lectern (at " + std::to_string(f.x) + " " + std::to_string(f.y) + " " + std::to_string(f.z) + ")");
    }
    auto& sim = ctx.spawnPlayer("lich_tome");
    auto  pid = uidOf(sim);
    sim.teleport(at(lect.offset(-2, 0, 0)), static_cast<DimensionType>(lect.dim));
    co_await ctx.ticks(2);
    auto* player = static_cast<SimulatedPlayer*>(actorById(pid));
    tome         = mobById(uid);
    ctx.require(player && tome, "the player and the tome are there");
    player->simulateLookAt(eyeOf(*tome), sim::LookDuration::Instant);
    bool woke = false;
    for (int i = 0; i < 40 && !woke; ++i) {
        co_await ctx.ticks(1);
        if (auto* p = static_cast<SimulatedPlayer*>(actorById(pid)))
            if (auto* t = mobById(uid)) p->simulateLookAt(eyeOf(*t), sim::LookDuration::Instant);
        tome = mobById(uid);
        woke = tome && !tomeOnLectern(*tome);
    }
    ctx.check(woke, "the tome left its lectern when looked at");
    // it woke through its lectern target selector (the player), not because the lectern went away
    ctx.check(ctx.blockAt(lect) == "minecraft:lectern", "the lectern is still there");
    if (tome = mobById(uid); tome) ctx.check(targetOf(*tome) == actorById(pid), "the tome targets the player who looked at it");
    removeById(uid);
    removeAround(ctx, lect, kTomeBolt, 16.0f);
    ctx.cleanupBlock(lect);
});

// RisingZombie: rises for 130 ticks, then becomes a vanilla zombie.
GF_REGISTER_SELFTEST("lich.rising_zombie_converts", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy; // bosses and monsters leave a peaceful world
    co_await ctx.flatArea(2100, 16);
    auto c = ctx.testPos(2104, 2).offset(0, 1, 0);
    Mob* z = spawnAt(ctx, kRising, c);
    ctx.require(z != nullptr, "a rising zombie spawned");
    auto uid = uidOf(*z);
    co_await ctx.ticks(2);
    z = mobById(uid);
    ctx.require(z != nullptr, "it is there");
    ctx.check(risingTicks(*z) == 0 && z->isInvisible(), "buried and invisible until seen");
    setRisingTicks(*z, 1);
    co_await ctx.ticks(60);
    z = mobById(uid);
    ctx.check(z && risingTicks(*z) > 50 && risingTicks(*z) < 130, "rising");
    co_await ctx.ticks(80);
    ctx.check(mobById(uid) == nullptr, "the rising zombie is gone after 130 ticks");
    ctx.check(countAround(ctx, c, "minecraft:zombie", 3.0f) >= 1, "a zombie stands in its place");
    removeAround(ctx, c, "minecraft:zombie", 4.0f);
});

// Attributes against TF (tests/golden/mobs.json).
GF_REGISTER_SELFTEST("lich.attributes", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy; // bosses and monsters leave a peaceful world
    co_await ctx.flatArea(2100, 16);
    struct Row {
        char const* id;
        float       health, speed, attack;
    };
    Row const rows[] = {
        {kLich,   100.0f, 0.45f, 3.0f},
        {kMinion, 20.0f,  0.23f, 3.0f},
        {kTome,   20.0f,  0.25f, 4.0f},
    };
    int i = 0;
    for (auto const& r : rows) {
        auto p = ctx.testPos(2102 + 4 * i++, 2).offset(0, 1, 0);
        Mob* m = spawnAt(ctx, r.id, p);
        ctx.require(m != nullptr, std::string("spawned ") + r.id);
        ctx.check(m->getMaxHealth() == static_cast<int>(r.health), std::string(r.id) + " max health");
        ctx.check(std::abs(attr(*m, SharedAttributes::MOVEMENT_SPEED()) - r.speed) < 1e-3f, std::string(r.id) + " movement speed");
        ctx.check(std::abs(attr(*m, SharedAttributes::ATTACK_DAMAGE()) - r.attack) < 1e-3f, std::string(r.id) + " attack damage");
        m->remove();
    }
    co_await ctx.ticks(1);
});

// The Lich's loot table rolls a scepter, gold gear, pearls, bones, the trophy and essence.
GF_REGISTER_SELFTEST("lich.loot", [](Context& ctx) {
    gf::loot::LootContext lc;
    lc.region = ctx.region();
    lc.origin = at(ctx.testPos(2100, 2));
    lc.seed   = 21;
    auto items = gf::loot::roll("twilightforest:entities/lich", lc);
    bool trophy = false, scepter = false, essence = false;
    for (auto const& s : items) {
        auto id = game::items::idOf(s);
        trophy |= id == "twilightforest:lich_trophy";
        essence |= id == "twilightforest:exanimate_essence";
        scepter |= id == "twilightforest:twilight_scepter" || id == "twilightforest:lifedrain_scepter" || id == "twilightforest:zombie_scepter"
                || id == "twilightforest:fortification_scepter";
    }
    ctx.check(trophy, "the trophy drops");
    ctx.check(scepter, "one scepter drops");
    ctx.check(essence, "exanimate essence drops");
    ctx.check(gf::loot::roll("twilightforest:entities/lich_minion", lc).empty(), "a minion drops nothing");
    ctx.check(!gf::loot::roll("twilightforest:entities/death_tome_books", lc).empty(), "the tome's book table rolls a book");
});

// LichSpawnerBlockEntity: a player counts only above the spawner's y - 4.
GF_REGISTER_SELFTEST("lich.spawner_range", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy; // bosses and monsters leave a peaceful world
    co_await ctx.flatArea(2140, 32);
    auto spawner = ctx.testPos(2156, 2).offset(0, 8, 0);
    ctx.require(ctx.setBlock(spawner, kSpawner), "the lich spawner is placed");
    auto& sim = ctx.spawnPlayer("lich_spawner");
    auto  pid = uidOf(sim);
    // 6 blocks below the spawner (y - 6 < y - 4): out of range
    sim.teleport(at(spawner.offset(2, -6, 0)), static_cast<DimensionType>(spawner.dim));
    co_await ctx.ticks(40);
    ctx.check(countAround(ctx, spawner, kLich, 12.0f) == 0, "a player 6 below does not wake the lich");
    if (auto* p = actorById(pid)) p->teleport(at(spawner.offset(2, -3, 0)), static_cast<DimensionType>(spawner.dim));
    bool spawned = false;
    for (int i = 0; i < 60 && !spawned; ++i) {
        co_await ctx.ticks(1);
        spawned = countAround(ctx, spawner, kLich, 14.0f) > 0;
    }
    ctx.check(spawned, "a player 3 below wakes it");
    removeAround(ctx, spawner, kLich, 16.0f);
    ctx.cleanupBlock(spawner);
});

// The golden lich tower (rule R19): the layout of a fixed seed on flat ground (piece types and
// boxes relative to the origin; independent of the world) and the blocks of its lower 32 layers
// placed into a flat test area. The goldens hold while U22's room templates are absent (the
// placeholder rooms); with U22 present the hashes are logged for U22 / A8 to commit.
constexpr std::int64_t  kGoldenSeed   = 21;
constexpr std::uint64_t kGoldenLayout = 9018693297151821187ull; // U22 absent
// the placed layout depends on where it stands (its chunk seeds the layout): the golden holds for
// the test area of the e2e worlds (seed 20260521, the BDS spawn); elsewhere it is logged only
constexpr char const* kGoldenVolume   = "42aa3923e38e53c8"; // U22 absent
constexpr int         kGoldenVolumeAt[2] = {64, 100};

std::uint64_t layoutHash(std::vector<tower::PieceBox> const& pieces, BlockPos const& origin, std::map<std::string, int>& counts) {
    std::uint64_t hash = 1469598103934665603ull;
    auto          mix  = [&](std::uint64_t v) {
        hash ^= v;
        hash *= 1099511628211ull;
    };
    for (auto const& p : pieces) {
        counts[p.typeId]++;
        for (char c : p.typeId) mix(static_cast<unsigned char>(c));
        for (int v : {p.box.minX - origin.x, p.box.minY - origin.y, p.box.minZ - origin.z, p.box.maxX - origin.x, p.box.maxY - origin.y, p.box.maxZ - origin.z})
            mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(v)));
    }
    return hash;
}

void checkLayoutCounts(Context& ctx, std::map<std::string, int>& counts) {
    ctx.check(counts["twilightforest:TFLTTFoy"] == 1, "one foyer");
    ctx.check(counts["twilightforest:TFLTCTBase"] == 1, "one tower base");
    ctx.check(counts["twilightforest:TFLTCTTrim"] == 1, "the base trim");
    int slices = counts["twilightforest:TFLTCTSeg"] - 1; // the boss boundary is a segment too
    ctx.check(slices >= 12 && slices <= 15, "12-15 tower slices (" + std::to_string(slices) + ")");
    ctx.check(counts["twilightforest:TFLTTBoss"] == 1 && counts["twilightforest:TFLTTBossRoof"] == 1, "the boss room and its roof");
    ctx.check(counts["twilightforest:TFLTBridge"] >= 1, "wing bridges or covers");
    ctx.check(counts["twilightforest:TFLTTRoom"] >= 1, "wing rooms (placeholders while U22's rooms are missing)");
    // the yard: the dirt, the path from the vestibule, the cross path and two side paths, the lights
    ctx.check(counts["twilightforest:TFLTPath"] == 5, "the yard's dirt and four paths (" + std::to_string(counts["twilightforest:TFLTPath"]) + ")");
    ctx.check(counts["twilightforest:TFLTLight"] == 1, "the lights along the first path");
    ctx.check(counts["twilightforest:TFLTFence"] >= 1, "the perimeter fence (" + std::to_string(counts["twilightforest:TFLTFence"]) + " pieces)");
}

// The lich tower assembles: foyer, base, 12-15 slices, the boss room, its boundary and roof, wings
// (U22's rooms or the placeholder rooms) and the yard; its layout hash and the volume hash of its
// lower layers match the goldens.
GF_REGISTER_SELFTEST("lich.tower_layout", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy; // bosses and monsters leave a peaceful world
    bool     u22    = gf::structure::templates::get("twilightforest:lich_tower/5x5/altar") != nullptr;
    BlockPos origin{0, 64, 0};
    auto     pieces = tower::layoutForTest(origin, kGoldenSeed);
    std::map<std::string, int> counts;
    auto     hash = layoutHash(pieces, origin, counts);
    checkLayoutCounts(ctx, counts);
    std::string summary;
    for (auto const& [type, n] : counts) summary += " " + type.substr(15) + "=" + std::to_string(n);
    ctx.info("lich tower: " + std::to_string(pieces.size()) + " pieces," + summary + "; layout hash " + std::to_string(hash) + (u22 ? " (U22 rooms present)" : " (placeholder rooms)"));
    if (!u22 && kGoldenLayout != 0) ctx.check(hash == kGoldenLayout, "the layout matches the golden (" + std::to_string(hash) + " vs " + std::to_string(kGoldenLayout) + ")");
    // the same layout twice: deterministic
    std::map<std::string, int> again;
    ctx.check(layoutHash(tower::layoutForTest(origin, kGoldenSeed), origin, again) == hash, "the layout is deterministic");
    // its lower 32 layers in a flat area: the golden volume hash
    auto     area = co_await ctx.flatArea(2100, 120);
    BlockPos at{(area.minX + area.maxX) / 2, area.minY - 1, (area.minZ + area.maxZ) / 2};
    auto     placed = tower::layoutForTest(at, kGoldenSeed);
    core::BlockBox clip{area.minX, area.minY - 1, area.minZ, area.maxX, area.maxY, area.maxZ};
    auto*    source = ctx.region();
    ctx.require(source != nullptr, "the test region");
    tower::placeForTest([source](BlockPos const& c) -> std::unique_ptr<gf::worldgen::Region> { return std::make_unique<gf::structure::BlockSourceRegion>(*source, c); },
                        clip, kGoldenSeed);
    tower::forgetTestPieces();
    auto volume = ctx.snapshotVolume(clip);
    ctx.info("lich tower: volume hash " + volume + " of " + std::to_string(placed.size()) + " pieces at " + std::to_string(at.x) + " " + std::to_string(at.z)
             + (u22 ? " (U22 rooms present)" : " (placeholder rooms)"));
    bool goldenSpot = at.x == kGoldenVolumeAt[0] && at.z == kGoldenVolumeAt[1];
    if (!u22 && goldenSpot && std::string(kGoldenVolume).size() > 0) ctx.check(volume == kGoldenVolume, "the placed tower matches the golden volume (" + volume + ")");
    removeAround(ctx, core::GlobalPos{ctx.dimension(), at.x, at.y, at.z}, "minecraft:painting", 90.0f);
});

// The yard's controlled spawns (LichTowerPieces.YARD_SPAWNS): a rising zombie list on the paths,
// the interior list in the tower; TF's rising zombies only come from here.
GF_REGISTER_SELFTEST("lich.yard_spawns", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto center = ctx.testPos(2150, 2).offset(0, 0, 400);
    gf::landmark::ext::ScopedStart forced(kTowerKey, center);
    ctx.require(forced && forced->generated, "forced a lich tower start");
    auto pieces = gf::landmark::ext::startPieces(*forced);
    auto listAt = [&](core::BlockBox const& b) {
        int  x = (b.minX + b.maxX) / 2, y = b.minY + 1, z = (b.minZ + b.maxZ) / 2;
        auto l = gf::landmark::ext::potentialSpawns(center.dim, "monster", x, y, z);
        std::vector<std::string> ids;
        if (l)
            for (auto const& e : *l) ids.push_back(e.value("type", std::string()));
        return ids;
    };
    // a spot of the yard that no tower piece covers (the highest index wins where they overlap)
    auto covered = [&](int x, int y, int z) {
        for (auto const& q : pieces)
            if (q.type != "twilightforest:TFLTPath" && q.type != "twilightforest:TFLTGrave" && q.type != "twilightforest:TFLTLight" && q.box.minX <= x && x <= q.box.maxX
                && q.box.minY <= y && y <= q.box.maxY && q.box.minZ <= z && z <= q.box.maxZ)
                return true;
        return false;
    };
    bool path = false, interior = false;
    for (auto const& p : pieces) {
        if (p.type != "twilightforest:TFLTPath" || path) continue;
        for (int x = p.box.minX; x <= p.box.maxX && !path; x += 3)
            for (int z = p.box.minZ; z <= p.box.maxZ && !path; z += 3) {
                int y = p.box.minY + 1;
                if (covered(x, y, z)) continue;
                auto l = gf::landmark::ext::potentialSpawns(center.dim, "monster", x, y, z);
                if (l)
                    for (auto const& e : *l) path |= e.value("type", std::string()) == "twilightforest:rising_zombie";
            }
    }
    ctx.check(path, "a rising zombie can spawn on the yard");
    for (auto const& p : pieces) {
        if (p.type == "twilightforest:TFLTCTBase" && !interior) {
            auto ids = listAt(p.box);
            interior = std::find(ids.begin(), ids.end(), "twilightforest:death_tome") != ids.end();
            ctx.check(interior, "the tower base spawns the interior list (death tomes)");
        }
    }
    co_return;
});

// For the client e2e's screenshots, in stages the scenario waits for ("lich.client_showcase: stage
// <name>"), each held for a few seconds in front of the real player (creative, so the hostile ones
// leave it alone):
//   mobs        the Lich (phase 1, shield up), a shadow clone, a Lich Minion, a Death Tome resting on
//               a lectern and rising zombies at 20, 60 and 100 ticks of their rise, all facing the
//               player;
//   projectiles a lich bolt, a lich bomb and a tome bolt hanging in the air;
//   phase3      a Lich without shield and summons (the angry_lich particles, the phase-3 arms);
//   death60 / death140  a Lich at deathTime 60 (shaking) and 140 (the crown sinking);
//   tower_far / tower_yard  a lich tower generated ahead, seen from afar and from its yard.
namespace {

/// The yaw (Bedrock: 0 south, 90 west) from `from` toward `to`.
float yawToward(Vec3 const& from, Vec3 const& to) { return static_cast<float>(std::atan2(-(to.x - from.x), to.z - from.z) * 180.0 / 3.14159265358979); }

void face(Mob& m, float yaw) {
    m.setYBodyRotation(yaw);
    m.setYHeadRot(yaw);
    const_cast<Vec2&>(m.getRotation()) = Vec2{0.0f, yaw};
}

/// Teleports the real player to `at` looking at `target`.
void lookFrom(Player& p, Vec3 const& at, Vec3 const& target) {
    double dx = target.x - at.x, dy = target.y - (at.y + 1.62), dz = target.z - at.z;
    double yaw   = std::atan2(-dx, dz) * 180.0 / 3.14159265358979;
    double pitch = -std::atan2(dy, std::sqrt(dx * dx + dz * dz)) * 180.0 / 3.14159265358979;
    char   cmd[256];
    std::snprintf(cmd, sizeof(cmd), "tp \"%s\" %.2f %.2f %.2f %.1f %.1f", p.getRealName().c_str(), at.x, at.y, at.z, yaw, pitch);
    (void)gf::selftest::runServerCommand(cmd, dimOf(p));
}

Player* realPlayer() {
    Player* real = nullptr;
    if (auto level = ll::service::getLevel())
        level->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
            if (!p.isSimulatedPlayer()) real = &p;
            return real == nullptr;
        }));
    return real;
}

} // namespace

GF_REGISTER_SELFTEST("lich.client_showcase", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy; // bosses and monsters leave a peaceful world
    Player*         real = realPlayer();
    if (!real) {
        ctx.info("no real player: nothing to show");
        co_return;
    }
    std::string name = real->getRealName();
    int         dim  = dimOf(*real);
    auto        f    = real->getFeetPos();
    auto        v    = viewVector(*real);
    double      len  = std::sqrt(double(v.x) * v.x + double(v.z) * v.z);
    double      fx = len > 1e-6 ? v.x / len : 0.0, fz = len > 1e-6 ? v.z / len : 1.0;
    auto        ahead = [&](double d, double side, float dy = 0.0f) { return Vec3{float(f.x + fx * d - fz * side), f.y + dy, float(f.z + fz * d + fx * side)}; };
    auto*       region = &real->getDimensionBlockSource();
    // the minion and the tome are hostile: the player watches in creative mode, at noon, still
    (void)gf::selftest::runServerCommand("gamemode creative \"" + name + "\"", dim);
    (void)gf::selftest::runServerCommand("time set 6000", dim);
    Vec3 eye{f.x, f.y + 1.62f, f.z};
    std::vector<std::int64_t> shown;
    std::map<std::int64_t, int> frozenRise;
    auto keepStill = [&]() {
        for (auto id : shown)
            if (auto* m = mobById(id)) {
                stopNavigation(*m);
                face(*m, yawToward(m->getFeetPos(), eye));
                if (auto it = frozenRise.find(id); it != frozenRise.end()) setRisingTicks(*m, it->second);
            }
    };
    auto hold = [&](int ticks) -> ll::coro::CoroTask<> {
        for (int i = 0; i < ticks; ++i) {
            co_await ctx.ticks(1);
            keepStill();
        }
    };
    auto clear = [&]() {
        for (auto id : shown) removeById(id);
        shown.clear();
        frozenRise.clear();
    };

    // ---- the mobs ----
    if (Mob* lich = spawnMob(*region, kLich, ahead(7, 0))) {
        lichState(*lich)->attackCooldown = 100000;
        shown.push_back(uidOf(*lich));
        if (Mob* clone = spawnClone(*lich, ahead(7, -3))) {
            lichState(*clone)->attackCooldown = 100000;
            shown.push_back(uidOf(*clone));
        }
    }
    if (Mob* minion = spawnMob(*region, kMinion, ahead(7, 3))) shown.push_back(uidOf(*minion));
    // the lectern facing the player, the tome on it
    BlockPos lectern = game::blockPosOf(ahead(4, -2));
    float    toPlayer = yawToward(Vec3{lectern.x + 0.5f, float(lectern.y), lectern.z + 0.5f}, eye);
    int      dir      = ((static_cast<int>(std::lround(toPlayer / 90.0f)) % 4) + 4) % 4; // 0 south, 1 west, 2 north, 3 east
    if (auto const* lb = game::blockWithStates("minecraft:lectern", Json{{"direction", dir}})) game::setBlock(*region, lectern, *lb);
    if (Mob* tome = spawnLecternTome(*region, lectern)) shown.push_back(uidOf(*tome));
    int k = 0;
    for (int ticks : {20, 60, 100}) {
        if (Mob* z = spawnMob(*region, kRising, ahead(4, 0.5 + 1.5 * k++))) {
            setRisingTicks(*z, ticks);
            shown.push_back(uidOf(*z));
            frozenRise[uidOf(*z)] = ticks;
        }
    }
    co_await hold(60);
    log().info("lich.client_showcase: stage mobs ({} shown)", shown.size());
    co_await hold(20 * 6);
    clear();
    if (auto const* air = game::blockWith("minecraft:air")) game::setBlock(*region, lectern, *air);

    // ---- the projectiles ----
    std::vector<std::int64_t> flying;
    if (Mob* lich = spawnMob(*region, kLich, ahead(12, 0))) {
        lichState(*lich)->attackCooldown = 100000;
        lich->setInvisible(true);
        shown.push_back(uidOf(*lich));
        int i = 0;
        for (char const* id : {kLichBolt, kLichBomb, kTomeBolt})
            if (Actor* p = gf::projectile::shoot(*lich, id, Vec3{0.0f, 0.0f, 1.0f}, 0.01f, 0.0f)) {
                teleportTo(*p, ahead(3.5, -1.2 + 1.2 * i++, 1.5f));
                flying.push_back(uidOf(*p));
            }
    }
    for (int t = 0; t < 20 * 7; ++t) {
        for (auto id : flying)
            if (auto* p = actorById(id)) gf::projectile::ext::setVelocity(*p, Vec3{0.0f, 0.0f, 0.0f});
        if (t == 30) log().info("lich.client_showcase: stage projectiles ({} flying)", flying.size());
        co_await ctx.ticks(1);
        keepStill();
    }
    for (auto id : flying) removeById(id);
    clear();

    // ---- phase 3 ----
    if (Mob* lich = spawnMob(*region, kLich, ahead(5, 0))) {
        lichState(*lich)->attackCooldown = 100000;
        setShield(*lich, 0);
        setMinionsLeft(*lich, 0);
        setMainHand(*lich, "minecraft:golden_sword");
        shown.push_back(uidOf(*lich));
    }
    co_await hold(40);
    log().info("lich.client_showcase: stage phase3");
    co_await hold(20 * 5);
    clear();

    // ---- the death animation (the client draws it from the death_time property) ----
    for (int deathTime : {60, 140}) {
        if (Mob* lich = spawnMob(*region, kLich, ahead(5, 0))) {
            lichState(*lich)->attackCooldown = 100000;
            if (deathTime >= 115) setMainHand(*lich, "");
            gf::entity::setProperty(*lich, "twilightforest:death_time", deathTime);
            shown.push_back(uidOf(*lich));
        }
        co_await hold(30);
        log().info("lich.client_showcase: stage death{}", deathTime);
        co_await hold(20 * 5);
        clear();
    }

    // ---- the tower, generated ahead, seen from afar and from its yard ----
    Vec3 towerAt   = ahead(140, 0);
    Vec3 viewpoint = ahead(20, 0, 55.0f);
    real           = realPlayer(); // never kept across ticks (R16)
    if (!real) co_return;
    lookFrom(*real, viewpoint, Vec3{towerAt.x, towerAt.y + 45.0f, towerAt.z});
    co_await ctx.ticks(20 * 6); // the chunks around the tower load
    real = realPlayer();
    if (!real) co_return;
    region       = &real->getDimensionBlockSource();
    BlockPos at  = BlockPos{int(std::floor(towerAt.x)), int(std::floor(f.y)) - 1, int(std::floor(towerAt.z))};
    bool placed  = gf::landmark::generateAt(kTowerKey, *region, at);
    log().info("lich.client_showcase: tower {} at {} {} {}", placed ? "placed" : "not placed", at.x, at.y, at.z);
    co_await ctx.ticks(20 * 8);
    log().info("lich.client_showcase: stage tower_far");
    co_await ctx.ticks(20 * 5);
    // the yard: from beside the far end of the path, looking at the foyer
    std::optional<gf::landmark::Start> start;
    for (auto const& d : {std::pair{0, 0}, std::pair{24, 0}, std::pair{-24, 0}, std::pair{0, 24}, std::pair{0, -24}})
        if (!start) start = gf::landmark::startAt(dim, BlockPos{at.x + d.first, at.y + 1, at.z + d.second});
    if (start) {
        core::BlockBox foyer{}, path{};
        bool           haveFoyer = false, havePath = false;
        for (auto const& p : gf::landmark::ext::startPieces(*start)) {
            if (p.type == "twilightforest:TFLTTFoy") foyer = p.box, haveFoyer = true;
            if (p.type == "twilightforest:TFLTFence" && !havePath) path = p.box, havePath = true;
        }
        if (haveFoyer) {
            Vec3 foyerC{(foyer.minX + foyer.maxX) / 2.0f, float(foyer.minY), (foyer.minZ + foyer.maxZ) / 2.0f};
            Vec3 from = havePath ? Vec3{(path.minX + path.maxX) / 2.0f, float(path.maxY) + 6.0f, (path.minZ + path.maxZ) / 2.0f} : Vec3{foyerC.x, foyerC.y + 12.0f, foyerC.z};
            if (auto* p = realPlayer()) lookFrom(*p, from, Vec3{foyerC.x, foyerC.y + 4.0f, foyerC.z});
            co_await ctx.ticks(20 * 4);
            log().info("lich.client_showcase: stage tower_yard");
            co_await ctx.ticks(20 * 5);
            // the tower from the foot of the base, looking up
            if (auto* p = realPlayer()) lookFrom(*p, Vec3{foyerC.x, foyerC.y + 30.0f, foyerC.z}, Vec3{float(at.x), float(at.y) + 60.0f, float(at.z)});
            co_await ctx.ticks(20 * 4);
            log().info("lich.client_showcase: stage tower_side");
            co_await ctx.ticks(20 * 5);
        }
    }
});

} // namespace gf::feature::lich
