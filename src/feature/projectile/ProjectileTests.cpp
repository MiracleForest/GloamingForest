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
#include "mc/deps/core/math/Vec2.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/alchemy/Potion.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/entity/EntityInternal.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/entity/Handles.h"
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
    bool        bounce    = false; ///< the block hit keeps the projectile and steers it up (ext::setVelocity)
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
                    [](Actor& p, BlockPos const&, int face) {
                        hits().face = face;
                        if (hits().bounce) gp::ext::setVelocity(p, Vec3{0.0f, 0.4f, 0.0f});
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

/// Spawns any actor (a boat: not a mob) through the actor factory.
Actor* spawnActor(core::GlobalPos const& at, std::string const& id) {
    auto* region = game::regionOf(at.dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level) return nullptr;
    auto owned = level->getActorFactory().createSpawnedActor(ActorDefinitionIdentifier(id), nullptr, Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f}, Vec2{0.0f, 0.0f});
    return owned ? level->addEntity(*region, std::move(owned)) : nullptr;
}

void removeActorById(std::int64_t uid) {
    if (auto* a = fe::actorById(uid)) a->remove();
}

/// The actor whose incoming damage the combat pipeline cancels while a CancelHitsScope lives, and
/// the hits cancelled. The contract's handlers cannot be removed: one handler for the process,
/// active only while a scope names a target.
struct CancelHits {
    std::int64_t target    = 0;
    int          cancelled = 0;
    bool         parry     = false; ///< EntityEvents#onParryProjectile: deflect, new owner, impact cancelled
    bool         directIsProjectile = false; ///< DamageInfo::direct was the projectile (attacker its shooter)
    std::string  seen; ///< "attacker / direct" type names of the last cancelled hit
};
CancelHits& cancelHits() {
    static CancelHits c;
    return c;
}
int cancelledHits() { return cancelHits().cancelled; }
struct CancelHitsScope {
    explicit CancelHitsScope(std::int64_t target) {
        static bool const listening = [] {
            gf::combat::onIncoming(gf::combat::priority::Highest, [](gf::combat::DamageInfo& d) {
                auto& c = cancelHits();
                if (c.target == 0 || fe::uidOf(d.victim) != c.target) return gf::combat::Verdict::Pass;
                ++c.cancelled;
                c.directIsProjectile = d.direct && gp::ext::isProjectile(*d.direct) && d.attacker && !gp::ext::isProjectile(*d.attacker);
                c.seen = (d.attacker ? d.attacker->getTypeName() : std::string("none")) + " / " + (d.direct ? d.direct->getTypeName() : std::string("none"));
                Actor* projectile    = d.direct && gp::ext::isProjectile(*d.direct) ? d.direct : (d.attacker && gp::ext::isProjectile(*d.attacker) ? d.attacker : nullptr);
                if (c.parry && projectile) {
                    gp::ext::setVelocity(*projectile, Vec3{-0.5f, 0.0f, 0.0f});
                    gp::ext::setOwner(*projectile, &d.victim);
                    gp::ext::cancelImpact(*projectile);
                }
                return gf::combat::Verdict::Cancel;
            });
            return true;
        }();
        (void)listening;
        cancelHits() = {.target = target, .cancelled = 0};
    }
    ~CancelHitsScope() { cancelHits() = {}; }
    CancelHitsScope(CancelHitsScope const&)            = delete;
    CancelHitsScope& operator=(CancelHitsScope const&) = delete;
};

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
        auto y = stuck->getPosition().y;
        ctx.check(gp::ext::inGround(*stuck), "a kept projectile that was not steered rests where it hit");
        co_await ctx.ticks(2);
        if ((stuck = fe::actorById(pid))) {
            ctx.check(std::abs(stuck->getPosition().y - y) < 1.0e-3f, "and stays there");
            stuck->remove();
        }
    } else {
        ctx.check(false, "a kept projectile stays in the ground");
    }
    // a block hit that keeps the projectile and steers it (ChainBlock#bounce): it flies on with the
    // new velocity (ThrowableProjectile#tick), nothing zeroed
    hits().face   = -1;
    hits().bounce = true;
    shooter       = fe::playerById(spId);
    ctx.require(shooter != nullptr, "the shooter is still there");
    p = gp::shoot(*shooter, kFixture, Vec3{0.0f, -1.0f, 0.0f}, 0.5f, 0.0f);
    ctx.require(p != nullptr, "a bouncing projectile");
    ctx.check(!p->hasCategory(ActorCategory::Mob), "an upstream projectile is no mob (Java: no LivingEntity)");
    pid = fe::uidOf(*p);
    for (int i = 0; i < 30 && hits().face < 0; ++i) co_await ctx.ticks(1);
    hits().bounce = false;
    if (auto* b = fe::actorById(pid)) {
        auto v = gp::ext::velocity(*b);
        ctx.check(!gp::ext::inGround(*b) && v.y > 0.2f, "a steered block hit flies on (vy " + std::to_string(v.y) + ")");
        b->remove();
    } else {
        ctx.check(false, "a steered block hit keeps the projectile");
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
    // the hit lands through the combat pipeline (gf::combat::hurt)
    ctx.check(health - t->getHealth() == 6, "ceil(3 * 2) = 6 arrow damage (" + std::to_string(health - t->getHealth()) + ")");
    ctx.check(fe::actorById(pid) == nullptr, "an arrow that hit is gone");
    if (auto* a = fe::actorById(pid)) a->remove();
    // a hit that does not land (an onIncoming handler cancels the target's damage): the arrow is
    // deflected (ProjectileDeflection.REVERSE, then * 0.2) and no doPostHurtEffects runs
    {
        CancelHitsScope cancel(targetId);
        hits().entity.clear();
        t = fe::actorById(targetId);
        ctx.require(t != nullptr, "the target is still there");
        int  healthBefore = t->getHealth();
        int  postBefore   = postHurtsForTest();
        auto* shooter2    = fe::playerById(spId);
        ctx.require(shooter2 != nullptr, "the shooter is still there");
        eye  = shooter2->getEyePos();
        tpos = t->getFeetPos();
        p    = gp::shoot(*shooter2, kFixture, Vec3{tpos.x - eye.x, tpos.y + 1.0f - eye.y, tpos.z - eye.z}, 3.0f, 0.0f);
        ctx.require(p != nullptr, "a second arrow is shot");
        pid = fe::uidOf(*p);
        for (int i = 0; i < 20 && hits().entity.empty(); ++i) co_await ctx.ticks(1);
        ctx.check(hits().entity == "gf_test:boss", "the second arrow hit the target (" + hits().entity + ")");
        ctx.check(cancelledHits() >= 1, "the combat pipeline cancelled its damage (" + std::to_string(cancelledHits()) + ")");
        if (auto* a = fe::actorById(pid)) {
            auto v = gp::ext::velocity(*a);
            ctx.check(v.x < 0.0f && v.x > -0.35f, "the arrow whose hit did not land bounces back at a tenth of its speed (vx " + std::to_string(v.x) + ")");
        } else {
            ctx.check(false, "a deflected arrow keeps flying");
        }
        ctx.check(postHurtsForTest() == postBefore, "no doPostHurtEffects for a hit that did not land");
        if (auto* tt = fe::actorById(targetId)) ctx.check(tt->getHealth() == healthBefore, "and the target took no damage");
        // (F10's DamageInfo order for a child source; checked by combat.child_source)
        ctx.info("DamageInfo of an arrow hit: attacker / direct = " + cancelHits().seen);
        if (auto* a = fe::actorById(pid)) a->remove();
    }
    // a parried hit (EntityEvents#onParryProjectile: AIM_DEFLECT, the blocker owns it, the impact
    // event cancelled): no REVERSE, the arrow flies on with the parry's velocity
    {
        CancelHitsScope cancel(targetId);
        cancelHits().parry = true;
        hits().entity.clear();
        t = fe::actorById(targetId);
        ctx.require(t != nullptr, "the target is still there");
        auto* shooter3 = fe::playerById(spId);
        ctx.require(shooter3 != nullptr, "the shooter is still there");
        eye  = shooter3->getEyePos();
        tpos = t->getFeetPos();
        p    = gp::shoot(*shooter3, kFixture, Vec3{tpos.x - eye.x, tpos.y + 1.0f - eye.y, tpos.z - eye.z}, 3.0f, 0.0f);
        ctx.require(p != nullptr, "a parried arrow is shot");
        pid = fe::uidOf(*p);
        for (int i = 0; i < 20 && cancelledHits() == 0; ++i) co_await ctx.ticks(1);
        ctx.check(cancelledHits() >= 1, "the parry ran");
        ctx.check(hits().entity.empty(), "a cancelled impact runs no hit hook of the type");
        if (auto* a = fe::actorById(pid)) {
            auto v = gp::ext::velocity(*a);
            ctx.check(v.x < -0.4f, "the parried arrow keeps the parry's velocity, not reversed (vx " + std::to_string(v.x) + ")");
            ctx.check(gp::ext::owner(*a) == fe::actorById(targetId), "the parrying target owns the arrow (Projectile#setOwner)");
            a->remove();
        } else {
            ctx.check(false, "a parried arrow keeps flying");
        }
    }
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
    // Entity#canBeHitByProjectile: a boat is pickable, the arrow hits it (the engine's own damage)
    hits().entity.clear();
    auto* boat = spawnActor(at.offset(5, 0, 0), "minecraft:boat");
    ctx.require(boat != nullptr, "a boat spawns");
    auto boatId = fe::uidOf(*boat);
    shooter     = fe::playerById(spId);
    ctx.require(shooter != nullptr, "the shooter is still there");
    eye       = shooter->getEyePos();
    auto bpos = boat->getPosition();
    p         = gp::shoot(*shooter, kFixture, Vec3{bpos.x - eye.x, bpos.y + 0.2f - eye.y, bpos.z - eye.z}, 3.0f, 0.0f);
    ctx.require(p != nullptr, "an arrow at the boat");
    pid = fe::uidOf(*p);
    for (int i = 0; i < 20 && hits().entity.empty(); ++i) co_await ctx.ticks(1);
    ctx.check(hits().entity == "minecraft:boat", "the arrow hit the boat (" + hits().entity + ")");
    co_await ctx.ticks(1);
    ctx.info(std::string("the boat's hurt ") + (fe::actorById(pid) ? "did not land (the arrow bounced)" : "landed (the arrow is gone)"));
    if (auto* a = fe::actorById(pid)) a->remove();
    if (auto* a = fe::actorById(boatId)) a->remove();
    // Arrow#doPostHurtEffects of the fired stack (TFArrow: the parent arrow's): a tipped arrow of
    // poison gives poison for 1/8 of the potion's duration
    auto* pig = spawnMob(at.offset(3, 0, 2), "minecraft:pig");
    ctx.require(pig != nullptr, "a pig spawns");
    auto pigId  = fe::uidOf(*pig);
    auto poison = Potion::Poison();
    ctx.require(poison != nullptr, "the poison potion");
    auto tipped = game::items::make("minecraft:arrow", 1, static_cast<short>(poison->mId + 1));
    ctx.require(tipped.has_value(), "a tipped arrow of poison");
    int potionTicks = poison->mEffects->empty() ? 0 : poison->mEffects->front().mDuration->mValue;
    applyFiredStackEffectsForTest(*pig, *tipped);
    auto const* effect = pig->getEffect(poison->mEffects->empty() ? 0u : poison->mEffects->front().mId);
    ctx.check(effect != nullptr, "the pig is poisoned by the tipped arrow");
    if (effect) ctx.check(effect->mDuration->mValue == std::max(potionTicks / 8, 1), "for 1/8 of the potion's duration (" + std::to_string(effect->mDuration->mValue) + " of " + std::to_string(potionTicks) + ")");
    removeActorById(pigId); // out of the next shot's way
    // through the combat pipeline the hit lands and the effects come with it
    {
        auto* fresh = spawnMob(at.offset(5, 0, 2), "minecraft:pig");
        ctx.require(fresh != nullptr, "a second pig spawns");
        auto freshId = fe::uidOf(*fresh);
        int  before  = postHurtsForTest();
        shooter      = fe::playerById(spId);
        ctx.require(shooter != nullptr, "the shooter is still there");
        eye       = shooter->getEyePos();
        auto fpos = fresh->getFeetPos();
        p = gp::shoot(*shooter, kFixture, Vec3{fpos.x - eye.x, fpos.y + 0.5f - eye.y, fpos.z - eye.z}, 3.0f, 0.0f, {{"pickupItem", game::items::toJson(*tipped)}});
        ctx.require(p != nullptr, "a tipped arrow is shot");
        for (int i = 0; i < 20 && postHurtsForTest() == before; ++i) co_await ctx.ticks(1);
        ctx.check(postHurtsForTest() > before, "a landed hit runs doPostHurtEffects");
        if (auto* f = fe::actorById(freshId); f && f->isAlive())
            ctx.check(f->getEffect(poison->mEffects->front().mId) != nullptr, "and the shot pig is poisoned");
        else ctx.info("the shot pig died of the hit (the poison is not checked)");
        removeActorById(freshId);
    }
    removeActorById(pigId);
    (void)box;
});

} // namespace gf::feature::projectile::tests
