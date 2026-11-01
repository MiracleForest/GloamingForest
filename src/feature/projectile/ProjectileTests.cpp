// Selftests of F8's "projectile" contract (rule R12: prefix entity.*, dx 5800..5899): a simulated
// throwable hits an entity and a block face, sticks when its block hit keeps it, follows
// ThrowableProjectile's drag and gravity, and steering replaces its velocity. The projectile
// fixture is gf_test:boss_part (no engine physics), registered only while the test runs.

#include <algorithm>
#include <cmath>

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "core/Stub.h"
#include "feature/entity/Entity.h"
#include "feature/entity/EntityInternal.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/projectile/Projectile.h"
#include "feature/projectile/ProjectileInternal.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace gf::feature::projectile::tests {

using gf::selftest::Context;
namespace fe = gf::feature::entity;
namespace gp = gf::projectile;

namespace {

constexpr char const* kFixture = "gf_test:boss_part";

struct Hits {
    std::string entity;
    int         face      = -1;
    bool        keepBlock = false;
    bool        steer     = false;
};
Hits& hits() {
    static Hits h;
    return h;
}

/// The fixture projectile type exists only while the test runs.
struct ProjectileScope {
    ProjectileScope() {
        hits() = {};
        gp::registerType(
            kFixture,
            {
                .gravity = 0.03f,
                .drag    = 0.99f,
                .onHitEntity =
                    [](Actor&, Actor& target) {
                        hits().entity = target.getTypeName();
                        return true;
                    },
                .onHitBlock =
                    [](Actor&, BlockPos const&, int face) {
                        hits().face = face;
                        return !hits().keepBlock;
                    },
            }
        );
        static bool steering = false;
        if (!std::exchange(steering, true)) {
            gp::addSteering([](Actor&, std::string const& id) -> std::optional<Vec3> {
                if (id != kFixture || !hits().steer) return std::nullopt;
                return Vec3{0.0f, 0.0f, 0.5f};
            });
        }
    }
    ~ProjectileScope() {
        hits().steer = false;
        unregisterTypeForTest(kFixture);
    }
};

Mob* spawnMob(core::GlobalPos const& at, std::string const& id) {
    auto* region = game::regionOf(at.dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level) return nullptr;
    return level->getSpawner().spawnMob(*region, ActorDefinitionIdentifier(id), nullptr, Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f}, false, false, false);
}

} // namespace

/// The target is the fixture boss: not on peaceful (BaseTFBoss#checkDespawn).
struct EasyScope {
    SharedTypes::Legacy::Difficulty previous = SharedTypes::Legacy::Difficulty::Peaceful;
    EasyScope() {
        if (auto level = ll::service::getLevel()) {
            previous = level->getDifficulty();
            level->setDifficulty(SharedTypes::Legacy::Difficulty::Easy);
        }
    }
    ~EasyScope() {
        if (auto level = ll::service::getLevel()) level->setDifficulty(previous);
    }
};

GF_REGISTER_SELFTEST("entity.projectile", [](Context& ctx) -> ll::coro::CoroTask<> {
    EasyScope       easy;
    ProjectileScope scope;
    auto            box  = co_await ctx.flatArea(5870, 10);
    auto            at   = ctx.testPos(5871, 2);
    auto&           sp   = ctx.spawnPlayer();
    auto            spId = sp.getOrCreateUniqueID().rawID;
    sp.teleport(Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f}, DimensionType(at.dim));
    co_await ctx.ticks(2);
    // an entity hit: a fixture boss 6 blocks east
    auto* target = spawnMob(at.offset(6, 0, 0), "gf_test:boss");
    ctx.require(target != nullptr, "a target spawns");
    auto  targetId = fe::uidOf(*target);
    auto* shooter  = fe::playerById(spId);
    ctx.require(shooter != nullptr, "the shooter is there");
    auto  eye  = shooter->getEyePos();
    auto  tpos = target->getFeetPos();
    Vec3  dir{tpos.x - eye.x, tpos.y + 1.0f - eye.y, tpos.z - eye.z};
    auto* p = gp::shoot(*shooter, kFixture, dir, 1.0f, 0.0f, {{"probe", 1}});
    ctx.require(p != nullptr, "shoot spawns the projectile");
    auto pid = fe::uidOf(*p);
    ctx.check(gp::ext::isProjectile(*p), "ext::isProjectile");
    ctx.check(gp::ext::owner(*p) == shooter, "ext::owner is the shooter");
    ctx.check(gf::entity::data(*p, "init").value("probe", 0) == 1, "the init document is the projectile's data");
    for (int i = 0; i < 30 && hits().entity.empty(); ++i) co_await ctx.ticks(1);
    ctx.check(hits().entity == "gf_test:boss", "onHitEntity hit the target (" + hits().entity + ")");
    co_await ctx.ticks(2);
    ctx.check(fe::actorById(pid) == nullptr, "a hit that returns true removes the projectile");
    if (auto* t = fe::actorById(targetId)) t->remove();
    // a block hit straight down: the top face (1)
    hits().face = -1;
    shooter = fe::playerById(spId);
    ctx.require(shooter != nullptr, "the shooter is still there");
    p = gp::shoot(*shooter, kFixture, Vec3{0.0f, -1.0f, 0.0f}, 0.5f, 0.0f);
    ctx.require(p != nullptr, "a second projectile");
    pid = fe::uidOf(*p);
    for (int i = 0; i < 30 && hits().face < 0; ++i) co_await ctx.ticks(1);
    ctx.check(hits().face == 1, "onHitBlock got the top face (" + std::to_string(hits().face) + ")");
    co_await ctx.ticks(2);
    ctx.check(fe::actorById(pid) == nullptr, "removed after the block hit");
    // a block hit that keeps the projectile: it sticks (AbstractArrow#inGround)
    hits().face      = -1;
    hits().keepBlock = true;
    shooter          = fe::playerById(spId);
    ctx.require(shooter != nullptr, "the shooter is still there");
    p = gp::shoot(*shooter, kFixture, Vec3{0.0f, -1.0f, 0.0f}, 0.5f, 0.0f);
    ctx.require(p != nullptr, "a third projectile");
    pid = fe::uidOf(*p);
    for (int i = 0; i < 30 && hits().face < 0; ++i) co_await ctx.ticks(1);
    co_await ctx.ticks(3);
    if (auto* stuck = fe::actorById(pid)) {
        auto v = gp::ext::velocity(*stuck);
        ctx.check(v.x == 0 && v.y == 0 && v.z == 0, "a kept projectile stays in the ground, still");
        stuck->remove();
    } else {
        ctx.check(false, "a kept projectile stays in the ground");
    }
    // drag and gravity: horizontal shot, y after two ticks = y0 + 0 + (0 * 0.99 - 0.03)
    hits().keepBlock = false;
    shooter          = fe::playerById(spId);
    ctx.require(shooter != nullptr, "the shooter is still there");
    p = gp::shoot(*shooter, kFixture, Vec3{1.0f, 0.0f, 0.0f}, 0.2f, 0.0f);
    ctx.require(p != nullptr, "a fourth projectile");
    pid      = fe::uidOf(*p);
    float y0 = p->getPosition().y;
    co_await ctx.ticks(2);
    auto* q = fe::actorById(pid);
    ctx.require(q != nullptr, "the fourth projectile flies");
    float dy = q->getPosition().y - y0;
    ctx.check(dy < -0.01f && dy > -0.2f, "gravity pulls the projectile down (" + std::to_string(dy) + ")");
    auto v = gp::ext::velocity(*q);
    ctx.check(v.x < 0.2f && v.x > 0.18f, "drag slows it (" + std::to_string(v.x) + ")");
    // steering replaces the velocity
    hits().steer = true;
    co_await ctx.ticks(2);
    if (auto* r = fe::actorById(pid)) {
        auto w = gp::ext::velocity(*r);
        ctx.check(w.z > 0.4f, "steering set the velocity (" + std::to_string(w.z) + ")");
        r->remove();
    }
    (void)box;
    (void)liveCount();
});

// TFArrow (AbstractArrow): the default entity hit (ceil(speed * 2) arrow damage, or a deflection
// when it does not land), gravity 0.05, sticking in a block and the pickup by a player
GF_REGISTER_SELFTEST("entity.arrow", [](Context& ctx) -> ll::coro::CoroTask<> {
    EasyScope easy;
    struct ArrowScope {
        ArrowScope() {
            hits() = {};
            gp::ext::registerArrow(
                kFixture,
                {.onHitEntity =
                     [](Actor&, Actor& target) {
                         hits().entity = target.getTypeName();
                         return false;
                     }},
                {.baseDamage = 2.0}
            );
        }
        ~ArrowScope() { unregisterTypeForTest(kFixture); }
    } scope;
    auto  box  = co_await ctx.flatArea(5875, 8);
    auto  at   = ctx.testPos(5876, 2);
    auto& sp   = ctx.spawnPlayer();
    auto  spId = sp.getOrCreateUniqueID().rawID;
    sp.teleport(Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f}, DimensionType(at.dim));
    co_await ctx.ticks(2);
    // an entity hit with a full-power shot (3.0)
    auto* target = spawnMob(at.offset(6, 0, 0), "gf_test:boss");
    ctx.require(target != nullptr, "a target spawns");
    auto  targetId = fe::uidOf(*target);
    int   health   = target->getHealth();
    auto* shooter  = fe::playerById(spId);
    ctx.require(shooter != nullptr, "the shooter is there");
    auto eye  = shooter->getEyePos();
    auto tpos = target->getFeetPos();
    auto* p   = gp::shoot(*shooter, kFixture, Vec3{tpos.x - eye.x, tpos.y + 1.0f - eye.y, tpos.z - eye.z}, 3.0f, 0.0f);
    ctx.require(p != nullptr, "the arrow is shot");
    auto pid = fe::uidOf(*p);
    ctx.check(gp::ext::pickup(*p) == gp::ext::Pickup::Allowed, "a player's arrow may be picked up");
    ctx.check(gp::ext::baseDamage(*p) == 2.0, "AbstractArrow's base damage 2");
    for (int i = 0; i < 20 && hits().entity.empty(); ++i) co_await ctx.ticks(1);
    ctx.check(hits().entity == "gf_test:boss", "the arrow hit the target and the subclass hook ran after the hit (" + hits().entity + ")");
    co_await ctx.ticks(1);
    auto* t = fe::actorById(targetId);
    ctx.require(t != nullptr, "the target survived");
    auto calls = core::stubCalls();
    if (std::find(calls.begin(), calls.end(), "combat.hurt") == calls.end()) {
        ctx.check(health - t->getHealth() == 6, "ceil(3 * 2) = 6 arrow damage (" + std::to_string(health - t->getHealth()) + ")");
        ctx.check(fe::actorById(pid) == nullptr, "an arrow that hit is gone");
    } else {
        // the combat contract is still F0's stub: the hit does not land, the arrow is deflected
        if (auto* a = fe::actorById(pid)) ctx.check(gp::ext::velocity(*a).x < 0.0f, "an arrow whose hit did not land bounces back (REVERSE * 0.2)");
        else ctx.check(false, "a deflected arrow keeps flying");
    }
    if (auto* a = fe::actorById(pid)) a->remove();
    if (auto* a = fe::actorById(targetId)) a->remove();
    // gravity 0.05 (AbstractArrow), not the throwable's 0.03
    shooter = fe::playerById(spId);
    ctx.require(shooter != nullptr, "the shooter is still there");
    p = gp::shoot(*shooter, kFixture, Vec3{1.0f, 0.0f, 0.0f}, 0.2f, 0.0f);
    ctx.require(p != nullptr, "a slow arrow");
    pid      = fe::uidOf(*p);
    float y0 = p->getPosition().y;
    co_await ctx.ticks(2);
    if (auto* q = fe::actorById(pid)) {
        float dy = q->getPosition().y - y0;
        ctx.check(dy < -0.04f && dy > -0.07f, "an arrow falls 0.05 in its second tick (" + std::to_string(dy) + ")");
        q->remove();
    }
    // stuck in the ground, then picked up by the player standing on it
    shooter = fe::playerById(spId);
    ctx.require(shooter != nullptr, "the shooter is still there");
    p = gp::shoot(*shooter, kFixture, Vec3{0.0f, -1.0f, 0.0f}, 0.5f, 0.0f);
    ctx.require(p != nullptr, "an arrow into the ground");
    pid = fe::uidOf(*p);
    bool stuck = false;
    for (int i = 0; i < 20 && !stuck; ++i) {
        co_await ctx.ticks(1);
        if (auto* a = fe::actorById(pid)) stuck = gp::ext::inGround(*a);
    }
    ctx.check(stuck, "the arrow sticks in the ground (AbstractArrow#inGround)");
    bool gone = false;
    for (int i = 0; i < 20 && !gone; ++i) {
        co_await ctx.ticks(1);
        gone = fe::actorById(pid) == nullptr;
    }
    ctx.check(gone, "the player picked the stuck arrow up");
    int arrows = 0;
    if (auto* pl = fe::playerById(spId)) {
        auto& inv = pl->getInventory();
        for (int i = 0; i < inv.getContainerSize(); ++i)
            if (game::items::idOf(inv.getItem(i)) == "minecraft:arrow") arrows += game::items::countOf(inv.getItem(i));
    }
    ctx.check(arrows >= 1, "the pickup item (minecraft:arrow) is in the inventory (" + std::to_string(arrows) + ")");
    if (auto* a = fe::actorById(pid)) a->remove();
    (void)box;
});

} // namespace gf::feature::projectile::tests
