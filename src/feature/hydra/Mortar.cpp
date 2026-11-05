// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/boss/HydraMortar.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// twilightforest:hydra_mortar, a ThrowableProjectile simulated by F8's projectile kit (gravity
// 0.05, drag 0.99). Its own state (fuse, blasting, on ground, the owner that a reflection changes)
// is kept in data(mortar, "hydra_mortar"). A mortar landing on a block's top stays on the ground
// and slides on with its horizontal speed (Java's ThrowableProjectile keeps moving it and never
// clears onGround): a wall it slides into, any side or bottom face it hits, or a blasting mortar's
// any block detonates it; on the ground its fuse (80) counts down. A player's hit reflects it
// along the player's look (speed 1.5, inaccuracy 0.1, fuse + 20) and makes the player its owner.

#include "feature/hydra/Hydra.h"

#include <cmath>
#include <limits>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/phys/AABB.h"

#include "core/jrand/JavaOrder.h"
#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/entity/Entity.h"
#include "feature/entity/EntityInternal.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/projectile/Projectile.h"
#include "mod/Log.h"

namespace gf::feature::hydra {

namespace fe = gf::feature::entity;
namespace ge = gf::entity;
namespace gp = gf::projectile;
namespace ia = gf::interactions;

namespace {

constexpr int   kFuse         = 80;
constexpr int   kBurnSeconds  = 5;  ///< BURN_FACTOR
constexpr float kDirectDamage = 18; ///< DIRECT_DAMAGE
constexpr int   kFaceUp       = 1;  ///< FacingID up
constexpr float kDegToRad     = 3.1415927F / 180.0F;

int                        gDetonations = 0;
std::vector<ia::HandlerId> gHandlers;

ge::Json& stateOf(Actor& mortar) {
    auto& d = ge::data(mortar, "hydra_mortar");
    if (!d.is_object()) d = ge::Json{{"fuse", kFuse}, {"blasting", false}, {"onGround", false}, {"stuck", false}, {"owner", 0}, {"detonated", false}};
    return d;
}

bool mobGriefing() {
    auto lv = ll::service::getLevel();
    if (!lv) return false;
    GameRuleId id;
    id.mValue = static_cast<int>(GameRules::GameRulesIndex::MobGriefing);
    return lv->getGameRules().getBool(id, true);
}

Actor* ownerOf(Actor& mortar) {
    auto uid = stateOf(mortar).value("owner", std::int64_t{0});
    return uid ? fe::actorById(uid) : nullptr;
}

/// Entity#is: a TF part "is" its parent too (HydraPart#is).
bool sameEntity(Actor& a, Actor* b) {
    if (!b) return false;
    if (&a == b) return true;
    return fe::parentOfPart(a) == fe::uidOf(*b);
}

/// HydraMortar#detonate.
void detonate(Actor& mortar) {
    auto& st = stateOf(mortar);
    if (st.value("detonated", false)) return;
    st["detonated"] = true;
    ++gDetonations;
    bool  blasting = st.value("blasting", false);
    float power    = blasting ? 4.0F : 0.1F;
    bool  grief    = mobGriefing();
    auto  pos      = mortar.getPosition();
    auto& region   = mortar.getDimensionBlockSource();
    if (auto lv = ll::service::getLevel())
        lv->explode(region, &mortar, pos, power, grief, grief, std::numeric_limits<float>::max(), false);
    Actor*      owner = ownerOf(mortar);
    auto const& bb    = mortar.getAABB();
    AABB        near{Vec3{bb.min.x - 1.0F, bb.min.y - 1.0F, bb.min.z - 1.0F}, Vec3{bb.max.x + 1.0F, bb.max.y + 1.0F, bb.max.z + 1.0F}};
    int         hits = 0;
    for (auto* e : fe::actorsIn(region, static_cast<int>(mortar.getDimensionId()), near)) {
        if (!e || e == &mortar || !e->isAlive()) continue;
        bool part  = fe::parentOfPart(*e) != 0;
        bool hydra = e->getTypeName() == kHydra || (part && ge::ext::parentOf(*e) && ge::ext::parentOf(*e)->getTypeName() == kHydra);
        if (!part && !fe::asMob(*e)) continue;
        if ((!e->isFireImmune() || hydra) && gf::combat::hurt(*e, kDirectDamage, "twilightforest:hydra_mortar", owner, &mortar)) {
            if (!part) e->setOnFire(kBurnSeconds, true);
            ++hits;
        }
    }
    log().info("hydra: mortar detonated ({}, {} hit)", blasting ? "blasting" : "normal", hits);
    fe::removeLater(fe::uidOf(mortar));
}

bool onHitEntity(Actor& mortar, Actor& target) {
    auto& st = stateOf(mortar);
    if (st.value("detonated", false)) return true;
    Actor* owner = ownerOf(mortar);
    if (!owner) return false;
    bool otherMortar = target.getTypeName() == kMortar;
    bool sameOwner   = otherMortar && sameEntity(*owner, ownerOf(target));
    bool partOfOwner = owner->getTypeName() == kHydra && fe::parentOfPart(target) == fe::uidOf(*owner);
    if ((!otherMortar || sameOwner) && !sameEntity(target, owner) && !partOfOwner) {
        detonate(mortar);
        return true;
    }
    return false;
}

bool onHitBlock(Actor& mortar, BlockPos const&, int face) {
    auto& st = stateOf(mortar);
    if (st.value("detonated", false)) return true;
    if (!st.value("blasting", false)) {
        // if we hit a wall, explode
        if (face != kFaceUp) {
            detonate(mortar);
            return true;
        }
        // we hit the ground: y motion 0, on ground, sliding on
        auto v         = gp::ext::velocity(mortar);
        st["onGround"] = true;
        st["stuck"]    = true;
        st["slide"]    = {v.x, v.z};
        return false;
    }
    detonate(mortar);
    return true;
}

bool solidAt(BlockSource& region, BlockPos const& p) { return fe::mobs::legacySolid(region, p); }

void tick(Actor& mortar) {
    auto& st = stateOf(mortar);
    if (st.value("detonated", false)) return;
    if (!st.value("onGround", false)) return;
    if (st.value("stuck", false) && st.contains("slide") && st["slide"].is_array()) {
        // ThrowableProjectile#tick on the ground: the horizontal motion goes on (0.99 drag)
        double sx = st["slide"][0].get<double>(), sz = st["slide"][1].get<double>();
        if (sx * sx + sz * sz > 1.0E-6) {
            auto&    region = mortar.getDimensionBlockSource();
            auto     p      = mortar.getPosition();
            Vec3     next{static_cast<float>(p.x + sx), p.y, static_cast<float>(p.z + sz)};
            BlockPos at{static_cast<int>(std::floor(next.x)), static_cast<int>(std::floor(next.y + 0.01)), static_cast<int>(std::floor(next.z))};
            BlockPos below{at.x, at.y - 1, at.z};
            if (solidAt(region, at)) {
                // a side face: explode
                detonate(mortar);
                return;
            }
            if (!solidAt(region, below)) {
                // off a ledge: flying again (still "on ground" for the fuse, as in Java)
                st["stuck"] = false;
                gp::ext::setVelocity(mortar, Vec3{static_cast<float>(sx), 0.0F, static_cast<float>(sz)});
            } else {
                mortar.moveTo(next, mortar.getRotation());
            }
            st["slide"] = {sx * 0.99, sz * 0.99};
        }
    }
    int fuse   = st.value("fuse", kFuse);
    st["fuse"] = fuse - 1;
    if (fuse <= 0) detonate(mortar);
}

ia::Result onAttack(ia::EntityContext const& c) {
    reflectMortar(c.entity, c.player);
    return ia::Result::Consume;
}

} // namespace

Actor* launchMortar(Mob& hydra, int head, Vec3 const& headPos, float headXRot, float headYRot, bool blasting) {
    // HydraMortar(type, level, head): 3.5 along the head's look, 1 up
    auto   look = core::hydra::viewVector(headXRot, headYRot);
    Vec3   pos{static_cast<float>(headPos.x + look.x * 3.5), static_cast<float>(headPos.y + 1.0 + look.y * 3.5), static_cast<float>(headPos.z + look.z * 3.5)};
    // shootFromRotation(head, xRot, yRot, -20, 0.5, 1)
    float  f  = -core::jrand::mthSin(headYRot * kDegToRad) * core::jrand::mthCos(headXRot * kDegToRad);
    float  f1 = -core::jrand::mthSin((headXRot + -20.0F) * kDegToRad);
    float  f2 = core::jrand::mthCos(headYRot * kDegToRad) * core::jrand::mthCos(headXRot * kDegToRad);
    Actor* m  = gp::shoot(hydra, kMortar, Vec3{f, f1, f2}, 0.5F, 1.0F);
    if (!m) return nullptr;
    m->moveTo(pos, Vec2{0, 0});
    auto& st       = stateOf(*m);
    st["blasting"] = blasting;
    st["owner"]    = fe::uidOf(hydra);
    st["head"]     = head;
    return m;
}

std::optional<MortarView> mortarView(Actor& mortar) {
    if (mortar.getTypeName() != kMortar) return std::nullopt;
    auto&      st = stateOf(mortar);
    MortarView v;
    v.fuse     = st.value("fuse", kFuse);
    v.onGround = st.value("onGround", false);
    v.blasting = st.value("blasting", false);
    v.owner    = st.value("owner", std::int64_t{0});
    return v;
}

void reflectMortar(Actor& mortar, Actor& attacker) {
    auto& st = stateOf(mortar);
    if (st.value("detonated", false)) return;
    // reflect faster and more accurately: shoot(look, 1.5, 0.1)
    auto           look = attacker.getViewVector(1.0F);
    double         len  = std::sqrt(look.x * look.x + look.y * look.y + look.z * look.z);
    if (len < 1.0E-9) return;
    fe::JavaRandom r(fe::freshSeed());
    double         spread = 0.0172275 * 0.1;
    Vec3           v{static_cast<float>((look.x / len + r.triangle(0.0, spread)) * 1.5), static_cast<float>((look.y / len + r.triangle(0.0, spread)) * 1.5),
           static_cast<float>((look.z / len + r.triangle(0.0, spread)) * 1.5)};
    gp::ext::setVelocity(mortar, v);
    st["onGround"] = false;
    st["stuck"]    = false;
    st["fuse"]     = st.value("fuse", kFuse) + 20;
    if (fe::asMob(attacker)) st["owner"] = fe::uidOf(attacker);
    log().info("hydra: mortar reflected by {}", attacker.getTypeName());
}

int detonationsForTest() { return gDetonations; }

void installMortar() {
    gp::registerType(kMortar, {
                                  .gravity     = 0.05F, // HydraMortar#getDefaultGravity
                                  .drag        = 0.99F,
                                  .onHitEntity = onHitEntity,
                                  .onHitBlock  = onHitBlock,
                                  .tick        = tick,
                              });
    gHandlers.push_back(ia::onAttack({.block = kMortar}, onAttack, ia::priority::High));
}

void uninstallMortar() {
    for (auto id : gHandlers) ia::remove(id);
    gHandlers.clear();
}

} // namespace gf::feature::hydra
