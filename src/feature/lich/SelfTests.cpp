// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// U21 (lich): selftests (lich.*, testPos dx 2100-2199, rule R12).

#include <cmath>
#include <map>
#include <string>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/lich/Common.h"
#include "feature/lich/Lich.h"
#include "feature/loot/Loot.h"
#include "feature/projectile/Projectile.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/landmark/LandmarkTest.h"
#include "feature/landmark/Landmark.h"
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

float attr(Actor& a, Attribute const& h) {
    auto i = a.getAttribute(h);
    return i.mPtr ? i.mPtr->mCurrentValue : -1.0f;
}

} // namespace

// Lich#getPhase: shield up -> 1; no shield, minions left -> 2; neither -> 3.
GF_REGISTER_SELFTEST("lich.phases", [](Context& ctx) -> ll::coro::CoroTask<> {
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
    setTarget(*lich, &ctx.player());
    for (int i = 0; i < 6; ++i) checkAndSpawnMinions(*lich);
    co_await ctx.ticks(1);
    lich = mobById(uid);
    ctx.require(lich != nullptr, "the lich is still there");
    lichState(*lich)->attackCooldown = 1000; // no shadow attacks or teleports during the test
    int alive = countMyMinions(*lich);
    ctx.check(alive <= 3, "at most 3 minions at once (" + std::to_string(alive) + ")");
    ctx.check(!wantsNewMinion(*lich) || alive < 3, "a lich with 3 minions wants no more");
    int left = lichState(*lich)->minionsLeft;
    ctx.check(left == 9 - 3, "3 summons used (" + std::to_string(left) + " left)");
    // the summons are used up 3 at a time while the minions die
    for (int round = 0; round < 4; ++round) {
        removeAround(ctx, c, kMinion, 48.0f);
        co_await ctx.ticks(1);
        lich = mobById(uid);
        if (!lich) break;
        for (int i = 0; i < 4; ++i) checkAndSpawnMinions(*lich);
    }
    lich = mobById(uid);
    ctx.require(lich != nullptr, "the lich is still there");
    lichState(*lich)->attackCooldown = 1000; // no shadow attacks or teleports during the test
    ctx.check(lichState(*lich)->minionsLeft == 0, "9 summons in all");
    removeAround(ctx, c, kMinion, 48.0f);
    co_await ctx.ticks(1);
    lich = mobById(uid);
    ctx.check(lich && phaseOf(*lich) == 3, "no summons and no minions: phase 3");
    removeById(uid);
});

// LichBolt#hurt: a hit re-shoots the bolt along the attacker's look with the attacker as owner; a
// player-owned bolt breaks a shield of a phase-1 lich.
GF_REGISTER_SELFTEST("lich.bolt_reflect", [](Context& ctx) -> ll::coro::CoroTask<> {
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
    reflectBolt(*bolt, *player);
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

// DeathTome on a lectern: a player looking at it within 3 blocks wakes it; it leaves the lectern.
GF_REGISTER_SELFTEST("lich.death_tome_lectern", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(2120, 16);
    auto lect = ctx.testPos(2128, 2).offset(0, 1, 0);
    ctx.setBlock(lect, "minecraft:lectern");
    Mob* tome = spawnAt(ctx, kTome, lect, 0.0f);
    ctx.require(tome != nullptr, "a death tome spawned");
    auto uid = uidOf(*tome);
    setTomeOnLectern(*tome, true);
    co_await ctx.ticks(2);
    tome = mobById(uid);
    ctx.require(tome && tomeOnLectern(*tome), "the tome rests on the lectern");
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
    removeById(uid);
    removeAround(ctx, lect, kTomeBolt, 16.0f);
    ctx.cleanupBlock(lect);
});

// RisingZombie: rises for 130 ticks, then becomes a vanilla zombie.
GF_REGISTER_SELFTEST("lich.rising_zombie_converts", [](Context& ctx) -> ll::coro::CoroTask<> {
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

// The lich tower assembles: foyer, base, 12-15 slices, the boss room, its boundary and roof, with
// wings (U22's rooms or the placeholder rooms). The piece boxes relative to the start give the
// layout hash (logged; the e2e scenario compares it between runs of the same seed).
GF_REGISTER_SELFTEST("lich.tower_layout", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto center = ctx.testPos(2150, 2).offset(0, 0, 400);
    gf::landmark::ext::ScopedStart forced(kTowerKey, center);
    ctx.require(forced && forced->generated, "forced a lich tower start");
    auto pieces = gf::landmark::ext::startPieces(*forced);
    std::map<std::string, int> counts;
    std::uint64_t              hash = 1469598103934665603ull;
    auto mix = [&](std::int64_t v) {
        hash ^= static_cast<std::uint64_t>(v);
        hash *= 1099511628211ull;
    };
    for (auto const& p : pieces) {
        counts[p.type]++;
        for (int v : {p.box.minX - center.x, p.box.minY - center.y, p.box.minZ - center.z, p.box.maxX - center.x, p.box.maxY - center.y, p.box.maxZ - center.z}) mix(v);
    }
    ctx.check(counts["twilightforest:TFLTTFoy"] == 1, "one foyer");
    ctx.check(counts["twilightforest:TFLTCTBase"] == 1, "one tower base");
    ctx.check(counts["twilightforest:TFLTCTTrim"] == 1, "the base trim");
    int slices = counts["twilightforest:TFLTCTSeg"] - 1; // the boss boundary is a segment too
    ctx.check(slices >= 12 && slices <= 15, "12-15 tower slices (" + std::to_string(slices) + ")");
    ctx.check(counts["twilightforest:TFLTTBoss"] == 1 && counts["twilightforest:TFLTTBossRoof"] == 1, "the boss room and its roof");
    ctx.check(counts["twilightforest:TFLTBridge"] >= 1, "wing bridges or covers");
    ctx.check(counts["twilightforest:TFLTTRoom"] >= 1, "wing rooms (placeholders while U22's rooms are missing)");
    std::string summary;
    for (auto const& [type, n] : counts) summary += " " + type.substr(15) + "=" + std::to_string(n);
    ctx.info("lich tower: " + std::to_string(pieces.size()) + " pieces," + summary + "; layout hash " + std::to_string(hash));
    co_return;
});

// For the client e2e's screenshots: the Lich, a shadow clone, a minion, a Death Tome on a lectern,
// a half-risen zombie and the projectiles in front of the real player for `hold` seconds, then a
// lich tower generated 48 blocks ahead of it.
GF_REGISTER_SELFTEST("lich.client_showcase", [](Context& ctx) -> ll::coro::CoroTask<> {
    Player* real = nullptr;
    if (auto level = ll::service::getLevel())
        level->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
            if (!p.isSimulatedPlayer()) real = &p;
            return real == nullptr;
        }));
    if (!real) {
        ctx.info("no real player: nothing to show");
        co_return;
    }
    auto   f   = real->getFeetPos();
    auto   v   = viewVector(*real);
    double len = std::sqrt(double(v.x) * v.x + double(v.z) * v.z);
    double fx = len > 1e-6 ? v.x / len : 0.0, fz = len > 1e-6 ? v.z / len : 1.0;
    int    dim = dimOf(*real);
    auto   ahead = [&](double d, double side, float dy = 0.0f) {
        return Vec3{float(f.x + fx * d - fz * side), f.y + dy, float(f.z + fz * d + fx * side)};
    };
    auto* region = &real->getDimensionBlockSource();
    std::vector<std::int64_t> shown;
    Mob* lich = spawnMob(*region, kLich, ahead(6, 0));
    if (lich) {
        lichState(*lich)->attackCooldown = 100000;
        shown.push_back(uidOf(*lich));
        if (Mob* clone = spawnClone(*lich, ahead(6, -3))) {
            lichState(*clone)->attackCooldown = 100000;
            shown.push_back(uidOf(*clone));
        }
    }
    if (Mob* minion = spawnMob(*region, kMinion, ahead(5, 3))) shown.push_back(uidOf(*minion));
    if (Mob* tome = spawnMob(*region, kTome, ahead(4, -1.5, 1.0f))) shown.push_back(uidOf(*tome));
    if (Mob* z = spawnMob(*region, kRising, ahead(4, 1.5))) {
        setRisingTicks(*z, 100);
        shown.push_back(uidOf(*z));
    }
    log().info("lich.client_showcase: mobs ready ({} shown)", shown.size());
    for (int i = 0; i < 20 * 15; ++i) {
        co_await ctx.ticks(1);
        // keep them still for the screenshots
        for (auto id : shown)
            if (auto* m = mobById(id)) {
                stopNavigation(*m);
                if (isType(*m, kRising) && risingTicks(*m) > 110) setRisingTicks(*m, 100);
            }
    }
    for (auto id : shown) removeById(id);
    // the tower, generated now 48 blocks ahead
    auto towerAt = ahead(48, 0);
    bool placed  = gf::landmark::generateAt(kTowerKey, *region, BlockPos{int(towerAt.x), int(f.y), int(towerAt.z)});
    log().info("lich.client_showcase: tower {} at {} {} {}", placed ? "placed" : "not placed", int(towerAt.x), int(f.y), int(towerAt.z));
    (void)dim;
    co_await ctx.ticks(20 * 20);
});

} // namespace gf::feature::lich
